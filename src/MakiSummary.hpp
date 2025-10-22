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
#include <algorithm>

#include "IncludeTree.hpp"
#include "LineMatcher.hpp"
#include "MakiWrapper.hpp"

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
    std::string Spelling = "";
    std::string InvocationLocation = "";

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT
    (
        MakiArgSummary,
        Name,
        ASTKind,
        Type,
        IsLValue,
        ExpandedWhereAddressableValueRequired,
        ExpandedWhereModifiableValueRequired,
        ActualArgLocBegin,
        ActualArgLocEnd,
        Spelling,
        InvocationLocation
    )
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
    bool IsInvokedInStmtBlock;
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
    std::string Spelling = "";

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT
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
        IsInvokedInStmtBlock,
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
        Args,
        Spelling
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
};

struct MakiRangeSummary
{
    std::string Location; // cuLoc
    std::string LocationEnd; // cuLoc
    std::string ASTKind;
    bool IsLValue;
    std::string ParentLocation; // srcLoc
    CodeRangeAnalysisTaskExtraInfo ExtraInfo;
    
    // Later collected in complementRangeSummaries
    bool IsPlaceholder = false;
    // For expr: ParentLocation; for stmt(s): ifGroupLocation; for decl(s): ifGroupLocation
    std::string ReferenceLocation = ""; // srcLoc

    // Later collected in Seeder
    std::string Spelling = "";

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT
    (
        MakiRangeSummary,
        Location, LocationEnd, ASTKind, IsLValue, ParentLocation, ExtraInfo,
        IsPlaceholder, ReferenceLocation, Spelling
    )

    // Complement summary vectors with other vectors of different DefineSets
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

        auto baseOf = [](std::string_view k) -> std::string_view
        {
            if (k == "Decl" || k == "Decls") return "Decl";
            if (k == "Stmt" || k == "Stmts") return "Stmt";
            return k; // For other kinds, base is itself
        };
        auto areASTKindCompatible = [&](std::string_view a, std::string_view b) -> bool
        {
            return a.empty() || b.empty() || baseOf(a) == baseOf(b);
        };
        auto unifyASTKind = [&](std::string_view a, std::string_view b) -> std::string_view
        {
            assert(areASTKindCompatible(a, b));
            if (a.empty()) return b;
            if (b.empty()) return a;
            if (a == b) return a;
            std::string_view baseA = baseOf(a);
            if (baseA == a) return b; // Prefer plural
            if (baseA == b) return a; // Prefer plural
            return a;
        };

        // Collect all non-empty ASTKinds and ParentLocations for each source location and check for consistency
        std::map<std::string, std::string> commonASTKinds; // srcLocation -> unified ASTKind (plural if any present)
        std::map<std::string, std::string> commonParentSrcLocs; // srcLocation -> unified ParentLocation (non-empty if any present)
        for (const auto & [rangeSummaryVec, inverseLineMap] : std::views::zip(rangeSummaryVecs, inverseLineMaps))
        {
            for (const MakiRangeSummary & rangeSummary : rangeSummaryVec)
            {
                // ASTKind
                std::string srcLoc = LineMatcher::cuLocToSrcLoc(rangeSummary.Location, inverseLineMap);

                std::string currentASTKind = rangeSummary.ASTKind;
                std::string commonASTKind = commonASTKinds.contains(srcLoc) ? commonASTKinds.at(srcLoc) : "";
                if (!areASTKindCompatible(commonASTKind, currentASTKind))
                {
                    SPDLOG_ERROR("Inconsistent ASTKind for location {}: {} vs {}", srcLoc, currentASTKind, commonASTKind);
                    throw std::runtime_error("Inconsistent ASTKind for location " + srcLoc);
                }
                // Update to unified (prefer plural when any present)
                commonASTKinds[srcLoc] = unifyASTKind(commonASTKind, currentASTKind);

                // ParentLocation
                std::string currentParentLoc = rangeSummary.ParentLocation;
                std::string currentParentSrcLoc = "";
                if (!currentParentLoc.empty())
                {
                    currentParentSrcLoc = LineMatcher::cuLocToSrcLoc(currentParentLoc, inverseLineMap);
                }
                std::string commonParentLoc = commonParentSrcLocs.contains(srcLoc) ? commonParentSrcLocs.at(srcLoc) : "";
                if (!currentParentSrcLoc.empty() && !commonParentLoc.empty() && currentParentSrcLoc != commonParentLoc)
                {
                    SPDLOG_ERROR("Inconsistent ParentLocation for location {}: {} vs {}", srcLoc, currentParentLoc, commonParentLoc);
                    throw std::runtime_error("Inconsistent ParentLocation for location " + srcLoc);
                }
                // Update to unified (prefer non-empty when any present)
                if (commonParentLoc.empty())
                {
                    commonParentSrcLocs[srcLoc] = currentParentSrcLoc;
                    SPDLOG_TRACE("Set common ParentLocation for {} to {}", srcLoc, currentParentSrcLoc);
                }
            }
        }

