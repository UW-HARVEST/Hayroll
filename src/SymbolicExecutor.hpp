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
        premise = simplifyOrOfAnd(premise);
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

// A set of states at the same program point, meant for lock-step execution.
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

    void defineAll(SymbolSegmentPtr segment)
    {
        for (State & state : states)
        {
            state.symbolTable = state.symbolTable->define(segment);
        }
    }
};

class SymbolicExecutor
{
public:
    const CPreproc lang;
    std::unique_ptr<z3::context> ctx;
    std::filesystem::path srcPath;
    std::filesystem::path projPath; // Any #include not under projPath will be concretely executed.
    IncludeResolver includeResolver;
    ASTBank astBank;
    MacroExpander macroExpander;
    IncludeTreePtr includeTree;
    // The root symbol segment stores #undefs produced by the key assumption:
    // any macro name that is ever defined or undefined in the code,
    // it is not intended to be supplemented by the user from the command line (-D).
    SymbolTablePtr symbolTableRoot;
    PremiseTreeScribe scribe;
    std::optional<std::vector<std::string>> macroWhitelist;

    bool analyzeInvocations;

    SymbolicExecutor
    (
        std::filesystem::path srcPath,
        std::filesystem::path projPath,
        const std::vector<std::filesystem::path> & includePaths = {},
        std::optional<std::vector<std::string>> macroWhitelist = std::nullopt,
        bool analyzeInvocations = false
    )
        : lang(CPreproc()), ctx(std::make_unique<z3::context>()), srcPath(std::filesystem::canonical(srcPath)),
          projPath(std::filesystem::canonical(projPath)), includeResolver(ClangExe, includePaths),
          astBank(lang), macroExpander(lang, ctx.get()),
          includeTree(IncludeTree::make(TSNode{}, std::filesystem::canonical(srcPath))),
          symbolTableRoot(SymbolTable::make(SymbolSegment::make(), nullptr, macroWhitelist)),
          scribe(), analyzeInvocations(analyzeInvocations), macroWhitelist(macroWhitelist)
    {
        astBank.addFileOrFind(srcPath);
    }

    SymbolicExecutor(SymbolicExecutor && other) = default;

    Warp run()
    {
        SymbolSegment::totalSymbolSegments = 0;
        SymbolSegment::totalSymbols = 0;
        SymbolTable::totalSymbolTables = 0;

        // Generate a base symbol table with the predefined macros.
        std::string builtinMacros = includeResolver.getBuiltinMacros();
        const TSTree & predefinedMacroTree = astBank.addAnonymousSource(std::move(builtinMacros));
        State builtinMacroState{symbolTableRoot, ctx->bool_val(true)};
        ProgramPoint predefinedMacroProgramPoint{IncludeTree::make(TSNode{}, "<built-in>"), predefinedMacroTree.rootNode()};
        Warp predefinedMacroWarp{std::move(predefinedMacroProgramPoint), {std::move(builtinMacroState)}};
        predefinedMacroWarp = executeTranslationUnit(std::move(predefinedMacroWarp));
        assert(predefinedMacroWarp.states.size() == 1);
        SymbolTablePtr builtinMacroSymbolTable = predefinedMacroWarp.states[0].symbolTable;
        assert(builtinMacroSymbolTable != nullptr);

        const TSTree & tree = astBank.find(srcPath);
        TSNode root = tree.rootNode();
        // The initial state is the root node of the tree.
        State startState{builtinMacroSymbolTable, ctx->bool_val(true)};
        Warp startWarp{ProgramPoint{includeTree, root}, {std::move(startState)}};
        // Start the premise tree with a true premise.
        // When a state reaches an #error, it does not stop, instead, it conjuncts the negation
        // of its premise to the root node of the premise tree.
        scribe = PremiseTreeScribe(startWarp.programPoint, ctx->bool_val(true));
        Warp endWarp = executeTranslationUnit(std::move(startWarp));
        
        return endWarp;
    }

