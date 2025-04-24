#ifndef HAYROLL_SYMBOLICEXECUTOR_HPP
#define HAYROLL_SYMBOLICEXECUTOR_HPP

#include <string>
#include <optional>
#include <vector>
#include <variant>
#include <filesystem>
#include <ranges>
#include <format>

#include <z3++.h>

#include <spdlog/spdlog.h>

#include "Util.hpp"
#include "TreeSitter.hpp"
#include "TreeSitterCPreproc.hpp"
#include "SymbolTable.hpp"
#include "IncludeResolver.hpp"
#include "IncludeTree.hpp"
#include "ProgramPoint.hpp"
#include "MacroExpander.hpp"
#include "ASTBank.hpp"
#include "PremiseTree.hpp"

namespace Hayroll
{

// Represents a symbolic execution state.
struct State
{
    SymbolTablePtr symbolTable;
    z3::expr premise;

    // Splits the state into two states.
    // The program point stays the same.
    // The symbol table is shared between the two states, but made immutable,
    // so if any one of the states modifies it, it will create a new layer.
    std::tuple<State, State> split() const
    {
        symbolTable->makeImmutable();
        State state1{symbolTable, premise};
        State state2{std::move(symbolTable), premise};
        return {std::move(state1), std::move(state2)};
    }

    // Merges the other state into this one if they have the same symbol table.
    // Returns whether the merge was successful or not.
    // The user is not supposed to even try to merge states with different include trees or nodes.
    bool mergeInplace(const State & other)
    {
        // It's okay to try to merge states with different symbol tables,
        // but they won't be merged eventually.
        if (symbolTable != other.symbolTable) return false;

        premise = premise || other.premise;
        return true;
    }

    void simplify()
    {
        SPDLOG_DEBUG(std::format("Simplifying merged state premise: {}", premise.to_string()));
        premise = simplifyOrOfAnd(premise);
        SPDLOG_DEBUG(std::format("Simplified merged state premise: {}", premise.to_string()));
    }

    std::string toString() const
    {
        return std::format
        (
            "State:\nsymbolTable:\n{}premise:\n{}",
            symbolTable->toString(),
            premise.to_string()
        );
    }

    std::string toStringFull() const
    {
        return std::format
        (
            "State:\nsymbolTable:\n{}premise:\n{}",
            symbolTable->toStringFull(),
            premise.to_string()
        );
    }
};

// A set of states at the same program point.
struct Warp
{
    ProgramPoint programPoint;
    std::vector<State> states;

    std::string toString() const
    {
        std::string str = std::format("Warp:\nprogramPoint: {}\nstates ({}):\n", programPoint.toString(), states.size());
        for (const State & state : states)
        {
            str += state.toString();
            str += "\n";
        }
        str += "End of warp\n";
        return str;
    }

    void defineAll(Symbol && symbol)
    {
        for (State & state : states)
        {
            Symbol symbolCopy = symbol;
            state.symbolTable = state.symbolTable->define(std::move(symbolCopy));
        }
    }
};

class SymbolicExecutor
{
public:
    const CPreproc lang;
    z3::context ctx;
    std::filesystem::path srcPath;
    std::filesystem::path projPath; // Any #include not under projPath will be concretely executed.
    IncludeResolver includeResolver;
    ASTBank astBank;
    MacroExpander macroExpander;
    IncludeTreePtr includeTree;
    PremiseTreeScribe scribe;

    SymbolicExecutor(std::filesystem::path srcPath, std::filesystem::path projPath, const std::vector<std::filesystem::path> & includePaths = {})
        : lang(CPreproc()), ctx(z3::context()), srcPath(std::filesystem::canonical(srcPath)),
          projPath(std::filesystem::canonical(projPath)), includeResolver(CLANG_EXE, includePaths),
          astBank(lang), macroExpander(lang, ctx),
          includeTree(IncludeTree::make(TSNode{}, std::filesystem::canonical(srcPath))),
          scribe() // Init scribe after we have parsed predefined macros
    {
        astBank.addFile(srcPath);
    }

