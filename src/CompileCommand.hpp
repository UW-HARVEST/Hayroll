#ifndef HAYROLL_COMPILECOMMAND_HPP
#define HAYROLL_COMPILECOMMAND_HPP

#include <algorithm>
#include <string>
#include <vector>
#include <filesystem>
#include <functional>

#include <spdlog/spdlog.h>
#include "subprocess.hpp"
#include "json.hpp"

#include "Util.hpp"
#include "TempDir.hpp"
#include "DefineSet.hpp"

namespace Hayroll
{

// A representation of an item in compile_commands.json
struct CompileCommand
{
    std::vector<std::string> arguments;
    std::filesystem::path directory;
    std::filesystem::path file;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(CompileCommand, arguments, directory, file)

    std::vector<std::filesystem::path> getIncludePaths() const
    {
        std::vector<std::filesystem::path> paths;
        paths.push_back(directory); // Include the command's directory as well.
        for (const auto & arg : arguments)
        {
            if (!arg.starts_with("-I")) continue;
            std::string pathStr = arg.substr(2);
            std::filesystem::path path(pathStr);
            std::filesystem::path absolutePath = path;
            if (!absolutePath.is_absolute())
            {
                absolutePath = directory / path; // Make it absolute relative to the command's directory.
            }
            assert(absolutePath.is_absolute());
            paths.push_back(absolutePath);
        }
        return paths;
    }

    CompileCommand withUpdatedFilePathPrefix
    (
        const std::filesystem::path & newPrefix,
        const std::filesystem::path & oldPrefix
    ) const
    {
        // Attempt to compute path of file relative to its directory.
        std::filesystem::path relativeFile;
        bool contained = false;
        try 
        {
            relativeFile = std::filesystem::relative(this->file, oldPrefix);
            // If the relative path starts with ".." then file is not inside directory.
            if (!relativeFile.empty())
            {
                auto firstComp = *relativeFile.begin();
                contained = (firstComp != "..");
            }
        }
        catch (...)
        {
            contained = false;
        }

        if (!contained)
        {
            // Original behavior: preserve relative layout under new directory.
            SPDLOG_ERROR
            (
                "Error: file {} is not under the old prefix {}, cannot preserve relative layout.",
                this->file.string(),
                oldPrefix.string()
            );
            assert(false);
        }

        std::filesystem::path updatedFile = newPrefix / relativeFile;

        return withUpdatedFile(updatedFile);
    }

    // Update the file extension of the command's file and arguments.
    // Multiple extensions are considered the extension of the file.
    // e.g. in "xxx.cu.c" the extension is ".cu.c".
    CompileCommand withUpdatedFileExtension(const std::string & newExtension) const
    {
        std::string filename = this->file.filename().string();
        size_t dotPos = filename.find('.');
        std::string base = filename.substr(0, dotPos); // dotPos==npos -> whole string
        std::string newFilename = base + newExtension;
        std::filesystem::path updatedFile = this->file.parent_path() / newFilename;
        return withUpdatedFile(updatedFile);
    }

    // Update the file using absolute path.
    // This does not just change the filename, but also ignores the original file path.
    CompileCommand withUpdatedFile(std::filesystem::path newFile) const
    {
        newFile = std::filesystem::weakly_canonical(newFile);
        CompileCommand updatedCommand = *this;
        updatedCommand.file = newFile;
        // If the last argument is also a file path, update it as well.
        std::string lastArg = updatedCommand.arguments.back();
        try
        {
            std::filesystem::path lastArgPath(lastArg);
            if (lastArgPath.filename() == this->file.filename())
            {
                updatedCommand.arguments.back() = std::filesystem::relative(newFile, this->directory).string();
            }
        }
        catch (const std::exception & e)
        {
            SPDLOG_TRACE("Last argument is not a valid path: {}", e.what());
        }
        return updatedCommand;
    }