    Warp executeTranslationUnit(Warp && startWarp, std::optional<ProgramPoint> joinPoint = std::nullopt)
    {
        SPDLOG_TRACE("Executing translation unit: {}", startWarp.programPoint.toString());
        assert(startWarp.programPoint.node.isSymbol(lang.translation_unit_s));

        // Key assumption: any macro name that is ever defined or undefined in the code,
        // it is not intended to be supplemented by the user from the command line (-D).
        // An example of this is header guard macros.
        // We enforce this by scanning the translation unit for all #define and #undef nodes,
        // and undefining them in the symbol table.
        // This does not apply to whitelisted macros.
        for (TSNode node : startWarp.programPoint.node.iterateDescendants())
        {
            if (node.isSymbol(lang.preproc_def_s) || node.isSymbol(lang.preproc_function_def_s) || node.isSymbol(lang.preproc_undef_s))
            {
                // We need to undefine the macro in the symbol table.
                // This is done by creating a new state with the same symbol table,
                // but with the macro undefined.
                TSNode name = node.childByFieldId(lang.preproc_undef_s.name_f);
                std::string_view nameStr = name.textView();
                if (macroWhitelist)
                {
                    if (std::find(macroWhitelist->begin(), macroWhitelist->end(), nameStr) != macroWhitelist->end())
                    {
                        // In whitelist mode, do not undefine whitelisted macros.
                        continue;
                    }
                }
                symbolTableRoot->forceDefine(UndefinedSymbol{nameStr});
            }
        }

        // All states shall meet at the end of the translation unit (invalid node).
        if (!joinPoint) joinPoint = startWarp.programPoint.nextSibling();
        return executeInLockStep({std::move(startWarp)}, *joinPoint);
    }

    // Execute a single node or a segment of continuous #define nodes.
    // The node(s) of the state(s) returned shall be either its next sibling or a null node.
    // A null node here means it reached the end of the block_items or translation_unit it is in. It does not mean EOF. 
    // The null node is meant to be handled by executeInLockStep and set to the meeting point.
    std::optional<Warp> executeOne(Warp && startWarp)
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

        SPDLOG_TRACE("Executing one node: {}", startWarp.programPoint.toString());
        
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

        SPDLOG_TRACE("Executing continuous defines: {}", startWarp.programPoint.toString());

