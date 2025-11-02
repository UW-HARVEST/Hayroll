#ifndef HAYROLL_PIPELINE_HPP
#define HAYROLL_PIPELINE_HPP

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
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
#include "RustRefactorWrapper.hpp"

namespace Hayroll
{

class Pipeline
{
    using json = nlohmann::json;
    using ordered_json = nlohmann::ordered_json;

private:
    struct StageNames
    {
        static constexpr std::string_view Pioneer = "Pioneer";
        static constexpr std::string_view Splitter = "Splitter";
        static constexpr std::string_view Maki = "Maki";
        static constexpr std::string_view Seeder = "Seeder";
        static constexpr std::string_view C2Rust = "C2Rust";
        static constexpr std::string_view Reaper = "Reaper";
        static constexpr std::string_view Merger = "Merger";
        static constexpr std::string_view Cleaner = "Cleaner";
        inline static constexpr std::array<std::string_view, 8> Ordered
        {
            Pioneer,
            Splitter,
            Maki,
            Seeder,
            C2Rust,
            Reaper,
            Merger,
            Cleaner
        };
    };

    class StageTimer
    {
    public:
        class Scope
        {
        public:
            Scope(StageTimer & timer, std::string_view stage)
                : timer_(&timer), stage_(stage)
            {
                timer_->beginStage(stage_);
            }

            Scope(const Scope &) = delete;
            Scope & operator=(const Scope &) = delete;

            Scope(Scope && other) noexcept
                : timer_(std::exchange(other.timer_, nullptr)), stage_(std::move(other.stage_))
            {
            }

            Scope & operator=(Scope &&) = delete;

            ~Scope()
            {
                if (timer_) timer_->endStage(stage_);
            }

        private:
            friend class StageTimer;

            StageTimer * timer_;
            std::string stage_;
        };

        [[nodiscard]] std::unordered_map<std::string, std::chrono::nanoseconds> getStageDurations() const
        {
            return elapsedDurations_;
        }

        [[nodiscard]] std::chrono::nanoseconds totalDuration() const
        {
            return total_;
        }

        [[nodiscard]] ordered_json toJson() const
        {
            ordered_json stagesJson = ordered_json::object();
            for (std::string_view stageName : StageNames::Ordered)
            {
                const std::string stageKey(stageName);
                const auto it = elapsedDurations_.find(stageKey);
                const double valueMs = (it != elapsedDurations_.end()) ? toMillis(it->second) : 0.0;
                stagesJson[stageKey] = valueMs;
            }
            for (const auto & [name, duration] : elapsedDurations_)
            {
                const std::string_view nameView{name};
                if (std::find(StageNames::Ordered.begin(), StageNames::Ordered.end(), nameView) == StageNames::Ordered.end())
                {
                    stagesJson[name] = toMillis(duration);
                }
            }

            ordered_json result = ordered_json::object();
            result["stages"] = stagesJson;
            result["total_ms"] = toMillis(total_);
            return result;
        }

        static double toMillis(std::chrono::nanoseconds ns)
        {
            return std::chrono::duration<double, std::milli>(ns).count();
        }

    private:
        using clock = std::chrono::steady_clock;

        friend class Scope;

        void beginStage(std::string_view stage)
        {
            auto [it, inserted] = runningStages_.try_emplace(std::string(stage), clock::now());
            if (!inserted)
            {
                return;
            }
            elapsedDurations_.try_emplace(it->first, std::chrono::nanoseconds::zero());
        }

        void endStage(const std::string & stage)
        {
            auto it = runningStages_.find(stage);
            if (it == runningStages_.end())
            {
                return;
            }

            const auto now = clock::now();
            const auto delta = std::chrono::duration_cast<std::chrono::nanoseconds>(now - it->second);
            elapsedDurations_[stage] += delta;
            total_ += delta;
            runningStages_.erase(it);
        }

