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
#include "MacroExpander.hpp"
#include "ASTBank.hpp"
#include "PremiseTree.hpp"

namespace Hayroll
{

// Represents a symbolic execution state.
// Program point representation: include tree + node. 
// Stateful representation: symbol table + premise.
struct State
{
    ProgramPoint programPoint;
    SymbolTablePtr symbolTable;
    z3::expr premise;

    std::tuple<State, State> split() const
    {
        symbolTable->makeImmutable();
        State state1{programPoint, symbolTable, premise};
        State state2{programPoint, std::move(symbolTable), premise};
        return {std::move(state1), std::move(state2)};
    }

    // Merges the other state into this one if they have the same symbol table.
    // Returns whether the merge was successful or not.
    // The user is not supposed to even try to merge states with different include trees or nodes.
    bool mergeInplace(const State & other)
    {
        assert(programPoint == other.programPoint);

        // It's okay to try to merge states with different symbol tables,
        // but they won't be merged eventually.
        if (symbolTable != other.symbolTable) return false;

        premise = z3CtxtSolverSimplify(premise || other.premise);
        // premise = premise || other.premise;
        return true;
    }

    std::string toString() const
    {
        return std::format
        (
            "State:\nprogramPoint:\n{}\nsymbolTable:\n{}premise:\n{}",
            programPoint.toString(),
            symbolTable->toString(),
            premise.to_string()
        );
    }

    std::string toStringFull() const
    {
        return std::format
        (
            "State:\nprogramPoint:\n{}\nsymbolTable:\n{}premise:\n{}",
            programPoint.toStringFull(),
            symbolTable->toStringFull(),
            premise.to_string()
        );
    }
};

class SymbolicExecutor
{
public:
    SymbolicExecutor(std::filesystem::path srcPath)
        : lang(CPreproc()), ctx(z3::context()), srcPath(srcPath), includeResolver(CLANG_EXE, {}),
          astBank(lang), macroExpander(lang, ctx), includeTree(IncludeTree::make(0, srcPath)),
          scribe() // Init scribe later
    {
        astBank.addFile(srcPath);
    }

    std::vector<State> run()
    {
        // Generate a base symbol table with the predefined macros.
        std::string predefinedMacros = includeResolver.getPredefinedMacros();
        const TSTree & predefinedMacroTree = astBank.addAnonymousSource(std::move(predefinedMacros));
        State predefinedMacroState{IncludeTree::make(0, "<PREDEFINED_MACROS>"), predefinedMacroTree.rootNode(), SymbolTable::make(), ctx.bool_val(true)};
        std::vector<State> predefinedMacroStates = executeTranslationUnit(std::move(predefinedMacroState));
        assert(predefinedMacroStates.size() == 1);
        SymbolTablePtr predefinedMacroSymbolTable = predefinedMacroStates[0].symbolTable;
        assert(predefinedMacroSymbolTable != nullptr);

        const TSTree & tree = astBank.find(srcPath);
        TSNode root = tree.rootNode();
        predefinedMacroSymbolTable->makeImmutable(); // Just for debug clarity
        // The initial state is the root node of the tree.
        State startState{{includeTree, root}, predefinedMacroSymbolTable, ctx.bool_val(true)};
        scribe = PremiseTreeScribe(startState.programPoint, startState.premise);
        std::vector<State> endStates = executeTranslationUnit(std::move(startState));
        
        return endStates;
    }

    std::vector<State> executeTranslationUnit(State && startState)
    {
        SPDLOG_DEBUG(std::format("Executing translation unit: {}", startState.programPoint.toString()));
        assert(startState.programPoint.node.isSymbol(lang.translation_unit_s));
        // All states shall meet at the end of the translation unit (invalid node).
        return executeInLockStep({std::move(startState)}, TSNode{});
    }

    // Execute a single node or a segment of continuous #define nodes.
    // The node(s) of the state(s) returned shall be either its next sibling or a null node.
    // A null node here means it reached the end of the block_items or translation_unit it is in. It does not mean EOF. 
    // The null node is meant to be handled by executeInLockStep and set to the meeting point.
    std::vector<State> executeOne(State && startState)
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

