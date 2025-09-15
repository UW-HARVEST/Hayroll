#ifndef HAYROLL_MAKI_SUMMARY_HPP
#define HAYROLL_MAKI_SUMMARY_HPP

#include <string>
#include <string_view>
#include <vector>
#include <list>
#include <memory>
#include <utility>
#include <sstream>
#include <stdexcept>
#include <ranges>
#include <map>

#include "IncludeTree.hpp"

#include "json.hpp"

namespace Hayroll
{

// Maki's analysis result of an argument in a macro invocation
// This maps to the JSON structure in Maki's .cpp2c invocation summary file
struct MakiArgSummary
{
    std::string Name;
    std::string ASTKind;
    std::string Type;
    bool IsLValue;
    bool ExpandedWhereAddressableValueRequired;
    bool ExpandedWhereModifiableValueRequired;
    std::string ActualArgLocBegin; // /path/to/file.cpp:line:col
    std::string ActualArgLocEnd;

    // Later collected
    std::string Spelling;
    std::string InvocationLocation;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(MakiArgSummary, Name, ASTKind, Type, IsLValue, ExpandedWhereAddressableValueRequired, ExpandedWhereModifiableValueRequired, ActualArgLocBegin, ActualArgLocEnd)
};

// Maki's analysis result of a macro invocation
// This maps to the JSON structure in Maki's .cpp2c invocation summary file
struct MakiInvocationSummary
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
    std::vector<MakiArgSummary> Args;

    // Later collected
    std::string Spelling;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE
    (
        MakiInvocationSummary,
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
    bool canBeRustFn() const
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
};

struct MakiRangeSummary
{
    std::string Location;
    std::string LocationEnd;
    std::string ASTKind;
    std::string ParentLocation;
    bool IsPlaceholder;
    std::string Premise;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(MakiRangeSummary, Location, LocationEnd, ASTKind, ParentLocation, IsPlaceholder, Premise)

    // Take all range summaries generated from different DefineSets
    // For each item in a range summary vector,
    // if its ASTKind is "" but other vectors have a non-empty ASTKind at the same IncludeTreePtr,
    // fill in the ASTKind from other vectors and mark IsPlaceholder = true
    // if its ASTKind is non-empty, mark IsPlaceholder = false
    // if all vectors have "" at the same IncludeTreePtr. delete this item
    static std::vector<std::vector<MakiRangeSummary>> complementRangeSummaries
    (
        const std::vector<std::vector<MakiRangeSummary>> & rangeSummaryVecs,
        const std::vector<std::vector<std::pair<IncludeTreePtr, int>>> & inverseLineMaps
    )
    {
        assert(rangeSummaryVecs.size() == inverseLineMaps.size());

        // Collect all non-empty ASTKinds for each source location and check for consistency
        std::map<std::string, std::string> commonMap; // srcLocation -> unified ASTKind (plural if any present)

        auto baseOf = [](std::string_view k) -> std::string_view
        {
            if (k == "Decl" || k == "Decls") return "Decl";
            if (k == "Stmt" || k == "Stmts") return "Stmt";
            return k; // For other kinds, base is itself
        };
        auto areCompatible = [&](std::string_view a, std::string_view b) -> bool
        {
            return a.empty() || b.empty() || baseOf(a) == baseOf(b);
        };
        auto unifyKind = [&](std::string_view a, std::string_view b) -> std::string_view
        {
            assert(areCompatible(a, b));
            if (a.empty()) return b;
            if (b.empty()) return a;
            if (a == b) return a;
            std::string_view baseA = baseOf(a);
            if (baseA == a) return b; // Prefer plural
            if (baseA == b) return a; // Prefer plural
            return a;
        };
        for (const auto & [rangeSummaryVec, inverseLineMap] : std::views::zip(rangeSummaryVecs, inverseLineMaps))
        {
            for (const MakiRangeSummary & rangeSummary : rangeSummaryVec)
            {
                auto [path, line, col] = parseLocation(rangeSummary.Location);
                auto [includeTree, srcLine] = inverseLineMap.at(line);
                std::filesystem::path srcPath = includeTree->path;
                std::string srcLoc = makeLocation(srcPath, srcLine, col);
                std::string current = rangeSummary.ASTKind;
                std::string commonASTKind = commonMap.contains(srcLoc) ? commonMap.at(srcLoc) : "";
                if (!areCompatible(commonASTKind, current))
                {
                    SPDLOG_ERROR("Inconsistent ASTKind for location {}: {} vs {}", srcLoc, current, commonASTKind);
                    throw std::runtime_error("Inconsistent ASTKind for location " + srcLoc);
                }
                // Update to unified (prefer plural when any present)
                commonMap[srcLoc] = unifyKind(commonASTKind, current);
            }
        }

        // Populate the complemented range summary vectors
        std::vector<std::vector<MakiRangeSummary>> result;
        for (const auto & [rangeSummaryVec, inverseLineMap] : std::views::zip(rangeSummaryVecs, inverseLineMaps))
        {
            std::vector<MakiRangeSummary> complementedVec;
            for (const MakiRangeSummary & rangeSummary : rangeSummaryVec)
            {
                auto [path, line, col] = parseLocation(rangeSummary.Location);
                auto [includeTree, srcLine] = inverseLineMap.at(line);
                std::filesystem::path srcPath = includeTree->path;
                std::string srcLoc = makeLocation(srcPath, srcLine, col);
                std::string commonASTKind = commonMap.at(srcLoc);
                if (commonASTKind.empty())
                {
                    // All vectors have "" at this location; skip it
                    continue;
                }
                MakiRangeSummary complementedSummary = rangeSummary;
                complementedSummary.ASTKind = commonASTKind;
                complementedSummary.IsPlaceholder = rangeSummary.ASTKind.empty();
                complementedVec.push_back(complementedSummary);
            }
            result.push_back(complementedVec);
        }
        return result;
    }
};

std::pair<std::vector<MakiInvocationSummary>, std::vector<MakiRangeSummary>> parseCpp2cSummary(std::string_view cpp2cStr)
{
    std::vector<MakiInvocationSummary> invocations;
    std::vector<MakiRangeSummary> ranges;

    std::istringstream iss{std::string{cpp2cStr}};
    std::string line;
    while (std::getline(iss, line))
    {
        // Extract the first word (token before any whitespace)
        std::istringstream line_ss(line);
        std::string firstWord;
        line_ss >> firstWord;

        if (firstWord != "Invocation" && firstWord != "Range")
        {
            continue; // ignore unrelated lines
        }

        // Find the JSON payload starting at the first '{'
        size_t jsonPos = line.find('{');
        if (jsonPos == std::string::npos)
        {
            // No JSON on this line; skip silently
            continue;
        }
        std::string jsonString = line.substr(jsonPos);

        try
        {
            nlohmann::json j = nlohmann::json::parse(jsonString);
            if (firstWord == "Invocation")
            {
                invocations.push_back(j.get<MakiInvocationSummary>());
            }
            else if (firstWord == "Range")
            {
                ranges.push_back(j.get<MakiRangeSummary>());
            }
            else assert(false);
        }
        catch (nlohmann::json::parse_error &e)
        {
            throw std::runtime_error(std::string("Error: Failed to parse JSON: ") + e.what());
        }
    }

    return {std::move(invocations), std::move(ranges)};
}

} // namespace Hayroll

#endif // HAYROLL_MAKI_SUMMARY_HPP