        // Populate the complemented range summary vectors with extra Expr handling
        std::vector<std::vector<MakiRangeSummary>> rangeSummaryVecsComplemented;
        for (const auto & [rangeSummaryVec, inverseLineMap] : std::views::zip(rangeSummaryVecs, inverseLineMaps))
        {
            std::vector<MakiRangeSummary> complementedVec;
            for (const MakiRangeSummary & rangeSummary : rangeSummaryVec)
            {
                std::string srcLoc = LineMatcher::cuLocToSrcLoc(rangeSummary.Location, inverseLineMap);
                std::string commonASTKind = commonASTKinds.at(srcLoc);
                SPDLOG_TRACE("For {}, common ASTKind is {}", srcLoc, commonASTKind);
                std::string commonParentSrcLoc = commonParentSrcLocs.at(srcLoc);
                SPDLOG_TRACE("For {}, common ParentLocation is {}", srcLoc, commonParentSrcLoc);
                if (commonASTKind.empty())
                {
                    // All vectors have "" at this location; skip it
                    continue;
                }
                assert(commonASTKind != "Expr" || !commonParentSrcLoc.empty()); // If ASTKind is Expr, ParentLocation must be non-empty

                MakiRangeSummary complementedSummary = rangeSummary;
                complementedSummary.ASTKind = commonASTKind;
                complementedSummary.IsPlaceholder = rangeSummary.ASTKind.empty();
                complementedSummary.ParentLocation = commonParentSrcLoc;

                if (commonASTKind == "Expr")
                {
                    // For Expr, ReferenceLocation is ParentLocation
                    complementedSummary.ReferenceLocation = commonParentSrcLoc;
                }
                else if (commonASTKind == "Stmt" || commonASTKind == "Stmts" || commonASTKind == "Decl" || commonASTKind == "Decls")
                {
                    complementedSummary.ReferenceLocation = 
                        LineMatcher::cuLnColToSrcLoc
                        (
                            complementedSummary.ExtraInfo.ifGroupLnColBegin,
                            inverseLineMap
                        );
                }
                else assert(false);

                complementedVec.push_back(std::move(complementedSummary));
            }
            rangeSummaryVecsComplemented.push_back(std::move(complementedVec));
        }

        // Group all complemented vectors by reference location
        // For each group, if any vector has a non-placeholder item, remove all placeholder items
        // if all items are placeholders, keep one and remove the rest
        std::vector<std::vector<MakiRangeSummary>> rangeSummaryVecsFinal;
        for (const std::vector<MakiRangeSummary> & vec : rangeSummaryVecsComplemented)
        {
            std::map<std::string, std::vector<MakiRangeSummary>> groupedByRefLoc;
            for (const MakiRangeSummary & rangeSummary : vec)
            {
                groupedByRefLoc[rangeSummary.ReferenceLocation].push_back(rangeSummary);
            }

            std::vector<MakiRangeSummary> filteredVec;
            for (const auto & [refLoc, group] : groupedByRefLoc)
            {
                bool anyNonPlaceholder = std::ranges::any_of(group, [](const MakiRangeSummary & r) { return !r.IsPlaceholder; });
                if (anyNonPlaceholder)
                {
                    // Keep only non-placeholder items
                    std::ranges::copy_if(group, std::back_inserter(filteredVec), [](const MakiRangeSummary & r) { return !r.IsPlaceholder; });
                }
                else
                {
                    // Keep one placeholder item
                    filteredVec.push_back(group.front());
                }
            }

            rangeSummaryVecsFinal.push_back(std::move(filteredVec));
        }

        return rangeSummaryVecsFinal;
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