        SPDLOG_DEBUG(std::format("Executing one node: {}", startState.programPoint.toString()));
        
        const TSNode & node = startState.programPoint.node;
        const TSSymbol symbol = node.symbol();
        if (symbol == lang.preproc_if_s || symbol == lang.preproc_ifdef_s || symbol == lang.preproc_ifndef_s)
        {
            return executeIf(std::move(startState));
        }
        else if (symbol == lang.preproc_include_s || symbol == lang.preproc_include_next_s)
        {
            return executeInclude(std::move(startState));
        }
        else if (symbol == lang.preproc_def_s || symbol == lang.preproc_function_def_s || symbol == lang.preproc_undef_s)
        {
            return {executeContinuousDefines(std::move(startState))};
        }
        else if (symbol == lang.preproc_error_s)
        {
            executeError(std::move(startState));
            return {};
        }
        else if (symbol == lang.preproc_line_s)
        {
            return {executeLine(std::move(startState))};
        }
        else if (symbol == lang.c_tokens_s)
        {
            return {executeCTokens(std::move(startState))};
        }
        else assert(false);
    }
    
    State executeContinuousDefines(State && startState)
    {
        // All possible node types:
        // preproc_def
        // preproc_function_def
        // preproc_undef

        // Why not process the defines one by one? We may later do an optimization that pre-parses
        // all continuous define segments into a symbol table node to avoid repetitive parsing.
        // For now we just process them one by one.

        SPDLOG_DEBUG(std::format("Executing continuous defines: {}", startState.programPoint.toString()));

        TSNode & node = startState.programPoint.node;
        for 
        (
            TSNode & node = startState.programPoint.node;
            node && (node.isSymbol(lang.preproc_def_s) || node.isSymbol(lang.preproc_function_def_s) || node.isSymbol(lang.preproc_undef_s));
            node = node.nextSibling()
        )
        {
            SPDLOG_DEBUG(std::format("Executing define: {}", node.textView()));

            if (node.isSymbol(lang.preproc_def_s))
            {
                TSNode name = node.childByFieldId(lang.preproc_def_s.name_f);
                TSNode value = node.childByFieldId(lang.preproc_def_s.value_f); // May not exist
                std::string_view nameStr = name.textView();
                startState.symbolTable = startState.symbolTable->define(ObjectSymbol{nameStr, startState.programPoint, value});
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
                startState.symbolTable = startState.symbolTable->define(FunctionSymbol{nameStr, startState.programPoint, std::move(paramsStrs), body});
            }
            else if (node.isSymbol(lang.preproc_undef_s))
            {
                TSNode name = node.childByFieldId(lang.preproc_undef_s.name_f);
                std::string_view nameStr = name.textView();
                startState.symbolTable = startState.symbolTable->define(UndefinedSymbol{nameStr});
            }
            else assert(false);
        }

        return std::move(startState);
    }

    State executeCTokens(State && startState)
    {
        // All possible node types:
        // c_tokens
        assert(startState.programPoint.node.isSymbol(lang.c_tokens_s));

        // We should scan for possible macro expansions in identifiers.
        // The final output should contain information about which definitions were expanded.
        // For now, we just skip this node.
        startState.programPoint = startState.programPoint.nextSibling();

        return std::move(startState);
    }
    
    std::vector<State> executeIf(State && startState)
    {
        // All possible node types:
        // preproc_if
        // preproc_ifdef
        // preproc_ifndef
        const auto & [programPoint, symbolTable, premise] = startState;
        const auto & [includeTree, node] = programPoint;
        assert(node.isSymbol(lang.preproc_if_s) || node.isSymbol(lang.preproc_ifdef_s) || node.isSymbol(lang.preproc_ifndef_s));
        
        TSNode joinPoint = node.nextSibling(); // Take this node out before startState is moved.
        std::vector<State> states = collectIfBodies(std::move(startState));
        return executeInLockStep(std::move(states), joinPoint);
    }

