#include <iostream>
#include <filesystem>
#include <thread>
#include <mutex>
#include <atomic>
#include <optional>
#include <unordered_set>

#include <spdlog/spdlog.h>
#include "json.hpp"
#include "CLI11.hpp"

#include "Util.hpp"
#include "TempDir.hpp"
#include "CompileCommand.hpp"
#include "MakiWrapper.hpp"
#include "RewriteIncludesWrapper.hpp"
#include "SymbolicExecutor.hpp"
#include "Splitter.hpp"
#include "LineMatcher.hpp"
#include "Seeder.hpp"
#include "C2RustWrapper.hpp"
#include "ReaperWrapper.hpp"

int main(const int argc, const char* argv[])
{
    using namespace Hayroll;
    using json = nlohmann::json;

    size_t hardwareThreads = std::thread::hardware_concurrency();
    if (hardwareThreads == 0) hardwareThreads = 2;
    if (hardwareThreads > 16) hardwareThreads = 16; // Limit to 16 threads to avoid memory thrashin

    // Default logging level (can be raised with -v / -vv)
    spdlog::set_level(spdlog::level::info);

    std::filesystem::path compileCommandsJsonPath;
    std::filesystem::path outputDir;
    size_t jobs = 0;
    std::filesystem::path projDir;
    int verbose = 0;

    try
    {
        CLI::App app
        {
            "Hayroll pipeline (supports C2Rust compatibility mode with the 'transpile' subcommand)\n"
            "Patterns:\n 1) hayroll <compile_commands.json> <output_dir> [opts]\n 2) hayroll transpile <input_folder> -o <output_dir> [opts]"
        };
        app.set_help_flag("-h,--help", "Show help");

        // Shared options

        app.add_option("-p,--project-dir", projDir,
            "Project directory (defaults to folder containing compile_commands.json)")
            ->default_str("");
        app.add_option("-j,--jobs", jobs, "Worker threads")
            ->default_val(hardwareThreads);
        app.add_flag("-v,--verbose", verbose,
            "Increase verbosity (-v=debug, -vv=trace)")
            ->default_val(0);

        // Main (default) pattern positionals
        app.add_option("compile_commands", compileCommandsJsonPath, "Path to compile_commands.json");
        app.add_option("output_dir", outputDir, "Output directory");

        // Subcommand: transpile
        std::filesystem::path transpileInputFolder;
        CLI::App * subTranspile = app.add_subcommand("transpile", "C2Rust compatibility mode (expects <input_folder> and -o)");
        subTranspile->add_option("input_folder", transpileInputFolder, "Input folder containing compile_commands.json")
            ->required()
            ->check(CLI::ExistingDirectory);
        subTranspile->add_option("-o,--output-dir", outputDir,
            "Output directory")
            ->required();

        app.require_subcommand(0, 1);

        try
        {
            app.parse(argc, argv);
        }
        catch (const CLI::Error & e)
        {
            return app.exit(e);
        }

        if (subTranspile->parsed())
        {
            std::filesystem::path inputFolder = transpileInputFolder;
            compileCommandsJsonPath = inputFolder / "compile_commands.json";
            outputDir = outputDir;
        }
        else
        {
            // Normal mode requires two positionals
            if (compileCommandsJsonPath.empty() || outputDir.empty())
            {
                std::cerr << "Error: expected <compile_commands.json> <output_dir>.\n" << app.help() << std::endl;
                return 1;
            }
        }

        // Apply verbosity
        switch (verbose)
        {
            case 1: spdlog::set_level(spdlog::level::debug); break;
            case 2: spdlog::set_level(spdlog::level::trace); break;
            default: spdlog::set_level(spdlog::level::info); break;
        }

        compileCommandsJsonPath = std::filesystem::canonical(compileCommandsJsonPath);
        // Wipe the output directory if it exists
        if (std::filesystem::exists(outputDir))
        {
            std::filesystem::remove_all(outputDir);
        }
        std::filesystem::create_directories(outputDir);
        outputDir = std::filesystem::canonical(outputDir);

        if (!projDir.empty())
        {
            projDir = std::filesystem::canonical(projDir);
        }
        else
        {
            projDir = std::filesystem::canonical(compileCommandsJsonPath.parent_path());
            SPDLOG_INFO("Project directory not given, defaulting to: {}", projDir.string());
        }
    }
    catch (const std::exception & e)
    {
        std::cerr << "Argument parse error: " << e.what() << std::endl;
        return 1;
    }

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

    // Warn (not fail) if compile command entries point to multiple directories.
    {
        std::unordered_set<std::string> dirs;
        for (const CompileCommand & c : compileCommands) dirs.insert(c.directory.string());
        if (dirs.size() > 1) {
            SPDLOG_WARN("compile_commands.json entries span across {} directories; using project dir: {}", dirs.size(), projDir.string());
        }
    }

    if (jobs > static_cast<size_t>(numTasks)) jobs = static_cast<size_t>(numTasks);
    SPDLOG_INFO("Using {} worker thread(s)", jobs);

    // Process tasks in parallel; skip failures and report at the end
    std::vector<std::pair<std::filesystem::path, std::string>> failedTasks; // Failed file -> error
    std::mutex failedMutex;

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

                // Experimental
                // Splitter
                // premiseTree + compileCommands --Splitter-> DefineSets
                std::vector<DefineSet> defineSets = Splitter::run(premiseTree, command);
                {
                    CompileCommand outputCommand = command
                        .withUpdatedFilePathPrefix(outputDir, projDir)
                        .withUpdatedFileExtension(".defset.txt");
                    std::filesystem::path outputPath = outputCommand.file;
                    std::ostringstream oss;
                    if (defineSets.empty())
                    {
                        oss << "// No DefineSets generated\n";
                    }
                    else
                    {
                        for (size_t i = 0; i < defineSets.size(); ++i)
                        {
                            oss << "// DefineSet " << i << "\n";
                            oss << defineSets[i].toString() << "\n";
                        }
                    }
                    saveStringToFile(oss.str(), outputPath);
                    SPDLOG_INFO("{} DefineSet(s) for {} saved to single file: {}", defineSets.size(), command.file.string(), outputPath.string());


                    // Run rewrite-includes + LineMatcher + CodeRangeAnalysisTasks generation + Maki cpp2c on each DefineSet
                    // This is because CU line numbers may differ with different DefineSets,
                    // which affects LineMatcher and CodeRangeAnalysisTasks generation.
                    std::vector<std::string> cuStrs;
                    std::vector<std::unordered_map<Hayroll::IncludeTreePtr, std::vector<int>>> lineMaps;
                    std::vector<std::vector<std::pair<Hayroll::IncludeTreePtr, int>>> inverseLineMaps;
                    std::vector<std::string> cpp2cStrs;
                    std::vector<std::vector<Hayroll::MakiInvocationSummary>> cpp2cInvocations;
                    std::vector<std::vector<Hayroll::MakiRangeSummary>> cpp2cRanges;
                    for (size_t i = 0; i < defineSets.size(); ++i)
                    {
                        const DefineSet & defSet = defineSets[i];
                        CompileCommand commandWithDefineSet = command
                            .withUpdatedDefineSet(defSet);

                        std::string cuStr = RewriteIncludesWrapper::runRewriteIncludes(commandWithDefineSet);
                        cuStrs.push_back(cuStr);
                        // Save to filename.{i}.cu.c
                        {
                            CompileCommand outputCommand = commandWithDefineSet
                                .withUpdatedFilePathPrefix(outputDir, projDir)
                                .withUpdatedFileExtension(std::format(".{}.cu.c", i));
                            std::filesystem::path outputPath = outputCommand.file;
                            saveStringToFile(cuStr, outputPath);
                            SPDLOG_INFO("Compilation unit file for DefineSet {} of {} saved to: {}", i, command.file.string(), outputPath.string());
                        }

                        const auto [lineMap, inverseLineMap] = LineMatcher::run(cuStr, executor.includeTree, command.getIncludePaths());
                        lineMaps.push_back(lineMap);
                        inverseLineMaps.push_back(inverseLineMap);

                        std::vector<CodeRangeAnalysisTask> codeRangeAnalysisTasks = premiseTree->getCodeRangeAnalysisTasks(lineMap);
                        
                        std::string cpp2cStr = MakiWrapper::runCpp2cOnCu(commandWithDefineSet, codeRangeAnalysisTasks);
                        cpp2cStrs.push_back(cpp2cStr);
                        // Save to filename.{i}.cpp2c
                        {
                            CompileCommand outputCommand = commandWithDefineSet
                            .withUpdatedFilePathPrefix(outputDir, projDir)
                            .withUpdatedFileExtension(std::format(".{}.cpp2c", i));
                            std::filesystem::path outputPath = outputCommand.file;
                            saveStringToFile(cpp2cStr, outputPath);
                            SPDLOG_INFO("Maki cpp2c output for DefineSet {} of {} saved to: {}", i, command.file.string(), outputPath.string());
                        }

                        auto [invocations, ranges] = parseCpp2cSummary(cpp2cStr);
                        cpp2cInvocations.push_back(invocations);
                        cpp2cRanges.push_back(ranges);
                    }

                    // Compliment the ranges using each others' information
                    std::vector<std::vector<Hayroll::MakiRangeSummary>> complementedMakiRangeSummaries
                        = Hayroll::MakiRangeSummary::complementRangeSummaries(cpp2cRanges, inverseLineMaps);
                    // Save complemented ranges
                    for (size_t i = 0; i < defineSets.size(); ++i)
                    {
                        CompileCommand outputCommand = command
                            .withUpdatedFilePathPrefix(outputDir, projDir)
                            .withUpdatedFileExtension(std::format(".{}.cpp2c.ranges.json", i));
                        std::filesystem::path outputPath = outputCommand.file;
                        saveStringToFile(json(complementedMakiRangeSummaries[i]).dump(4), outputPath);
                        SPDLOG_INFO("Complemented Maki range summary for DefineSet {} of {} saved to: {}", i, command.file.string(), outputPath.string());
                    }
                }

                // LineMatcher
                // cuStr + includeTree + includePath --LineMatcher-> lineMap + inverseLineMap

                const auto [lineMap, inverseLineMap] = LineMatcher::run(
                    cuStr, executor.includeTree, command.getIncludePaths());
                SPDLOG_INFO("Hayroll Line mapping completed for {}", command.file.string());

                // CodeRangeAnalysisTasks
                // premiseTree + lineMap --> CodeRangeAnalysisTasks
                std::vector<CodeRangeAnalysisTask> codeRangeAnalysisTasks = premiseTree->getCodeRangeAnalysisTasks(lineMap);
                {
                    CompileCommand outputCommand = command
                        .withUpdatedFilePathPrefix(outputDir, projDir)
                        .withUpdatedFileExtension(".range_tasks.json");
                    std::filesystem::path outputPath = outputCommand.file;
                    saveStringToFile(json(codeRangeAnalysisTasks).dump(4), outputPath);
                    SPDLOG_INFO("Code range analysis tasks for {} saved to: {}", command.file.string(), outputPath.string());
                }

                // Analyze macro invocations using Maki
                // compileCommands + src --Maki-> cpp2cStr
                std::string cpp2cStr = MakiWrapper::runCpp2cOnCu(command, codeRangeAnalysisTasks);
                {
                    CompileCommand outputCommand = command
                        .withUpdatedFilePathPrefix(outputDir, projDir)
                        .withUpdatedFileExtension(".cpp2c");
                    std::filesystem::path outputPath = outputCommand.file;
                    saveStringToFile(cpp2cStr, outputPath);
                    SPDLOG_INFO("Maki cpp2c output for {} saved to: {}", command.file.string(), outputPath.string());
                }
                auto [cpp2cInvocations, cpp2cRanges] = parseCpp2cSummary(cpp2cStr);

                // Hayroll Seeder
                // compileCommands + cuStrs + includeTree + premiseTree --Seeder-> seededStr
                std::string cuSeededStr = Seeder::run(cpp2cInvocations, cpp2cRanges, cuStr, lineMap, inverseLineMap);
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
    threads.reserve(jobs);
    for (size_t t = 0; t < jobs; ++t) threads.emplace_back(worker);
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
