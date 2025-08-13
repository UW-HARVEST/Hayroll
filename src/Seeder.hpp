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

#include <spdlog/spdlog.h>
#include "json.hpp"

#include "SymbolicExecutor.hpp"
#include "LineMatcher.hpp"
#include "TextEditor.hpp"

using namespace nlohmann;

namespace Hayroll
{

class Seeder
{
public:
    static std::string escapeString(std::string_view str)
    {
        std::string result;
        for (char c : str)
        {
            switch (c)
            {
            case '\\':
                result += "\\\\";
                break;
            case '\"':
                result += "\\\"";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            case '\b':
                result += "\\b";
                break;
            case '\f':
                result += "\\f";
                break;
            case '\v':
                result += "\\v";
                break;
            default:
                result += c;
                break;
            }
        }
        return result;
    }

    // Parse a location string in the format "path:line:col" into a tuple of (path, line, col).
    // Canonicalizes the filename.
    static std::tuple<std::filesystem::path, int, int> parseLocation(const std::string_view loc)
    {
        assert(!loc.empty());

        std::string_view pathStr;
        int line;
        int col;

        size_t colonPos = loc.find(':');
        if (colonPos == std::string_view::npos)
        {
            throw std::invalid_argument("Invalid location format (no colon)." + std::string(loc));
        }

        pathStr = loc.substr(0, colonPos);
        size_t nextColonPos = loc.find(':', colonPos + 1);
        if (nextColonPos == std::string_view::npos)
        {
            throw std::invalid_argument("Invalid location format (no second colon)." + std::string(loc));
        }

        line = std::stoi(std::string(loc.substr(colonPos + 1, nextColonPos - colonPos - 1)));
        col = std::stoi(std::string(loc.substr(nextColonPos + 1)));

        std::filesystem::path path(pathStr);
        path = std::filesystem::weakly_canonical(path);

        return {path, line, col};
    }

    static std::string makeLocation
    (
        const std::filesystem::path & path,
        int line,
        int col
    )
    {
        return fmt::format("{}:{}:{}", path.string(), line, col);
    }

    // Tag structure to be serialized and instrumented into C code
    // Contains necessary information for Hayroll Reaper on the Rust side to reconstruct macros
    struct Tag
    {
        bool hayroll = true;
        bool begin;
        bool isArg;
        std::vector<std::string> argNames;
        std::string astKind;
        bool isLvalue;
        std::string name;
        std::string locBegin; // For invocation: invocation begin; for arg: arg begin
        std::string locEnd;   // For invocation: invocation end; for arg: arg end
        std::string locRefBegin; // For invocation: definition begin; for arg: invocation begin

        bool canFn;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Tag, hayroll, begin, isArg, argNames, astKind, isLvalue, name, locBegin, locEnd, locRefBegin, canFn)

        // Escape the JSON string to make it a valid C string that embeds into C code
        std::string stringLiteral() const
        {
            json j = *this;
            return "\"" + escapeString(j.dump()) + "\"";
        }
    };

    // InstrumentationTask will be transformed into TextEditor edits
    struct InstrumentationTask
    {
        int line;
        int col;
        std::string str;

        void addToEditor(TextEditor & editor) const
        {
            editor.insert(line, col, str);
        }

        std::string toString() const
        {
            return fmt::format("{}:{}: {}", line, col, str);
        }
    };