    Warp run()
    {
        // Generate a base symbol table with the predefined macros.
        std::string predefinedMacros = includeResolver.getPredefinedMacros();
        const TSTree & predefinedMacroTree = astBank.addAnonymousSource(std::move(predefinedMacros));
        State predefinedMacroState{SymbolTable::make(), ctx.bool_val(true)};
        Warp predefinedMacroWarp{ProgramPoint{IncludeTree::make(TSNode{}, "<PREDEFINED_MACROS>"), predefinedMacroTree.rootNode()}, {predefinedMacroState}};
        predefinedMacroWarp = executeTranslationUnit(std::move(predefinedMacroWarp));
        assert(predefinedMacroWarp.states.size() == 1);
        SymbolTablePtr predefinedMacroSymbolTable = predefinedMacroWarp.states[0].symbolTable;
        assert(predefinedMacroSymbolTable != nullptr);

        const TSTree & tree = astBank.find(srcPath);
        TSNode root = tree.rootNode();
        predefinedMacroSymbolTable->makeImmutable(); // Just for debug clarity
        // The initial state is the root node of the tree.
        State startState{predefinedMacroSymbolTable, ctx.bool_val(true)};
        Warp startWarp{ProgramPoint{includeTree, root}, {std::move(startState)}};
        // Start the premise tree with a false premise.
        // Only when a state reaches the end of the translation unit without being killed by an #error,
        // should the premise tree of the translation unit disjunct with the premise of that surviving state.
        // Not satisfying the root premise means the program would not compile with this set of flags. 
        scribe = PremiseTreeScribe(startWarp.programPoint, ctx.bool_val(false));
        Warp endWarp = executeTranslationUnit(std::move(startWarp));
        
        return endWarp;
    }

    Warp executeTranslationUnit(Warp && startWarp, std::optional<ProgramPoint> joinPoint = std::nullopt)
    {
        SPDLOG_DEBUG(std::format("Executing translation unit: {}", startWarp.programPoint.toString()));
        assert(startWarp.programPoint.node.isSymbol(lang.translation_unit_s));
        // All states shall meet at the end of the translation unit (invalid node).
        if (!joinPoint) joinPoint = startWarp.programPoint.nextSibling();
        return executeInLockStep({std::move(startWarp)}, *joinPoint);
    }

    // Execute a single node or a segment of continuous #define nodes.
    // The node(s) of the state(s) returned shall be either its next sibling or a null node.
    // A null node here means it reached the end of the block_items or translation_unit it is in. It does not mean EOF. 
    // The null node is meant to be handled by executeInLockStep and set to the meeting point.
    Warp executeOne(Warp && startWarp)
    {
        // All possible node types:
        // preproc_if
        // preproc_ifdef
        // preproc_ifndef
        // preproc_include
        // preproc_include_next
        // preproc_def
        // preproc_function_def
        // preproc_undef
        // preproc_error
        // preproc_line
        // c_tokens
        // preproc_call

        SPDLOG_DEBUG(std::format("Executing one node: {}", startWarp.programPoint.toString()));
        
        const TSNode & node = startWarp.programPoint.node;
        const TSSymbol symbol = node.symbol();
        if (symbol == lang.preproc_if_s || symbol == lang.preproc_ifdef_s || symbol == lang.preproc_ifndef_s)
        {
            return executeIf(std::move(startWarp));
        }
        else if (symbol == lang.preproc_include_s || symbol == lang.preproc_include_next_s)
        {
            return executeInclude(std::move(startWarp));
        }
        else if (symbol == lang.preproc_def_s || symbol == lang.preproc_function_def_s || symbol == lang.preproc_undef_s)
        {
            return {executeContinuousDefines(std::move(startWarp))};
        }
        else if (symbol == lang.preproc_error_s)
        {
            return {executeError(std::move(startWarp))};
        }
        else if (symbol == lang.preproc_line_s)
        {
            return {executeLine(std::move(startWarp))};
        }
        else if (symbol == lang.c_tokens_s)
        {
            return {executeCTokens(std::move(startWarp))};
        }
        else if (symbol == lang.preproc_call_s)
        {
            // Unknown preprocessor directive. Skip. 
            startWarp.programPoint = startWarp.programPoint.nextSibling();
            return {std::move(startWarp)};
        }
        else assert(false);
    }
    
