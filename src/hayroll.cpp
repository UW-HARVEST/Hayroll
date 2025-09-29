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

// Helper: compute output path from a base CompileCommand, optional extension override,
// write the given content, and log a unified info message in the form:
//   "{step} for {fileName} [DefineSet {i}] saved to: {path}"
// The DefineSet suffix is printed only when defineSetIndex has a value.
std::filesystem::path saveOutput
(
    const Hayroll::CompileCommand & base,
    const std::filesystem::path & outputDir,
    const std::filesystem::path & projDir,
    const std::string & content,
    const std::optional<std::string> & newExt,
    const std::string & step,
    const std::string & fileName,
    const std::optional<std::size_t> defineSetIndex = std::nullopt
)
{
    Hayroll::CompileCommand outCmd = base.withUpdatedFilePathPrefix(outputDir, projDir);
    if (newExt)
    {
        outCmd = outCmd.withUpdatedFileExtension(*newExt);
    }
    const std::filesystem::path outPath = outCmd.file;
    const std::string fileNameRelative = std::filesystem::relative(base.file, projDir).string();
    Hayroll::saveStringToFile(content, outPath);
    if (defineSetIndex)
    {
        SPDLOG_INFO("{} for {} DefineSet {} saved to: {}", step, fileNameRelative, *defineSetIndex, outPath.string());
    }
    else
    {
        SPDLOG_INFO("{} for {} saved to: {}", step, fileNameRelative, outPath.string());
    }
    return outPath;
}