        SymbolSegmentPtr segment = SymbolSegment::make();

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
                segment->define(ObjectSymbol{nameStr, startWarp.programPoint, value});
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
                segment->define(FunctionSymbol{nameStr, startWarp.programPoint, std::move(paramsStrs), body});
            }
            else if (node.isSymbol(lang.preproc_undef_s))
            {
                TSNode name = node.childByFieldId(lang.preproc_undef_s.name_f);
                std::string_view nameStr = name.textView();
                segment->define(UndefinedSymbol{nameStr});
            }
            else assert(false);
        }

        startWarp.defineAll(segment);

        return std::move(startWarp);
    }

    Warp executeCTokens(Warp && startWarp)
    {
        // All possible node types:
        // c_tokens
        assert(startWarp.programPoint.node.isSymbol(lang.c_tokens_s));

        const auto & [programPoint, states] = startWarp;
        const auto & [includeTree, node] = programPoint;

        if (!analyzeInvocations)
        {
            // Just skip the c_tokens node.
            startWarp.programPoint = startWarp.programPoint.nextSibling();
            return std::move(startWarp);
        }

        for (const TSNode & token : node.iterateChildren())
        {
            if (!token.isSymbol(lang.identifier_s)) continue;
            std::string_view name = token.textView();
            PremiseTree * premiseTreeNode = nullptr;
            z3::expr unexpandedPremise = ctx->bool_val(false);
            // defProgramPoint -> collectedNestedExpansionDefinitions
            std::unordered_map
            <
                ProgramPoint,
                std::pair<std::vector<ProgramPoint>, z3::expr>,
                ProgramPoint::Hasher
            > nestedExpansionUniformityChecker;
            for (const State & state : states)
            {
                const auto & [symbolTable, premise] = state;
                if (std::optional<Hayroll::Symbol> symbol = symbolTable->lookup(name))
                {
                    if (std::holds_alternative<ObjectSymbol>(*symbol) || std::holds_alternative<FunctionSymbol>(*symbol))
                    {
                        ProgramPoint defProgramPoint = symbolProgramPoint(*symbol);
                        if (!premiseTreeNode)
                        {
                            premiseTreeNode = scribe.createNode({includeTree, token}, ctx->bool_val(true));
                        }
                        premiseTreeNode->disjunctMacroPremise(defProgramPoint, premise);

                        std::vector<ProgramPoint> nestedExpansionDefinitions = macroExpander.collectNestedExpansionDefinitions(token, symbolTable);
                        SPDLOG_TRACE("Nested expansion definitions for token {}", name);
                        for (const ProgramPoint & nestedExpansion : nestedExpansionDefinitions)
                        {
                            SPDLOG_TRACE("  {}", nestedExpansion.toString());
                        }
                        if (auto it = nestedExpansionUniformityChecker.find(defProgramPoint); it == nestedExpansionUniformityChecker.end())
                        {
                            nestedExpansionUniformityChecker.emplace(defProgramPoint, std::make_pair(nestedExpansionDefinitions, premise));
                        }
                        else
                        {
                            // Check if the nested expansion definitions are the same.
                            std::vector<ProgramPoint> & existingDefinitions = it->second.first;
                            z3::expr & existingPremise = it->second.second;
                            if (existingDefinitions != nestedExpansionDefinitions)
                            {
                                std::string existingDefinitionsStr;
                                for (const ProgramPoint & def : existingDefinitions)
                                {
                                    existingDefinitionsStr += std::format
                                    (
                                        "{} {}\n",
                                        def.toString(),
                                        def.node.childByFieldId(lang.preproc_def_s.name_f).textView()
                                    );
                                }
                                std::string nestedExpansionDefinitionsStr;
                                for (const ProgramPoint & def : nestedExpansionDefinitions)
                                {
                                    nestedExpansionDefinitionsStr += std::format
                                    (
                                        "{} {}\n",
                                        def.toString(),
                                        def.node.childByFieldId(lang.preproc_def_s.name_f).textView()
                                    );
                                }
                                SPDLOG_WARN
                                (
                                    "Nested expansion definitions for token {} at {} are not uniform across states:\n"
                                    "Existing Premise: {}\n"
                                    "Existing Definitions:\n{}"
                                    "New Premise: {}\n"
                                    "New Definitions:\n{}",
                                    name,
                                    token.startPoint().toString(),
                                    simplifyOrOfAnd(existingPremise).to_string(),
                                    existingDefinitionsStr,
                                    simplifyOrOfAnd(premise).to_string(),
                                    nestedExpansionDefinitionsStr
                                );
                            }
                        }
                    }
                    else if (std::holds_alternative<UndefinedSymbol>(*symbol))
                    {
                        unexpandedPremise = unexpandedPremise || premise;
                    }
                    else assert(false);
                }
                else
                {
                    unexpandedPremise = unexpandedPremise || premise;
                }
            }

            // For tokens that are at least sometimes expanded, and also sometimes not expanded,
            // take down when it is not expanded.
            if (premiseTreeNode && z3Check(unexpandedPremise) == z3::sat)
            {
                premiseTreeNode->disjunctMacroPremise({includeTree, TSNode{}}, unexpandedPremise);
            }
        }
        
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
        const auto & [includeTree, node] = programPoint;
        assert(node.isSymbol(lang.preproc_if_s) || node.isSymbol(lang.preproc_ifdef_s) || node.isSymbol(lang.preproc_ifndef_s));

        SPDLOG_TRACE("Executing conditional: {}", programPoint.toString());
        
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

            // ifPremise -> ||(enterThenPremise)
            std::unordered_map<z3::expr, z3::expr, Z3ExprHash, Z3ExprEqual> premiseCollector;
            // Collect premises of states under whose symbol table the expanded ifPremise is identical.
            auto collectPremise = [&premiseCollector](const z3::expr & expandedIfPremise, const z3::expr statePremise)
            {
                auto it = premiseCollector.find(expandedIfPremise);
                if (it == premiseCollector.end()) premiseCollector.emplace(expandedIfPremise, statePremise);
                else it->second = it->second || statePremise;
            };
            // Aggregate and simplify by each key.
            auto aggregatePremise = [&premiseCollector, this]() -> z3::expr
            {
                z3::expr result = ctx->bool_val(false);
                for (const auto & [expandedIfPremise, statePremise] : premiseCollector)
                {
                    SPDLOG_TRACE
                    (
                        "Aggregating premise:\nexpandedIfPremise:\n{}\nstatePremise:\n{}\nsimplified:\n{}",
                        expandedIfPremise.to_string(),
                        statePremise.to_string(),
                        simplifyOrOfAnd(statePremise).to_string()
                    );
                    result = result || (expandedIfPremise && simplifyOrOfAnd(statePremise));
                }
                return simplifyOrOfAnd(result);
            };

            // Split each state and put them into the then and else warps.
            for (State & state : states)
            {
                const auto & [symbolTable, premise] = state;

                z3::expr ifPremise = macroExpander.symbolizeToBoolExpr(tokenList, symbolTable, prepend);
                z3::expr enterThenPremise = premise && ifPremise;
                z3::expr elsePremise = !ifPremise;
                z3::expr enterElsePremise = premise && elsePremise;

                collectPremise(ifPremise, premise);

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
                    PremiseTree * premiseTreeNode = scribe.createNode({includeTree, body}, ctx->bool_val(false));
                    premiseTreeNode->disjunctPremise(aggregatePremise());
                }
                std::vector<Warp> warps = collectIfBodies(std::move(elseWarp));
                warps.push_back(std::move(thenWarp));
                return warps;
            }
            else if (!thenWarp.states.empty()) // Only then branch possible
            {
                if (body)
                {
                    PremiseTree * premiseTreeNode = scribe.createNode({includeTree, body}, ctx->bool_val(false));
                    premiseTreeNode->disjunctPremise(aggregatePremise());
                }
                // Call the scribe to mark the else body as unreachable, since we are not going to recurse into it.
                if (alternative)
                {
                    scribe.createNode({includeTree, alternative}, ctx->bool_val(false));
                }
                return {std::move(thenWarp)};
            }
            else if (!elseWarp.states.empty()) // Only else branch possible
            {
                // Call the scribe to mark the then body as unreachable.
                if (body)
                {
                    scribe.createNode({includeTree, body}, ctx->bool_val(false));
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
                PremiseTree * premiseTreeNode = scribe.createNode({includeTree, body}, ctx->bool_val(false));
                z3::expr tempPremise = ctx->bool_val(false);
                for (const State & state : states)
                {
                    tempPremise = tempPremise || state.premise;
                }
                premiseTreeNode->disjunctPremise(simplifyOrOfAnd(tempPremise));
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

    std::optional<Warp> executeInclude(Warp && startWarp)
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

            std::optional<std::filesystem::path> optionalIncludePath = includeResolver.resolveInclude(isSystemInclude, pathStr, includeTree->getAncestorDirs());
            if (!optionalIncludePath)// Include not found, process as error.
            {
                z3::expr disallowed = ctx->bool_val(false);
                for (const State & state : states)
                {
                    disallowed = disallowed || state.premise;
                }
                scribe.conjunctPremiseOntoRoot(!simplifyOrOfAnd(disallowed));
                SPDLOG_TRACE("Include not found: {}, disallowed premise: {}", pathStr, disallowed.to_string());
                return std::nullopt;
            }
            std::filesystem::path includePath = *optionalIncludePath;
            if (includePath.string().starts_with(projPath.string())) // Header is in project path, execute symbolically. 
            {
                // Include found, add it to the AST bank and create a new state for it.
                const TSTree & tree = astBank.addFileOrFind(includePath);
                TSNode root = tree.rootNode();
                startWarp.programPoint = {includeTree->addChild(node, includePath), root};
                // Print the startWarp for debugging.
                SPDLOG_TRACE("Executing include symbolically: {}", startWarp.programPoint.toString());
                // Init the premise tree node with a false premise.
                PremiseTree * premiseTreeNode = scribe.createNode(startWarp.programPoint, ctx->bool_val(false));
                z3::expr tempPremise = ctx->bool_val(false);
                for (const State & state : states)
                {
                    tempPremise = tempPremise || state.premise;
                }
                premiseTreeNode->disjunctPremise(simplifyOrOfAnd(tempPremise));
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
                SPDLOG_TRACE("Executing include concretely: {}", startWarp.programPoint.toString());
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
        assert(startWarp.programPoint.node.isSymbol(lang.preproc_error_s));
        SPDLOG_TRACE("Executing error, keeping state: {}", startWarp.toString());
        z3::expr disallowed = ctx->bool_val(false);
        for (State & state : startWarp.states)
        {
            disallowed = disallowed || state.premise;
        }
        if (z3CheckTautology(disallowed))
        {
            SPDLOG_WARN("All states lead to #error at {}.", startWarp.programPoint.toString());
        }
        scribe.conjunctPremiseOntoRoot(!simplifyOrOfAnd(disallowed));
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
            SPDLOG_TRACE("Executing in lock step:");
            for (const Warp & warp : startWarps)
            {
                SPDLOG_TRACE("Warp: {}", warp.programPoint.toString());
            }
            SPDLOG_TRACE("Join point: {}", joinPoint.toString());
        #endif

        std::vector<std::tuple<ProgramPoint, Warp>> tasks; // (block_items/translation_unit body, warp)
        std::vector<Warp> blockedWarps;

        // Initialize the tasks with the start warps.
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
            SPDLOG_TRACE("Tasks left: {}", tasks.size());
            // Take one task from the queue (std::move). 
            auto [body, warp] = std::move(tasks.back()); // Do not use auto && here, or there will be memory corruption.
            tasks.pop_back();
            if (!warp.programPoint.node)
            {
                blockedWarps.push_back(std::move(warp));
            }
            else if (auto result = executeOne(std::move(warp)); result)
            {
                tasks.emplace_back(body, std::move(*result));
            }
        }

        #if DEBUG
        {
            SPDLOG_TRACE("Blocked warps ({}):", blockedWarps.size());
            SPDLOG_TRACE("Join point: {}", joinPoint.toString());
            for (const Warp & warp : blockedWarps)
            {
                SPDLOG_TRACE("{}", warp.toString());
            }
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
        {
            SPDLOG_TRACE("Merged states ({}):", mergedStates.size());
            SPDLOG_TRACE("Join point: {}", joinPoint.toString());
            for (const State & state : mergedStates)
            {
                SPDLOG_TRACE("{}", state.toString());
            }
        }
        #endif

        SPDLOG_TRACE("Total symbol segments: {}", SymbolSegment::totalSymbolSegments);
        SPDLOG_TRACE("Total symbols: {}", SymbolSegment::totalSymbols);
        SPDLOG_TRACE("Total symbol tables: {}", SymbolTable::totalSymbolTables);
        
        return {joinPoint, std::move(mergedStates)};
    }
};

} // namespace Hayroll

#endif // HAYROLL_SYMBOLICEXECUTOR_HPP