    CompileCommand withDeletedDefines() const
    {
        CompileCommand updatedCommand = *this;
        std::vector<std::string> newArgs;
        for (const auto & arg : updatedCommand.arguments)
        {
            if (arg.starts_with("-D")) continue;
            newArgs.push_back(arg);
        }
        updatedCommand.arguments = std::move(newArgs);
        return updatedCommand;
    }

    CompileCommand withAddedDefineSet(const DefineSet & defineSet) const
    {
        CompileCommand updatedCommand = *this;
        auto newArgs = defineSet.toOptions();
        // Insert after the compiler executable (the first argument)
        updatedCommand.arguments.insert(++updatedCommand.arguments.begin(), newArgs.begin(), newArgs.end());
        return updatedCommand;
    }

    CompileCommand withUpdatedDefineSet(const DefineSet & defineSet) const
    {
        return withDeletedDefines().withAddedDefineSet(defineSet);
    }

    CompileCommand withSanitizedFilename() const
    {
        CompileCommand updatedCommand = *this;

        auto sanitize = [](const std::string & name)
        {
            std::string sanitized;
            sanitized.reserve(name.size());
            bool lastWasUnderscore = false;
            for (char ch : name)
            {
                bool allow = (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_');
                if (allow)
                {
                    sanitized.push_back(ch);
                    lastWasUnderscore = (ch == '_');
                }
                else if (!lastWasUnderscore)
                {
                    sanitized.push_back('_');
                    lastWasUnderscore = true;
                }
            }
            while (!sanitized.empty() && sanitized.back() == '_')
            {
                sanitized.pop_back();
            }
            if (sanitized.empty())
            {
                sanitized = "file";
            }
            return sanitized;
        };

        std::filesystem::path original = updatedCommand.file.filename();
        std::string stem = original.stem().string();
        std::string extension = original.has_extension() ? original.extension().string() : std::string();

        std::string sanitizedStem = sanitize(stem);
        if (sanitizedStem.empty())
        {
            sanitizedStem = "file";
        }

        std::string sanitizedFilename = extension.empty() ? sanitizedStem : sanitizedStem + extension;
        std::filesystem::path updatedFile = updatedCommand.file.parent_path() / sanitizedFilename;

        return updatedCommand.withUpdatedFile(updatedFile);
    }

    CompileCommand withCleanup() const
    {
        CompileCommand updatedCommand = *this;
        auto isWerrorFlag = [](const std::string & arg)
        {
            return arg == "-Werror" || arg.starts_with("-Werror=");
        };
        std::erase_if(updatedCommand.arguments, isWerrorFlag);
        return updatedCommand;
    }

    static std::vector<CompileCommand> fromCompileCommandsJson(const nlohmann::json & json)
    {
        if (!json.is_array())
        {
            SPDLOG_ERROR("Expected an array in compile_commands.json, but got: {}", json.dump());
            throw std::runtime_error("Invalid compile_commands.json format");
        }

        std::vector<CompileCommand> commands;

        for (const auto & item : json)
        {
            CompileCommand command;
            command.arguments = item["arguments"].get<std::vector<std::string>>();
            // Directory: require absolute, then weakly_canonical.
            command.directory = item["directory"].get<std::filesystem::path>();
            assert(command.directory.is_absolute());
            command.directory = std::filesystem::weakly_canonical(command.directory);
            // File: if relative, resolve against (already canonicalized) directory, then weakly_canonical.
            command.file = item["file"].get<std::filesystem::path>();
            if (!command.file.is_absolute())
            {
                command.file = command.directory / command.file; // prefix directory
            }
            command.file = std::filesystem::weakly_canonical(command.file);

            commands.push_back(command);
        }

        return commands;
    }

    static nlohmann::json compileCommandsToJson(const std::vector<CompileCommand> & commands)
    {
        nlohmann::json jsonCommands = nlohmann::json::array();
        for (const auto & command : commands)
        {
            jsonCommands.push_back(command);
        }
        return jsonCommands;
    }
};

} // namespace Hayroll

#endif // HAYROLL_COMPILECOMMAND_HPP
