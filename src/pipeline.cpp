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

int main(const int argc, const char* argv[])
{
    using namespace Hayroll;
    using json = nlohmann::json;

    spdlog::set_level(spdlog::level::debug);

    // Take two arguments
    // 1. Path to the compile_commands.json file.
    // 2. Output directory.
    if (argc != 3)
    {
        std::cerr << "Usage: " << argv[0] << " <path_to_compile_commands.json> <output_directory>" << std::endl;
        return 1;
    }

    std::filesystem::path compileCommandsJsonPath = argv[1];
    compileCommandsJsonPath = std::filesystem::canonical(compileCommandsJsonPath);
    std::filesystem::path outputDir = argv[2];
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

    // Analyze macro invocations using Maki
    // compileCommands + src --Maki-> cpp2cStr

    std::string cpp2cStr = MakiWrapper::runCpp2cOnCu(compileCommands);
    SPDLOG_INFO("Maki analysis completed.");
    SPDLOG_DEBUG("cpp2cStr:\n{}", cpp2cStr);

    // Aggregate sources into compilation units
    // compileCommands + src --clang-frewrite-includes-> cuStrs

    std::vector<std::string> cuStrs;
    for (const CompileCommand & command : compileCommands)
    {
        std::string cuStr = RewriteIncludesWrapper::runRewriteIncludes(command);
        cuStrs.push_back(cuStr);

        SPDLOG_INFO("Rewritten includes for {}", command.file.string());
        SPDLOG_DEBUG("Compilation unit source:\n{}", cuStr);
    }
    assert(cuStrs.size() == numTasks);

    // Symbolic execution
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
        spdlog::set_level(spdlog::level::info);
        executor.run();
        spdlog::set_level(spdlog::level::debug);
        // Results are in the executor's member variables
        SPDLOG_INFO("Symbolic execution completed for: {}", executor.srcPath.string());
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

        SPDLOG_INFO("Line mapping completed for {}", compileCommands[i].file.string());

        SPDLOG_DEBUG("Line map for task {}:", i);
        for (const auto & [includeTreePtr, lines] : lineMaps.back())
        {
            SPDLOG_DEBUG("IncludeTree: {}, lines: {}", includeTreePtr->path.string(), lines.size());
            for (size_t j = 0; j < lines.size(); ++j)
            {
                SPDLOG_DEBUG("  Line {}: {}", j, lines[j]);
                SPDLOG_DEBUG("  Inverse Line {}: {}", lines[j], j);
            }
        }
    }
    assert(lineMaps.size() == numTasks);
    assert(inverseLineMaps.size() == numTasks);

    // Seeder
    // compileCommands + cuStrs + includeTree + premiseTree --Seeder-> seed
    std::vector<std::string> cuSeededStrs;
    for (int i = 0; i < numTasks; ++i)
    {
        // Seeder::run(cpp2cStr, premiseTree, srcStr, lineMap, inverseLineMap);
        const std::string & cuStr = cuStrs[i];
        const auto & lineMap = lineMaps[i];
        const auto & inverseLineMap = inverseLineMaps[i];

        std::string cuSeededStr = Seeder::run(cpp2cStr, std::nullopt, cuStr, lineMap, inverseLineMap);
        cuSeededStrs.push_back(std::move(cuSeededStr));

        SPDLOG_INFO("Seeded source for task {}: {}", i, cuSeededStrs.back());
    }
    assert(cuSeededStrs.size() == numTasks);

    // Write seeded sources to output directory
    for (int i = 0; i < numTasks; ++i)
    {
        const CompileCommand & command = compileCommands[i];
        const std::string & cuSeededStr = cuSeededStrs[i];
        std::filesystem::path outputPath = outputDir / command.getFilePathRelativeToDirectory();
        saveStringToFile(cuSeededStr, outputPath);
        SPDLOG_INFO("Seeded source written to: {}", outputPath.string());
    }

    return 0;
}
