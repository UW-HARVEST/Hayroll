#ifndef HAYROLL_SEEDER_HPP
#define HAYROLL_SEEDER_HPP

#include <iostream>
#include <fstream>
#include <string>
#include <string_view>
#include <sstream>
#include <vector>
#include <list>
#include <tuple>
#include <filesystem>
#include <format>
#include <algorithm>
#include <cstdint>

#include <spdlog/spdlog.h>
#include "json.hpp"

#include "Util.hpp"
#include "SymbolicExecutor.hpp"
#include "LineMatcher.hpp"
#include "TextEditor.hpp"
#include "MakiSummary.hpp"

using namespace nlohmann;

namespace Hayroll
{

class Seeder
{
public:
    // InstrumentationTask will be transformed into TextEditor edits
    struct InstrumentationTask
    {
        int line;
        int col;
        std::string str;
        int priority; // Lower value means before

        void addToEditor(TextEditor & editor) const
        {
            editor.insert(line, col, str, priority);
        }

        std::string toString() const
        {
            return std::format("{}:{}:({}) {} ", line, col, priority, str);
        }
    };

    // Build InstrumentationTasks based on AST kind, lvalue-ness, insertion positions, and tag string literals
    // This function encapsulates the pure string-edit generation logic and does not depend on other InvocationTag fields.
    static std::list<InstrumentationTask> genInstrumentationTasks
    (
        std::string_view astKind,
        std::optional<bool> isLvalue,
        int beginLine,
        int beginCol,
        int endLine,
        int endCol,
        std::string_view tagBeginLiteral,
        std::optional<std::string_view> tagEndLiteral,
        std::string_view spelling,
        int priorityLeft
    )
    {
        // Data validation
        assertWithTrace(!astKind.empty());
        assertWithTrace((astKind == "Expr") == isLvalue.has_value());
        assertWithTrace((astKind == "Stmt" || astKind == "Stmts") == tagEndLiteral.has_value());

        int priorityRight = -priorityLeft;

        std::list<InstrumentationTask> tasks;

        if (astKind == "Expr")
        {
            if (isLvalue.value())
            {
                // Template:
                // (*((*tagBegin)?(&(ORIGINAL_INVOCATION)):((__typeof__(spelling)*)(0))))
                InstrumentationTask taskLeft
                {
                    .line = beginLine,
                    .col = beginCol,
                    .str =
                    (
                        std::stringstream()
                        << "(*((*"
                        << tagBeginLiteral
                        << ")?(&("
                    ).str(),
                    .priority = priorityLeft
                };
                InstrumentationTask taskRight
                {
                    .line = endLine,
                    .col = endCol,
                    .str =
                    (
                        std::stringstream()
                        << ")):((__typeof__("
                        << spelling
                        << ")*)(0))))"
                    ).str(),
                    .priority = priorityRight
                };
                tasks.push_back(taskLeft);
                tasks.push_back(taskRight);
            }
            else // rvalue
            {
                // Template:
                // ((*tagBegin)?(ORIGINAL_INVOCATION):(*(__typeof__(spelling)*)(0)))
                InstrumentationTask taskLeft
                {
                    .line = beginLine,
                    .col = beginCol,
                    .str =
                    (
                        std::stringstream()
                        << "((*"
                        << tagBeginLiteral
                        << ")?("
                    ).str(),
                    .priority = priorityLeft
                };
                InstrumentationTask taskRight
                {
                    .line = endLine,
                    .col = endCol,
                    .str =
                    (
                        std::stringstream()
                        << "):(*(__typeof__("
                        << spelling
                        << ")*)(0)))"
                    ).str(),
                    .priority = priorityRight
                };
                tasks.push_back(taskLeft);
                tasks.push_back(taskRight);
            }
        }
        else if (astKind == "Stmt" || astKind == "Stmts")
        {
            // Template:
            // {*tagBegin;ORIGINAL_INVOCATION;*tagEnd;}
            InstrumentationTask taskLeft
            {
                .line = beginLine,
                .col = beginCol,
                .str =
                (
                    std::stringstream()
                    << "{*"
                    << tagBeginLiteral
                    << ";"
                ).str(),
                .priority = priorityLeft
            };
            InstrumentationTask taskRight
            {
                .line = endLine,
                .col = endCol,
                .str =
                (
                    std::stringstream()
                    << ";*"
                    << tagEndLiteral.value()
                    << ";}"
                ).str(),
                .priority = priorityRight
            };
            tasks.push_back(taskLeft);
            tasks.push_back(taskRight);
        }
        else if (astKind == "Decl" || astKind == "Decls")
        {
            // Template:
            // ORIGINAL_INVOCATION const char * HAYROLL_TAG_FOR_<uid> = tagBegin;\n
            
            // generate uid from locations and a short hash of tagBeginLiteral
            // Use lower 32 bits of std::hash and hex-encode to keep it compact
            uint32_t hash32 = static_cast<uint32_t>(std::hash<std::string_view>{}(tagBeginLiteral));
            std::string hashHex = std::format("{:08x}", hash32);
            std::string uid = std::format
            (
                "{}_{}_{}_{}_{}",
                beginLine, beginCol, endLine, endCol,
                hashHex
            );

            InstrumentationTask taskLeft
            {
                .line = beginLine,
                .col = endCol, // Avoid affecting ORIGINAL_INVOCATION's column
                .str =
                (
                    std::stringstream()
                    << " const char * HAYROLL_TAG_FOR_"
                    << uid
                    << " = "
                    << tagBeginLiteral
                    << ";"
                ).str(),
                .priority = 0 // Neutral
            };
            tasks.push_back(taskLeft);
            // Only one tag per declaration(s).
        }
        // else ignore unknown AST kinds

        return tasks;
    }


