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
#include <initializer_list>

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
        inline static constexpr std::initializer_list<std::string_view> Ordered =
        {
            Pioneer,
            Splitter,
            Maki,
            Seeder,
            C2Rust,
            Reaper,
            Merger
        };
    };

    class StageTimer
    {
    public:
        class Scope
        {
        public:
            Scope(StageTimer & timer, std::string_view stage)
                : timer(&timer), stage(stage)
            {
                this->timer->beginStage(stage);
            }

            Scope(const Scope &) = delete;
            Scope & operator=(const Scope &) = delete;

            Scope(Scope && other) noexcept
                : timer(std::exchange(other.timer, nullptr)), stage(std::move(other.stage))
            {
            }

            Scope & operator=(Scope &&) = delete;

            ~Scope()
            {
                if (timer) timer->endStage(stage);
            }

        private:
            friend class StageTimer;

            StageTimer * timer;
            std::string stage;
        };

        [[nodiscard]] std::unordered_map<std::string, std::chrono::nanoseconds> getStageDurations() const
        {
            return elapsedDurations;
        }

        [[nodiscard]] std::chrono::nanoseconds totalDuration() const
        {
            return total;
        }

        [[nodiscard]] ordered_json toJson() const
        {
            ordered_json stagesJson = ordered_json::object();
            for (std::string_view stageName : StageNames::Ordered)
            {
                const std::string stageKey(stageName);
                const auto it = elapsedDurations.find(stageKey);
                const double valueMs = (it != elapsedDurations.end()) ? toMillis(it->second) : 0.0;
                stagesJson[stageKey] = valueMs;
            }
            for (const auto & [name, duration] : elapsedDurations)
            {
                const std::string_view nameView{name};
                if (std::find(StageNames::Ordered.begin(), StageNames::Ordered.end(), nameView) == StageNames::Ordered.end())
                {
                    stagesJson[name] = toMillis(duration);
                }
            }

            ordered_json result = ordered_json::object();
            result["stages"] = stagesJson;
            result["total_ms"] = toMillis(total);
            result["loc_count"] = locCount;
            return result;
        }

        void setLocCount(int count)
        {
            locCount = count;
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
            auto [it, inserted] = runningStages.try_emplace(std::string(stage), clock::now());
            if (!inserted)
            {
                return;
            }
            elapsedDurations.try_emplace(it->first, std::chrono::nanoseconds::zero());
        }

        void endStage(const std::string & stage)
        {
            auto it = runningStages.find(stage);
            if (it == runningStages.end())
            {
                return;
            }

            const auto now = clock::now();
            const auto delta = std::chrono::duration_cast<std::chrono::nanoseconds>(now - it->second);
            elapsedDurations[stage] += delta;
            total += delta;
            runningStages.erase(it);
        }

        std::unordered_map<std::string, clock::time_point> runningStages;
        std::unordered_map<std::string, std::chrono::nanoseconds> elapsedDurations;
        std::chrono::nanoseconds total{0};
        int locCount{0};
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
        std::optional<std::vector<std::string>> symbolicMacroWhitelist,
        const bool enableInline,
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
        std::atomic<int> totalLocCount{0};

        std::atomic<std::size_t> nextIdx{0};
        std::atomic<std::size_t> totalSuccessfulSplits{0};
        std::atomic<std::size_t> completedTasks{0};

        auto worker = [&]()
        {
            while (true)
            {
                std::size_t taskIdx = nextIdx.fetch_add(1, std::memory_order_relaxed);
                if (taskIdx >= numTasks) break;
                const CompileCommand & command = compileCommands[taskIdx];
                std::filesystem::path srcPath = command.file;
                StageTimer stageTimer;
                std::size_t taskSuccessfulSplits = 0;

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
                    SymbolicExecutor executor(srcPath, projDir, command.getIncludePaths(), symbolicMacroWhitelist, false);
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
                            ".defset.raw.txt",
                            "Raw DefineSets summary",
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

                    const std::size_t candidateCount = defineSets.size();
                    cuStrs.reserve(candidateCount);
                    lineMaps.reserve(candidateCount);
                    inverseLineMaps.reserve(candidateCount);
                    cpp2cStrs.reserve(candidateCount);
                    cpp2cInvocations.reserve(candidateCount);
                    cpp2cRanges.reserve(candidateCount);

                    std::vector<DefineSet> validDefineSets;
                    validDefineSets.reserve(candidateCount);
                    std::vector<CompileCommand> validCommandsWithDefSets;
                    validCommandsWithDefSets.reserve(candidateCount);

                    int avgLocCount = 0;

                    {
                        StageTimer::Scope stage(stageTimer, StageNames::Maki);

                        auto makiStep = [&]
                        (
                            size_t i,
                            const CompileCommand & commandWithDefineSet,
                            const DefineSet & candidateDefineSet
                        )
                        {
                            try
                            {
                                const std::size_t validIndex = validDefineSets.size();

                                std::string cuStr = RewriteIncludesWrapper::runRewriteIncludes(commandWithDefineSet);
                                const auto [lineMap, inverseLineMap] = LineMatcher::run
                                (
                                    cuStr,
                                    executor.includeTree,
                                    command.getIncludePaths()
                                );

                                auto [codeRangeAnalysisTasks, defSetRustFeatureAtoms]
                                    = premiseTree->getCodeRangeAnalysisTasksAndRustFeatureAtoms(lineMap);

                                std::string cpp2cStr = MakiWrapper::runCpp2cOnCu(commandWithDefineSet, codeRangeAnalysisTasks);

                                auto [invocations, ranges] = parseCpp2cSummary(cpp2cStr);

                                saveOutput
                                (
                                    commandWithDefineSet,
                                    outputDir,
                                    projDir,
                                    cuStr,
                                    std::format(".{}.cu.c", validIndex),
                                    "Compilation unit file",
                                    command.file.string(),
                                    validIndex
                                );
                                saveOutput
                                (
                                    commandWithDefineSet,
                                    outputDir,
                                    projDir,
                                    cpp2cStr,
                                    std::format(".{}.cpp2c", validIndex),
                                    "Maki cpp2c output",
                                    command.file.string(),
                                    validIndex
                                );

                                cuStrs.push_back(std::move(cuStr));
                                lineMaps.push_back(lineMap);
                                inverseLineMaps.push_back(inverseLineMap);
                                cpp2cStrs.push_back(std::move(cpp2cStr));
                                cpp2cInvocations.push_back(std::move(invocations));
                                cpp2cRanges.push_back(std::move(ranges));
                                rustFeatureAtoms.insert(defSetRustFeatureAtoms.begin(), defSetRustFeatureAtoms.end());
                                validDefineSets.push_back(candidateDefineSet);
                                validCommandsWithDefSets.push_back(commandWithDefineSet);
                            }
                            catch (...)
                            {
                                SPDLOG_WARN
                                (
                                    "Skipping DefineSet {} (index {}) due to Maki failure",
                                    candidateDefineSet.toString(),
                                    i
                                );
                            }
                        };

                        for (std::size_t i = 0; i < candidateCount; ++i)
                        {
                            const CompileCommand & commandWithDefineSet = commandsWithDefSets[i];
                            const DefineSet & candidateDefineSet = defineSets[i];
                            makiStep(i, commandWithDefineSet, candidateDefineSet);
                        }

                        // If none of the candidate DefineSets are valid, fall back to an empty DefineSet
                        if (validDefineSets.empty())
                        {
                            SPDLOG_WARN
                            (
                                "All candidate DefineSets failed Maki; falling back to empty DefineSet"
                            );
                            const std::size_t i = 0;
                            DefineSet candidateDefineSet = DefineSet{};
                            const CompileCommand & commandWithDefineSet = command.withCleanup().withUpdatedDefineSet(candidateDefineSet);
                            makiStep(i, commandWithDefineSet, candidateDefineSet);
                        }

                        cpp2cRangesCompleted = Hayroll::MakiRangeSummary::complementRangeSummaries(cpp2cRanges, inverseLineMaps);
                        for (std::size_t i = 0; i < cpp2cRangesCompleted.size(); ++i)
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

                    defineSets = std::move(validDefineSets);
                    commandsWithDefSets = std::move(validCommandsWithDefSets);
                    const std::size_t defineCount = defineSets.size();
                    saveOutput
                    (
                        command,
                        outputDir,
                        projDir,
                        DefineSet::defineSetsToString(defineSets),
                        ".defset.txt",
                        "Valid DefineSets summary",
                        command.file.string(),
                        std::nullopt
                    );

                    if (!cuStrs.empty())
                    {
                        int taskTotalLocCount = 0;
                        for (const std::string & cuStr : cuStrs)
                        {
                            taskTotalLocCount += static_cast<int>(std::count(cuStr.begin(), cuStr.end(), '\n'));
                        }
                        avgLocCount = taskTotalLocCount / static_cast<int>(cuStrs.size());
                        stageTimer.setLocCount(avgLocCount);
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
                            ++taskSuccessfulSplits;
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

                        std::string reapedStr;
                        {
                            StageTimer::Scope stage(stageTimer, StageNames::Reaper);
                            reapedStr = RustRefactorWrapper::runReaper(c2rustStr);
                            saveOutput
                            (
                                command,
                                outputDir,
                                projDir,
                                reapedStr,
                                std::format(".{}.reaped.rs", i),
                                "Hayroll Reaper output",
                                command.file.string(),
                                i
                            );
                        }
                        reapedStrs.push_back(reapedStr);

                        if (enableInline)
                        {
                            std::string inlinedStr = RustRefactorWrapper::runInliner(reapedStr);
                            saveOutput
                            (
                                command,
                                outputDir,
                                projDir,
                                inlinedStr,
                                std::format(".{}.inlined.rs", i),
                                "Hayroll Inliner output",
                                command.file.string(),
                                i
                            );
                        }
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
                        std::string finalRustStr = mergedRustStrs.back();

                        // Cleaner shares merger's stage timer

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

                    totalSuccessfulSplits += taskSuccessfulSplits;
                    completedTasks++;
                    totalLocCount += avgLocCount;

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

        // Performance report
        ordered_json performance = ordered_json::object();
        
        ordered_json stageTotalsJson = ordered_json::object();
        // First, add all predefined stages in order (with 0 if not found)
        for (std::string_view stageName : StageNames::Ordered)
        {
            const std::string stageKey(stageName);
            const auto it = performanceStageTotals.find(stageKey);
            const double totalMs = (it != performanceStageTotals.end()) ? StageTimer::toMillis(it->second) : 0.0;
            stageTotalsJson[stageKey] = totalMs;
        }

        performance["stages"] = stageTotalsJson;
        const double totalMsAll = StageTimer::toMillis(performanceTotal);
        performance["total_ms"] = totalMsAll;
        performance["loc_count"] = totalLocCount.load();
        performance["task_count"] = numTasks;

        std::string performanceStr = performance.dump(4);
        Hayroll::saveStringToFile(performanceStr, outputDir / "performance.json");
        SPDLOG_INFO("Performance statistics saved to: {}", (outputDir / "performance.json").string());

        const std::size_t completedTaskCount = completedTasks.load(std::memory_order_relaxed);
        const std::size_t totalSplits = totalSuccessfulSplits.load(std::memory_order_relaxed);
        const double averageSplitsPerTask = completedTaskCount > 0
            ? static_cast<double>(totalSplits) / static_cast<double>(completedTaskCount)
            : 0.0;
        SPDLOG_INFO(
            "Successful splits: {} total; {:.2f} per completed task ({} completed task(s))",
            totalSplits,
            averageSplitsPerTask,
            completedTaskCount
        );

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
