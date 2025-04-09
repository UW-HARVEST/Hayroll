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

    // Merges two states with the same symbol table, premise disjuncted. 
    // Returns std::nullopt if the symbol tables are different. 
    // The user is not supposed to even try to merge states with different include trees or nodes.
    std::optional<State> merge(const State & other) const
    {
        assert(programPoint == other.programPoint);

        // It's okay to try to merge states with different symbol tables,
        // but they won't be merged eventually.
        if (symbolTable != other.symbolTable) return std::nullopt;

        z3::expr mergedPremise = premise || other.premise;
        return State{programPoint, symbolTable, mergedPremise};
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

        premise = premise || other.premise;
        return true;
    }

    std::string toString() const
    {
        return std::format
        (
            "State:\nprogramPoint:\n{}\nsymbolTable:\n{}premise:\n{}",
            programPoint.toStringFull(),
            symbolTable->toStringOneLayer(),
            premise.to_string()
        );
    }
};

class SymbolicExecutor
{
public:
    SymbolicExecutor(std::filesystem::path srcPath)
        : lang(CPreproc()), ctx(z3::context()), srcPath(srcPath), includeResolver(CLANG_EXE, {}),
          astBank(lang), macroExpander(lang, ctx), includeTree(IncludeTree::make(0, srcPath))
    {
        astBank.addFile(srcPath);
    }

    std::vector<State> run()
    {
        // Generate a base symbol table with the predefined macros.
        std::string predefinedMacros = includeResolver.getPredefinedMacros();
        const TSTree & predefinedMacroTree = astBank.addTempString(std::move(predefinedMacros));
        State predefinedMacroState{IncludeTree::make(0, "<PREDEFINED_MACROS>"), predefinedMacroTree.rootNode(), SymbolTable::make(), ctx.bool_val(true)};
        std::vector<State> predefinedMacroStates = executeTranslationUnit(std::move(predefinedMacroState));
        assert(predefinedMacroStates.size() == 1);
        ConstSymbolTablePtr predefinedMacroSymbolTable = predefinedMacroStates[0].symbolTable;
        assert(predefinedMacroSymbolTable != nullptr);

        const TSTree & tree = astBank.find(srcPath);
        TSNode root = tree.rootNode();
        // The initial state is the root node of the tree.
        State startState{{includeTree, root}, predefinedMacroSymbolTable->makeChild(), ctx.bool_val(true)};
        std::vector<State> endStates = executeTranslationUnit(std::move(startState));
        
        return endStates;
    }

    std::vector<State> executeTranslationUnit(State && startState)
    {
        SPDLOG_DEBUG(std::format("Executing translation unit: {}", startState.programPoint.toString()));
        assert(startState.programPoint.node.isSymbol(lang.translation_unit_s));
        startState.programPoint.node = startState.programPoint.node.preorderNext();
        // All states shall meet at the end of the translation unit (invalid node).
        return executeInLockStep({std::move(startState)}, TSNode{});
    }

    // Execute a single node or a segment of continuous #define nodes. 
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
        while (node.isSymbol(lang.preproc_def_s) || node.isSymbol(lang.preproc_function_def_s) || node.isSymbol(lang.preproc_undef_s))
        {
            SPDLOG_DEBUG(std::format("Processing define: {}", node.textView()));

            if (node.isSymbol(lang.preproc_def_s))
            {
                TSNode name = node.childByFieldId(lang.preproc_def_s.name_f);
                TSNode value = node.childByFieldId(lang.preproc_def_s.value_f); // May not exist
                std::string_view nameStr = name.textView();
                startState.symbolTable->define(ObjectSymbol{nameStr, startState.programPoint, value});
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
                startState.symbolTable->define(FunctionSymbol{nameStr, startState.programPoint, std::move(paramsStrs), body});
            }
            else if (node.isSymbol(lang.preproc_undef_s))
            {
                TSNode name = node.childByFieldId(lang.preproc_undef_s.name_f);
                std::string_view nameStr = name.textView();
                startState.symbolTable->define(UndefinedSymbol{nameStr});
            }
            else assert(false);

            if (TSNode nextNode = node.nextSibling()) node = nextNode;
            else
            {
                node = node.preorderSkip();
                break;
            }
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
        startState.programPoint.node = startState.programPoint.node.preorderSkip();

        return std::move(startState);
    }
    
    std::vector<State> executeIf(State && startState)
    {
        // All possible node types:
        // preproc_if
        // preproc_ifdef
        // preproc_ifndef

        // Skip for now.
        startState.programPoint.node = startState.programPoint.node.preorderSkip();

        return {std::move(startState)};
    }

    std::vector<State> executeInclude(State && startState)
    {
        // All possible node types:
        // preproc_include
        // preproc_include_next
        
        // Skip for now.
        startState.programPoint.node = startState.programPoint.node.preorderSkip();

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
        startState.programPoint.node = startState.programPoint.node.preorderSkip();
        return std::move(startState);
    }

    // Execute startStates in lock step until they reach the join point.
    // For syncing #if nodes and entire files. 
    // When all states reach the joinPoint node, they are merged if possible.
    std::vector<State> executeInLockStep(std::vector<State> && startStates, const TSNode & joinPoint)
    {
        // Print the program points of start states for debugging.
        #if DEBUG
            SPDLOG_DEBUG(std::format("Executing in lock step:\n"));
            for (const State & state : startStates)
            {
                SPDLOG_DEBUG(std::format("{}\n", state.programPoint.toString()));
            }
        #endif

        std::vector<State> tasks = std::move(startStates);
        std::vector<State> blockedStates;

        while (!tasks.empty())
        {
            SPDLOG_DEBUG(std::format("Tasks left: {}", tasks.size()));
            // Take one task from the queue (std::move). 
            State task = std::move(tasks.back());
            tasks.pop_back();
            for (State & taskDone : executeOne(std::move(task)))
            {
                State taskDoneOwned = std::move(taskDone);
                if (taskDoneOwned.programPoint.node == joinPoint)
                {
                    blockedStates.push_back(std::move(taskDoneOwned));
                }
                else
                {
                    tasks.push_back(std::move(taskDoneOwned));
                }
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
        
        return mergedStates;
    }
    
private:
    const CPreproc lang;
    z3::context ctx;
    std::filesystem::path srcPath;
    IncludeResolver includeResolver;
    ASTBank astBank;
    MacroExpander macroExpander;
    IncludeTreePtr includeTree;
};

} // namespace Hayroll

#endif // HAYROLL_SYMBOLICEXECUTOR_HPP
