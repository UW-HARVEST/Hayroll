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

#include <spdlog/spdlog.h>
#include "json.hpp"

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
    // Use nlohmann::json to escape a string as a C string literal (without surrounding quotes)
    static std::string escapeString(std::string_view str)
    {
        std::string dumped = nlohmann::json(str).dump();
        // Remove the surrounding quotes
        assert(dumped.size() >= 2 && dumped.front() == '"' && dumped.back() == '"');
        std::string escaped = dumped.substr(1, dumped.size() - 2);
        return escaped;
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
        return std::format("{}:{}:{}", path.string(), line, col);
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
        std::string cuLnColBegin; // Loc in the CU file, without filename (only "l:c")
        std::string cuLnColEnd;
        std::string locRefBegin; // For invocation: definition begin; for arg: invocation begin

        bool canBeFn;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Tag, hayroll, begin, isArg, argNames, astKind, isLvalue, name, locBegin, locEnd, 
                                       cuLnColBegin, cuLnColEnd, locRefBegin, canBeFn);

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
            return std::format("{}:{}: {}", line, col, str);
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

        std::filesystem::path srcPath = includeTree->path;
        std::filesystem::path locRefSrcPath = locRefIncludeTree->path;

        std::string srcLocBegin = makeLocation(srcPath, srcLine, col);
        std::string srcLocEnd = makeLocation(srcPath, srcLineEnd, colEnd);
        std::string srcLocRefBegin = makeLocation(locRefSrcPath, locRefSrcLine, locRefCol);
        std::string cuLnColBegin = std::format("{}:{}", line, col);
        std::string cuLnColEnd = std::format("{}:{}", lineEnd, colEnd);

        Tag tagBegin
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
            // ORIGINAL_INVOCATION const char * HAYROLL_TAG_FOR_<ORIGINAL_INVOCATION> = tagBegin;\n
            InstrumentationTask taskLeft
            {
                .line = line,
                .col = colEnd, // Avoid affecting ORIGINAL_INVOCATION's col
                .str = 
                (
                    std::stringstream()
                    << " const char * HAYROLL_TAG_FOR_"
                    << name
                    << " = "
                    << tagBegin.stringLiteral()
                    << ";"
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

    // Generate tags for the arguments
    static std::list<InstrumentationTask> collectArgInstrumentationTasks
    (
        const MakiArgSummary & arg,
        const std::vector<std::pair<IncludeTreePtr, int>> & inverseLineMap
    )
    {
        return genInstrumentationTasks
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

    // Collect the instrumentation tasks for the invocation and its arguments
    static std::list<InstrumentationTask> collectInvocationInstrumentationTasks
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

        std::list<InstrumentationTask> invocationTasks = genInstrumentationTasks(
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
        // Parse cpp2cStr into MakiInvocationSummary objects.
        // For each line, check the first word before the first whitespace (can be space or tab).
        // If it is Invocation, then treat the rest of the line as a JSON string and parse it.
        // If it is not, then ignore the line.
        std::vector<MakiInvocationSummary> invocations;
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
                        MakiInvocationSummary invocation = j.get<MakiInvocationSummary>();
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
            std::list<InstrumentationTask> invocationTasks = collectInvocationInstrumentationTasks(invocation, inverseLineMap);
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
                //     false, // canBeFn
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
