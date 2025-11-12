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
#include <ranges>
#include <algorithm>
#include <optional>
#include <utility>
#include <set>
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
    using json = nlohmann::json;
    using ordered_json = nlohmann::ordered_json;

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
        std::optional<bool> isLvalue, // only when astKind == "Expr"
        std::optional<bool> createScope, // only when astKind == "Stmt" or "Stmts"
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
            // When !createScope: *tagBegin;ORIGINAL_INVOCATION;*tagEnd;
            // When createScope: {*tagBegin; ORIGINAL_INVOCATION; *tagEnd;}
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
                    << (*createScope ? "{" : "")
                    << "*"
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
                    << ";"
                    << (*createScope ? "}" : "")
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
        std::string premise;

        bool canBeFn;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_ONLY_SERIALIZE
        (
            InvocationTag,
            hayroll, seedType, begin, isArg, argNames, astKind, isLvalue, name, locBegin, locEnd,
            cuLnColBegin, cuLnColEnd, locRefBegin, premise, canBeFn
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
        bool createScope,
        std::string_view name,
        std::string_view locRefBegin,
        std::string_view spelling,
        std::string_view premise,
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
            .premise = std::string(premise),

            .canBeFn = canBeFn
        };

        InvocationTag tagEnd = tagBegin;
        tagEnd.begin = false;

        return genInstrumentationTasks
        (
            astKind,
            astKind == "Expr" ? std::optional(isLvalue) : std::nullopt,
            (astKind == "Stmt" || astKind == "Stmts") ? std::optional(createScope) : std::nullopt,
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
            false, // createScope
            arg.Name,
            arg.InvocationLocation,
            arg.Spelling,
            "", // premise
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
                || inv.ASTKind == "Decl" || inv.ASTKind == "Decls" // Declarations cannot be functions

                // Scoping
                // || !inv.IsHygienic // Unhygienic macros should have been dropped earlier
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
                // || inv.IsAnyArgumentConditionallyEvaluated // Repetition
                || inv.mustAlterCallSiteToTransform()

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
            !inv.IsInvokedInStmtBlock, // createScope
            inv.Name,
            inv.DefinitionLocation,
            inv.Spelling,
            inv.Premise,
            canBeRustFn(inv),
            inverseLineMap
        );
        tasks.splice(tasks.end(), invocationTasks);
        
        return tasks;
    }

    static std::list<InstrumentationTask> genConditionalInstrumentationTasks
    (
        const MakiRangeSummary & range,
        bool createScope,
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
            (range.ASTKind == "Stmt" || range.ASTKind == "Stmts") ? std::optional(createScope) : std::nullopt,
            range.IsPlaceholder ? ifGroupLnBegin : lineBegin, // for placeholder ranges, enclose the whole #if group
            range.IsPlaceholder ? ifGroupColBegin : colBegin,
            range.IsPlaceholder ? ifGroupLnEnd : lineEnd,
            range.IsPlaceholder ? ifGroupColEnd : colEnd,
            range.IsPlaceholder, // Erase original when it's a placeholder, to avoid the tag being excluded from compilation
            tagBegin.stringLiteral(),
            (range.ASTKind == "Stmt" || range.ASTKind == "Stmts") ? std::optional(tagEnd.stringLiteral()) : std::nullopt,
            range.Spelling,
            -ifGroupLnEnd // priorityLeft: prefer outside, and give higher priority to outer #if groups
        );
    }

    struct SeedingReport
    {
        std::string name;
        std::string locInv; // invocation location (src)
        std::string locRef; // definition location (src)
        std::string astKind;
        bool isObjectLike;
        bool seeded;
        std::set<std::string> reasons;
        bool canBeFn;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE
        (
            SeedingReport,
            name, locInv, locRef, astKind, isObjectLike, seeded, reasons, canBeFn
        );
    };

    static std::string translateCuLocOrFallback
    (
        std::string_view cuLoc,
        const std::vector<std::pair<IncludeTreePtr, int>> & inverseLineMap
    )
    {
        if (cuLoc.empty())
        {
            return "";
        }
        try
        {
            auto [path, line, col] = parseLocation(cuLoc);
            if (line <= 0 || static_cast<std::size_t>(line) >= inverseLineMap.size())
            {
                return std::string(cuLoc);
            }
            const auto & [includeTree, srcLine] = inverseLineMap[line];
            if (!includeTree)
            {
                return std::string(cuLoc);
            }
            return makeLocation(includeTree->path, srcLine, col);
        }
        catch (const std::exception & ex)
        {
            SPDLOG_TRACE("Failed to translate CU location {}: {}", cuLoc, ex.what());
            return "";
        }
    }

    // Check if the invocation info is valid and thus should be kept
    // Invalid cases: empty fields, invalid path, non-Expr/Stmt ASTKind
    static std::tuple<bool, std::optional<SeedingReport>> dropInvocationSummary
    (
        const MakiInvocationSummary & invocation,
        const std::vector<std::pair<IncludeTreePtr, int>> & inverseLineMap
    )
    {
        std::set<std::string> reasons;

        if (invocation.DefinitionLocation.empty())
        {
            return {true, std::nullopt}; // No need to report this
        }
        if (invocation.InvocationLocation.empty())
        {
            return {true, std::nullopt}; // No need to report this
        }
        if (invocation.InvocationLocationEnd.empty())
        {
            return {true, std::nullopt}; // No need to report this
        }

        
        std::optional<std::tuple<std::filesystem::path, int, int>> invocationLoc;
        if (!invocation.InvocationLocation.empty())
        {
            try
            {
                invocationLoc = parseLocation(invocation.InvocationLocation);
            }
            catch (const std::exception &)
            {
                return {true, std::nullopt}; // No need to report this
            }
        }
        const std::filesystem::path * invocationPathPtr = invocationLoc ? &std::get<0>(*invocationLoc) : nullptr;

        std::optional<std::tuple<std::filesystem::path, int, int>> definitionLoc;
        if (!invocation.DefinitionLocation.empty())
        {
            try
            {
                definitionLoc = parseLocation(invocation.DefinitionLocation);
            }
            catch (const std::exception &)
            {
                return {true, std::nullopt}; // No need to report this
            }
        }

        if (invocationLoc && definitionLoc)
        {
            const auto & invPath = std::get<0>(*invocationLoc);
            int invLine = std::get<1>(*invocationLoc);
            const auto & defPath = std::get<0>(*definitionLoc);
            int defLine = std::get<1>(*definitionLoc);
            assert(invPath == defPath);

            const auto & [includeTree, srcLine] = inverseLineMap.at(invLine);
            const auto & [locRefIncludeTree, locRefSrcLine] = inverseLineMap.at(defLine);
            if (!includeTree || includeTree->isSystemInclude || !locRefIncludeTree || locRefIncludeTree->isSystemInclude)
            {
                return {true, std::nullopt}; // No need to report this
            }
        }

        if (invocation.Name.empty())
        {
            return {true, std::nullopt}; // No need to report this
        }
        if (invocation.ASTKind.empty())
        {
            reasons.insert("non-syntactic");
        }
        if (invocation.HasStringification)
        {
            reasons.insert("uses stringification");
        }
        if (invocation.HasTokenPasting)
        {
            reasons.insert("uses token pasting");
        }
        if (!invocation.IsHygienic)
        {
            reasons.insert("unhygienic");
        }
        if (invocation.IsInvokedWhereICERequired)
        {
            reasons.insert("requires integral constant expression");
        }
        if (invocation.NumArguments != static_cast<int>(invocation.Args.size()))
        {
            reasons.insert("argument non-syntactic");
        }

        constexpr static std::string_view validASTKinds[] = {"Expr", "Stmt", "Stmts", "Decl", "Decls"};
        if (!invocation.ASTKind.empty() &&
            std::find(std::begin(validASTKinds), std::end(validASTKinds), invocation.ASTKind) == std::end(validASTKinds))
        {
            reasons.insert("unsupported AST kind");
        }
        if (invocation.ReturnType.contains("("))
        {
            // Function pointer
            reasons.insert("unsupported AST kind");
        }

        for (const MakiArgSummary & arg : invocation.Args)
        {
            if (arg.ASTKind.empty())
            {
                reasons.insert("argument non-syntactic");
            }
            else if (std::find(std::begin(validASTKinds), std::end(validASTKinds), arg.ASTKind) == std::end(validASTKinds))
            {
                // Actually TypeLoc
                reasons.insert("argument unsupported AST kind");
            }
            if (arg.Type.contains("("))
            {
                // Function pointer
                reasons.insert("argument unsupported AST kind");
            }

            if (arg.Name.empty())
            {
                reasons.insert("argument missing name");
            }


            bool argBeginAvailable = !arg.ActualArgLocBegin.empty();
            bool argEndAvailable = !arg.ActualArgLocEnd.empty();
            if (!argBeginAvailable || !argEndAvailable)
            {
                reasons.insert("argument missing location");
            }

            std::optional<std::tuple<std::filesystem::path, int, int>> argBeginLoc;
            if (argBeginAvailable)
            {
                try
                {
                    argBeginLoc = parseLocation(arg.ActualArgLocBegin);
                }
                catch (const std::exception &)
                {
                    reasons.insert("argument invalid location");
                }
            }

            std::optional<std::tuple<std::filesystem::path, int, int>> argEndLoc;
            if (argEndAvailable)
            {
                try
                {
                    argEndLoc = parseLocation(arg.ActualArgLocEnd);
                }
                catch (const std::exception &)
                {
                    reasons.insert("argument invalid location");
                }
            }

            if (invocationPathPtr && argBeginLoc)
            {
                if (std::get<0>(*argBeginLoc) != *invocationPathPtr)
                {
                    reasons.insert("argument path mismatch");
                }
            }
            if (invocationPathPtr && argEndLoc)
            {
                if (std::get<0>(*argEndLoc) != *invocationPathPtr)
                {
                    reasons.insert("argument end path mismatch");
                }
            }
        }

        SeedingReport report = SeedingReport
        {
            .name = invocation.Name,
            .locInv = translateCuLocOrFallback(invocation.InvocationLocation, inverseLineMap),
            .locRef = translateCuLocOrFallback(invocation.DefinitionLocation, inverseLineMap),
            .astKind = invocation.ASTKind,
            .isObjectLike = invocation.IsObjectLike,
            .seeded = reasons.empty(),
            .reasons = std::move(reasons),
            .canBeFn = canBeRustFn(invocation)
        };

        bool shouldDrop = !report.seeded;
        return {shouldDrop, std::optional<SeedingReport>{std::move(report)}};
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
    // Returns the modified (CU) source code and a seeding report.
    static std::tuple<std::string, std::vector<SeedingReport>> run
    (
        std::vector<Hayroll::MakiInvocationSummary> invocations,
        std::vector<Hayroll::MakiRangeSummary> ranges,
        std::string_view srcStr,
        const std::unordered_map<Hayroll::IncludeTreePtr, std::vector<int>> & lineMap,
        const std::vector<std::pair<IncludeTreePtr, int>> & inverseLineMap
    )
    {
        std::vector<SeedingReport> seedingReport;

        // Remove invalid invocations while collecting drop reasons
        {
            std::vector<MakiInvocationSummary> filteredInvocations;
            filteredInvocations.reserve(invocations.size());
            for (auto & invocation : invocations)
            {
                auto [shouldDrop, reportEntry] = dropInvocationSummary(invocation, inverseLineMap);
                if (reportEntry) seedingReport.push_back(std::move(*reportEntry));
                if (!shouldDrop) filteredInvocations.push_back(std::move(invocation));
            }
            invocations = std::move(filteredInvocations);
        }

        // Remove invalid ranges (no report needed for now)
        std::erase_if(ranges, [&inverseLineMap](const MakiRangeSummary & range)
        {
            return dropRangeSummary(range, inverseLineMap);
        });

        TextEditor srcEditor{srcStr};

        // Extract spelling for invocations and arguments
        // Also attach premises against conditional ranges
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
            std::list<InstrumentationTask> rangeTasks = genConditionalInstrumentationTasks(range, !range.IsInStatementBlock, inverseLineMap);
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

        std::string seededSource = srcEditor.commit();
        return {seededSource, seedingReport};
    }

    static ordered_json seedingReportStatistics(std::vector<SeedingReport> reports)
    {
        // First, remove duplicate reports (those who have the same invocation location)
        {
            std::sort
            (
                reports.begin(),
                reports.end(),
                [](const Seeder::SeedingReport & a, const Seeder::SeedingReport & b)
                {
                    return a.locInv < b.locInv;
                }
            );
            auto last = std::unique
            (
                reports.begin(),
                reports.end(),
                [](const Seeder::SeedingReport & a, const Seeder::SeedingReport & b)
                {
                    return a.locInv == b.locInv;
                }
            );
            reports.erase(last, reports.end());
        }

        ordered_json statistics = ordered_json::object();
        auto countByPredicate = [&](const std::function<bool(const Seeder::SeedingReport &)> & predicate)
        {
            return std::count_if
            (
                reports.begin(),
                reports.end(),
                predicate
            );
        };
        statistics["macro"] = countByPredicate
        (
            [](const Seeder::SeedingReport & r) { return true; }
        );
        statistics["macro_seeded"] = countByPredicate
        (
            [](const Seeder::SeedingReport & r) { return r.seeded; }
        );
        statistics["macro_seeded_ratio"] = statistics["macro_seeded"].get<std::size_t>() /
            static_cast<double>(statistics["macro"].get<std::size_t>());
        statistics["macro_seeded_fn"] = countByPredicate
        (
            [](const Seeder::SeedingReport & r) { return r.seeded && r.canBeFn; }
        );
        statistics["macro_seeded_fn_ratio"] = statistics["macro_seeded_fn"].get<std::size_t>() /
            static_cast<double>(statistics["macro_seeded"].get<std::size_t>());
        statistics["macro_seeded_macro"] = countByPredicate
        (
            [](const Seeder::SeedingReport & r) { return r.seeded && !r.canBeFn; }
        );
        statistics["macro_seeded_macro_ratio"] = statistics["macro_seeded_macro"].get<std::size_t>() /
            static_cast<double>(statistics["macro"].get<std::size_t>());
        statistics["macro_rejected"] = countByPredicate
        (
            [](const Seeder::SeedingReport & r) { return !r.seeded; }
        );
        statistics["macro_rejected_ratio"] = statistics["macro_rejected"].get<std::size_t>() /
            static_cast<double>(statistics["macro"].get<std::size_t>());
        statistics["macro_syntactic"] = countByPredicate
        (
            [](const Seeder::SeedingReport & r) { return r.astKind != ""; }
        );
        statistics["macro_syntactic_ratio"] = statistics["macro_syntactic"].get<std::size_t>() /
            static_cast<double>(statistics["macro"].get<std::size_t>());
        statistics["macro_syntactic_seeded"] = countByPredicate
        (
            [](const Seeder::SeedingReport & r) { return r.astKind != "" && r.seeded; }
        );
        statistics["macro_syntactic_seeded_ratio"] = statistics["macro_syntactic_seeded"].get<std::size_t>() /
            static_cast<double>(statistics["macro_syntactic"].get<std::size_t>());
        statistics["macro_syntactic_seeded_fn"] = countByPredicate
        (
            [](const Seeder::SeedingReport & r) { return r.astKind != "" && r.seeded && r.canBeFn; }
        );
        statistics["macro_syntactic_seeded_fn_ratio"] = statistics["macro_syntactic_seeded_fn"].get<std::size_t>() /
            static_cast<double>(statistics["macro_syntactic"].get<std::size_t>());
        statistics["macro_syntactic_seeded_macro"] = countByPredicate
        (
            [](const Seeder::SeedingReport & r) { return r.astKind != "" && r.seeded && !r.canBeFn; }
        );
        statistics["macro_syntactic_seeded_macro_ratio"] = statistics["macro_syntactic_seeded_macro"].get<std::size_t>() /
            static_cast<double>(statistics["macro_syntactic"].get<std::size_t>());
        statistics["macro_syntactic_rejected"] = countByPredicate
        (
            [](const Seeder::SeedingReport & r) { return r.astKind != "" && !r.seeded; }
        );
        statistics["macro_syntactic_rejected_ratio"] = statistics["macro_syntactic_rejected"].get<std::size_t>() /
            static_cast<double>(statistics["macro_syntactic"].get<std::size_t>());
        statistics["macro_expr"] = countByPredicate
        (
            [](const Seeder::SeedingReport & r) { return r.astKind == "Expr"; }
        );
        statistics["macro_expr_ratio"] = statistics["macro_expr"].get<std::size_t>() /
            static_cast<double>(statistics["macro"].get<std::size_t>());
        statistics["macro_expr_seeded"] = countByPredicate
        (
            [](const Seeder::SeedingReport & r) { return r.astKind == "Expr" && r.seeded; }
        );
        statistics["macro_expr_seeded_ratio"] = statistics["macro_expr_seeded"].get<std::size_t>() /
            static_cast<double>(statistics["macro_expr"].get<std::size_t>());
        statistics["macro_expr_seeded_fn"] = countByPredicate
        (
            [](const Seeder::SeedingReport & r) { return r.astKind == "Expr" && r.seeded && r.canBeFn; }
        );
        statistics["macro_expr_seeded_fn_ratio"] = statistics["macro_expr_seeded_fn"].get<std::size_t>() /
            static_cast<double>(statistics["macro_expr"].get<std::size_t>());
        statistics["macro_expr_seeded_macro"] = countByPredicate
        (
            [](const Seeder::SeedingReport & r) { return r.astKind == "Expr" && r.seeded && !r.canBeFn; }
        );
        statistics["macro_expr_seeded_macro_ratio"] = statistics["macro_expr_seeded_macro"].get<std::size_t>() /
            static_cast<double>(statistics["macro_expr"].get<std::size_t>());
        statistics["macro_expr_rejected"] = countByPredicate
        (
            [](const Seeder::SeedingReport & r) { return r.astKind == "Expr" && !r.seeded; }
        );
        statistics["macro_expr_rejected_ratio"] = statistics["macro_expr_rejected"].get<std::size_t>() /
            static_cast<double>(statistics["macro_expr"].get<std::size_t>());
        statistics["macro_stmt"] = countByPredicate
        (
            [](const Seeder::SeedingReport & r) { return r.astKind == "Stmt" || r.astKind == "Stmts"; }
        );
        statistics["macro_stmt_ratio"] = statistics["macro_stmt"].get<std::size_t>() /
            static_cast<double>(statistics["macro"].get<std::size_t>());
        statistics["macro_stmt_seeded"] = countByPredicate
        (
            [](const Seeder::SeedingReport & r) { return (r.astKind == "Stmt" || r.astKind == "Stmts") && r.seeded; }
        );
        statistics["macro_stmt_seeded_ratio"] = statistics["macro_stmt_seeded"].get<std::size_t>() /
            static_cast<double>(statistics["macro_stmt"].get<std::size_t>());
        statistics["macro_stmt_seeded_fn"] = countByPredicate
        (
            [](const Seeder::SeedingReport & r) { return (r.astKind == "Stmt" || r.astKind == "Stmts") && r.seeded && r.canBeFn; }
        );
        statistics["macro_stmt_seeded_fn_ratio"] = statistics["macro_stmt_seeded_fn"].get<std::size_t>() /
            static_cast<double>(statistics["macro_stmt"].get<std::size_t>());
        statistics["macro_stmt_seeded_macro"] = countByPredicate
        (
            [](const Seeder::SeedingReport & r) { return (r.astKind == "Stmt" || r.astKind == "Stmts") && r.seeded && !r.canBeFn; }
        );
        statistics["macro_stmt_seeded_macro_ratio"] = statistics["macro_stmt_seeded_macro"].get<std::size_t>() /
            static_cast<double>(statistics["macro_stmt"].get<std::size_t>());
        statistics["macro_stmt_rejected"] = countByPredicate
        (
            [](const Seeder::SeedingReport & r) { return (r.astKind == "Stmt" || r.astKind == "Stmts") && !r.seeded; }
        );
        statistics["macro_stmt_rejected_ratio"] = statistics["macro_stmt_rejected"].get<std::size_t>() /
            static_cast<double>(statistics["macro_stmt"].get<std::size_t>());
        statistics["macro_decl"] = countByPredicate
        (
            [](const Seeder::SeedingReport & r) { return r.astKind == "Decl" || r.astKind == "Decls"; }
        );
        statistics["macro_decl_ratio"] = statistics["macro_decl"].get<std::size_t>() /
            static_cast<double>(statistics["macro"].get<std::size_t>());
        statistics["macro_decl_seeded"] = countByPredicate
        (
            [](const Seeder::SeedingReport & r) { return (r.astKind == "Decl" || r.astKind == "Decls") && r.seeded; }
        );
        statistics["macro_decl_seeded_ratio"] = statistics["macro_decl_seeded"].get<std::size_t>() /
            static_cast<double>(statistics["macro_decl"].get<std::size_t>());
        statistics["macro_decl_seeded_fn"] = countByPredicate
        (
            [](const Seeder::SeedingReport & r) { return (r.astKind == "Decl" || r.astKind == "Decls") && r.seeded && r.canBeFn; }
        );
        statistics["macro_decl_seeded_fn_ratio"] = statistics["macro_decl_seeded_fn"].get<std::size_t>() /
            static_cast<double>(statistics["macro_decl"].get<std::size_t>());
        statistics["macro_decl_seeded_macro"] = countByPredicate
        (
            [](const Seeder::SeedingReport & r) { return (r.astKind == "Decl" || r.astKind == "Decls") && r.seeded && !r.canBeFn; }
        );
        statistics["macro_decl_seeded_macro_ratio"] = statistics["macro_decl_seeded_macro"].get<std::size_t>() /
            static_cast<double>(statistics["macro_decl"].get<std::size_t>());
        statistics["macro_decl_rejected"] = countByPredicate
        (
            [](const Seeder::SeedingReport & r) { return (r.astKind == "Decl" || r.astKind == "Decls") && !r.seeded; }
        );
        statistics["macro_decl_rejected_ratio"] = statistics["macro_decl_rejected"].get<std::size_t>() /
            static_cast<double>(statistics["macro_decl"].get<std::size_t>());
        statistics["macro_typeloc"] = countByPredicate
        (
            [](const Seeder::SeedingReport & r) { return r.astKind == "TypeLoc"; }
        );
        statistics["macro_typeloc_ratio"] = statistics["macro_typeloc"].get<std::size_t>() /
            static_cast<double>(statistics["macro"].get<std::size_t>());
        statistics["macro_non_syntactic"] = countByPredicate
        (
            [](const Seeder::SeedingReport & r) { return r.astKind == ""; }
        );
        statistics["macro_non_syntactic_ratio"] = statistics["macro_non_syntactic"].get<std::size_t>() /
            static_cast<double>(statistics["macro"].get<std::size_t>());

        statistics["failing_reasons"] = ordered_json::object();
        auto & failingReasons = statistics["failing_reasons"];
        for (const Seeder::SeedingReport & report : reports)
        {
            if (report.seeded) continue;
            for (const std::string & reason : report.reasons)
            {
                failingReasons[reason] = failingReasons.value(reason, 0) + 1;
            }
        }

        return statistics;
    }
};

} // namespace Hayroll

#endif // HAYROLL_SEEDER_HPP