    // Generates states for all possible branch bodies of the if statement.
    std::vector<State> collectIfBodies(State && startState)
    {
        // All possible node types:
        // preproc_if
        // preproc_ifdef
        // preproc_ifndef
        // preproc_elif
        // preproc_elifdef
        // preproc_elifndef
        // preproc_else

        const auto & [programPoint, symbolTable, premise] = startState;
        const auto & [includeTree, node] = programPoint;

        if (!node)
        {
            // Empty else branch.
            return {std::move(startState)};
        }

        if (node.isSymbol(lang.preproc_if_s))
        {
            TSNode condition = node.childByFieldId(lang.preproc_if_s.condition_f);
            assert(condition.isSymbol(lang.preproc_tokens_s));
            TSNode body = node.childByFieldId(lang.preproc_if_s.body_f); // May or may not have body.
            TSNode alternative = node.childByFieldId(lang.preproc_if_s.alternative_f); // May or may not have alternative.

            z3::expr ifPremise = macroExpander.expandAndSymbolizeToBoolExpr(condition, symbolTable);
            // z3::expr fullIfPremise = ifPremise && premise;
            z3::expr fullIfPremise = z3CtxtSolverSimplify(ifPremise && premise);
            z3::expr elsePremise = !ifPremise;
            // z3::expr fullElsePremise = elsePremise && premise;
            z3::expr fullElsePremise = z3CtxtSolverSimplify(elsePremise && premise);

            if (body)
            {
                assert(body.isSymbol(lang.block_items_s));
                scribe.addPremiseOrCreateChild(ProgramPoint{includeTree, body}, fullIfPremise);
            }

            bool fullIfPremiseIsSat = z3Check(fullIfPremise) == z3::sat;
            bool fullElsePremiseIsSat = z3Check(fullElsePremise) == z3::sat;

            if (fullIfPremiseIsSat && fullElsePremiseIsSat) // Both branch possible
            {
                auto [thenState, elseState] = startState.split();
                thenState.premise = fullIfPremise;
                thenState.programPoint.node = body; // Can be null, which means to skip the body.
                elseState.premise = fullElsePremise;
                elseState.programPoint.node = alternative; // Can be null, which means to skip the alternative.
                std::vector<State> result = collectIfBodies(std::move(elseState));
                result.push_back(std::move(thenState));
                return result;
            }
            else if (fullIfPremiseIsSat) // Only then branch possible
            {
                // No need to split, just execute the then branch.
                State thenState = std::move(startState);
                thenState.premise = fullIfPremise;
                thenState.programPoint.node = body; // Can be null, which means to skip the body.
                return {std::move(thenState)};
            }
            else if (fullElsePremiseIsSat) // Only else branch possible
            {
                // No need to split, just execute the else branch.
                State elseState = std::move(startState);
                elseState.premise = fullElsePremise;
                elseState.programPoint.node = alternative; // Can be null, which means to skip the alternative.
                return collectIfBodies(std::move(elseState));
            }
            else assert(false);
        }
        else if (node.isSymbol(lang.preproc_ifdef_s))
        {
            assert(false); // Not implemented yet.
        }
        else if (node.isSymbol(lang.preproc_ifndef_s))
        {
            assert(false); // Not implemented yet.
        }
        else if (node.isSymbol(lang.preproc_elif_s))
        {
            assert(false); // Not implemented yet.
        }
        else if (node.isSymbol(lang.preproc_elifdef_s))
        {
            assert(false); // Not implemented yet.
        }
        else if (node.isSymbol(lang.preproc_elifndef_s))
        {
            assert(false); // Not implemented yet.
        }
        else if (node.isSymbol(lang.preproc_else_s))
        {
            TSNode body = node.childByFieldId(lang.preproc_else_s.body_f); // May or may not have body.
            if (body)
            {
                assert(body.isSymbol(lang.block_items_s));
                startState.programPoint.node = body;
                scribe.addPremiseOrCreateChild(ProgramPoint{includeTree, body}, startState.premise);
                return {std::move(startState)};
            }
            else
            {
                // No body, just skip.
                startState.programPoint.node = TSNode{};
                // Just return an empty node, so the lock step executor will put it into the finished queue once seeing it.
                return {std::move(startState)};
            }
        }
        else assert(false);
    }

