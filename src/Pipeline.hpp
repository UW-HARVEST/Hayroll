#ifndef HAYROLL_PIPELINE_HPP
#define HAYROLL_PIPELINE_HPP

#include <atomic>
#include <filesystem>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>
#include "json.hpp"

#include "CompileCommand.hpp"
#include "Util.hpp"
#include "TempDir.hpp"
#include "MakiWrapper.hpp"
#include "MakiSummary.hpp"
#include "RewriteIncludesWrapper.hpp"
#include "SymbolicExecutor.hpp"
#include "Splitter.hpp"
#include "LineMatcher.hpp"
#include "Seeder.hpp"
#include "C2RustWrapper.hpp"
#include "ReaperWrapper.hpp"
#include "MergerWrapper.hpp"
#include "CleanerWrapper.hpp"

namespace Hayroll
{

class Pipeline
{
    using json = nlohmann::json;

public:
    static std::filesystem::path saveOutput
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
        Hayroll::CompileCommand outCmd = base.withUpdatedFilePathPrefix(outputDir / "src", projDir);
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

    static int run
    (
        const std::filesystem::path & compileCommandsJsonPath,
        const std::filesystem::path & outputDir,
        const std::filesystem::path & projDir,
        std::size_t jobs
    )
    {
        // Load compile_commands.json
        const std::string compileCommandsJsonStr = loadFileToString(compileCommandsJsonPath);
        json compileCommandsJson = json::parse(compileCommandsJsonStr);
        std::vector<CompileCommand> compileCommands = CompileCommand::fromCompileCommandsJson(compileCommandsJson);
        const std::size_t numTasks = compileCommands.size();

        SPDLOG_INFO("Number of tasks: {}", numTasks);
        for (const CompileCommand & command : compileCommands)
        {
            SPDLOG_INFO(command.file.string());
        }

        // Warn (not fail) if compile command entries point to multiple directories.
        {
            std::unordered_set<std::string> dirs;
            for (const CompileCommand & c : compileCommands) dirs.insert(c.directory.string());
            if (dirs.size() > 1)
            {
                SPDLOG_WARN(
                    "compile_commands.json entries span across {} directories; using project dir: {}",
                    dirs.size(),
                    projDir.string()
                );
            }
        }

        if (jobs > numTasks) jobs = numTasks;
        SPDLOG_INFO("Using {} worker thread(s)", jobs);

        // Process tasks in parallel; skip failures and report at the end
        std::vector<std::pair<std::filesystem::path, std::string>> failedTasks; // Failed file -> error
        std::mutex failedMutex;

        // Collect Cargo.toml from all subtasks and splits
        std::vector<std::string> allCargoTomls;
        std::set<std::string> allRustFeatureAtoms;
        std::mutex collectionMutex;

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
                    saveOutput
                    (
                        command,
                        outputDir,
                        projDir,
                        srcStr,
                        std::nullopt,
                        "Source file",
                        srcPath.string(),
                        std::nullopt
                    );

                    // Hayroll Pioneer symbolic execution
                    // compileCommands + cpp2cStr --SymbolicExecutor-> includeTree + premiseTree
                    SymbolicExecutor executor(srcPath, projDir, command.getIncludePaths());
                    executor.run();
                    PremiseTree * premiseTree = executor.scribe.borrowTree();
                    saveOutput
                    (
                        command,
                        outputDir,
                        projDir,
                        premiseTree->toString(),
                        ".premise_tree.raw.txt",
                        "Raw premise tree",
                        command.file.string(),
                        std::nullopt
                    );
                    premiseTree->refine();
                    saveOutput
                    (
                        command,
                        outputDir,
                        projDir,
                        premiseTree->toString(),
                        ".premise_tree.txt",
                        "Premise tree",
                        command.file.string(),
                        std::nullopt
                    );

                    // Splitter
                    std::vector<DefineSet> defineSets = Splitter::run(premiseTree, command);
                    saveOutput
                    (
                        command,
                        outputDir,
                        projDir,
                        DefineSet::defineSetsToString(defineSets),
                        ".defset.txt",
                        "DefineSets summary",
                        command.file.string(),
                        std::nullopt
                    );

                    std::vector<CompileCommand> commandsWithDefSets;
                    for (std::size_t i = 0; i < defineSets.size(); ++i)
                    {
                        CompileCommand commandWithDefineSet = command.withUpdatedDefineSet(defineSets[i]);
                        commandsWithDefSets.push_back(commandWithDefineSet);
                    }

                    // Run rewrite-includes + LineMatcher + CodeRangeAnalysisTasks generation + Maki cpp2c on each DefineSet
                    std::vector<std::string> cuStrs;
                    std::vector<std::unordered_map<Hayroll::IncludeTreePtr, std::vector<int>>> lineMaps;
                    std::vector<std::vector<std::pair<Hayroll::IncludeTreePtr, int>>> inverseLineMaps;
                    std::vector<std::string> cpp2cStrs;
                    std::vector<std::vector<Hayroll::MakiInvocationSummary>> cpp2cInvocations;
                    std::vector<std::vector<Hayroll::MakiRangeSummary>> cpp2cRanges;
                    std::set<std::string> rustFeatureAtoms;
                    for (std::size_t i = 0; i < defineSets.size(); ++i)
                    {
                        const DefineSet & defSet = defineSets[i];
                        CompileCommand commandWithDefineSet = commandsWithDefSets[i];

                        std::string cuStr = RewriteIncludesWrapper::runRewriteIncludes(commandWithDefineSet);
                        cuStrs.push_back(cuStr);
                        // Save to filename.{i}.cu.c
                        saveOutput
                        (
                            commandWithDefineSet,
                            outputDir,
                            projDir,
                            cuStr,
                            std::format(".{}.cu.c", i),
                            "Compilation unit file",
                            command.file.string(),
                            i
                        );

                        const auto [lineMap, inverseLineMap] = LineMatcher::run
                        (
                            cuStr,
                            executor.includeTree,
                            command.getIncludePaths()
                        );
                        lineMaps.push_back(lineMap);
                        inverseLineMaps.push_back(inverseLineMap);

                        auto [codeRangeAnalysisTasks, defSetRustFeatureAtoms] = premiseTree->getCodeRangeAnalysisTasksAndRustFeatureAtoms(lineMap);
                        rustFeatureAtoms.insert(defSetRustFeatureAtoms.begin(), defSetRustFeatureAtoms.end());

                        std::string cpp2cStr = MakiWrapper::runCpp2cOnCu(commandWithDefineSet, codeRangeAnalysisTasks);
                        cpp2cStrs.push_back(cpp2cStr);
                        // Save to filename.{i}.cpp2c
                        saveOutput
                        (
                            commandWithDefineSet,
                            outputDir,
                            projDir,
                            cpp2cStr,
                            std::format(".{}.cpp2c", i),
                            "Maki cpp2c output",
                            command.file.string(),
                            i
                        );

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
                        saveOutput
                        (
                            command,
                            outputDir,
                            projDir,
                            json(cpp2cRangesCompleted[i]).dump(4),
                            std::format(".{}.cpp2c.ranges.json", i),
                            "Complemented Maki range summary",
                            command.file.string(),
                            i
                        );
                    }

                    std::vector<std::string> cuSeededStrs;
                    std::vector<std::string> c2rustStrs;
                    std::vector<std::string> cargoTomls;
                    std::vector<std::string> reapedStrs;
                    for (std::size_t i = 0; i < defineSets.size(); ++i)
                    {
                        // Hayroll Seeder
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
                        saveOutput
                        (
                            command,
                            outputDir,
                            projDir,
                            cuSeededStr,
                            std::format(".{}.seeded.cu.c", i),
                            "Hayroll Seeded compilation unit",
                            command.file.string(),
                            i
                        );

                        // c2rust -> .seeded.rs
                        auto [c2rustStr, cargoToml] = C2RustWrapper::transpile(cuSeededStr, commandsWithDefSets[i]);
                        c2rustStrs.push_back(c2rustStr);
                        cargoTomls.push_back(cargoToml);
                        // Save to filename.{i}.seeded.rs
                        saveOutput
                        (
                            command,
                            outputDir,
                            projDir,
                            c2rustStr,
                            std::format(".{}.seeded.rs", i),
                            "C2Rust output",
                            command.file.string(),
                            i
                        );
                        // Also save Cargo.toml
                        saveOutput
                        (
                            command,
                            outputDir,
                            projDir,
                            cargoToml,
                            std::format(".{}.Cargo.toml", i),
                            "C2Rust Cargo.toml",
                            command.file.string(),
                            i
                        );

                        // Reaper -> .rs (takes Rust content, not a path)
                        std::string reaperStr = ReaperWrapper::runReaper(c2rustStr);
                        reapedStrs.push_back(reaperStr);
                        saveOutput
                        (
                            command,
                            outputDir,
                            projDir,
                            reaperStr,
                            std::format(".{}.reaped.rs", i),
                            "Hayroll Reaper output",
                            command.file.string(),
                            i
                        );
                    }

                    // Append this task's Cargo.toml list to the global collection (thread-safe)
                    {
                        std::lock_guard<std::mutex> lk(collectionMutex);
                        allCargoTomls.insert(allCargoTomls.end(), cargoTomls.begin(), cargoTomls.end());
                        allRustFeatureAtoms.insert(rustFeatureAtoms.begin(), rustFeatureAtoms.end());
                    }

                    // If multiple DefineSets, run Merger accumulatively
                    std::vector<std::string> mergedRustStrs;
                    mergedRustStrs.push_back(reapedStrs[0]); // The first one is not merged
                    for (std::size_t i = 1; i < defineSets.size(); ++i)
                    {
                        std::string merged = MergerWrapper::runMerger(mergedRustStrs[i - 1], reapedStrs[i]);
                        mergedRustStrs.push_back(merged);
                        saveOutput
                        (
                            command,
                            outputDir,
                            projDir,
                            merged,
                            std::format(".{}.merged.rs", i),
                            "Hayroll Merger output",
                            command.file.string(),
                            i
                        );
                    }

                    std::string finalRustStr = mergedRustStrs.back();
                    finalRustStr = CleanerWrapper::runCleaner(finalRustStr);
                    saveOutput
                    (
                        command,
                        outputDir,
                        projDir,
                        finalRustStr,
                        ".rs",
                        "Hayroll final output",
                        command.file.string(),
                        std::nullopt
                    );

                    SPDLOG_INFO("Task {}/{} {} completed", taskIdx + 1, numTasks, command.file.string());
                }
                catch (const std::exception & e)
                {
                    std::lock_guard<std::mutex> lock(failedMutex);
                    failedTasks.emplace_back(command.file, e.what());
                    SPDLOG_ERROR("Task {}/{} {} failed: {}", taskIdx + 1, numTasks, command.file.string(), e.what());
                }
                catch (...)
                {
                    std::lock_guard<std::mutex> lock(failedMutex);
                    failedTasks.emplace_back(command.file, "unknown error");
                    SPDLOG_ERROR("Task {}/{} {} failed: unknown error", taskIdx + 1, numTasks, command.file.string());
                }
            }
        };

        std::vector<std::thread> threads;
        threads.reserve(jobs);
        for (std::size_t t = 0; t < jobs; ++t)
        {
            threads.emplace_back(worker);
        }
        for (auto & th : threads)
        {
            th.join();
        }

        SPDLOG_INFO("Collected {} Cargo.toml snippet(s) from subtasks", allCargoTomls.size());
        
        // Build files
        std::string buildRs = C2RustWrapper::genBuildRs();
        std::string mergedCargoToml = C2RustWrapper::mergeCargoTomls(allCargoTomls);
        std::string cargoTomlWithFeatures = C2RustWrapper::addFeaturesToCargoToml(mergedCargoToml, allRustFeatureAtoms);
        std::string libRs = C2RustWrapper::genLibRs(projDir, compileCommands);
        std::string rustToolchainToml = C2RustWrapper::genRustToolchainToml();
        auto saveBuildFile = [&](const std::string & content, const std::string & fileName)
        {
            std::filesystem::path outPath = outputDir / fileName;
            Hayroll::saveStringToFile(content, outPath);
            SPDLOG_INFO("Build file {} saved to: {}", fileName, outPath.string());
        };
        saveBuildFile(buildRs, "build.rs");
        saveBuildFile(cargoTomlWithFeatures, "Cargo.toml");
        saveBuildFile(libRs, "lib.rs");
        saveBuildFile(rustToolchainToml, "rust-toolchain.toml");

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

};

} // namespace Hayroll

#endif // HAYROLL_PIPELINE_HPP
