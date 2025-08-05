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

    std::filesystem::path getFilePathRelativeToDirectory() const
    {
        if (file.is_absolute())
        {
            return std::filesystem::relative(file, directory);
        }
        return file; // If it's not absolute, just return it as is.
    }

    CompileCommand withUpdatedDirectory(const std::filesystem::path & newDirectory) const
    {
        CompileCommand updatedCommand = *this;
        std::filesystem::path relativeFile = std::filesystem::relative(this->file, this->directory);
        updatedCommand.file = newDirectory / relativeFile;
        updatedCommand.directory = newDirectory;
        return updatedCommand;
    }

    // Update the file extension of the command's file and arguments.
    // Multiple extensions are considered the extension of the file.
    // e.g. in "xxx.cu.c" the extension is ".cu.c".
    CompileCommand withUpdatedExtension
    (
        const std::string & newExtension
    ) const
    {
        CompileCommand updatedCommand = *this;
        std::string filename = file.filename().string();
        std::string newFilename = filename.substr(0, filename.find('.')) + newExtension;
        updatedCommand.file = updatedCommand.file.replace_filename(newFilename);
        // If the last argument is also a file path, update it as well.
        // Do filtering on whether the last argument is a file path.
        std::string lastArg = updatedCommand.arguments.back();
        try
        {
            std::filesystem::path lastArgPath(lastArg);
            std::string lastArgFilename = lastArgPath.filename().string();
            if (lastArgFilename == filename)
            {
                lastArgPath = lastArgPath.replace_filename(newFilename);
                updatedCommand.arguments.back() = lastArgPath.string();
            }
        }
        catch (const std::exception & e)
        {
            SPDLOG_DEBUG("Last argument is not a valid path: {}", e.what());
        }
        return updatedCommand;
    }

    // Update the file using absolute path.
    // This does not just change the filename, but also ignores the original file path.
    CompileCommand withUpdatedFile
    (
        std::filesystem::path newFile
    ) const
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
            SPDLOG_DEBUG("Last argument is not a valid path: {}", e.what());
        }
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
            command.directory = item["directory"].get<std::filesystem::path>();
            command.directory = std::filesystem::weakly_canonical(command.directory);
            command.file = item["file"].get<std::filesystem::path>();
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