    Warp executeContinuousDefines(Warp && startWarp)
    {
        // All possible node types:
        // preproc_def
        // preproc_function_def
        // preproc_undef

        // Why not process the defines one by one? We may later do an optimization that pre-parses
        // all continuous define segments into a symbol table node to avoid repetitive parsing.
        // For now we just process them one by one.

        SPDLOG_DEBUG(std::format("Executing continuous defines: {}", startWarp.programPoint.toString()));

        TSNode & node = startWarp.programPoint.node;
        for 
        (
            TSNode & node = startWarp.programPoint.node;
            node && (node.isSymbol(lang.preproc_def_s) || node.isSymbol(lang.preproc_function_def_s) || node.isSymbol(lang.preproc_undef_s));
            node = node.nextSibling()
        )
        {
            if (node.isSymbol(lang.preproc_def_s))
            {
                TSNode name = node.childByFieldId(lang.preproc_def_s.name_f);
                TSNode value = node.childByFieldId(lang.preproc_def_s.value_f); // May not exist
                std::string_view nameStr = name.textView();
                startWarp.defineAll(ObjectSymbol{nameStr, startWarp.programPoint, value});
            }
            else if (node.isSymbol(lang.preproc_function_def_s))
            {
                TSNode name = node.childByFieldId(lang.preproc_function_def_s.name_f);
                TSNode params = node.childByFieldId(lang.preproc_function_def_s.parameters_f);
                assert(params.isSymbol(lang.preproc_params_s));
                TSNode body = node.childByFieldId(lang.preproc_function_def_s.value_f); // May not exist
                std::string_view nameStr = name.textView();
                std::vector<std::string> paramsStrs;
                for (TSNode param : params.iterateChildren())
                {
                    if (!param.isSymbol(lang.identifier_s)) continue;
                    paramsStrs.push_back(param.text());
                }
                startWarp.defineAll(FunctionSymbol{nameStr, startWarp.programPoint, std::move(paramsStrs), body});
            }
            else if (node.isSymbol(lang.preproc_undef_s))
            {
                TSNode name = node.childByFieldId(lang.preproc_undef_s.name_f);
                std::string_view nameStr = name.textView();
                startWarp.defineAll(UndefinedSymbol{nameStr});
            }
            else assert(false);
        }

        return std::move(startWarp);
    }

    Warp executeCTokens(Warp && startWarp)
    {
        // All possible node types:
        // c_tokens
        assert(startWarp.programPoint.node.isSymbol(lang.c_tokens_s));

        // We should scan for possible macro expansions in identifiers.
        // The final output should contain information about which definitions were expanded.
        // For now, we just skip this node.
        startWarp.programPoint = startWarp.programPoint.nextSibling();

        return std::move(startWarp);
    }
    
    Warp executeIf(Warp && startWarp)
    {
        // All possible node types:
        // preproc_if
        // preproc_ifdef
        // preproc_ifndef
        const auto & [programPoint, states] = startWarp;
        const auto [includeTree, node] = programPoint;
        assert(node.isSymbol(lang.preproc_if_s) || node.isSymbol(lang.preproc_ifdef_s) || node.isSymbol(lang.preproc_ifndef_s));

        SPDLOG_DEBUG(std::format("Executing conditional: {}", startWarp.programPoint.toString()));
        
        ProgramPoint joinPoint = programPoint.nextSibling();
        std::vector<Warp> warps = collectIfBodies(std::move(startWarp));
        return executeInLockStep(std::move(warps), joinPoint);
    }