    std::vector<State> executeInclude(State && startState)
    {
        // All possible node types:
        // preproc_include
        // preproc_include_next
        
        // Skip for now.
        startState.programPoint = startState.programPoint.nextSibling();

        return {};
    }

    void executeError(State && startState)
    {
        assert(startState.programPoint.node.isSymbol(lang.preproc_error_s));
    }

    State executeLine(State && startState)
    {
        assert(startState.programPoint.node.isSymbol(lang.preproc_line_s));
        // Just skip this node.
        startState.programPoint = startState.programPoint.nextSibling();
        return std::move(startState);
    }

    // The nodes of all startStates should either be a block_items or a translation_unit.
    // This function will execute all items in the block_items or translation_unit in lock step,
    // and then set the node of the result states to joinPoint.
    // When all states reach the joinPoint, they will be merged if possible.
    std::vector<State> executeInLockStep(std::vector<State> && startStates, const TSNode & joinPoint)
    {
        // Print the program points of start states for debugging.
        #if DEBUG
            SPDLOG_DEBUG(std::format("Executing in lock step:"));
            for (const State & state : startStates)
            {
                SPDLOG_DEBUG(std::format("{}", state.programPoint.toString()));
            }
            ProgramPoint joinProgramPoint = {startStates[0].programPoint.includeTree, joinPoint};
            SPDLOG_DEBUG(std::format("Join point: {}", joinProgramPoint.toString()));
        #endif

        std::vector<State> tasks = std::move(startStates);
        std::vector<State> blockedStates;

        for (State & task : tasks)
        {
            assert(!task.programPoint.node || task.programPoint.node.isSymbol(lang.block_items_s) || task.programPoint.node.isSymbol(lang.translation_unit_s));
            if (task.programPoint.node) task.programPoint = task.programPoint.firstChild(); // This may return a null node. That is okay.
            // task.programPoint.node itself may be null, that means the entire #if is bypassed.
        }

        while (!tasks.empty())
        {
            SPDLOG_DEBUG(std::format("Tasks left: {}", tasks.size()));
            // Take one task from the queue (std::move). 
            State task = std::move(tasks.back());
            tasks.pop_back();
            if (!task.programPoint.node)
            {
                // This means we reached the end of the block_items or translation_unit.
                // We need to set the node to the join point.
                task.programPoint.node = joinPoint;
                blockedStates.push_back(std::move(task));
                continue;
            }

            for (State & taskDone : executeOne(std::move(task)))
            {
                tasks.push_back(std::move(taskDone));
            }
        }

        // Before merging, sort all blocked states by their symbol table pointer value.
        // This way we can do a one pass merge.
        std::sort(blockedStates.begin(), blockedStates.end(), [](const State & a, const State & b)
        {
            return a.symbolTable < b.symbolTable;
        });

        #if DEBUG
            SPDLOG_DEBUG(std::format("Blocked states ({}):", blockedStates.size()));
            for (const State & state : blockedStates)
            {
                SPDLOG_DEBUG(std::format("{}", state.toString()));
            }
        #endif

        std::vector<State> mergedStates;
        for (State & blockedState : blockedStates)
        {
            State blockedStateOwned = std::move(blockedState);
            if (mergedStates.empty() || !mergedStates.back().mergeInplace(blockedStateOwned))
            {
                mergedStates.push_back(std::move(blockedStateOwned));
            }
        }

        #if DEBUG
            SPDLOG_DEBUG(std::format("Merged states ({}):", mergedStates.size()));
            for (const State & state : mergedStates)
            {
                SPDLOG_DEBUG(std::format("{}", state.toString()));
            }
        #endif
        
        return mergedStates;
    }
    
public:
    const CPreproc lang;
    z3::context ctx;
    std::filesystem::path srcPath;
    IncludeResolver includeResolver;
    ASTBank astBank;
    MacroExpander macroExpander;
    IncludeTreePtr includeTree;
    PremiseTreeScribe scribe;
};

} // namespace Hayroll

#endif // HAYROLL_SYMBOLICEXECUTOR_HPP