int main(const int argc, const char* argv[])
{
    using namespace Hayroll;
    using json = nlohmann::json;

    std::size_t hardwareThreads = std::thread::hardware_concurrency();
    if (hardwareThreads == 0) hardwareThreads = 2;
    if (hardwareThreads > 16) hardwareThreads = 16; // Limit to 16 threads to avoid memory thrashin

    // Default logging level (can be raised with -v / -vv)
    spdlog::set_level(spdlog::level::info);

    std::filesystem::path compileCommandsJsonPath;
    std::filesystem::path outputDir;
    std::size_t jobs = 0;
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
    std::size_t numTasks = compileCommands.size();

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

    if (jobs > numTasks) jobs = numTasks;
    SPDLOG_INFO("Using {} worker thread(s)", jobs);

    // Process tasks in parallel; skip failures and report at the end
    std::vector<std::pair<std::filesystem::path, std::string>> failedTasks; // Failed file -> error
    std::mutex failedMutex;

    std::atomic<std::size_t> nextIdx{0};

    auto worker = [&]()
    {
        while (true)
        {
            std::size_t taskIdx = nextIdx.fetch_add(1, std::memory_order_relaxed);
            if (taskIdx >= numTasks) break;
            const CompileCommand & command = compileCommands[taskIdx];
            std::filesystem::path srcPath = command.file;
            try
            {
                // Copy all source files to the output directory
                // compileCommands + src -> outputDir
                std::string srcStr = loadFileToString(srcPath);
                saveOutput(command, outputDir, projDir, srcStr, std::nullopt,
                            "Source file", srcPath.string());

                // Hayroll Pioneer symbolic execution
                // compileCommands + cpp2cStr --SymbolicExecutor-> includeTree + premiseTree
                SymbolicExecutor executor(srcPath, projDir, command.getIncludePaths());
                executor.run();
                PremiseTree * premiseTree = executor.scribe.borrowTree();
                saveOutput(command, outputDir, projDir, premiseTree->toString(),
                    ".premise_tree.raw.txt", "Raw premise tree", command.file.string());
                premiseTree->refine();
                saveOutput(command, outputDir, projDir, premiseTree->toString(),
                    ".premise_tree.txt", "Premise tree", command.file.string());

                // Splitter
                // premiseTree + compileCommands --Splitter-> DefineSets
                std::vector<DefineSet> defineSets = Splitter::run(premiseTree, command);
                {
                    saveOutput(command, outputDir, projDir, DefineSet::defineSetsToString(defineSets),
                        ".defset.txt", "DefineSets summary", command.file.string());

                    std::vector<CompileCommand> commandsWithDefSets;
                    for (std::size_t i = 0; i < defineSets.size(); ++i)
                    {
                        CompileCommand commandWithDefineSet = command
                            .withUpdatedDefineSet(defineSets[i]);
                        commandsWithDefSets.push_back(commandWithDefineSet);
                    }

                    // Run rewrite-includes + LineMatcher + CodeRangeAnalysisTasks generation + Maki cpp2c on each DefineSet
                    // This is because CU line numbers may differ with different DefineSets,
                    // which affects LineMatcher and CodeRangeAnalysisTasks generation.
                    std::vector<std::string> cuStrs;
                    std::vector<std::unordered_map<Hayroll::IncludeTreePtr, std::vector<int>>> lineMaps;
                    std::vector<std::vector<std::pair<Hayroll::IncludeTreePtr, int>>> inverseLineMaps;
                    std::vector<std::string> cpp2cStrs;
                    std::vector<std::vector<Hayroll::MakiInvocationSummary>> cpp2cInvocations;
                    std::vector<std::vector<Hayroll::MakiRangeSummary>> cpp2cRanges;
                    for (std::size_t i = 0; i < defineSets.size(); ++i)
                    {
                        const DefineSet & defSet = defineSets[i];
                        CompileCommand commandWithDefineSet = commandsWithDefSets[i];

                        std::string cuStr = RewriteIncludesWrapper::runRewriteIncludes(commandWithDefineSet);
                        cuStrs.push_back(cuStr);
                        // Save to filename.{i}.cu.c
                        saveOutput(commandWithDefineSet, outputDir, projDir, cuStr,
                            std::format(".{}.cu.c", i), "Compilation unit file", command.file.string(), i);

                        const auto [lineMap, inverseLineMap] = LineMatcher::run(cuStr, executor.includeTree, command.getIncludePaths());
                        lineMaps.push_back(lineMap);
                        inverseLineMaps.push_back(inverseLineMap);

                        std::vector<CodeRangeAnalysisTask> codeRangeAnalysisTasks = premiseTree->getCodeRangeAnalysisTasks(lineMap);
                        
                        std::string cpp2cStr = MakiWrapper::runCpp2cOnCu(commandWithDefineSet, codeRangeAnalysisTasks);
                        cpp2cStrs.push_back(cpp2cStr);
                        // Save to filename.{i}.cpp2c
                        saveOutput(commandWithDefineSet, outputDir, projDir, cpp2cStr,
                            std::format(".{}.cpp2c", i), "Maki cpp2c output", command.file.string(), i);

                        auto [invocations, ranges] = parseCpp2cSummary(cpp2cStr);
                        cpp2cInvocations.push_back(invocations);
                        cpp2cRanges.push_back(ranges);
                    }

                    // Compliment the ranges using each others' information
                    std::vector<std::vector<Hayroll::MakiRangeSummary>> cpp2cRangesCompleted
                        = Hayroll::MakiRangeSummary::complementRangeSummaries(cpp2cRanges, inverseLineMaps);
                    // Save complemented ranges
                    for (std::size_t i = 0; i < defineSets.size(); ++i)
                    {
                        saveOutput(command, outputDir, projDir,
                            json(cpp2cRangesCompleted[i]).dump(4),
                            std::format(".{}.cpp2c.ranges.json", i),
                            "Complemented Maki range summary", command.file.string(), i);
                    }

                    std::vector<std::string> cuSeededStrs;
                    std::vector<std::string> c2rustStrs;
                    std::vector<std::string> reaperStrs;
                    for (std::size_t i = 0; i < defineSets.size(); ++i)
                    {
                        // Hayroll Seeder
                        // cpp2cInvocations + cpp2cRangesCompleted + cuStrs + lineMap + inverseLineMap --Seeder-> seeded
                        std::string cuSeededStr = Seeder::run
                        (
                            cpp2cInvocations[i],
                            cpp2cRangesCompleted[i],
                            cuStrs[i],
                            lineMaps[i],
                            inverseLineMaps[i]
                        );
                        cuSeededStrs.push_back(cuSeededStr);
                        // Save to filename.{i}.seeded.cu.c
                        saveOutput(command, outputDir, projDir, cuSeededStr,
                            std::format(".{}.seeded.cu.c", i),
                            "Hayroll Seeded compilation unit", command.file.string(), i);

                        // c2rust -> .seeded.rs
                        std::string c2rustStr = C2RustWrapper::runC2Rust(cuSeededStr, commandsWithDefSets[i]);
                        c2rustStrs.push_back(c2rustStr);
                        // Save to filename.{i}.seeded.rs
                        saveOutput(command, outputDir, projDir, c2rustStr,
                            std::format(".{}.seeded.rs", i),
                            "C2Rust output", command.file.string(), i);

                        // Reaper -> .rs
                        std::string reaperStr = ReaperWrapper::runReaper(c2rustStr);
                        reaperStrs.push_back(reaperStr);
                        // Save to filename.{i}.rs
                        saveOutput(command, outputDir, projDir, reaperStr,
                            std::format(".{}.rs", i),
                            "Hayroll Reaper output", command.file.string(), i);

                        SPDLOG_INFO("Task {}/{} {} completed", taskIdx, numTasks, command.file.string());
                    }
                }
            }
            catch (const std::exception & e)
            {
                std::lock_guard<std::mutex> lock(failedMutex);
                failedTasks.emplace_back(command.file, e.what());
                SPDLOG_ERROR("Task {}/{} {} failed: {}", taskIdx, numTasks, command.file.string(), e.what());
            }
            catch (...)
            {
                std::lock_guard<std::mutex> lock(failedMutex);
                failedTasks.emplace_back(command.file, "unknown error");
                SPDLOG_ERROR("Task {}/{} {} failed: unknown error", taskIdx, numTasks, command.file.string());
            }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(jobs);
    for (std::size_t t = 0; t < jobs; ++t) threads.emplace_back(worker);
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