    // InvocationTag structure to be serialized and instrumented into C code
    // Contains necessary information for Hayroll Reaper on the Rust side to reconstruct macros
    struct InvocationTag
    {
        const std::string_view hayroll = "invocation";
        bool begin;
        bool isArg;
        std::vector<std::string> argNames;
        std::string astKind;
        bool isLvalue;
        std::string name;
        std::string locBegin; // For invocation: invocation begin; for arg: arg begin
        std::string locEnd;   // For invocation: invocation end; for arg: arg end
        std::string cuLnColBegin; // Loc in the CU file, without filename (only "l:c")
        std::string cuLnColEnd;
        std::string locRefBegin; // For invocation: definition begin; for arg: invocation begin

        bool canBeFn;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_ONLY_SERIALIZE
        (
            InvocationTag,
            hayroll, begin, isArg, argNames, astKind, isLvalue, name, locBegin, locEnd, 
            cuLnColBegin, cuLnColEnd, locRefBegin, canBeFn
        );

        // Escape the JSON string to make it a valid C string that embeds into C code
        std::string stringLiteral() const
        {
            json j = *this;
            return "\"" + escapeString(j.dump()) + "\"";
        }
    };

    // Generate instrumentation tasks based on the provided parameters
    static std::list<InstrumentationTask> genInvocationInstrumentationTasks
    (
        std::string_view locBegin,
        std::string_view locEnd,
        bool isArg,
        const std::vector<std::string> & argNames,
        std::string_view astKind,
        bool isLvalue,
        std::string_view name,
        std::string_view locRefBegin,
        std::string_view spelling,
        bool canBeFn,
        const std::vector<std::pair<IncludeTreePtr, int>> & inverseLineMap
    )
    {
        auto [path, line, col] = parseLocation(locBegin);
        auto [pathEnd, lineEnd, colEnd] = parseLocation(locEnd);
        auto [locRefPath, locRefLine, locRefCol] = parseLocation(locRefBegin);
        assert(path == pathEnd);
        assert(locRefPath == path); // Should all be the only CU file

        // Map the compilation unit line numbers back to the source file line numbers
        auto [includeTree, srcLine] = inverseLineMap.at(line);
        auto [includeTreeEnd, srcLineEnd] = inverseLineMap.at(lineEnd);
        auto [locRefIncludeTree, locRefSrcLine] = inverseLineMap.at(locRefLine);
        assert(includeTree == includeTreeEnd);
        // Non-project include filtering should have been done
        assert(includeTree && !includeTree->isSystemInclude && locRefIncludeTree && !locRefIncludeTree->isSystemInclude);

        std::string srcLocBegin = LineMatcher::cuLocToSrcLoc(locBegin, inverseLineMap);
        std::string srcLocEnd = LineMatcher::cuLocToSrcLoc(locEnd, inverseLineMap);
        std::string srcLocRefBegin = LineMatcher::cuLocToSrcLoc(locRefBegin, inverseLineMap);
        std::string cuLnColBegin = std::format("{}:{}", line, col);
        std::string cuLnColEnd = std::format("{}:{}", lineEnd, colEnd);

        InvocationTag tagBegin
        {
            .begin = true,
            .isArg = isArg,
            .argNames = argNames,
            .astKind = std::string(astKind),
            .isLvalue = isLvalue,
            .name = std::string(name),
            .locBegin = srcLocBegin,
            .locEnd = srcLocEnd,
            .cuLnColBegin = cuLnColBegin,
            .cuLnColEnd = cuLnColEnd,
            .locRefBegin = srcLocRefBegin,

            .canBeFn = canBeFn
        };

        InvocationTag tagEnd = tagBegin;
        tagEnd.begin = false;

        return genInstrumentationTasks
        (
            astKind,
            astKind == "Expr" ? std::optional(isLvalue) : std::nullopt,
            line,
            col,
            lineEnd,
            colEnd,
            tagBegin.stringLiteral(),
            (astKind == "Stmt" || astKind == "Stmts") ? std::optional(tagEnd.stringLiteral()) : std::nullopt,
            spelling,
            1 // priorityLeft: prefer inside
        );
    }

