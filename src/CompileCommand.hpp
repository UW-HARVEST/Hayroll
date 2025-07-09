#ifndef HAYROLL_COMPILECOMMAND_HPP
#define HAYROLL_COMPILECOMMAND_HPP

#include <string>
#include <vector>
#include <filesystem>

#include <spdlog/spdlog.h>
#include "subprocess.hpp"
#include "json.hpp"

#include "Util.hpp"
#include "TempDir.hpp"

namespace Hayroll
{

// A representation of an item in compile_commands.json
struct CompileCommand
{
    std::vector<std::string> arguments;
    std::filesystem::path directory;
    std::filesystem::path file;
    std::filesystem::path output;

    nlohmann::json toJson() const
    {
        nlohmann::json jsonCommand;
        jsonCommand["arguments"] = arguments;
        jsonCommand["directory"] = directory.string();
        jsonCommand["file"] = file.string();
        jsonCommand["output"] = output.string();
        return jsonCommand;
    }

    std::vector<std::filesystem::path> getIncludePaths() const
    {
        std::vector<std::filesystem::path> paths;
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

    CompileCommand substituteSourceFilename(const std::filesystem::path & newSourceFile) const
    {
        CompileCommand newCommand = *this;
        newCommand.file = newSourceFile;
        newCommand.output = newSourceFile.parent_path() / "build" / newSourceFile.filename();
        return newCommand;
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
            command.directory = item["directory"].get<std::filesystem::path>();
            command.file = item["file"].get<std::filesystem::path>();
            command.output = item["output"].get<std::filesystem::path>();
            if (!command.directory.is_absolute())
            {
                SPDLOG_ERROR("Expected absolute path for directory in compile_commands.json, but got: {}", command.directory.string());
                throw std::runtime_error("Invalid directory path in compile_commands.json");
            }
            if (!command.file.is_absolute())
            {
                SPDLOG_ERROR("Expected absolute path for file in compile_commands.json, but got: {}", command.file.string());
                throw std::runtime_error("Invalid file path in compile_commands.json");
            }
            if (!command.output.is_absolute())
            {
                SPDLOG_ERROR("Expected absolute path for output in compile_commands.json, but got: {}", command.output.string());
                throw std::runtime_error("Invalid output path in compile_commands.json");
            }
            commands.push_back(command);
        }

        return commands;
    }

    static nlohmann::json compileCommandsToJson(const std::vector<CompileCommand> & commands)
    {
        nlohmann::json jsonCommands = nlohmann::json::array();
        for (const auto & command : commands)
        {
            jsonCommands.push_back(command.toJson());
        }
        return jsonCommands;
    }
};

} // namespace Hayroll

#endif // HAYROLL_COMPILECOMMAND_HPP