        std::unordered_map<std::string, clock::time_point> runningStages_;
        std::unordered_map<std::string, std::chrono::nanoseconds> elapsedDurations_;
        std::chrono::nanoseconds total_{0};
    };

public:
    static std::filesystem::path saveOutput
    (
        const Hayroll::CompileCommand & base,
        const std::filesystem::path & outputDir,
        const std::filesystem::path & projDir,
        const std::string_view content,
        const std::optional<std::string> & newExt,
        const std::string & step,
        const std::string & fileName,
        const std::optional<std::size_t> defineSetIndex = std::nullopt
    )
    {
        Hayroll::CompileCommand outCmd = base.withSanitizedPaths(projDir)
            .withUpdatedFilePathPrefix(outputDir / "src", projDir);
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
        std::vector<Seeder::SeedingReport> allSeedingReports;
        std::mutex collectionMutex;
        std::unordered_map<std::string, std::chrono::nanoseconds> performanceStageTotals;
        std::chrono::nanoseconds performanceTotal{0};

        std::atomic<std::size_t> nextIdx{0};

        auto worker = [&]()
        {
            while (true)
            {
                std::size_t taskIdx = nextIdx.fetch_add(1, std::memory_order_relaxed);
                if (taskIdx >= numTasks) break;
                const CompileCommand & command = compileCommands[taskIdx];
                std::filesystem::path srcPath = command.file;
                StageTimer stageTimer;

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
                    PremiseTree * premiseTree = nullptr;
                    {
                        StageTimer::Scope stage(stageTimer, StageNames::Pioneer);
                        executor.run();
                        premiseTree = executor.scribe.borrowTree();
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
                    }

                    // Splitter
                    std::vector<DefineSet> defineSets;
                    std::vector<CompileCommand> commandsWithDefSets;
                    {
                        StageTimer::Scope stage(stageTimer, StageNames::Splitter);
                        defineSets = Splitter::run(premiseTree, command);
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

                        commandsWithDefSets.reserve(defineSets.size());
                        for (std::size_t i = 0; i < defineSets.size(); ++i)
                        {
                            commandsWithDefSets.push_back(command.withCleanup().withUpdatedDefineSet(defineSets[i]));
                        }
                    }

                    // Run rewrite-includes + LineMatcher + CodeRangeAnalysisTasks generation + Maki cpp2c on each DefineSet
                    std::vector<std::string> cuStrs;
                    std::vector<std::unordered_map<Hayroll::IncludeTreePtr, std::vector<int>>> lineMaps;
                    std::vector<std::vector<std::pair<Hayroll::IncludeTreePtr, int>>> inverseLineMaps;
                    std::vector<std::string> cpp2cStrs;
                    std::vector<std::vector<Hayroll::MakiInvocationSummary>> cpp2cInvocations;
                    std::vector<std::vector<Hayroll::MakiRangeSummary>> cpp2cRanges;
                    std::vector<std::vector<Hayroll::MakiRangeSummary>> cpp2cRangesCompleted;
                    std::set<std::string> rustFeatureAtoms;

                    const std::size_t defineCount = defineSets.size();
                    cuStrs.reserve(defineCount);
                    lineMaps.reserve(defineCount);
                    inverseLineMaps.reserve(defineCount);
                    cpp2cStrs.reserve(defineCount);
                    cpp2cInvocations.reserve(defineCount);
                    cpp2cRanges.reserve(defineCount);

                    {
                        StageTimer::Scope stage(stageTimer, StageNames::Maki);
                        for (std::size_t i = 0; i < defineCount; ++i)
                        {
                            const CompileCommand & commandWithDefineSet = commandsWithDefSets[i];

                            std::string cuStr = RewriteIncludesWrapper::runRewriteIncludes(commandWithDefineSet);
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
                            cuStrs.push_back(cuStr);

                            const auto [lineMap, inverseLineMap] = LineMatcher::run
                            (
                                cuStrs.back(),
                                executor.includeTree,
                                command.getIncludePaths()
                            );
                            lineMaps.push_back(lineMap);
                            inverseLineMaps.push_back(inverseLineMap);

                            auto [codeRangeAnalysisTasks, defSetRustFeatureAtoms]
                                = premiseTree->getCodeRangeAnalysisTasksAndRustFeatureAtoms(lineMap);
                            rustFeatureAtoms.insert(defSetRustFeatureAtoms.begin(), defSetRustFeatureAtoms.end());

                            std::string cpp2cStr = MakiWrapper::runCpp2cOnCu(commandWithDefineSet, codeRangeAnalysisTasks);
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
                            cpp2cStrs.push_back(cpp2cStr);

                            auto [invocations, ranges] = parseCpp2cSummary(cpp2cStrs.back());
                            cpp2cInvocations.push_back(invocations);
                            cpp2cRanges.push_back(ranges);
                        }

                        cpp2cRangesCompleted = Hayroll::MakiRangeSummary::complementRangeSummaries(cpp2cRanges, inverseLineMaps);
                        for (std::size_t i = 0; i < defineCount; ++i)
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
                    }

                    std::vector<std::string> cargoTomls;
                    cargoTomls.reserve(defineCount);
                    std::vector<std::string> reapedStrs;
                    reapedStrs.reserve(defineCount);
                    std::vector<Seeder::SeedingReport> seedingReports;
                    seedingReports.reserve(defineCount);

                    for (std::size_t i = 0; i < defineCount; ++i)
                    {
                        std::string cuSeededStr;
                        std::vector<Seeder::SeedingReport> seedingReportEntries;
                        {
                            StageTimer::Scope stage(stageTimer, StageNames::Seeder);
                            auto seederResult = Seeder::run
                            (
                                cpp2cInvocations[i],
                                cpp2cRangesCompleted[i],
                                cuStrs[i],
                                lineMaps[i],
                                inverseLineMaps[i]
                            );
                            cuSeededStr = std::move(std::get<0>(seederResult));
                            seedingReportEntries = std::move(std::get<1>(seederResult));
                            const std::string seedingReportStr = json(seedingReportEntries).dump(4);
                            saveOutput
                            (
                                command,
                                outputDir,
                                projDir,
                                seedingReportStr,
                                std::format(".{}.seeder_report.json", i),
                                "Hayroll Seeder report",
                                command.file.string(),
                                i
                            );
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
                        }
                        seedingReports.insert
                        (
                            seedingReports.end(),
                            seedingReportEntries.begin(),
                            seedingReportEntries.end()
                        );

                        std::string c2rustStr;
                        std::string cargoToml;
                        {
                            StageTimer::Scope stage(stageTimer, StageNames::C2Rust);
                            auto transpileResult = C2RustWrapper::transpile(cuSeededStr, commandsWithDefSets[i]);
                            c2rustStr = std::move(std::get<0>(transpileResult));
                            cargoToml = std::move(std::get<1>(transpileResult));
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
                        }
                        cargoTomls.push_back(cargoToml);

                        std::string reaperStr;
                        {
                            StageTimer::Scope stage(stageTimer, StageNames::Reaper);
                            reaperStr = RustRefactorWrapper::runReaper(c2rustStr);
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
                        reapedStrs.push_back(reaperStr);
                    }

                    if (reapedStrs.empty())
                    {
                        throw std::runtime_error("No reaped outputs generated for " + command.file.string());
                    }

                    std::vector<std::string> mergedRustStrs;
                    mergedRustStrs.reserve(reapedStrs.size());
                    mergedRustStrs.push_back(reapedStrs[0]);
                    {
                        StageTimer::Scope stage(stageTimer, StageNames::Merger);
                        for (std::size_t i = 1; i < reapedStrs.size(); ++i)
                        {
                            std::string merged = RustRefactorWrapper::runMerger(mergedRustStrs[i - 1], reapedStrs[i]);
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
                            mergedRustStrs.push_back(std::move(merged));
                        }
                    }

                    std::string finalRustStr = mergedRustStrs.back();
                    {
                        StageTimer::Scope stage(stageTimer, StageNames::Cleaner);
                        finalRustStr = RustRefactorWrapper::runCleaner(finalRustStr);
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
                    }

                    {
                        std::lock_guard<std::mutex> lk(collectionMutex);
                        allCargoTomls.insert(allCargoTomls.end(), cargoTomls.begin(), cargoTomls.end());
                        allRustFeatureAtoms.insert(rustFeatureAtoms.begin(), rustFeatureAtoms.end());
                        allSeedingReports.insert(allSeedingReports.end(), seedingReports.begin(), seedingReports.end());
                    }

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

                {
                    const auto stageDurationsSnapshot = stageTimer.getStageDurations();
                    const std::chrono::nanoseconds totalDurationSnapshot = stageTimer.totalDuration();
                    std::lock_guard<std::mutex> lk(collectionMutex);
                    for (const auto & [stageName, duration] : stageDurationsSnapshot)
                    {
                        performanceStageTotals[stageName] += duration;
                    }
                    performanceTotal += totalDurationSnapshot;
                }

                try
                {
                    const ordered_json perfJson = stageTimer.toJson();
                    const std::string perfContent = perfJson.dump(4);
                    saveOutput
                    (
                        command,
                        outputDir,
                        projDir,
                        perfContent,
                        ".perf.json",
                        "Hayroll performance profile",
                        command.file.string(),
                        std::nullopt
                    );
                }
                catch (const std::exception & e)
                {
                    SPDLOG_ERROR("Failed to save performance profile for {}: {}", command.file.string(), e.what());
                }
                catch (...)
                {
                    SPDLOG_ERROR("Failed to save performance profile for {}: unknown error", command.file.string());
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

        // Seeding report analysis
        ordered_json statistics = Seeder::seedingReportStatistics(allSeedingReports);

        std::string statisticsStr = statistics.dump(4);
        Hayroll::saveStringToFile(statisticsStr, outputDir / "statistics.json");
        SPDLOG_INFO("Statistics saved to: {}", (outputDir / "statistics.json").string());

        ordered_json performance = ordered_json::object();
        ordered_json stageTotalsJson = ordered_json::object();
        const std::size_t taskCount = numTasks;
        const double taskCountDouble = taskCount > 0 ? static_cast<double>(taskCount) : 1.0;
        for (std::string_view stageName : StageNames::Ordered)
        {
            const std::string stageKey(stageName);
            const auto it = performanceStageTotals.find(stageKey);
            const double totalMs = (it != performanceStageTotals.end()) ? StageTimer::toMillis(it->second) : 0.0;
            const double valueMs = (taskCount > 0) ? (totalMs / taskCountDouble) : 0.0;
            stageTotalsJson[stageKey] = valueMs;
        }
        for (const auto & [name, duration] : performanceStageTotals)
        {
            const std::string_view nameView{name};
            if (std::find(StageNames::Ordered.begin(), StageNames::Ordered.end(), nameView) == StageNames::Ordered.end())
            {
                const double totalMs = StageTimer::toMillis(duration);
                stageTotalsJson[name] = (taskCount > 0) ? (totalMs / taskCountDouble) : 0.0;
            }
        }
        performance["stages"] = stageTotalsJson;
        const double totalMsAll = StageTimer::toMillis(performanceTotal);
        performance["total_ms"] = (taskCount > 0) ? (totalMsAll / taskCountDouble) : 0.0;
        performance["task_count"] = taskCount;

        std::string performanceStr = performance.dump(4);
        Hayroll::saveStringToFile(performanceStr, outputDir / "performance.json");
        SPDLOG_INFO("Performance statistics saved to: {}", (outputDir / "performance.json").string());

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
