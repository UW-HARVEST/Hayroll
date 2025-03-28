// Reads the .cpp2c invocation summary file and generates the instrumentation task

#include <iostream>
#include <fstream>
#include <string>
#include <string_view>
#include <sstream>
#include <vector>
#include <list>
#include <tuple>
#include <filesystem>

#include "json.hpp"

using namespace nlohmann;

std::string escapeString(std::string_view str)
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

// Parse a location string in the format "path:line:col" into a tuple of (path, line, col)
std::tuple<std::filesystem::path, int, int> parseLocation(const std::string_view loc)
{
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
    path = std::filesystem::canonical(path);

    return {path, line, col};
}

// Tag structure to hold the information about the instrumentation task
// This maps to the JSON structure in the .cpp2c invocation summary file
struct Tag
{
    bool hayroll = true;
    bool begin;
    bool isArg;
    std::vector<std::string> argNames;
    std::string astKind;
    bool isLvalue;
    std::string name;
    std::string locDecl;
    std::string locInv;
    std::string locArg;

    bool canFn;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Tag, hayroll, begin, isArg, argNames, astKind, isLvalue, name, locDecl, locInv, locArg, canFn)

    // Escape the JSON string to make it a valid C string that embeds into C code
    std::string stringLiteral() const
    {
        json j = *this;
        return "\"" + escapeString(j.dump()) + "\"";
    }
};

// InstrumentationTask structure that can be transformed into an awk command
struct InstrumentationTask
{
    std::string filename;
    int line;
    int col;
    std::string str;

    bool operator<(const InstrumentationTask & other) const
    {
        // Insert from the end of the file to the beginning.
        if (filename != other.filename)
        {
            return filename < other.filename;
        }
        if (line != other.line)
        {
            return line > other.line;
        }
        return col > other.col;
    }

    std::string awkCommand() const
    {
        // Example: awk 'NR==144 {print substr($0, 1, 31) "ESCAPED_SPELLING" substr($0, 32)} NR!=144' /home/husky/libmcs/libm/include/internal_config.h > /tmp/hayroll.tmp && mv /tmp/hayroll.tmp /home/husky/libmcs/libm/include/internal_config.h
        std::string escaped = escapeString(str);
        return "awk 'NR==" + std::to_string(line) + " {print substr($0, 1, " + std::to_string(col - 1) + ") \"" + escaped + "\" substr($0, " + std::to_string(col) + ")} NR!=" + std::to_string(line) + "' " + filename + " > /tmp/hayroll.tmp && mv /tmp/hayroll.tmp " + filename;
    }

    std::string readable() const
    {
        return filename + ":" + std::to_string(line) + ":" + std::to_string(col) + ": " + str;
    }
};