    // Generates states for all possible branch bodies of the if statement.
    std::vector<Warp> collectIfBodies(Warp && startWarp)
    {
        // All possible node types:
        // preproc_if
        // preproc_ifdef
        // preproc_ifndef
        // preproc_elif
        // preproc_elifdef
        // preproc_elifndef
        // preproc_else

        auto & [programPoint, states] = startWarp;
        const auto & [includeTree, node] = programPoint;

        if (!node)
        {
            // Empty else branch.
            return {std::move(startWarp)};
        }

        if
        (
            node.isSymbol(lang.preproc_if_s)
            || node.isSymbol(lang.preproc_ifdef_s)
            || node.isSymbol(lang.preproc_ifndef_s)
            || node.isSymbol(lang.preproc_elif_s)
            || node.isSymbol(lang.preproc_elifdef_s)
            || node.isSymbol(lang.preproc_elifndef_s)
        )
        {
            TSNode body = node.childByFieldId(lang.preproc_if_s.body_f); // May or may not have body.
            TSNode alternative = node.childByFieldId(lang.preproc_if_s.alternative_f); // May or may not have alternative.
            Warp thenWarp{{includeTree, body}, {}};
            Warp elseWarp{{includeTree, alternative}, {}};

            std::vector<TSNode> tokenList;
            MacroExpander::Prepend prepend = MacroExpander::Prepend::None;
            // #if and #elif have field "condition".
            if (node.isSymbol(lang.preproc_if_s) || node.isSymbol(lang.preproc_elif_s))
            {
                TSNode tokens = node.childByFieldId(lang.preproc_if_s.condition_f);
                assert(tokens.isSymbol(lang.preproc_tokens_s));
                tokenList = lang.tokensToTokenVector(tokens);
            }
            // #xxxdefxxx series have field "name"
            else if 
            (
                node.isSymbol(lang.preproc_ifdef_s)
                || node.isSymbol(lang.preproc_ifndef_s)
                || node.isSymbol(lang.preproc_elifdef_s)
                || node.isSymbol(lang.preproc_elifndef_s)
            )
            {
                TSNode name = node.childByFieldId(lang.preproc_ifdef_s.name_f);
                assert(name.isSymbol(lang.identifier_s));
                tokenList.push_back(name);
                if (node.isSymbol(lang.preproc_ifdef_s) || node.isSymbol(lang.preproc_elifdef_s))
                {
                    prepend = MacroExpander::Prepend::Defined;
                }
                else if (node.isSymbol(lang.preproc_ifndef_s) || node.isSymbol(lang.preproc_elifndef_s))
                {
                    prepend = MacroExpander::Prepend::NotDefined;
                }
            }
            else assert(false);

            // Split each state and put them into the then and else warps.
            for (State & state : states)
            {
                const auto & [symbolTable, premise] = state;

                z3::expr ifPremise = macroExpander.symbolizeToBoolExpr(tokenList, symbolTable, prepend);
                z3::expr enterThenPremise = premise && ifPremise;
                z3::expr elsePremise = !ifPremise;
                z3::expr enterElsePremise = premise && elsePremise;

                bool enterThenPremiseIsSat = z3Check(enterThenPremise) == z3::sat;
                bool enterElsePremiseIsSat = z3Check(enterElsePremise) == z3::sat;

                if (enterThenPremiseIsSat && enterElsePremiseIsSat) // Both branch possible
                {
                    auto && [thenState, elseState] = state.split();
                    thenState.premise = enterThenPremise;
                    thenWarp.states.push_back(std::move(thenState));
                    elseState.premise = enterElsePremise;
                    elseWarp.states.push_back(std::move(elseState));
                }
                else if (enterThenPremiseIsSat) // Only then branch possible
                {
                    // No need to split, just execute the then branch.
                    State && thenState = std::move(state);
                    thenState.premise = enterThenPremise;
                    thenWarp.states.push_back(std::move(thenState));
                }
                else if (enterElsePremiseIsSat) // Only else branch possible
                {
                    // No need to split, just execute the else branch.
                    State && elseState = std::move(state);
                    elseState.premise = enterElsePremise;
                    elseWarp.states.push_back(std::move(elseState));
                }
                else assert(false);
            }

            if (!thenWarp.states.empty() && !elseWarp.states.empty()) // Both branch possible
            {
                // Call the scribe to create a new premise node for the body.
                if (body)
                {
                    scribe.addPremiseOrCreateChild({includeTree, body}, ctx.bool_val(false));
                }
                std::vector<Warp> warps = collectIfBodies(std::move(elseWarp));
                warps.push_back(std::move(thenWarp));
                return warps;
            }
            else if (!thenWarp.states.empty()) // Only then branch possible
            {
                if (body)
                {
                    scribe.addPremiseOrCreateChild({includeTree, body}, ctx.bool_val(false));
                }
                // Call the scribe to mark the else body as unreachable, since we are not going to recurse into it.
                if (alternative)
                {
                    scribe.addPremiseOrCreateChild({includeTree, alternative}, ctx.bool_val(false));
                }
                return {std::move(thenWarp)};
            }
            else if (!elseWarp.states.empty()) // Only else branch possible
            {
                // Call the scribe to mark the then body as unreachable.
                if (body)
                {
                    scribe.addPremiseOrCreateChild({includeTree, body}, ctx.bool_val(false));
                }
                return collectIfBodies(std::move(elseWarp));
            }
            else assert(false); // There should be at least one state in one of the warps.
        }
        else if (node.isSymbol(lang.preproc_else_s))
        {
            TSNode body = node.childByFieldId(lang.preproc_else_s.body_f); // May or may not have body.
            if (body)
            {
                assert(body.isSymbol(lang.block_items_s));
                scribe.addPremiseOrCreateChild({includeTree, body}, ctx.bool_val(false));
                startWarp.programPoint.node = body;
                return {std::move(startWarp)};
            }
            else
            {
                // No body, just skip.
                startWarp.programPoint.node = TSNode{};
                // Just return an empty node, so the lock step executor will put it into the finished queue once seeing it.
                return {std::move(startWarp)};
            }
        }
        else assert(false);
    }