    // Generate tags for the arguments
    static std::list<InstrumentationTask> collectArgInstrumentationTasks
    (
        const MakiArgSummary & arg,
        const std::vector<std::pair<IncludeTreePtr, int>> & inverseLineMap
    )
    {
        return genInvocationInstrumentationTasks
        (
            arg.ActualArgLocBegin,
            arg.ActualArgLocEnd,
            true, // isArg
            {}, // argNames
            arg.ASTKind,
            arg.IsLValue,
            arg.Name,
            arg.InvocationLocation,
            arg.Spelling,
            false, // canBeFn
            inverseLineMap
        );
    }

    // HAYROLL original concept
    // Whether the function can be turned into a Rust function
    static bool canBeRustFn(const MakiInvocationSummary & inv)
    {
        return 
            !(
                // Syntactic
                !inv.isAligned()

                // Scoping
                || !inv.IsHygienic
                // || inv.IsInvokedWhereModifiableValueRequired // HAYROLL can handle lvalues
                // || inv.IsInvokedWhereAddressableValueRequired // HAYROLL can handle lvalues
                // || inv.IsAnyArgumentExpandedWhereModifiableValueRequired // HAYROLL can handle lvalues
                // || inv.IsAnyArgumentExpandedWhereAddressableValueRequired // HAYROLL can handle lvalues
                // || inv.DoesBodyReferenceMacroDefinedAfterMacro // Don't worry about nested macros for now
                // || inv.DoesBodyReferenceDeclDeclaredAfterMacro // Any local decls would trigger this. It's fine as long as it's hygienic.
                || inv.DoesSubexpressionExpandedFromBodyHaveLocalType
                // || inv.DoesSubexpressionExpandedFromBodyHaveTypeDefinedAfterMacro // Don't worry about declaration sequences in Rust. We can put the function at the end. 
                // || inv.IsAnyArgumentTypeDefinedAfterMacro // Don't worry about declaration sequences in Rust. We can put the function at the end. 
                || inv.IsAnyArgumentTypeLocalType

                // Typing
                || inv.IsExpansionTypeAnonymous
                || inv.IsAnyArgumentTypeAnonymous
                // || inv.DoesSubexpressionExpandedFromBodyHaveLocalType // Repetition
                // || inv.IsAnyArgumentTypeDefinedAfterMacro // Repetition
                // || inv.DoesSubexpressionExpandedFromBodyHaveTypeDefinedAfterMacro  // Repetition
                || inv.IsAnyArgumentTypeVoid
                || (inv.IsObjectLike && inv.IsExpansionTypeVoid)
                // || IsAnyArgumentTypeLocalType // Repetition

                // Calling convention
                || inv.DoesAnyArgumentHaveSideEffects
                || inv.IsAnyArgumentConditionallyEvaluated

                // Language specific
                || inv.mustUseMetaprogrammingToTransform()
            );
    }