// Generate instrumentation tasks based on the provided parameters
std::list<InstrumentationTask> genInstrumentationTasks
(
    std::string_view locBegin,
    std::string_view locEnd,
    bool isArg,
    const std::vector<std::string> & argNames,
    std::string_view astKind,
    bool isLvalue,
    std::string_view name,
    std::string_view locDecl,
    std::string_view locInv, // Only for args, the locInv for the invocation is locBegin
    std::string_view spelling,
    bool canFn
)
{
    auto [path, line, col] = parseLocation(locBegin);
    auto [pathEnd, lineEnd, colEnd] = parseLocation(locEnd);

    Tag tagBegin
    {
        .begin = true,
        .isArg = isArg,
        .argNames = argNames,
        .astKind = std::string(astKind),
        .isLvalue = isLvalue,
        .name = std::string(name),
        .locDecl = std::string(locDecl),
        .locInv = isArg ? std::string(locInv) : std::string(locBegin),
        .locArg = isArg ? std::string(locBegin) : "",

        .canFn = canFn
    };

    Tag tagEnd
    {
        .begin = false,
        .isArg = isArg,
        .argNames = {},
        .astKind = "",
        .isLvalue = isLvalue,
        .name = std::string(name),
        .locDecl = std::string(locDecl),
        .locInv = isArg ? std::string(locInv) : std::string(locBegin),
        .locArg = isArg ? std::string(locBegin) : "",

        .canFn = canFn
    };

    std::list<InstrumentationTask> tasks;
    if (astKind == "Expr")
    {
        if (isLvalue)
        {
            // Template:
            // (*((*tagBegin)?(&(ORIGINAL_INVOCATION)):((__typeof__(spelling)*)(0))))
            InstrumentationTask taskLeft
            {
                .filename = std::string(path),
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
                .filename = std::string(path),
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
                .filename = std::string(path),
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
                .filename = std::string(path),
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
    else if (astKind == "Stmt")
    {
        // Template:
        // {*tagBegin;ORIGINAL_INVOCATION;*tagEnd;}
        InstrumentationTask taskLeft
        {
            .filename = std::string(path),
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
            .filename = std::string(path),
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

    return tasks;
}

// ArgInfo structure to hold the information about the arguments
// This maps to the JSON structure in the .cpp2c invocation summary file
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
    std::list<InstrumentationTask> collectInstrumentationTasks() const
    {
        return genInstrumentationTasks(
            ActualArgLocBegin,
            ActualArgLocEnd,
            true, // isArg
            {}, // argNames
            ASTKind,
            IsLValue,
            Name,
            "", // locDecl
            InvocationLocation,
            Spelling,
            false // canFn
        );
    }
};

// MakiInvocationInfo structure to hold the information about the invocation
// This maps to the JSON structure in the .cpp2c invocation summary file
struct MakiInvocationInfo
{
    // In accordance with the Maki macros.py
    std::string Name;
    std::string DefinitionLocation;
    std::string InvocationLocation;
    std::string ASTKind; // ['Decl', 'Stmt', 'TypeLoc', 'Expr']
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
            (ASTKind == "Stmt" || ASTKind == "Expr")
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
    std::list<InstrumentationTask> collectInstrumentationTasks() const
    {
        std::list<InstrumentationTask> tasks;
        for (const ArgInfo & arg : Args)
        {
            std::list<InstrumentationTask> argTasks = arg.collectInstrumentationTasks();
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
            "", // locInv, not required for invocations
            Spelling,
            canRustFn()
        );
        tasks.splice(tasks.end(), invocationTasks);
        
        return tasks;
    }
};

// Check if the invocation info is valid and should be kept
bool keepInvocationInfo(const MakiInvocationInfo & invocation, const std::filesystem::path & projectRootDir)
{
    if
    (
        invocation.DefinitionLocation.empty()
        || invocation.Name.empty()
        || invocation.ASTKind.empty()
        || invocation.ReturnType.empty()
        || invocation.InvocationLocation.empty()
        || invocation.InvocationLocationEnd.empty()
    )
    {
        return false;
    }
    auto [path, line, col] = parseLocation(invocation.InvocationLocation);
    // If the path is not a subpath of the project root directory, skip it.
    if (!path.string().starts_with(projectRootDir.string()))
    {
        return false;
    }
    // If ASTKind is not "Expr" or "Stmt", skip it.
    if (invocation.ASTKind != "Expr" && invocation.ASTKind != "Stmt")
    {
        return false;
    }
    return true;
}

int main(const int argc, const char* argv[])
{
    // Take the first argument as the .cpp2c invocation summary file. Check if it is provided.
    // Take the second argument as the project root directory. Check if it is provided.
    //     Source files outside the project root directory will not be instrumented.
    std::string cpp2cFilePathStr;
    std::string projectRootDirStr;

    if (argc != 3)
    {
        std::cerr << "Usage: " << argv[0] << " <cpp2c invocation summary file> <project root directory>" << std::endl;
        return 1;
    }
    cpp2cFilePathStr = argv[1];
    projectRootDirStr = argv[2];

    std::filesystem::path projectRootDir(projectRootDirStr);
    projectRootDir = std::filesystem::canonical(projectRootDir);

    // Open the file. For each line, check the first word before the first whitespace (can be space or tab).
    // If it is Invocation, then treat the rest of the line as a JSON string and parse it.
    // If it is not, then ignore the line.
    std::vector<MakiInvocationInfo> invocations;
    {
        std::ifstream cpp2cFile(cpp2cFilePathStr);
        if (!cpp2cFile.is_open())
        {
            std::cerr << "Error: Could not open file " << cpp2cFilePathStr << std::endl;
            return 1;
        }

        std::string line;
        while (std::getline(cpp2cFile, line))
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
                    if (keepInvocationInfo(invocation, projectRootDir))
                    {
                        invocations.push_back(invocation);
                    }
                }
                catch (json::parse_error& e)
                {
                    std::cerr << "Error: Failed to parse JSON: " << e.what() << std::endl;
                    return 1;
                }
            }
        }

        cpp2cFile.close();
    }
    

    // Collect the spelling of each invocation and argument.
    // They are the strings between xxxLocBegin and xxxLocEnd.
    // We can use awk to extract them.
    // Example awk 'NR==144 {print substr($0, 32, 15)}' /home/husky/libmcs/libm/include/internal_config.h
    for (MakiInvocationInfo & invocation : invocations)
    {
        auto [path, line, col] = parseLocation(invocation.InvocationLocation);
        auto [pathEnd, lineEnd, colEnd] = parseLocation(invocation.InvocationLocationEnd);
        assert(line == lineEnd);
        std::string awkCommand = "awk 'NR==" + std::to_string(line)
            + " {print substr($0, " + std::to_string(col)
            + ", " + std::to_string(colEnd - col)
            + ")}' " + std::string(path);

        std::array<char, 128> buffer;
        std::string result;
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(awkCommand.c_str(), "r"), pclose);
        if (!pipe)
        {
            std::cerr << "Error: popen() failed!" << std::endl;
            return 1;
        }
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
        {
            result += buffer.data();
        }
        // get rid of the newline character
        if (!result.empty() && result.back() == '\n')
        {
            result.pop_back();
        }
        if (result.size() != colEnd - col)
        {
            std::cerr << "Error: awk command result size does not match the expected size: " << result << " vs " << colEnd << " - " << col << std::endl;
            return 1;
        }
        invocation.Spelling = result;

        for (ArgInfo & arg : invocation.Args)
        {
            auto [argPath, argLine, argCol] = parseLocation(arg.ActualArgLocBegin);
            auto [argPathEnd, argLineEnd, argColEnd] = parseLocation(arg.ActualArgLocEnd);
            assert(argLine == argLineEnd);
            std::string argAwkCommand = "awk 'NR==" + std::to_string(argLine)
            + " {print substr($0, " + std::to_string(argCol)
            + ", " + std::to_string(argColEnd - argCol)
            + ")}' " + std::string(argPath);

            std::string argResult;
            std::unique_ptr<FILE, decltype(&pclose)> argPipe(popen(argAwkCommand.c_str(), "r"), pclose);
            if (!argPipe) {
                std::cerr << "Error: popen() failed!" << std::endl;
                return 1;
            }
            while (fgets(buffer.data(), buffer.size(), argPipe.get()) != nullptr)
            {
                argResult += buffer.data();
            }
            // get rid of the newline character
            if (!argResult.empty() && argResult.back() == '\n')
            {
                argResult.pop_back();
            }
            if (argResult.size() != argColEnd - argCol)
            {
                std::cerr << "Error: awk command result size does not match the expected size: " << argResult << " vs " << argColEnd << " - " << argCol << std::endl;
                return 1;
            }
            arg.Spelling = argResult;
            arg.InvocationLocation = invocation.InvocationLocation;
        }
    }

    // Collect instrumentation tasks.
    std::list<InstrumentationTask> tasks;
    for (const MakiInvocationInfo & invocation : invocations)
    {
        std::list<InstrumentationTask> invocationTasks = invocation.collectInstrumentationTasks();
        tasks.splice(tasks.end(), invocationTasks);
    }

    // Sort the tasks so that we can insert them from the end of the file to the beginning.
    tasks.sort();

    // Execute the awk commands.
    for (const InstrumentationTask & task : tasks)
    {
        std::cout << task.readable() << std::endl;
        int ret = system(task.awkCommand().c_str());
        if (ret != 0)
        {
            std::cerr << "Error: Failed to execute awk command: " << task.awkCommand() << std::endl;
            return 2;
        }
    }

    return 0;
}