    Warp executeInclude(Warp && startWarp)
    {
        // All possible node types:
        // preproc_include
        // preproc_include_next

        const auto & [programPoint, states] = startWarp;
        const auto & [includeTree, node] = programPoint;

        ProgramPoint joinPoint = startWarp.programPoint.nextSibling();
        
        if (node.isSymbol(lang.preproc_include_s))
        {
            TSNode pathNode = node.childByFieldId(lang.preproc_include_s.path_f);
            assert(pathNode.isSymbol(lang.string_literal_s) || pathNode.isSymbol(lang.system_lib_string_s));
            bool isSystemInclude = pathNode.isSymbol(lang.system_lib_string_s);
            TSNode stringContentNode = pathNode.childByFieldId(lang.string_literal_s.content_f); // Both types have this field.
            assert(stringContentNode.isSymbol(lang.string_content_s));
            std::string_view pathStr = stringContentNode.textView();

            std::filesystem::path includePath = includeResolver.resolveInclude(isSystemInclude, pathStr, includeTree->getAncestorDirs());
            if (includePath.empty()) return {}; // Include not found, process as error.
            else if (includePath.string().starts_with(projPath.string())) // Header is in project path, execute symbolically. 
            {
                // Include found, add it to the AST bank and create a new state for it.
                const TSTree & tree = astBank.addFile(includePath);
                TSNode root = tree.rootNode();
                startWarp.programPoint = {includeTree->addChild(node, includePath), root};
                // Print the startWarp for debugging.
                SPDLOG_DEBUG(std::format("Executing include symbolically: {}", startWarp.programPoint.toString()));
                // Init the premise tree node with a false premise.
                scribe.addPremiseOrCreateChild(startWarp.programPoint, ctx.bool_val(false));
                return executeTranslationUnit(std::move(startWarp), joinPoint);
            }
            else // Header is outsde of project path, execute concretely.
            {
                std::string concretelyExecuted = includeResolver.getConcretelyExecutedMacros(includePath);
                // Include found, add it to the AST bank and create a new state for it.
                const TSTree & concretelyExecutedTree = astBank.addAnonymousSource(std::move(concretelyExecuted));
                TSNode root = concretelyExecutedTree.rootNode();
                TSNode fistChild = root.firstChildForByte(0);
                assert(fistChild.isSymbol(lang.preproc_def_s) || fistChild.isSymbol(lang.preproc_function_def_s) || fistChild.isSymbol(lang.preproc_undef_s));
                startWarp.programPoint = {includeTree->addChild(node, includePath, true), fistChild};
                // Print the startWarp for debugging.
                SPDLOG_DEBUG(std::format("Executing include concretely: {}", startWarp.programPoint.toString()));
                // No scribe needed for concrete execution
                // We need to set the node to the join point, so it will be merged with the other states.
                startWarp = executeContinuousDefines(std::move(startWarp));
                startWarp.programPoint = std::move(joinPoint);
                return {std::move(startWarp)};
            }
        }
        // else if (node.isSymbol(lang.preproc_include_next_s)) // Skip for now
        // {
        // }
        else assert(false);

        return {};
    }

    Warp executeError(Warp && startWarp)
    {
        // Bug: we are allowing error states to continue for convinience of merging premises for inner nodes.
        // As a makeup, we should exclude this case from the premise tree root.
        assert(startWarp.programPoint.node.isSymbol(lang.preproc_error_s));
        SPDLOG_DEBUG(std::format("Executing error, keeping state: {}", startWarp.toString()));
        // Just skip this node.
        startWarp.programPoint = startWarp.programPoint.nextSibling();
        return std::move(startWarp);
    }

    Warp executeLine(Warp && startWarp)
    {
        assert(startWarp.programPoint.node.isSymbol(lang.preproc_line_s));
        // Just skip this node.
        startWarp.programPoint = startWarp.programPoint.nextSibling();
        return std::move(startWarp);
    }