    static bool requiresLvalue(const MakiArgSummary & inv)
    {
        return inv.ExpandedWhereAddressableValueRequired || inv.ExpandedWhereModifiableValueRequired;
    }

    // Collect the instrumentation tasks for the invocation body and its arguments
    static std::list<InstrumentationTask> collectBodyInstrumentationTasks
    (
        const MakiInvocationSummary & inv,
        const std::vector<std::pair<IncludeTreePtr, int>> & inverseLineMap
    )
    {
        // Skip instrumentation for system includes
        {
            auto [path, line, col] = parseLocation(inv.InvocationLocation);
            auto [locRefPath, locRefLine, locRefCol] = parseLocation(inv.DefinitionLocation);
            assert(locRefPath == path); // Should all be the only CU file
    
            // Map the compilation unit line numbers back to the source file line numbers
            auto [includeTree, srcLine] = inverseLineMap.at(line);
            auto [locRefIncludeTree, locRefSrcLine] = inverseLineMap.at(locRefLine);
            
            if (!includeTree || includeTree->isSystemInclude || !locRefIncludeTree || locRefIncludeTree->isSystemInclude)
            {
                // Either the expansion or the definition of this macro is
                // in a file that was concretely executed by Hayroll Pioneer.
                // We don't instrument this code section.
                // This may result in standard library macros not being instrumented.
                SPDLOG_TRACE
                (
                    "Skipping instrumentation for {}: {}:{} (no include tree)",
                    inv.Name, path.string(), srcLine
                );
                return {};
            }
        }

        std::list<InstrumentationTask> tasks;
        for (const MakiArgSummary & arg : inv.Args)
        {
            std::list<InstrumentationTask> argTasks = collectArgInstrumentationTasks(arg, inverseLineMap);
            tasks.splice(tasks.end(), argTasks);
        }

        std::vector<std::string> argNames;
        for (const MakiArgSummary & arg : inv.Args)
        {
            argNames.push_back(arg.Name);
        }

        std::list<InstrumentationTask> invocationTasks = genInvocationInstrumentationTasks
        (
            inv.InvocationLocation,
            inv.InvocationLocationEnd,
            false, // isArg
            argNames,
            inv.ASTKind,
            inv.IsLValue,
            inv.Name,
            inv.DefinitionLocation,
            inv.Spelling,
            canBeRustFn(inv),
            inverseLineMap
        );
        tasks.splice(tasks.end(), invocationTasks);
        
        return tasks;
    }

    // Check if the invocation info is valid and thus should be kept
    // Invalid cases: empty fields, invalid path, non-Expr/Stmt ASTKind
    static bool keepInvocationInfo(const MakiInvocationSummary & invocation)
    {
        if
        (
            invocation.DefinitionLocation.empty()
            || invocation.Name.empty()
            || invocation.ASTKind.empty()
            // || invocation.ReturnType.empty() // Decl and Decls may not have a return type
            || invocation.InvocationLocation.empty()
            || invocation.InvocationLocationEnd.empty()
        )
        {
            return false;
        }
        auto [path, line, col] = parseLocation(invocation.InvocationLocation);
        constexpr static std::string_view validASTKinds[] = {"Expr", "Stmt", "Stmts", "Decl", "Decls"};
        if (std::find(std::begin(validASTKinds), std::end(validASTKinds), invocation.ASTKind) == std::end(validASTKinds))
        {
            return false;
        }
        return true;
    }

