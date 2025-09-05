#include <iostream>
#include <filesystem>
#include <thread>
#include <mutex>
#include <atomic>
#include <optional>
#include <unordered_set>

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

    spdlog::set_level(spdlog::level::debug);

    std::filesystem::path compileCommandsJsonPath;
    std::filesystem::path outputDir;
    std::optional<size_t> optNumThreads; // optional -j
    std::optional<std::filesystem::path> optProjectDir; // optional -p / --project-dir

    if (argc >= 2 && std::string(argv[1]) == "transpile" && argc >= 5)
    {
        // c2rust-style input format
        if (!(std::string(argv[3]) == "-o" || std::string(argv[3]) == "--output-dir"))
        {
            std::cerr << "Usage: " << argv[0] << " transpile <path_to_folder_including_compile_commands.json> -o|--output-dir <output_directory> [-j|--jobs|--threads N]" << std::endl;
            return 1;
        }
        compileCommandsJsonPath = std::filesystem::path(argv[2]) / "compile_commands.json";
        outputDir = argv[4];

        // optional: -j N | --jobs N | --threads N | -p DIR | --project-dir DIR
        for (int i = 5; i + 1 < argc; )
        {
            std::string opt = argv[i];
            if (opt == "-j" || opt == "--jobs" || opt == "--threads")
            {
                try { optNumThreads = static_cast<size_t>(std::stoul(argv[i+1])); }
                catch (...) { std::cerr << "Invalid thread count: " << argv[i+1] << std::endl; return 1; }
                i += 2;
            }
            else if (opt == "-p" || opt == "--project-dir")
            {
                optProjectDir = std::filesystem::path(argv[i+1]);
                i += 2;
            }
            else
            {
                std::cerr << "Unknown option: " << opt << std::endl;
                std::cerr << "Usage: " << argv[0] << " transpile <path> -o|--output-dir <out> [-p|--project-dir DIR] [-j|--jobs|--threads N]" << std::endl;
                return 1;
            }
        }
    }
    else if (argc >= 3 && std::string(argv[1]) != "transpile")
    {
        // Original input format
        compileCommandsJsonPath = argv[1];
        outputDir = argv[2];

        // optional: -j N | --jobs N | --threads N | -p DIR | --project-dir DIR
        for (int i = 3; i + 1 < argc; )
        {
            std::string opt = argv[i];
            if (opt == "-j" || opt == "--jobs" || opt == "--threads")
            {
                try { optNumThreads = static_cast<size_t>(std::stoul(argv[i+1])); }
                catch (...) { std::cerr << "Invalid thread count: " << argv[i+1] << std::endl; return 1; }
                i += 2;
            }
            else if (opt == "-p" || opt == "--project-dir")
            {
                optProjectDir = std::filesystem::path(argv[i+1]);
                i += 2;
            }
            else
            {
                std::cerr << "Unknown option: " << opt << std::endl;
                std::cerr << "Usage: " << argv[0] << " <path_to_compile_commands.json> <output_directory> [-p|--project-dir DIR] [-j|--jobs|--threads N]" << std::endl;
                return 1;
            }
        }
    }
    else
    {
        std::cerr << "Usage: " << argv[0] << " <path_to_compile_commands.json> <output_directory> [-p|--project-dir DIR] [-j|--jobs|--threads N]" << std::endl;
        std::cerr << "   or: " << argv[0] << " transpile <path_to_folder_including_compile_commands.json> -o|--output-dir <output_directory> [-p|--project-dir DIR] [-j|--jobs|--threads N]" << std::endl;
        std::cerr << "Notes: If --project-dir is omitted, the project directory defaults to the folder containing compile_commands.json." << std::endl;
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

    // Determine project directory: user override or folder containing compile_commands.json
    std::filesystem::path projDir = optProjectDir.has_value()
        ? std::filesystem::canonical(optProjectDir.value())
        : compileCommandsJsonPath.parent_path();
    SPDLOG_INFO("Project directory resolved to: {} ({} mode)", projDir.string(), (optProjectDir.has_value() ? "user-specified" : "default-from-compile_commands.json"));

    // Warn (not fail) if compile command entries point to multiple directories.
    {
        std::unordered_set<std::string> dirs;
        for (const CompileCommand & c : compileCommands) dirs.insert(c.directory.string());
        if (dirs.size() > 1) {
            SPDLOG_WARN("compile_commands.json entries span across {} directories; using project dir: {}", dirs.size(), projDir.string());
        }
    }

    // Process tasks in parallel; skip failures and report at the end
    std::vector<std::pair<std::filesystem::path, std::string>> failedTasks; // Failed file -> error
    std::mutex failedMutex;

    size_t hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 2;
    if (hw > 16) hw = 16; // Limit to 16 threads to avoid memory thrashing
    size_t numThreads = optNumThreads.value_or(hw);
    if (numThreads == 0) numThreads = 1;
    if (numThreads > static_cast<size_t>(numTasks)) numThreads = static_cast<size_t>(numTasks);
    SPDLOG_INFO("Using {} worker thread(s)", numThreads);

    std::atomic<size_t> nextIdx{0};

    auto worker = [&]()
    {
        while (true)
        {
            size_t i = nextIdx.fetch_add(1, std::memory_order_relaxed);
            if (i >= static_cast<size_t>(numTasks)) break;
            const CompileCommand & command = compileCommands[i];
            std::filesystem::path srcPath = command.file;
            try
            {
                // Copy all source files to the output directory
                // compileCommands + src -> outputDir
                {
                    CompileCommand outputCommand = command.withUpdatedFilePathPrefix(outputDir, projDir);
                    std::filesystem::path outputPath = outputCommand.file;
                    std::string srcStr = loadFileToString(srcPath);
                    saveStringToFile(srcStr, outputPath);
                    SPDLOG_INFO("Source file {} copied to: {}", srcPath.string(), outputPath.string());
                }

                // Aggregate sources into compilation units
                // compileCommands + src --clang-frewrite-includes-> cuStrs
                std::string cuStr = RewriteIncludesWrapper::runRewriteIncludes(command);
                {
                    CompileCommand outputCommand = command
                        .withUpdatedFilePathPrefix(outputDir, projDir)
                        .withUpdatedFileExtension(".cu.c");
                    std::filesystem::path outputPath = outputCommand.file;
                    saveStringToFile(cuStr, outputPath);
                    SPDLOG_INFO("Compilation unit file for {} saved to: {}", command.file.string(), outputPath.string());
                }

                // Hayroll Pioneer symbolic execution
                // compileCommands + cpp2cStr --SymbolicExecutor-> includeTree + premiseTree
                SymbolicExecutor executor(srcPath, projDir, command.getIncludePaths());
                executor.run();
                SPDLOG_INFO("Hayroll Pioneer symbolic execution completed for: {}", executor.srcPath.string());
                PremiseTree * premiseTree = executor.scribe.borrowTree();
                premiseTree->refine();
                {
                    std::string premiseTreeStr = premiseTree->toString();
                    CompileCommand outputCommand = command
                        .withUpdatedFilePathPrefix(outputDir, projDir)
                        .withUpdatedFileExtension(".premise_tree.txt");
                    std::filesystem::path outputPath = outputCommand.file;
                    saveStringToFile(premiseTreeStr, outputPath);
                    SPDLOG_INFO("Premise tree for {} saved to: {}", command.file.string(), outputPath.string());
                }

                // LineMatcher
                // cuStr + includeTree + includePath --LineMatcher-> lineMap + inverseLineMap

                const auto [lineMap, inverseLineMap] = LineMatcher::run(
                    cuStr, executor.includeTree, command.getIncludePaths());
                SPDLOG_INFO("Hayroll Line mapping completed for {}", command.file.string());

                // CodeRangeAnalysisTasks
                // premiseTree + lineMap --> CodeRangeAnalysisTasks
                std::vector<CodeRangeAnalysisTask> tasks = premiseTree->getCodeRangeAnalysisTasks(lineMap);
                {
                    CompileCommand outputCommand = command
                        .withUpdatedFilePathPrefix(outputDir, projDir)
                        .withUpdatedFileExtension(".range_tasks.json");
                    std::filesystem::path outputPath = outputCommand.file;
                    saveStringToFile(json(tasks).dump(4), outputPath);
                    SPDLOG_INFO("Code range analysis tasks for {} saved to: {}", command.file.string(), outputPath.string());
                }

                // Analyze macro invocations using Maki
                // compileCommands + src --Maki-> cpp2cStr
                std::string cpp2cStr = MakiWrapper::runCpp2cOnCu(command, projDir, tasks);
                {
                    CompileCommand outputCommand = command
                        .withUpdatedFilePathPrefix(outputDir, projDir)
                        .withUpdatedFileExtension(".cpp2c");
                    std::filesystem::path outputPath = outputCommand.file;
                    saveStringToFile(cpp2cStr, outputPath);
                    SPDLOG_INFO("Maki cpp2c output for {} saved to: {}", command.file.string(), outputPath.string());
                }

                // Hayroll Seeder
                // compileCommands + cuStrs + includeTree + premiseTree --Seeder-> seededStr
                std::string cuSeededStr = Seeder::run(cpp2cStr, std::nullopt, cuStr, lineMap, inverseLineMap);
                {
                    CompileCommand outputCommand = command
                        .withUpdatedFilePathPrefix(outputDir, projDir)
                        .withUpdatedFileExtension(".seeded.cu.c");
                    std::filesystem::path outputPath = outputCommand.file;
                    saveStringToFile(cuSeededStr, outputPath);
                    SPDLOG_INFO("Hayroll Seeded compilation unit for {} saved to: {}", command.file.string(), outputPath.string());
                }

                // c2rust -> .seeded.rs
                std::string c2rustStr = C2RustWrapper::runC2Rust(cuSeededStr, command);
                {
                    CompileCommand outputCommand = command
                        .withUpdatedFilePathPrefix(outputDir, projDir)
                        .withUpdatedFileExtension(".seeded.rs");
                    std::filesystem::path outputPath = outputCommand.file;
                    saveStringToFile(c2rustStr, outputPath);
                    SPDLOG_INFO("C2Rust output for {} saved to: {}", command.file.string(), outputPath.string());
                }

                // Reaper -> .rs
                std::string reaperStr = ReaperWrapper::runReaper(c2rustStr);
                {
                    CompileCommand outputCommand = command
                        .withUpdatedFilePathPrefix(outputDir, projDir)
                        .withUpdatedFileExtension(".rs");
                    std::filesystem::path outputPath = outputCommand.file;
                    saveStringToFile(reaperStr, outputPath);
                    SPDLOG_INFO("Hayroll Reaper output for {} saved to: {}", command.file.string(), outputPath.string());
                }
            }
            catch (const std::exception & e)
            {
                std::lock_guard<std::mutex> lock(failedMutex);
                failedTasks.emplace_back(command.file, e.what());
                SPDLOG_ERROR("Task {} failed: {}", command.file.string(), e.what());
            }
            catch (...)
            {
                std::lock_guard<std::mutex> lock(failedMutex);
                failedTasks.emplace_back(command.file, "unknown error");
                SPDLOG_ERROR("Task {} failed: unknown error", command.file.string());
            }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(numThreads);
    for (size_t t = 0; t < numThreads; ++t) threads.emplace_back(worker);
    for (auto & th : threads) th.join();

    // Print final results
    if (!failedTasks.empty())
    {
        SPDLOG_ERROR("{} task(s) failed:", failedTasks.size());
        for (const auto & p : failedTasks)
        {
            SPDLOG_ERROR("  {} -> {}", p.first.string(), p.second);
        }
    }
    else
    {
        SPDLOG_INFO("Hayroll pipeline completed. See output directory: {}", outputDir.string());
    }

    return failedTasks.empty() ? 0 : 1;
}
