#include <iostream>
#include <fstream>
#include <string>
#include <string_view>
#include <sstream>
#include <vector>
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
        // Example: awk 'NR==144 {print substr($0, 1, 31) "ESCAPED_SPELLING" substr($0, 32)} NR!=144' /home/hurrypeng/libmcs/libm/include/internal_config.h > /tmp/hayroll.tmp && mv /tmp/hayroll.tmp /home/hurrypeng/libmcs/libm/include/internal_config.h
        std::string escaped = escapeString(str);
        return "awk 'NR==" + std::to_string(line) + " {print substr($0, 1, " + std::to_string(col - 1) + ") \"" + escaped + "\" substr($0, " + std::to_string(col) + ")} NR!=" + std::to_string(line) + "' " + filename + " > /tmp/hayroll.tmp && mv /tmp/hayroll.tmp " + filename;
    }

    std::string readable() const
    {
        return filename + ":" + std::to_string(line) + ":" + std::to_string(col) + ": " + str;
    }
};

struct Tag
{
    bool hayroll = true;
    bool begin;
    bool isArg;
    std::string astKind;
    bool isLvalue;
    std::string name;
    std::string locDecl;
    std::string locInv;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Tag, hayroll, begin, isArg, astKind, isLvalue, name, locDecl, locInv)

    std::string stringLiteral() const
    {
        json j = *this;
        return "\"" + escapeString(j.dump()) + "\"";
        // return "\"\"";
    }
};

std::vector<InstrumentationTask> collectInstrumentationTasks
(
    std::string_view locBegin,
    std::string_view locEnd,
    bool isArg,
    std::string_view astKind,
    bool isLvalue,
    std::string_view name,
    std::string_view locDecl,
    std::string_view spelling
)
{
    auto [path, line, col] = parseLocation(locBegin);
    auto [pathEnd, lineEnd, colEnd] = parseLocation(locEnd);

    Tag tagBegin
    {
        .begin = true,
        .isArg = isArg,
        .astKind = std::string(astKind),
        .isLvalue = isLvalue,
        .name = std::string(name),
        .locDecl = std::string(locDecl)
    };

    Tag tagEnd
    {
        .begin = false,
        .isArg = false,
        .astKind = "",
        .isLvalue = false,
        .name = std::string(name),
        .locDecl = std::string(locDecl)
    };

    std::vector<InstrumentationTask> tasks;
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
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ArgInfo, Name, ASTKind, Type, IsLValue, ActualArgLocBegin, ActualArgLocEnd)

    std::vector<InstrumentationTask> collectInstrumentationTasks() const
    {
        return ::collectInstrumentationTasks(
            ActualArgLocBegin,
            ActualArgLocEnd,
            true, // isArg
            ASTKind,
            IsLValue,
            Name,
            "", // locDecl
            Spelling
        );
    }
};

struct InvocationInfo
{
    std::string DefinitionLocation;
    std::string Name;
    std::string ASTKind;
    std::string ReturnType;
    std::string InvocationLocation;
    std::string InvocationLocationEnd;
    std::vector<ArgInfo> Args;
    int NumArguments;

    // Later collected
    std::string Spelling;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(InvocationInfo, DefinitionLocation, Name, ASTKind, ReturnType, InvocationLocation, InvocationLocationEnd, Args, NumArguments)

    std::vector<InstrumentationTask> collectInstrumentationTasks() const
    {
        std::vector<InstrumentationTask> tasks;
        for (const ArgInfo & arg : Args)
        {
            std::vector<InstrumentationTask> argTasks = arg.collectInstrumentationTasks();
            tasks.insert(tasks.end(), argTasks.begin(), argTasks.end());
        }

        std::vector<InstrumentationTask> invocationTasks = ::collectInstrumentationTasks(
            InvocationLocation,
            InvocationLocationEnd,
            false, // isArg
            ASTKind,
            false, // lvalue
            Name,
            DefinitionLocation,
            Spelling
        );
        tasks.insert(tasks.end(), invocationTasks.begin(), invocationTasks.end());

        return tasks;
    }
};

bool keepInvocationInfo(const InvocationInfo& invocation, const std::filesystem::path & projectRootDir)
{
    if (
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
    std::vector<InvocationInfo> invocations;
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
                    InvocationInfo invocation = j.get<InvocationInfo>();
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
    for (InvocationInfo & invocation : invocations)
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
        }
    }

    // Collect instrumentation tasks.
    std::vector<InstrumentationTask> tasks;
    for (const InvocationInfo & invocation : invocations)
    {
        std::vector<InstrumentationTask> invocationTasks = invocation.collectInstrumentationTasks();
        tasks.insert(tasks.end(), invocationTasks.begin(), invocationTasks.end());
    }

    // Sort the tasks so that we can insert them from the end of the file to the beginning.
    std::sort(tasks.begin(), tasks.end());

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

    // for (const auto& invocation : invocations)
    // {
    //     std::cout << "Invocation: " << invocation.Name << std::endl;
    //     std::cout << "Definition Location: " << invocation.DefinitionLocation << std::endl;
    //     std::cout << "AST Kind: " << invocation.ASTKind << std::endl;
    //     std::cout << "Return Type: " << invocation.ReturnType << std::endl;
    //     std::cout << "Invocation Location: " << invocation.InvocationLocation << std::endl;
    //     std::cout << "Invocation Location End: " << invocation.InvocationLocationEnd << std::endl;
    //     std::cout << "Number of Arguments: " << invocation.NumArguments << std::endl;
    //     std::cout << "Spelling: " << invocation.Spelling << std::endl;  
    //     for (const auto& arg : invocation.Args)
    //     {
    //         std::cout << "Argument Name: " << arg.Name << std::endl;
    //         std::cout << "Argument Type: " << arg.Type << std::endl;
    //         std::cout << "Is LValue: " << arg.IsLValue << std::endl;
    //         std::cout << "Actual Argument Location Begin: " << arg.ActualArgLocBegin << std::endl;
    //         std::cout << "Actual Argument Location End: " << arg.ActualArgLocEnd << std::endl;
    //         std::cout << "Spelling: " << arg.Spelling << std::endl;
    //     }
    //     std::cout << std::endl;
    // }

    return 0;
}