    // The nodes of all startWarps should either be a block_items or a translation_unit.
    // This function will execute all items in the block_items or translation_unit in lock step,
    // and then set the node of the result states to joinPoint.
    // When all states reach the joinPoint, they will be merged if possible.
    Warp executeInLockStep(std::vector<Warp> && startWarps, const ProgramPoint & joinPoint)
    {
        // Print the program points of start states for debugging.
        #if DEBUG
            SPDLOG_DEBUG(std::format("Executing in lock step:"));
            for (const Warp & warp : startWarps)
            {
                SPDLOG_DEBUG(std::format("Warp: {}", warp.programPoint.toString()));
            }
            SPDLOG_DEBUG(std::format("Join point: {}", joinPoint.toString()));
        #endif

        std::vector<std::tuple<ProgramPoint, Warp>> tasks; // (block_items/translation_unit body, warp)
        std::vector<Warp> blockedWarps;

        for (Warp & warp : startWarps)
        {
            Warp warpOwned = std::move(warp);
            assert
            (
                !warpOwned.programPoint.node
                || warpOwned.programPoint.node.isSymbol(lang.block_items_s)
                || warpOwned.programPoint.node.isSymbol(lang.translation_unit_s)
            );
            if (warpOwned.programPoint.node)
            {
                // The branch has a body.
                ProgramPoint body = warpOwned.programPoint;
                assert
                (
                    body.node.isSymbol(lang.block_items_s)
                    || body.node.isSymbol(lang.translation_unit_s)
                );
                warpOwned.programPoint = warpOwned.programPoint.firstChild();
                // This may return a null node if task.programPoint is a translation_unit and the file is empty. It's okay.
                tasks.emplace_back(body, std::move(warpOwned));
            }
            else
            {
                // This branch does not have a body. Process as if it directly made its way to the join point.
                blockedWarps.push_back(std::move(warpOwned));
            }
            // task.programPoint.node itself may be null, that means the entire #if is bypassed.
        }

        while (!tasks.empty())
        {
            SPDLOG_DEBUG(std::format("Tasks left: {}", tasks.size()));
            // Take one task from the queue (std::move). 
            auto [body, warp] = std::move(tasks.back()); // Do not use auto && here, or there will be memory corruption.
            tasks.pop_back();
            if (!warp.programPoint.node)
            {
                // This means it successfully reached the end of the block_items or translation_unit (not being killed by #error).
                // We need to call the scribe to add its premise to the block_items or translation_unit and set the node to the join point.
                for (State & state : warp.states)
                {
                    scribe.addPremiseOrCreateChild(body, state.premise);
                }
                blockedWarps.push_back(std::move(warp));
                continue;
            }

            tasks.emplace_back(body, executeOne(std::move(warp)));
        }

        #if DEBUG
            SPDLOG_DEBUG(std::format("Blocked warps ({}):", blockedWarps.size()));
            SPDLOG_DEBUG(std::format("Join point: {}", joinPoint.toString()));
            for (const Warp & warp : blockedWarps)
            {
                SPDLOG_DEBUG(std::format("{}", warp.toString()));
            }
        #endif

        // Collect states in all blocked warps.
        std::vector<State> blockedStates;
        for (Warp & blockedWarp : blockedWarps)
        {
            for (State & state : blockedWarp.states)
            {
                blockedStates.push_back(std::move(state));
            }
        }

        // Before merging, sort all blocked states by their symbol table pointer value.
        // This way we can do a one pass merge.
        std::sort(blockedStates.begin(), blockedStates.end(), [](const State & a, const State & b)
        {
            return a.symbolTable < b.symbolTable;
        });

        std::vector<State> mergedStates;
        for (State & blockedState : blockedStates)
        {
            State blockedStateOwned = std::move(blockedState);
            if (mergedStates.empty() || !mergedStates.back().mergeInplace(blockedStateOwned))
            {
                mergedStates.push_back(std::move(blockedStateOwned));
            }
        }

        for (State & mergedState : mergedStates)
        {
            mergedState.simplify();
        }

        #if DEBUG
            SPDLOG_DEBUG(std::format("Merged states ({}):", mergedStates.size()));
            SPDLOG_DEBUG(std::format("Join point: {}", joinPoint.toString()));
            for (const State & state : mergedStates)
            {
                SPDLOG_DEBUG(std::format("{}", state.toString()));
            }
        #endif
        
        return {joinPoint, std::move(mergedStates)};
    }
};

} // namespace Hayroll

#endif // HAYROLL_SYMBOLICEXECUTOR_HPP