    // Generate instrumentation tasks based on the provided parameters
    static std::list<InstrumentationTask> genInstrumentationTasks
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
        bool canFn,
        const std::vector<std::pair<IncludeTreePtr, int>> & inverseLineMap
    )
    {
        auto [path, line, col] = parseLocation(locBegin);
        auto [pathEnd, lineEnd, colEnd] = parseLocation(locEnd);
        auto [locRefPath, locRefLine, locRefCol] = parseLocation(locRefBegin);
        assert(path == pathEnd);
        assert(locRefPath == path); // Should all be the only CU file

        // Map the compilation unit line numbers back to the source file line numbers
        auto [includeTree, srcLine] = inverseLineMap[line];
        auto [includeTreeEnd, srcLineEnd] = inverseLineMap[lineEnd];
        auto [locRefIncludeTree, locRefSrcLine] = inverseLineMap[locRefLine];
        assert(includeTree == includeTreeEnd);

        if (!includeTree)
        {
            // This code section was copied from a header file which was concretely executed
            // by Hayroll Pioneer, so the include tree is not available.
            // We don't instrument this code section.
            SPDLOG_TRACE
            (
                "Skipping instrumentation for {}: {}:{} (no include tree)",
                name, path.string(), srcLine
            );
            return {};
        }

        std::filesystem::path srcPath = includeTree->path;
        std::filesystem::path locRefSrcPath = locRefIncludeTree->path;

        std::string srcLocBegin = makeLocation(srcPath, srcLine, col);
        std::string srcLocEnd = makeLocation(srcPath, srcLineEnd, colEnd);
        std::string srcLocRef = makeLocation(locRefSrcPath, locRefSrcLine, locRefCol);

        Tag tagBegin
        {
            .begin = true,
            .isArg = isArg,
            .argNames = argNames,
            .astKind = std::string(astKind),
            .isLvalue = isLvalue,
            .name = std::string(name),
            .locBegin = std::string(srcLocBegin),
            .locEnd = std::string(srcLocEnd),
            .locRefBegin = std::string(srcLocRef),

            .canFn = canFn
        };

        Tag tagEnd = tagBegin;
        tagEnd.begin = false;

        std::list<InstrumentationTask> tasks;
        if (astKind == "Expr")
        {
            if (isLvalue)
            {
                // Template:
                // (*((*tagBegin)?(&(ORIGINAL_INVOCATION)):((__typeof__(spelling)*)(0))))
                InstrumentationTask taskLeft
                {
                    .line = line,
                    .col = col,
                    .str = 
                    (
                        std::stringstream()
                        << "(*((*"
                        << tagBegin.stringLiteral()
                        << ")?(&("
                    ).str()
                };
                InstrumentationTask taskRight
                {
                    .line = lineEnd,
                    .col = colEnd,
                    .str = 
                    (
                        std::stringstream()
                        << ")):((__typeof__("
                        << spelling
                        << ")*)(0))))"
                    ).str()
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
                    .line = line,
                    .col = col,
                    .str = 
                    (
                        std::stringstream()
                        << "((*"
                        << tagBegin.stringLiteral()
                        << ")?("
                    ).str()
                };
                InstrumentationTask taskRight
                {
                    .line = lineEnd,
                    .col = colEnd,
                    .str = 
                    (
                        std::stringstream()
                        << "):(*(__typeof__("
                        << spelling
                        << ")*)(0)))"
                    ).str()
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
                .line = line,
                .col = col,
                .str = 
                (
                    std::stringstream()
                    << "{*"
                    << tagBegin.stringLiteral()
                    << ";"
                ).str()
            };
            InstrumentationTask taskRight
            {
                .line = lineEnd,
                .col = colEnd,
                .str = 
                (
                    std::stringstream()
                    << ";*"
                    << tagEnd.stringLiteral()
                    << ";}"
                ).str()
            };
            tasks.push_back(taskLeft);
            tasks.push_back(taskRight);
        }
        else if (astKind == "Decl" || astKind == "Decls")
        {
            // Template:
            // const char * HAYROLL_TAG_FOR_<ORIGINAL_INVOCATION> = tagBegin;\n
            InstrumentationTask taskLeft
            {
                .line = line,
                .col = col,
                .str = 
                (
                    std::stringstream()
                    << "const char * HAYROLL_TAG_FOR_"
                    << name
                    << " = "
                    << tagBegin.stringLiteral()
                    << ";\n"
                ).str()
            };
            tasks.push_back(taskLeft);
            // Only one tag per declaration(s).
            // Reaper will make use of #[c2rust::src_loc = "ln:col"] attribute to locate the declaration.
        }
        else if (astKind == "Debug")
        {
            // Template:
            // // tagBegin (\n)
            // ORIGINAL_INVOCATION
            // // tagEnd (\n)
            InstrumentationTask taskLeft
            {
                .line = line,
                .col = col,
                .str = 
                (
                    std::stringstream()
                    << "// "
                    << tagBegin.stringLiteral()
                    << "\n"
                ).str()
            };
            InstrumentationTask taskRight
            {
                .line = lineEnd,
                .col = colEnd,
                .str = 
                (
                    std::stringstream()
                    << "// "
                    << tagEnd.stringLiteral()
                    << "\n"
                ).str()
            };
            tasks.push_back(taskLeft);
            tasks.push_back(taskRight);
        }
        else {} // Do nothing for other AST kinds

        return tasks;
    }

    // ArgInfo structure to hold the information about the arguments of a function-like macro
    // This maps to the JSON structure in Maki's .cpp2c invocation summary file
    struct ArgInfo
    {
        std::string Name;
        std::string ASTKind;
        std::string Type;
        bool IsLValue;
        std::string ActualArgLocBegin; // /path/to/file.cpp:line:col
        std::string ActualArgLocEnd;

        // Later collected
        std::string Spelling;
        std::string InvocationLocation;
        
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(ArgInfo, Name, ASTKind, Type, IsLValue, ActualArgLocBegin, ActualArgLocEnd)

        // Generate tags for the arguments
        std::list<InstrumentationTask> collectInstrumentationTasks
        (
            const std::vector<std::pair<IncludeTreePtr, int>> & inverseLineMap
        ) const
        {
            return genInstrumentationTasks(
                ActualArgLocBegin,
                ActualArgLocEnd,
                true, // isArg
                {}, // argNames
                ASTKind,
                IsLValue,
                Name,
                InvocationLocation,
                Spelling,
                false, // canFn
                inverseLineMap
            );
        }
    };

    // MakiInvocationInfo structure to hold the information about the invocation
    // This maps to the JSON structure in Maki's .cpp2c invocation summary file
    struct MakiInvocationInfo
    {
        // In accordance with the Maki macros.py
        std::string Name;
        std::string DefinitionLocation;
        std::string InvocationLocation;
        std::string ASTKind; // {"Expr", "Stmt", "Stmts", "Decl", "Decls", "TypeLoc", "Debug"}
        std::string TypeSignature;
        int InvocationDepth;
        int NumASTRoots;
        int NumArguments;
        bool HasStringification;
        bool HasTokenPasting;
        bool HasAlignedArguments;
        bool HasSameNameAsOtherDeclaration;
        bool IsExpansionControlFlowStmt;
        bool DoesBodyReferenceMacroDefinedAfterMacro;
        bool DoesBodyReferenceDeclDeclaredAfterMacro;
        bool DoesBodyContainDeclRefExpr;
        bool DoesSubexpressionExpandedFromBodyHaveLocalType;
        bool DoesSubexpressionExpandedFromBodyHaveTypeDefinedAfterMacro;
        bool DoesAnyArgumentHaveSideEffects;
        bool DoesAnyArgumentContainDeclRefExpr;
        bool IsHygienic;
        bool IsDefinitionLocationValid;
        bool IsInvocationLocationValid;
        bool IsObjectLike;
        bool IsInvokedInMacroArgument;
        bool IsNamePresentInCPPConditional;
        bool IsExpansionICE;
        bool IsExpansionTypeNull;
        bool IsExpansionTypeAnonymous;
        bool IsExpansionTypeLocalType;
        bool IsExpansionTypeDefinedAfterMacro;
        bool IsExpansionTypeVoid;
        bool IsAnyArgumentTypeNull;
        bool IsAnyArgumentTypeAnonymous;
        bool IsAnyArgumentTypeLocalType;
        bool IsAnyArgumentTypeDefinedAfterMacro;
        bool IsAnyArgumentTypeVoid;
        bool IsInvokedWhereModifiableValueRequired;
        bool IsInvokedWhereAddressableValueRequired;
        bool IsInvokedWhereICERequired;
        bool IsAnyArgumentExpandedWhereModifiableValueRequired;
        bool IsAnyArgumentExpandedWhereAddressableValueRequired;
        bool IsAnyArgumentConditionallyEvaluated;
        bool IsAnyArgumentNeverExpanded;
        bool IsAnyArgumentNotAnExpression;

        // HAYROLL Extras
        std::string ReturnType;
        bool IsLValue;
        std::string InvocationLocationEnd;
        std::vector<ArgInfo> Args;

        // Later collected
        std::string Spelling;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE
        (
            MakiInvocationInfo,
            Name,
            DefinitionLocation,
            InvocationLocation,
            ASTKind,
            TypeSignature,
            InvocationDepth,
            NumASTRoots,
            NumArguments,
            HasStringification,
            HasTokenPasting,
            HasAlignedArguments,
            HasSameNameAsOtherDeclaration,
            IsExpansionControlFlowStmt,
            DoesBodyReferenceMacroDefinedAfterMacro,
            DoesBodyReferenceDeclDeclaredAfterMacro,
            DoesBodyContainDeclRefExpr,
            DoesSubexpressionExpandedFromBodyHaveLocalType,
            DoesSubexpressionExpandedFromBodyHaveTypeDefinedAfterMacro,
            DoesAnyArgumentHaveSideEffects,
            DoesAnyArgumentContainDeclRefExpr,
            IsHygienic,
            IsDefinitionLocationValid,
            IsInvocationLocationValid,
            IsObjectLike,
            IsInvokedInMacroArgument,
            IsNamePresentInCPPConditional,
            IsExpansionICE,
            IsExpansionTypeNull,
            IsExpansionTypeAnonymous,
            IsExpansionTypeLocalType,
            IsExpansionTypeDefinedAfterMacro,
            IsExpansionTypeVoid,
            IsAnyArgumentTypeNull,
            IsAnyArgumentTypeAnonymous,
            IsAnyArgumentTypeLocalType,
            IsAnyArgumentTypeDefinedAfterMacro,
            IsAnyArgumentTypeVoid,
            IsInvokedWhereModifiableValueRequired,
            IsInvokedWhereAddressableValueRequired,
            IsInvokedWhereICERequired,
            IsAnyArgumentExpandedWhereModifiableValueRequired,
            IsAnyArgumentExpandedWhereAddressableValueRequired,
            IsAnyArgumentConditionallyEvaluated,
            IsAnyArgumentNeverExpanded,
            IsAnyArgumentNotAnExpression,
            ReturnType,
            IsLValue,
            InvocationLocationEnd,
            Args
        )

        // Get the filename from the definition location
        std::string definitionLocationFilename() const
        {
            if (!IsDefinitionLocationValid)
            {
                return DefinitionLocation;
            }
            else
            {
                size_t colonPos = DefinitionLocation.find(':');
                if (colonPos == std::string::npos)
                {
                    return DefinitionLocation;
                }
                return DefinitionLocation.substr(0, colonPos);
            }
        }

        // Concept translated from Maki implementation
        bool isFunctionLike() const
        {
            return !IsObjectLike;
        }

        // Concept translated from Maki implementation
        bool isTopLevelNonArgument() const
        {
            return InvocationDepth == 0
                && !IsInvokedInMacroArgument
                && IsInvocationLocationValid
                && IsDefinitionLocationValid;
        }

        // Concept translated from Maki implementation
        bool isAligned() const
        {
            assert(isTopLevelNonArgument());
            return isTopLevelNonArgument()
                && NumASTRoots == 1
                && HasAlignedArguments;
        }

        // Concept translated from Maki implementation
        bool hasSemanticData() const
        {
            return isTopLevelNonArgument()
                && !IsAnyArgumentNeverExpanded
                && isAligned()
                && !(ASTKind == "Expr" && IsExpansionTypeNull);
        }

        // Concept translated from Maki implementation
        bool canBeTurnedIntoEnum() const
        {
            assert(hasSemanticData());
            // Enums have to be ICEs
            return IsExpansionICE;
        }

        // Concept translated from Maki implementation
        bool canBeTurnedIntoVariable() const
        {
            assert(hasSemanticData());
            return
                // Variables must be exprs
                ASTKind == "Expr"
                // Variables cannot contain DeclRefExprs
                && !DoesBodyContainDeclRefExpr
                && !DoesAnyArgumentContainDeclRefExpr
                // Variables cannot be invoked where ICEs are required
                && !IsInvokedWhereICERequired
                // Variables cannot have the void type
                && !IsExpansionTypeVoid;
        }

        // Concept translated from Maki implementation
        bool canBeTurnedIntoEnumOrVariable() const
        {
            assert(hasSemanticData());
            return canBeTurnedIntoEnum() || canBeTurnedIntoVariable();
        }

        // Concept translated from Maki implementation
        bool canBeTurnedIntoFunction() const
        {
            assert(hasSemanticData());
            return 
                // Functions must be stmts or expressions
                (ASTKind == "Stmt" || ASTKind == "Stmts" || ASTKind == "Expr")
                // Functions cannot be invoked where ICEs are required
                && !IsInvokedWhereICERequired;
        }

        // Concept translated from Maki implementation
        bool canBeTurnedIntoAFunctionOrVariable() const
        {
            assert(hasSemanticData());
            return canBeTurnedIntoFunction() || canBeTurnedIntoVariable();
        }

        // Concept translated from Maki implementation
        bool canBeTurnedIntoTypeDef() const
        {
            assert(hasSemanticData());
            return ASTKind == "TypeLoc";
        }

        // Concept translated from Maki implementation
        bool mustAlterArgumentsOrReturnTypeToTransform() const
        {
            assert(hasSemanticData());
            return !IsHygienic
                || IsInvokedWhereModifiableValueRequired
                || IsInvokedWhereAddressableValueRequired
                || IsAnyArgumentExpandedWhereModifiableValueRequired
                || IsAnyArgumentExpandedWhereAddressableValueRequired;
        }

        // Concept translated from Maki implementation
        bool mustAlterDeclarationsToTransform() const
        {
            assert(hasSemanticData());
            return HasSameNameAsOtherDeclaration
                || DoesBodyReferenceMacroDefinedAfterMacro
                || DoesBodyReferenceDeclDeclaredAfterMacro
                || DoesSubexpressionExpandedFromBodyHaveLocalType
                || DoesSubexpressionExpandedFromBodyHaveTypeDefinedAfterMacro
                || IsExpansionTypeAnonymous
                || IsExpansionTypeLocalType
                || IsExpansionTypeDefinedAfterMacro
                || IsAnyArgumentTypeAnonymous
                || IsAnyArgumentTypeLocalType
                || IsAnyArgumentTypeDefinedAfterMacro
                || ASTKind == "TypeLoc";
        }

        // Concept translated from Maki implementation
        bool mustAlterCallSiteToTransform() const
        {
            if (!isAligned())
            {
                return true;
            }

            assert(hasSemanticData());
            return IsExpansionControlFlowStmt || IsAnyArgumentConditionallyEvaluated;
        }

        // Concept translated from Maki implementation
        bool mustCreateThunksToTransform() const
        {
            return DoesAnyArgumentHaveSideEffects || IsAnyArgumentTypeVoid;
        }

        // Concept translated from Maki implementation
        bool mustUseMetaprogrammingToTransform() const
        {
            return (HasStringification || HasTokenPasting) 
                ||
                (
                    hasSemanticData()
                    && isFunctionLike()
                    && canBeTurnedIntoFunction()
                    && IsAnyArgumentNotAnExpression
                );
        }

        // Concept translated from Maki implementation
        bool satisfiesASyntacticProperty() const
        {
            return !isAligned();
        }

        // Concept translated from Maki implementation
        bool satisfiesAScopingRuleProperty() const
        {
            assert(hasSemanticData());
            return !IsHygienic
                || IsInvokedWhereModifiableValueRequired
                || IsInvokedWhereAddressableValueRequired
                || IsAnyArgumentExpandedWhereModifiableValueRequired
                || IsAnyArgumentExpandedWhereAddressableValueRequired
                || DoesBodyReferenceMacroDefinedAfterMacro
                || DoesBodyReferenceDeclDeclaredAfterMacro
                || DoesSubexpressionExpandedFromBodyHaveLocalType
                || DoesSubexpressionExpandedFromBodyHaveTypeDefinedAfterMacro
                || IsAnyArgumentTypeDefinedAfterMacro
                || IsAnyArgumentTypeLocalType;
        }

        // Concept translated from Maki implementation
        bool satisfiesATypingProperty() const
        {
            assert(hasSemanticData());
            return IsExpansionTypeAnonymous
                || IsAnyArgumentTypeAnonymous
                || DoesSubexpressionExpandedFromBodyHaveLocalType
                || IsAnyArgumentTypeDefinedAfterMacro
                || DoesSubexpressionExpandedFromBodyHaveTypeDefinedAfterMacro
                || IsAnyArgumentTypeVoid
                || (IsObjectLike && IsExpansionTypeVoid)
                || IsAnyArgumentTypeLocalType;
        }

        // Concept translated from Maki implementation
        bool satisfiesACallingConventionProperty() const
        {
            assert(hasSemanticData());
            return DoesAnyArgumentHaveSideEffects
                || IsAnyArgumentConditionallyEvaluated;
        }

        // Concept translated from Maki implementation
        bool satisfiesALanguageSpecificProperty() const
        {
            return mustUseMetaprogrammingToTransform();
        }

        // HAYROLL original concept
        // Whether the function can be turned into a Rust function
        bool canRustFn() const
        {
            return 
                !(
                    // Syntactic
                    !isAligned()

                    // Scoping
                    || !IsHygienic
                    // || IsInvokedWhereModifiableValueRequired // HAYROLL can handle lvalues
                    // || IsInvokedWhereAddressableValueRequired // HAYROLL can handle lvalues
                    // || IsAnyArgumentExpandedWhereModifiableValueRequired // HAYROLL can handle lvalues
                    // || IsAnyArgumentExpandedWhereAddressableValueRequired // HAYROLL can handle lvalues
                    // || DoesBodyReferenceMacroDefinedAfterMacro // Don't worry about nested macros for now
                    // || DoesBodyReferenceDeclDeclaredAfterMacro // Any local decls would trigger this. It's fine as long as it's hygienic.
                    || DoesSubexpressionExpandedFromBodyHaveLocalType
                    // || DoesSubexpressionExpandedFromBodyHaveTypeDefinedAfterMacro // Don't worry about declaration sequences in Rust. We can put the function at the end. 
                    // || IsAnyArgumentTypeDefinedAfterMacro // Don't worry about declaration sequences in Rust. We can put the function at the end. 
                    || IsAnyArgumentTypeLocalType

                    // Typing
                    || IsExpansionTypeAnonymous
                    || IsAnyArgumentTypeAnonymous
                    // || DoesSubexpressionExpandedFromBodyHaveLocalType // Repetition
                    // || IsAnyArgumentTypeDefinedAfterMacro // Repetition
                    // || DoesSubexpressionExpandedFromBodyHaveTypeDefinedAfterMacro  // Repetition
                    || IsAnyArgumentTypeVoid
                    || (IsObjectLike && IsExpansionTypeVoid)
                    // || IsAnyArgumentTypeLocalType // Repetition

                    // Calling convention
                    || DoesAnyArgumentHaveSideEffects
                    || IsAnyArgumentConditionallyEvaluated

                    // Language specific
                    || mustUseMetaprogrammingToTransform()
                );
        }

        // Collect the instrumentation tasks for the invocation and its arguments
        std::list<InstrumentationTask> collectInstrumentationTasks
        (
            const std::vector<std::pair<IncludeTreePtr, int>> & inverseLineMap
        ) const
        {
            std::list<InstrumentationTask> tasks;
            for (const ArgInfo & arg : Args)
            {
                std::list<InstrumentationTask> argTasks = arg.collectInstrumentationTasks(inverseLineMap);
                tasks.splice(tasks.end(), argTasks);
            }

            std::vector<std::string> argNames;
            for (const ArgInfo & arg : Args)
            {
                argNames.push_back(arg.Name);
            }

            std::list<InstrumentationTask> invocationTasks = genInstrumentationTasks(
                InvocationLocation,
                InvocationLocationEnd,
                false, // isArg
                argNames,
                ASTKind,
                IsLValue,
                Name,
                DefinitionLocation,
                Spelling,
                canRustFn(),
                inverseLineMap
            );
            tasks.splice(tasks.end(), invocationTasks);
            
            return tasks;
        }
    };

    // Check if the invocation info is valid and thus should be kept
    // Invalid cases: empty fields, invalid path, non-Expr/Stmt ASTKind
    static bool keepInvocationInfo(const MakiInvocationInfo & invocation)
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

    // Tags the srcStr (C source code at compilation unit level) with the instrumentation tasks collected from
    // 1. the cpp2cStr (.cpp2c invocation summary file produced by Maki)
    // 2. the premiseTree (optional) (conditional macro information produced by Hayroll)
    // Also requires the lineMap ((includeTree, line) <-> line in compilation unit file) and inverseLineMap.
    // Returns the modified (CU) source code as a string.
    static std::string run
    (
        std::string_view cpp2cStr,
        std::optional<PremiseTree *> premiseTreeOpt,
        std::string_view srcStr,
        const std::unordered_map<Hayroll::IncludeTreePtr, std::vector<int>> & lineMap,
        const std::vector<std::pair<IncludeTreePtr, int>> & inverseLineMap
    )
    {
        // Parse cpp2cStr into MakiInvocationInfo objects.
        // For each line, check the first word before the first whitespace (can be space or tab).
        // If it is Invocation, then treat the rest of the line as a JSON string and parse it.
        // If it is not, then ignore the line.
        std::vector<MakiInvocationInfo> invocations;
        {
            std::vector<std::string> cpp2cLines;
            {
                std::istringstream iss{std::string{cpp2cStr}};
                std::string line;
                while (std::getline(iss, line))
                {
                    cpp2cLines.push_back(line);
                }
            }

            for (const std::string & line : cpp2cLines)
            {
                std::istringstream iss(line);
                std::string firstWord;
                iss >> firstWord;

                if (firstWord == "Invocation")
                {
                    std::string jsonString = line.substr(line.find_first_of("{"));
                    try
                    {
                        json j = json::parse(jsonString);
                        MakiInvocationInfo invocation = j.get<MakiInvocationInfo>();
                        if (keepInvocationInfo(invocation))
                        {
                            invocations.push_back(invocation);
                        }
                    }
                    catch (json::parse_error& e)
                    {
                        throw std::runtime_error("Error: Failed to parse JSON: " + std::string(e.what()));
                    }
                }
            }
        }

        TextEditor srcEditor{srcStr};

        for (MakiInvocationInfo & invocation : invocations)
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

            for (ArgInfo & arg : invocation.Args)
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
        for (const MakiInvocationInfo & invocation : invocations)
        {
            std::list<InstrumentationTask> invocationTasks = invocation.collectInstrumentationTasks(inverseLineMap);
            tasks.splice(tasks.end(), invocationTasks);
        }


        if (premiseTreeOpt.has_value())
        {
            PremiseTree * premiseTree = premiseTreeOpt.value();
            assert(premiseTree != nullptr);
            for (const Hayroll::PremiseTree * premiseTreeNode : premiseTree->getDescendants())
            {
                // For each premise tree node that is not a macro expansion node,
                // insert "Debug" instrumentation tasks, with the premise as its name.
                if (premiseTreeNode->isMacroExpansion())
                {
                    continue; // Skip macro expansions
                }

                int lnBegin = premiseTreeNode->programPoint.node.startPoint().row + 1;
                int colBegin = premiseTreeNode->programPoint.node.startPoint().column + 1;
                int lnEnd = premiseTreeNode->programPoint.node.endPoint().row + 1;
                int colEnd = premiseTreeNode->programPoint.node.endPoint().column + 1;

                SPDLOG_TRACE
                (
                    "Premise: {} at IncludeTree {}: {}:{}-{}:{}",
                    premiseTreeNode->premise.to_string(),
                    premiseTreeNode->programPoint.includeTree->stacktrace(),
                    lnBegin, colBegin, lnEnd, colEnd
                );

                if (!lineMap.contains(premiseTreeNode->programPoint.includeTree))
                {
                    SPDLOG_TRACE
                    (
                        "IncludeTree {} not found in lineMap. Skipping premise {}.",
                        premiseTreeNode->programPoint.includeTree->stacktrace(),
                        premiseTreeNode->premise.to_string()
                    );
                    continue; // Skip if the IncludeTree is not in the lineMap
                }

                const std::vector<int> & lineMapSub = lineMap.at(premiseTreeNode->programPoint.includeTree);

                int cuLnBegin = lineMapSub.at(lnBegin);
                int cuLnEnd = lineMapSub.at(lnEnd);
                
                std::string locBegin = makeLocation
                (
                    premiseTreeNode->programPoint.includeTree->path, // This does not matter
                    cuLnBegin,
                    colBegin
                );

                std::string locEnd = makeLocation
                (
                    premiseTreeNode->programPoint.includeTree->path, // This does not matter
                    cuLnEnd,
                    colEnd
                );

                // std::list<InstrumentationTask> premiseTasks = genInstrumentationTasks
                // (
                //     locBegin,
                //     locEnd,
                //     false, // isArg
                //     {}, // argNames
                //     "Debug",
                //     false, // isLvalue
                //     premiseTreeNode->premise.to_string(),
                //     "", // locRef
                //     "", // spelling
                //     false, // canFn
                //     inverseLineMap
                // );

                // tasks.splice(tasks.end(), premiseTasks);
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
