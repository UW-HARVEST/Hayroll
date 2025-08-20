#ifndef HAYROLL_MAKI_SUMMARY_HPP
#define HAYROLL_MAKI_SUMMARY_HPP

#include <string>
#include <string_view>
#include <vector>
#include <list>
#include <memory>

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
    std::string ActualArgLocBegin; // /path/to/file.cpp:line:col
    std::string ActualArgLocEnd;

    // Later collected
    std::string Spelling;
    std::string InvocationLocation;
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(MakiArgSummary, Name, ASTKind, Type, IsLValue, ActualArgLocBegin, ActualArgLocEnd)
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
};

} // namespace Hayroll

#endif // HAYROLL_MAKI_SUMMARY_HPP
