#include <iostream>
#include <filesystem>

#include <spdlog/spdlog.h>
#include "json.hpp"

#include "Util.hpp"
#include "TempDir.hpp"
#include "CompileCommand.hpp"
#include "MakiWrapper.hpp"
#include "RewriteIncludesWrapper.hpp"
#include "SymbolicExecutor.hpp"
#include "LineMatcher.hpp"
#include "Seeder.hpp"
#include "C2RustWrapper.hpp"
#include "ReaperWrapper.hpp"

int main(const int argc, const char* argv[])
{
    using namespace Hayroll;
    using json = nlohmann::json;

    spdlog::set_level(spdlog::level::info);

    std::filesystem::path compileCommandsJsonPath;
    std::filesystem::path outputDir;

    if (std::string(argv[1]) == "transpile" && argc == 5)
    {
        // c2rust-style input format
        compileCommandsJsonPath = std::filesystem::path(argv[2]) / "compile_commands.json";
        outputDir = argv[4];
    }
    else if (std::string(argv[1]) != "transpile" && argc == 3)
    {
        // Original input format
        compileCommandsJsonPath = argv[1];
        outputDir = argv[2];
    }
    else
    {
        std::cerr << "Usage: " << argv[0] << " <path_to_compile_commands.json> <output_directory>" << std::endl;
        std::cerr << "   or: " << argv[0] << " transpile <path_to_folder_including_compile_commands.json> -o|--output-dir <output_directory>" << std::endl;
        return 1;
    }

    compileCommandsJsonPath = std::filesystem::canonical(compileCommandsJsonPath);
    // Wipe the output directory if it exists
    if (std::filesystem::exists(outputDir))
    {
        std::filesystem::remove_all(outputDir);
    }
    std::filesystem::create_directories(outputDir);
    outputDir = std::filesystem::canonical(outputDir);

    // Load compile_commands.json
    // compileCommandJsonPath -> compileCommands

    std::string compileCommandsJsonStr = loadFileToString(compileCommandsJsonPath);
    json compileCommandsJson = json::parse(compileCommandsJsonStr);
    std::vector<CompileCommand> compileCommands = CompileCommand::fromCompileCommandsJson(compileCommandsJson);
    int numTasks = compileCommands.size();

    SPDLOG_INFO("Number of tasks: {}", numTasks);
    for (const CompileCommand & command : compileCommands)
    {
        SPDLOG_INFO(command.file.string());
    }

    // Assume that all compile commands are from the same project directory.
    std::filesystem::path projDir = compileCommands.front().directory;
    for (const CompileCommand & command : compileCommands)
    {
        if (command.directory != projDir)
        {
            std::cerr << "Error: All compile commands must be from the same project directory." << std::endl;
            return 1;
        }
    }

    // Copy all source files to the output directory
    // compileCommands + src -> outputDir

    for (const CompileCommand & command : compileCommands)
    {
        std::filesystem::path srcPath = command.file;
        CompileCommand outputCommand = command.withUpdatedDirectory(outputDir);
        std::filesystem::path outputPath = outputCommand.file;
        std::string srcStr = loadFileToString(srcPath);
        saveStringToFile(srcStr, outputPath);
        SPDLOG_INFO("Source file {} copied to: {}", srcPath.string(), outputPath.string());
    }

    // Analyze macro invocations using Maki
    // compileCommands + src --Maki-> cpp2cStr

    std::vector<std::string> cpp2cStrs;
    for (const CompileCommand & command : compileCommands)
    {
        std::string cpp2cStr = MakiWrapper::runCpp2cOnCu(command);
        CompileCommand outputCommand = command
            .withUpdatedDirectory(outputDir)
            .withUpdatedExtension(".cpp2c");
        std::filesystem::path outputPath = outputCommand.file;
        saveStringToFile(cpp2cStr, outputPath);
        SPDLOG_INFO("Maki cpp2c output for {} saved to: {}", command.file.string(), outputPath.string());
        cpp2cStrs.push_back(cpp2cStr);
    }
    assert(cpp2cStrs.size() == numTasks);

    // Aggregate sources into compilation units
    // compileCommands + src --clang-frewrite-includes-> cuStrs

    std::vector<std::string> cuStrs;
    for (const CompileCommand & command : compileCommands)
    {
        std::string cuStr = RewriteIncludesWrapper::runRewriteIncludes(command);
        cuStrs.push_back(cuStr);

        CompileCommand outputCommand = command
            .withUpdatedDirectory(outputDir)
            .withUpdatedExtension(".cu.c");
        std::filesystem::path outputPath = outputCommand.file;
        saveStringToFile(cuStr, outputPath);
        SPDLOG_INFO("Compilation unit file for {} saved to: {}", command.file.string(), outputPath.string());
    }
    assert(cuStrs.size() == numTasks);

    // Hayroll Pioneer symbolic execution
    // compileCommands + cpp2cStr --SymbolicExecutor-> includeTree + premiseTree

    std::vector<SymbolicExecutor> symbolicExecutors;
    for (const CompileCommand & command : compileCommands)
    {
        std::filesystem::path srcPath = command.file;
        SymbolicExecutor executor(srcPath, projDir, command.getIncludePaths());
        symbolicExecutors.push_back(std::move(executor));
    }
    for (SymbolicExecutor & executor : symbolicExecutors)
    {
        executor.run();
        // Results are in the executor's member variables
        SPDLOG_INFO("Hayroll Pioneer symbolic execution completed for: {}", executor.srcPath.string());
    }
    assert(symbolicExecutors.size() == numTasks);

    // LineMatcher
    // cuStrs + includeTrees + includePaths --LineMatcher-> lineMaps + inverseLineMaps

    std::vector<std::unordered_map<Hayroll::IncludeTreePtr, std::vector<int>>> lineMaps;
    std::vector<std::vector<std::pair<Hayroll::IncludeTreePtr, int>>> inverseLineMaps;
    for (int i = 0; i < numTasks; ++i)
    {
        const std::string & cuStr = cuStrs[i];
        const IncludeTreePtr & includeTree = symbolicExecutors[i].includeTree;
        const std::vector<std::filesystem::path> & includePaths = compileCommands[i].getIncludePaths();

        const auto & [lineMap, inverseLineMap] = LineMatcher::run(cuStr, includeTree, includePaths);
        lineMaps.push_back(std::move(lineMap));
        inverseLineMaps.push_back(std::move(inverseLineMap));

        SPDLOG_INFO("Hayroll Line mapping completed for {}", compileCommands[i].file.string());
    }
    assert(lineMaps.size() == numTasks);
    assert(inverseLineMaps.size() == numTasks);

    // Hayroll Seeder
    // compileCommands + cuStrs + includeTree + premiseTree --Seeder-> seed
    std::vector<std::string> cuSeededStrs;
    for (int i = 0; i < numTasks; ++i)
    {
        const CompileCommand & command = compileCommands[i];
        const std::string & cpp2cStr = cpp2cStrs[i];
        const std::string & cuStr = cuStrs[i];
        const auto & lineMap = lineMaps[i];
        const auto & inverseLineMap = inverseLineMaps[i];

        std::string cuSeededStr = Seeder::run(cpp2cStr, std::nullopt, cuStr, lineMap, inverseLineMap);
        cuSeededStrs.push_back(cuSeededStr);

        // Save the seeded source to a file
        CompileCommand outputCommand = command
            .withUpdatedDirectory(outputDir)
            .withUpdatedExtension(".seeded.cu.c");
        std::filesystem::path outputPath = outputCommand.file;
        saveStringToFile(cuSeededStr, outputPath);
        SPDLOG_INFO("Hayroll Seeded compilation unit for {} saved to: {}", command.file.string(), outputPath.string());
    }
    assert(cuSeededStrs.size() == numTasks);

    // c2rust
    std::vector<std::string> c2rustStrs;
    for (int i = 0; i < numTasks; ++i)
    {
        const CompileCommand & command = compileCommands[i];
        const std::string & cuSeededStr = cuSeededStrs[i];
        std::string c2rustStr = C2RustWrapper::runC2Rust(cuSeededStr, command);
        c2rustStrs.push_back(c2rustStr);

        // Save the C2Rust output to a file
        CompileCommand outputCommand = command
            .withUpdatedDirectory(outputDir)
            .withUpdatedExtension(".seeded.rs");
        std::filesystem::path outputPath = outputCommand.file;
        saveStringToFile(c2rustStr, outputPath);
        SPDLOG_INFO("C2Rust output for {} saved to: {}", command.file.string(), outputPath.string());
    }
    assert(c2rustStrs.size() == numTasks);

    // Hayroll Reaper
    std::vector<std::string> reaperStrs;
    for (int i = 0; i < numTasks; ++i)
    {
        const CompileCommand & command = compileCommands[i];
        const std::string & c2rustStr = c2rustStrs[i];
        std::string reaperStr = ReaperWrapper::runReaper(c2rustStr);
        reaperStrs.push_back(reaperStr);

        // Save the Reaper output to a file
        CompileCommand outputCommand = command
            .withUpdatedDirectory(outputDir)
            .withUpdatedExtension(".rs");
        std::filesystem::path outputPath = outputCommand.file;
        saveStringToFile(reaperStr, outputPath);
        SPDLOG_INFO("hayroll Reaper output for {} saved to: {}", command.file.string(), outputPath.string());
    }
    assert(reaperStrs.size() == numTasks);

    // Print final results
    SPDLOG_INFO("Hayroll Pipeline completed. See output directory: {}", outputDir.string());

    return 0;
}