    struct ConditionalTag
    {
        const std::string_view hayroll = "conditional";
        bool begin;
        std::string astKind;
        bool isLvalue;
        std::string locBegin;
        std::string locEnd;
        std::string cuLnColBegin;
        std::string cuLnColEnd;
        std::string locRefBegin; // Parent AST node location, for unifying same-slot expressions. This is not sound for mult-ary operators.
        std::string premise;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_ONLY_SERIALIZE
        (
            ConditionalTag,
            hayroll, begin, astKind, isLvalue, locBegin, locEnd, cuLnColBegin, cuLnColEnd, locRefBegin, premise
        )
    };

    // Tags the srcStr (C source code at compilation unit level) with the instrumentation tasks collected from
    // 1. invocations: the MakiInvocationSummary vector
    // 2. ranges: the MakiRangeSummary vector
    // Also requires the lineMap ((includeTree, line) <-> line in compilation unit file) and inverseLineMap.
    // Returns the modified (CU) source code as a string.
    static std::string run
    (
        std::vector<Hayroll::MakiInvocationSummary> invocations,
        const std::vector<Hayroll::MakiRangeSummary> & ranges,
        std::string_view srcStr,
        const std::unordered_map<Hayroll::IncludeTreePtr, std::vector<int>> & lineMap,
        const std::vector<std::pair<IncludeTreePtr, int>> & inverseLineMap
    )
    {
        // Remove invalid invocations (erase-remove idiom)
        std::erase_if(invocations, [](const MakiInvocationSummary & inv)
        {
            return !keepInvocationInfo(inv);
        });

        TextEditor srcEditor{srcStr};

        // Extract spelling for invocations and arguments
        for (MakiInvocationSummary & invocation : invocations)
        {
            auto [path, line, col] = parseLocation(invocation.InvocationLocation);
            auto [pathEnd, lineEnd, colEnd] = parseLocation(invocation.InvocationLocationEnd);
            SPDLOG_TRACE
            (
                "Extracting spelling for invocation {} at {}: {}:{}-{}:{}",
                invocation.Name,
                path.string(),
                line, col, lineEnd, colEnd
            );
            invocation.Spelling = srcEditor.get(line, col, colEnd - col);

            for (MakiArgSummary & arg : invocation.Args)
            {
                auto [argPath, argLine, argCol] = parseLocation(arg.ActualArgLocBegin);
                auto [argPathEnd, argLineEnd, argColEnd] = parseLocation(arg.ActualArgLocEnd);
                SPDLOG_TRACE
                (
                    "Extracting spelling for argument {} at {}: {}:{}-{}:{}",
                    arg.Name,
                    argPath.string(),
                    argLine, argCol, argLineEnd, argColEnd
                );
                arg.Spelling = srcEditor.get(argLine, argCol, argColEnd - argCol);
                arg.InvocationLocation = invocation.InvocationLocation;
            }
        }

        // Collect instrumentation tasks.
        std::list<InstrumentationTask> tasks;
        for (const MakiInvocationSummary & invocation : invocations)
        {
            std::list<InstrumentationTask> invocationTasks = collectBodyInstrumentationTasks(invocation, inverseLineMap);
            tasks.splice(tasks.end(), invocationTasks);
        }

        for (const InstrumentationTask & task : tasks)
        {
            SPDLOG_TRACE(task.toString());
            task.addToEditor(srcEditor);
        }
        
        return srcEditor.commit();
    }
};

} // namespace Hayroll

#endif // HAYROLL_SEEDER_HPP
