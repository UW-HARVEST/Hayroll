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
        int line; // if line == -1, append new line at the end of the file
        int col;
        bool eraseOriginal; // Whether to erase the original code before inserting the instrumentation
        bool nonErasable;
        int lineEnd; // Only for eraseOriginal tasks
        int colEnd; // Only for eraseOriginal tasks
        std::string str;
        int priority; // Lower value means before

        void addToEditor(TextEditor & editor) const
        {
            if (eraseOriginal)
            {
                editor.erase(line, col, lineEnd, colEnd, priority);
            }
            if (line == -1)
            {
                editor.append(str, priority);
            }
            else
            {
                editor.insert(line, col, str, priority);
            }
        }

        std::string toString() const
        {
            return std::format("{}:{}:({}) {} ", line, col, priority, str);
        }
    };

    // CRTP mixin that provides stringLiteral() for any type that can be serialized by nlohmann::json
    // Requirement: Derived must have an ADL-visible to_json(json&, const Derived&) (provided by NLOHMANN_* macros)
    template <typename Derived>
    struct JsonStringLiteralMixin
    {
        // Escape the JSON string to make it a valid C string that embeds into C code
        std::string stringLiteral() const
        {
            const Derived & self = static_cast<const Derived &>(*this);
            json j = self; // triggers ADL to_json for Derived
            return "\"" + escapeString(j.dump()) + "\"";
        }
    };

    // Build InstrumentationTasks based on AST kind, lvalue-ness, insertion positions, and tag string literals
    static std::list<InstrumentationTask> genInstrumentationTasks
    (
        std::string_view astKind,
        std::optional<bool> isLvalue,
        int beginLine,
        int beginCol,
        int endLine,
        int endCol,
        bool eraseOriginal,
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
                    .eraseOriginal = eraseOriginal,
                    .nonErasable = eraseOriginal,
                    .lineEnd = endLine,
                    .colEnd = endCol,
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
                    .eraseOriginal = false,
                    .nonErasable = eraseOriginal,
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
                    .eraseOriginal = eraseOriginal,
                    .nonErasable = eraseOriginal,
                    .lineEnd = endLine,
                    .colEnd = endCol,
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
                    .eraseOriginal = false,
                    .nonErasable = eraseOriginal,
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
                .eraseOriginal = eraseOriginal,
                .nonErasable = eraseOriginal,
                .lineEnd = endLine,
                .colEnd = endCol,
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
                .eraseOriginal = false,
                .nonErasable = eraseOriginal,
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
                .line = -1, // Append at the end of the file
                .eraseOriginal = eraseOriginal,
                .nonErasable = eraseOriginal,
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
        else
        {
            SPDLOG_ERROR("Unsupported AST kind for instrumentation: {}", astKind);
            assertWithTrace(false);
        }

        return tasks;
    }

    // InvocationTag structure to be serialized and instrumented into C code
    // Contains necessary information for Hayroll Reaper on the Rust side to reconstruct macros
    struct InvocationTag : JsonStringLiteralMixin<InvocationTag>
    {
        bool hayroll = true;
        const std::string_view seedType = "invocation";
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
            hayroll, seedType, begin, isArg, argNames, astKind, isLvalue, name, locBegin, locEnd,
            cuLnColBegin, cuLnColEnd, locRefBegin, canBeFn
        );
    };

    // Generate instrumentation tasks based on the provided parameters
    static std::list<InstrumentationTask> genBodyInstrumentationTasks
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
        auto [pathBegin, lineBegin, colBegin] = parseLocation(locBegin);
        auto [pathEnd, lineEnd, colEnd] = parseLocation(locEnd);
        auto [locRefPath, locRefLine, locRefCol] = parseLocation(locRefBegin);
        assert(pathBegin == pathEnd);
        assert(locRefPath == pathBegin); // Should all be the only CU file

        // Map the compilation unit line numbers back to the source file line numbers
        auto [includeTree, srcLine] = inverseLineMap.at(lineBegin);
        auto [includeTreeEnd, srcLineEnd] = inverseLineMap.at(lineEnd);
        auto [locRefIncludeTree, locRefSrcLine] = inverseLineMap.at(locRefLine);
        assert(includeTree == includeTreeEnd);
        // Non-project include filtering should have been done
        assert(includeTree && !includeTree->isSystemInclude && locRefIncludeTree && !locRefIncludeTree->isSystemInclude);

        std::string srcLocBegin = LineMatcher::cuLocToSrcLoc(locBegin, inverseLineMap);
        std::string srcLocEnd = LineMatcher::cuLocToSrcLoc(locEnd, inverseLineMap);
        std::string srcLocRefBegin = LineMatcher::cuLocToSrcLoc(locRefBegin, inverseLineMap);
        std::string cuLnColBegin = locToLnCol(locBegin);
        std::string cuLnColEnd = locToLnCol(locEnd);

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
            lineBegin,
            colBegin,
            lineEnd,
            colEnd,
            false, // Do not erase original for body instrumentation
            tagBegin.stringLiteral(),
            (astKind == "Stmt" || astKind == "Stmts") ? std::optional(tagEnd.stringLiteral()) : std::nullopt,
            spelling,
            1 // priorityLeft: prefer inside
        );
    }

    // Generate tags for the arguments
    static std::list<InstrumentationTask> genArgInstrumentationTasks
    (
        const MakiArgSummary & arg,
        const std::vector<std::pair<IncludeTreePtr, int>> & inverseLineMap
    )
    {
        return genBodyInstrumentationTasks
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
    static std::list<InstrumentationTask> genInvocationInstrumentationTasks
    (
        const MakiInvocationSummary & inv,
        const std::vector<std::pair<IncludeTreePtr, int>> & inverseLineMap
    )
    {
        std::list<InstrumentationTask> tasks;
        for (const MakiArgSummary & arg : inv.Args)
        {
            std::list<InstrumentationTask> argTasks = genArgInstrumentationTasks(arg, inverseLineMap);
            tasks.splice(tasks.end(), argTasks);
        }

        std::vector<std::string> argNames;
        for (const MakiArgSummary & arg : inv.Args)
        {
            argNames.push_back(arg.Name);
        }

        std::list<InstrumentationTask> invocationTasks = genBodyInstrumentationTasks
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

    static std::list<InstrumentationTask> genConditionalInstrumentationTasks
    (
        const MakiRangeSummary & range,
        const std::vector<std::pair<IncludeTreePtr, int>> & inverseLineMap
    )
    {
        auto [pathBegin, lineBegin, colBegin] = parseLocation(range.Location);
        auto [pathEnd, lineEnd, colEnd] = parseLocation(range.LocationEnd);
        assert(pathBegin == pathEnd);
        std::string srcLocBegin = LineMatcher::cuLocToSrcLoc(range.Location, inverseLineMap);
        std::string srcLocEnd = LineMatcher::cuLocToSrcLoc(range.LocationEnd, inverseLineMap);
        std::string cuLnColBegin = locToLnCol(range.Location);
        std::string cuLnColEnd = locToLnCol(range.LocationEnd);
        std::string locRefBegin = range.ReferenceLocation;
        auto [ifGroupLnBegin, ifGroupColBegin] = parseLnCol(range.ExtraInfo.ifGroupLnColBegin);
        auto [ifGroupLnEnd, ifGroupColEnd] = parseLnCol(range.ExtraInfo.ifGroupLnColEnd);

        ConditionalTag tagBegin
        {
            .begin = true,
            .astKind = range.ASTKind,
            .isLvalue = range.IsLValue,
            .locBegin = srcLocBegin,
            .locEnd = srcLocEnd,
            .cuLnColBegin = cuLnColBegin,
            .cuLnColEnd = cuLnColEnd,
            .locRefBegin = locRefBegin,
            .isPlaceholder = range.IsPlaceholder,
            .premise = range.ExtraInfo.premise,
            .mergedVariants = { srcLocBegin }
        };
        ConditionalTag tagEnd = tagBegin;
        tagEnd.begin = false;

        return genInstrumentationTasks
        (
            range.ASTKind,
            range.ASTKind == "Expr" ? std::optional(range.IsLValue) : std::nullopt,
            range.IsPlaceholder ? ifGroupLnBegin : lineBegin, // for placeholder ranges, enclose the whole #if group
            range.IsPlaceholder ? ifGroupColBegin : colBegin,
            range.IsPlaceholder ? ifGroupLnEnd : lineEnd,
            range.IsPlaceholder ? ifGroupColEnd : colEnd,
            range.IsPlaceholder, // Erase original when it's a placeholder, to avoid the tag being excluded from compilation
            tagBegin.stringLiteral(),
            (range.ASTKind == "Stmt" || range.ASTKind == "Stmts") ? std::optional(tagEnd.stringLiteral()) : std::nullopt,
            range.Spelling,
            -1 // priorityLeft: prefer outside
        );
    }

    // Check if the invocation info is valid and thus should be kept
    // Invalid cases: empty fields, invalid path, non-Expr/Stmt ASTKind
    static bool dropInvocationSummary
    (
        const MakiInvocationSummary & invocation,
        const std::vector<std::pair<IncludeTreePtr, int>> & inverseLineMap
    )
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
            return true;
        }
        auto [path, line, col] = parseLocation(invocation.InvocationLocation);
        constexpr static std::string_view validASTKinds[] = {"Expr", "Stmt", "Stmts", "Decl", "Decls"};
        if (std::find(std::begin(validASTKinds), std::end(validASTKinds), invocation.ASTKind) == std::end(validASTKinds))
        {
            return true;
        }
        for (const MakiArgSummary & arg : invocation.Args)
        {
            if
            (
                arg.ASTKind.empty()
                || arg.Name.empty()
                || arg.ActualArgLocBegin.empty()
                || arg.ActualArgLocEnd.empty()
            )
            {
                return true;
            }
            auto [argPath, argLine, argCol] = parseLocation(arg.ActualArgLocBegin);
            if (argPath != path)
            {
                return true;
            }
            auto [argPathEnd, argLineEnd, argColEnd] = parseLocation(arg.ActualArgLocEnd);
            if (argPathEnd != path)
            {
                return true;
            }
        }
        // System-include filtering using inverseLineMap
        {
            auto [locRefPath, locRefLine, locRefCol] = parseLocation(invocation.DefinitionLocation);
            assert(locRefPath == path);
            auto [includeTree, srcLine] = inverseLineMap.at(line);
            auto [locRefIncludeTree, locRefSrcLine] = inverseLineMap.at(locRefLine);
            if (!includeTree || includeTree->isSystemInclude || !locRefIncludeTree || locRefIncludeTree->isSystemInclude)
            {
                SPDLOG_TRACE
                (
                    "Skipping instrumentation for {}: {}:{} (no include tree)",
                    invocation.Name, path.string(), srcLine
                );
                return true;
            }
        }
        return false;
    }

    struct ConditionalTag : JsonStringLiteralMixin<ConditionalTag>
    {
        bool hayroll = true;
        const std::string_view seedType = "conditional";
        bool begin;
        std::string astKind;
        bool isLvalue;
        std::string locBegin;
        std::string locEnd;
        std::string cuLnColBegin;
        std::string cuLnColEnd;
        // Parent AST node location, for unifying same-slot expressions. This is not sound for mult-ary operators.
        std::string locRefBegin;
        bool isPlaceholder;
        std::string premise;
        // For merger to record which variants (identified via locBegins) ar included in this seed
        std::vector<std::string> mergedVariants;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_ONLY_SERIALIZE
        (
            ConditionalTag,
            hayroll, seedType, begin, astKind, isLvalue, locBegin, locEnd,
            cuLnColBegin, cuLnColEnd, locRefBegin, isPlaceholder, premise,
            mergedVariants
        );
    };

    static bool dropRangeSummary
    (
        const MakiRangeSummary & range,
        const std::vector<std::pair<IncludeTreePtr, int>> & inverseLineMap
    )
    {
        if
        (
            range.Location.empty()
            || range.LocationEnd.empty()
            || range.ASTKind.empty()
            || range.ExtraInfo.premise.empty()
        )
        {
            return true;
        }
        auto [path, line, col] = parseLocation(range.Location);
        constexpr static std::string_view validASTKinds[] = {"Expr", "Stmt", "Stmts", "Decl", "Decls"};
        if (std::find(std::begin(validASTKinds), std::end(validASTKinds), range.ASTKind) == std::end(validASTKinds))
        {
            return true;
        }
        // System-include filtering using inverseLineMap
        {
            auto [includeTree, srcLine] = inverseLineMap.at(line);
            if (!includeTree || includeTree->isSystemInclude)
            {
                SPDLOG_TRACE
                (
                    "Skipping instrumentation for conditional premise {} at {}: {} (no include tree)",
                    range.ExtraInfo.premise, path.string(), srcLine
                );
                return true;
            }
        }
        return false;
    }

    // Tags the srcStr (C source code at compilation unit level) with the instrumentation tasks collected from
    // 1. invocations: the MakiInvocationSummary vector
    // 2. ranges: the MakiRangeSummary vector
    // Also requires the lineMap ((includeTree, line) <-> line in compilation unit file) and inverseLineMap.
    // Returns the modified (CU) source code as a string.
    static std::string run
    (
        std::vector<Hayroll::MakiInvocationSummary> invocations,
        std::vector<Hayroll::MakiRangeSummary> ranges,
        std::string_view srcStr,
        const std::unordered_map<Hayroll::IncludeTreePtr, std::vector<int>> & lineMap,
        const std::vector<std::pair<IncludeTreePtr, int>> & inverseLineMap
    )
    {
        // Remove invalid invocations and ranges
        std::erase_if(invocations, [&inverseLineMap](const MakiInvocationSummary & inv)
        {
            return dropInvocationSummary(inv, inverseLineMap);
        });
        std::erase_if(ranges, [&inverseLineMap](const MakiRangeSummary & range)
        {
            return dropRangeSummary(range, inverseLineMap);
        });

        TextEditor srcEditor{srcStr};

        // Extract spelling for invocations and arguments
        for (MakiInvocationSummary & invocation : invocations)
        {
            auto [pathBegin, lineBegin, colBegin] = parseLocation(invocation.InvocationLocation);
            auto [pathEnd, lineEnd, colEnd] = parseLocation(invocation.InvocationLocationEnd);
            SPDLOG_TRACE
            (
                "Extracting spelling for invocation {} at {}: {}:{}-{}:{}",
                invocation.Name,
                pathBegin.string(),
                lineBegin, colBegin, lineEnd, colEnd
            );
            invocation.Spelling = srcEditor.get(lineBegin, colBegin, lineEnd, colEnd);

            for (MakiArgSummary & arg : invocation.Args)
            {
                auto [argPathBegin, argLineBegin, argColBegin] = parseLocation(arg.ActualArgLocBegin);
                auto [argPathEnd, argLineEnd, argColEnd] = parseLocation(arg.ActualArgLocEnd);
                SPDLOG_TRACE
                (
                    "Extracting spelling for argument {} at {}: {}:{}-{}:{}",
                    arg.Name,
                    argPathBegin.string(),
                    argLineBegin, argColBegin, argLineEnd, argColEnd
                );
                arg.Spelling = srcEditor.get(argLineBegin, argColBegin, argLineEnd, argColEnd);
                arg.InvocationLocation = invocation.InvocationLocation;
            }
        }

        // Extract spelling for ranges
        for (MakiRangeSummary & range : ranges)
        {
            auto [pathBegin, lineBegin, colBegin] = parseLocation(range.Location);
            auto [pathEnd, lineEnd, colEnd] = parseLocation(range.LocationEnd);
            SPDLOG_TRACE
            (
                "Extracting spelling for range {} at {}: {}:{}-{}:{}",
                range.ExtraInfo.premise,
                pathBegin.string(),
                lineBegin, colBegin, lineEnd, colEnd
            );
            range.Spelling = srcEditor.get(lineBegin, colBegin, lineEnd, colEnd);
        }

        // Collect instrumentation tasks
        std::list<InstrumentationTask> tasks;
        for (const MakiInvocationSummary & invocation : invocations)
        {
            std::list<InstrumentationTask> invocationTasks = genInvocationInstrumentationTasks(invocation, inverseLineMap);
            tasks.splice(tasks.end(), invocationTasks);
        }
        for (const MakiRangeSummary & range : ranges)
        {
            std::list<InstrumentationTask> rangeTasks = genConditionalInstrumentationTasks(range, inverseLineMap);
            tasks.splice(tasks.end(), rangeTasks);
        }

        // Prevent inserting invocation tags into placeholder conditional ranges
        // Remove tasks that are overlapped by any eraseOriginal task
        // Policy: If a task A (nonErasable == false) has its position/range overlapping
        // with any task B (eraseOriginal == true), drop A. The erasing range takes precedence.
        if (!tasks.empty())
        {
            // Compare (line, col) pairs in source order
            auto isBefore = [](int l1, int c1, int l2, int c2)
            {
                return (l1 < l2) || (l1 == l2 && c1 < c2);
            };
            // Ensure (lineBegin,colBegin) is not after (lineEnd,colEnd)
            auto normalizeRange = [&](int &lineBegin, int &colBegin, int &lineEnd, int &colEnd)
            {
                if (isBefore(lineEnd, colEnd, lineBegin, colBegin))
                {
                    std::swap(lineBegin, lineEnd);
                    std::swap(colBegin, colEnd);
                }
            };

            // Collect erasing tasks having a concrete span
            std::vector<const InstrumentationTask *> erasingTasks;
            erasingTasks.reserve(tasks.size());
            for (const auto &task : tasks)
            {
                if (task.eraseOriginal && task.line >= 0 && task.lineEnd >= 0)
                {
                    erasingTasks.push_back(&task);
                }
            }

            if (!erasingTasks.empty())
            {
                for (auto it = tasks.begin(); it != tasks.end();)
                {
                    const InstrumentationTask &taskA = *it;
                    // Skip protected (nonErasable) or position-less tasks
                    if (taskA.nonErasable || taskA.line < 0)
                    {
                        ++it;
                        continue;
                    }

                    // Range for A: if not eraseOriginal treat as a point
                    int aLineBegin = taskA.line;
                    int aColBegin  = taskA.col;
                    int aLineEnd   = taskA.eraseOriginal ? taskA.lineEnd : taskA.line;
                    int aColEnd    = taskA.eraseOriginal ? taskA.colEnd  : taskA.col;
                    normalizeRange(aLineBegin, aColBegin, aLineEnd, aColEnd);

                    bool removeA = false;
                    for (const auto *erasePtr : erasingTasks)
                    {
                        const InstrumentationTask &taskB = *erasePtr;
                        int bLineBegin = taskB.line;
                        int bColBegin  = taskB.col;
                        int bLineEnd   = taskB.lineEnd;
                        int bColEnd    = taskB.colEnd;
                        normalizeRange(bLineBegin, bColBegin, bLineEnd, bColEnd);
                        // Overlap test: !(A_end < B_begin || B_end < A_begin)
                        bool aEndBeforeBBegin = isBefore(aLineEnd, aColEnd, bLineBegin, bColBegin);
                        bool bEndBeforeABegin = isBefore(bLineEnd, bColEnd, aLineBegin, aColBegin);
                        if (!(aEndBeforeBBegin || bEndBeforeABegin))
                        {
                            removeA = true;
                            break;
                        }
                    }

                    if (removeA)
                    {
                        it = tasks.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
            }
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
