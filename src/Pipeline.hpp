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

    static std::optional<std::pair<std::string, std::filesystem::path>> resolveBinaryTarget
    (
        const std::vector<CompileCommand> & compileCommands,
        const std::filesystem::path & projDir,
        const std::filesystem::path & outputDir,
        std::string_view query
    )
    {
        std::filesystem::path queryPath(query);
        if (!queryPath.is_absolute())
        {
            queryPath = projDir / queryPath;
        }
        queryPath = queryPath.lexically_normal();

        std::vector<const CompileCommand*> matches;
        for (const CompileCommand & command : compileCommands)
        {
            std::filesystem::path commandStem = command.file;
            commandStem.replace_extension("");
            commandStem = commandStem.lexically_normal();
            if (commandStem == queryPath)
            {
                matches.push_back(&command);
            }
        }

        if (matches.empty())
        {
            SPDLOG_ERROR(
                "Binary target '{}' did not match any translation unit (provide the path to the source file without its extension, relative to the project directory or absolute).",
                query
            );
            return std::nullopt;
        }
        if (matches.size() > 1)
        {
            SPDLOG_ERROR(
                "Binary target '{}' is ambiguous; found {} translation units with the same stem",
                query,
                matches.size()
            );
            for (const CompileCommand * cmd : matches)
            {
                SPDLOG_ERROR("  candidate: {}", cmd->file.string());
            }
            return std::nullopt;
        }

        CompileCommand projectedCommand = matches.front()->withSanitizedPaths(projDir)
            .withUpdatedFilePathPrefix(outputDir / "src", projDir)
            .withUpdatedFileExtension(".rs");
        std::filesystem::path relativePath;
        try
        {
            relativePath = std::filesystem::relative(projectedCommand.file, outputDir);
        }
        catch (const std::exception & e)
        {
            SPDLOG_ERROR("Failed to compute binary output path for '{}': {}", query, e.what());
            return std::nullopt;
        }
        if (relativePath.empty())
        {
            SPDLOG_ERROR("Binary target '{}' produced an empty relative path", query);
            return std::nullopt;
        }
        relativePath = relativePath.lexically_normal();

        SPDLOG_INFO(
            "Binary target '{}' will generate a [[bin]] entry pointing to {}",
            query,
            relativePath.generic_string()
        );
        return std::make_optional(std::make_pair(std::string(query), relativePath));
    }

    static int run
    (
        const std::filesystem::path & compileCommandsJsonPath,
        const std::filesystem::path & outputDir,
        const std::filesystem::path & projDir,
        std::optional<std::vector<std::string>> symbolicMacroWhitelist,
        const bool enableInline,
        const bool keepSrcLoc,
        std::size_t jobs,
        std::optional<std::string> binaryTargetName
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

        std::optional<std::pair<std::string, std::filesystem::path>> binaryTargetConfig;
        if (binaryTargetName)
        {
            binaryTargetConfig = resolveBinaryTarget(compileCommands, projDir, outputDir, *binaryTargetName);
            if (!binaryTargetConfig)
            {
                return 1;
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

                    // Splitter two-phase: gather Maki successes, then run downstream with complemented ranges
                    struct MakiCandidate
                    {
                        DefineSet defineSet;
                        CompileCommand commandWithDefineSet;
                        std::string cuStr;
                        std::unordered_map<Hayroll::IncludeTreePtr, std::vector<int>> lineMap;
                        std::vector<std::pair<Hayroll::IncludeTreePtr, int>> inverseLineMap;
                        std::string cpp2cStr;
                        std::vector<Hayroll::MakiInvocationSummary> cpp2cInvocations;
                        std::vector<Hayroll::MakiRangeSummary> cpp2cRanges;
                        std::set<std::string> rustFeatureAtoms;
                    };

                    std::vector<MakiCandidate> makiCandidates;
                    Splitter splitter(premiseTree, command);
                    Splitter::Feedback feedback = Splitter::Feedback::initial();

                    auto runMaki = [&](const DefineSet & defineSet) -> bool
                    {
                        CompileCommand commandWithDefineSet = command.withCleanup().withUpdatedDefineSet(defineSet);
                        std::string failedStage(StageNames::Maki);

                        try
                        {
                            StageTimer::Scope stage(stageTimer, StageNames::Maki);
                            std::string cuStr = RewriteIncludesWrapper::runRewriteIncludes(commandWithDefineSet);
                            const auto lineMapResults = LineMatcher::run
                            (
                                cuStr,
                                executor.includeTree,
                                command.getIncludePaths()
                            );
                            auto [codeRangeAnalysisTasks, atoms]
                                = premiseTree->getCodeRangeAnalysisTasksAndRustFeatureAtoms(lineMapResults.first);

                            std::string cpp2cStr = MakiWrapper::runCpp2cOnCu(commandWithDefineSet, codeRangeAnalysisTasks);
                            auto [invocations, ranges] = parseCpp2cSummary(cpp2cStr);

                            makiCandidates.push_back
                            (
                                MakiCandidate
                                {
                                    defineSet,
                                    commandWithDefineSet,
                                    std::move(cuStr),
                                    lineMapResults.first,
                                    lineMapResults.second,
                                    std::move(cpp2cStr),
                                    std::move(invocations),
                                    std::move(ranges),
                                    atoms
                                }
                            );
                            feedback = Splitter::Feedback::success();
                            return true;
                        }
                        catch (const std::exception & e)
                        {
                            SPDLOG_WARN
                            (
                                "Skipping DefineSet {} due to failure at stage {}",
                                defineSet.toString(),
                                failedStage
                            );
                            feedback = Splitter::Feedback::failStage(failedStage, e.what());
                            return false;
                        }
                        catch (...)
                        {
                            SPDLOG_WARN
                            (
                                "Skipping DefineSet {} due to unknown failure.",
                                defineSet.toString()
                            );
                            feedback = Splitter::Feedback::failStage("Unknown", "unknown error");
                            return false;
                        }
                    };

                    while (true)
                    {
                        std::optional<DefineSet> defineSetOpt;
                        {
                            StageTimer::Scope stage(stageTimer, StageNames::Splitter);
                            defineSetOpt = splitter.next(feedback);
                        }
                        if (!defineSetOpt) break;

                        runMaki(*defineSetOpt);
                    }

                    if (makiCandidates.empty())
                    {
                        SPDLOG_WARN("No Maki-successful DefineSet; falling back to empty DefineSet.");
                        if (!runMaki(DefineSet{}))
                        {
                            throw std::runtime_error("Maki failed for fallback empty DefineSet for " + command.file.string());
                        }
                    }

                    std::vector<std::vector<Hayroll::MakiRangeSummary>> cpp2cRangesList;
                    std::vector<std::vector<std::pair<Hayroll::IncludeTreePtr, int>>> inverseLineMapList;
                    cpp2cRangesList.reserve(makiCandidates.size());
                    inverseLineMapList.reserve(makiCandidates.size());
                    for (const MakiCandidate & candidate : makiCandidates)
                    {
                        cpp2cRangesList.push_back(candidate.cpp2cRanges);
                        inverseLineMapList.push_back(candidate.inverseLineMap);
                    }
                    auto cpp2cRangesCompletedAll = Hayroll::MakiRangeSummary::complementRangeSummaries
                    (
                        cpp2cRangesList,
                        inverseLineMapList
                    );

                    std::vector<DefineSet> successfulDefineSets;
                    std::vector<std::string> cargoTomls;
                    std::vector<std::string> reapedStrs;
                    std::vector<Seeder::SeedingReport> seedingReports;
                    std::set<std::string> rustFeatureAtoms;
                    int taskLocCount = 0;

                    for (std::size_t i = 0; i < makiCandidates.size(); ++i)
                    {
                        const MakiCandidate & candidate = makiCandidates[i];
                        const std::vector<Hayroll::MakiRangeSummary> & cpp2cRangesCompleted = cpp2cRangesCompletedAll[i];

                        std::vector<Seeder::SeedingReport> seedingReportEntries;
                        std::string cuSeededStr;
                        std::string c2rustStr;
                        std::string cargoToml;
                        std::string reapedStr;
                        std::string inlinedStr;
                        std::string failedStage = "Unknown";

                        try
                        {
                            {
                                failedStage = StageNames::Seeder;
                                StageTimer::Scope stage(stageTimer, StageNames::Seeder);
                                auto seederResult = Seeder::run
                                (
                                    candidate.cpp2cInvocations,
                                    cpp2cRangesCompleted,
                                    candidate.cuStr,
                                    candidate.lineMap,
                                    candidate.inverseLineMap
                                );
                                cuSeededStr = std::move(std::get<0>(seederResult));
                                seedingReportEntries = std::move(std::get<1>(seederResult));
                            }

                            {
                                failedStage = StageNames::C2Rust;
                                StageTimer::Scope stage(stageTimer, StageNames::C2Rust);
                                auto transpileResult = C2RustWrapper::transpile(cuSeededStr, candidate.commandWithDefineSet);
                                c2rustStr = std::move(std::get<0>(transpileResult));
                                cargoToml = std::move(std::get<1>(transpileResult));
                            }

                            {
                                failedStage = StageNames::Reaper;
                                StageTimer::Scope stage(stageTimer, StageNames::Reaper);
                                reapedStr = RustRefactorWrapper::runReaper(c2rustStr, keepSrcLoc);
                            }

                            if (enableInline)
                            {
                                inlinedStr = RustRefactorWrapper::runInliner(reapedStr);
                            }

                            // Officially assigned now
                            const std::size_t splitId = reapedStrs.size();

                            const std::string seedingReportStr = json(seedingReportEntries).dump(4);
                            saveOutput
                            (
                                candidate.commandWithDefineSet,
                                outputDir,
                                projDir,
                                candidate.cuStr,
                                std::format(".{}.cu.c", splitId),
                                "Compilation unit file",
                                command.file.string(),
                                splitId
                            );
                            saveOutput
                            (
                                candidate.commandWithDefineSet,
                                outputDir,
                                projDir,
                                candidate.cpp2cStr,
                                std::format(".{}.cpp2c", splitId),
                                "Maki cpp2c output",
                                command.file.string(),
                                splitId
                            );
                            saveOutput
                            (
                                command,
                                outputDir,
                                projDir,
                                json(cpp2cRangesCompleted).dump(4),
                                std::format(".{}.cpp2c.ranges.json", splitId),
                                "Complemented Maki range summary",
                                command.file.string(),
                                splitId
                            );
                            saveOutput
                            (
                                command,
                                outputDir,
                                projDir,
                                seedingReportStr,
                                std::format(".{}.seeder_report.json", splitId),
                                "Hayroll Seeder report",
                                command.file.string(),
                                splitId
                            );
                            saveOutput
                            (
                                command,
                                outputDir,
                                projDir,
                                cuSeededStr,
                                std::format(".{}.seeded.cu.c", splitId),
                                "Hayroll Seeded compilation unit",
                                command.file.string(),
                                splitId
                            );
                            saveOutput
                            (
                                command,
                                outputDir,
                                projDir,
                                c2rustStr,
                                std::format(".{}.seeded.rs", splitId),
                                "C2Rust output",
                                command.file.string(),
                                splitId
                            );
                            saveOutput
                            (
                                command,
                                outputDir,
                                projDir,
                                cargoToml,
                                std::format(".{}.Cargo.toml", splitId),
                                "C2Rust Cargo.toml",
                                command.file.string(),
                                splitId
                            );
                            saveOutput
                            (
                                command,
                                outputDir,
                                projDir,
                                reapedStr,
                                std::format(".{}.reaped.rs", splitId),
                                "Hayroll Reaper output",
                                command.file.string(),
                                splitId
                            );

                            if (enableInline)
                            {
                                saveOutput
                                (
                                    command,
                                    outputDir,
                                    projDir,
                                    inlinedStr,
                                    std::format(".{}.inlined.rs", splitId),
                                    "Hayroll Inliner output",
                                    command.file.string(),
                                    splitId
                                );
                            }

                            successfulDefineSets.push_back(candidate.defineSet);
                            reapedStrs.push_back(reapedStr);
                            cargoTomls.push_back(cargoToml);
                            seedingReports.insert
                            (
                                seedingReports.end(),
                                seedingReportEntries.begin(),
                                seedingReportEntries.end()
                            );

                            rustFeatureAtoms.insert(candidate.rustFeatureAtoms.begin(), candidate.rustFeatureAtoms.end());
                            for (const auto & [name, _] : candidate.defineSet.defines)
                            {
                                rustFeatureAtoms.insert("def" + name);
                            }

                            if (!candidate.cuStr.empty())
                            {
                                taskLocCount += static_cast<int>(std::count(candidate.cuStr.begin(), candidate.cuStr.end(), '\n'));
                                stageTimer.setLocCount(taskLocCount / static_cast<int>(reapedStrs.size()));
                            }

                            ++taskSuccessfulSplits;
                        }
                        catch (const std::exception & e)
                        {
                            SPDLOG_WARN
                            (
                                "Skipping DefineSet {} due to failure at stage {}: {}",
                                candidate.defineSet.toString(),
                                failedStage,
                                e.what()
                            );
                        }
                        catch (...)
                        {
                            SPDLOG_WARN
                            (
                                "Skipping DefineSet {} due to unknown failure.",
                                candidate.defineSet.toString()
                            );
                        }
                    }

                    if (reapedStrs.empty())
                    {
                        throw std::runtime_error("No reaped outputs generated for " + command.file.string());
                    }

                    saveOutput
                    (
                        command,
                        outputDir,
                        projDir,
                        DefineSet::defineSetsToString(successfulDefineSets),
                        ".defset.txt",
                        "Valid DefineSets summary",
                        command.file.string(),
                        std::nullopt
                    );
                    std::vector<std::string> mergedRustStrs;
                    mergedRustStrs.reserve(reapedStrs.size());
                    mergedRustStrs.push_back(reapedStrs[0]);
                    {
                        StageTimer::Scope stage(stageTimer, StageNames::Merger);
                        for (std::size_t i = 1; i < reapedStrs.size(); ++i)
                        {
                            std::string merged = RustRefactorWrapper::runMerger(mergedRustStrs[i - 1], reapedStrs[i], keepSrcLoc);
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

                        finalRustStr = RustRefactorWrapper::runCleaner(finalRustStr, keepSrcLoc);
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

                    for (const DefineSet & defSet : successfulDefineSets)
                    {
                        for (auto [name, val] : defSet.defines)
                        {
                            rustFeatureAtoms.insert("def" + name);
                        }
                    }

                    {
                        std::lock_guard<std::mutex> lk(collectionMutex);
                        allCargoTomls.insert(allCargoTomls.end(), cargoTomls.begin(), cargoTomls.end());
                        allRustFeatureAtoms.insert(rustFeatureAtoms.begin(), rustFeatureAtoms.end());
                        allSeedingReports.insert(allSeedingReports.end(), seedingReports.begin(), seedingReports.end());
                    }

                    totalSuccessfulSplits += taskSuccessfulSplits;
                    completedTasks++;
                    int avgLocCount = successfulDefineSets.empty() ? 0 : taskLocCount / static_cast<int>(successfulDefineSets.size());
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
        std::string cargoTomlWithBinTarget = mergedCargoToml;
        if (binaryTargetConfig)
        {
            cargoTomlWithBinTarget = C2RustWrapper::addBinaryTargetToCargoToml
            (
                mergedCargoToml,
                binaryTargetConfig->first,
                binaryTargetConfig->second.generic_string()
            );
        }
        std::string cargoTomlWithFeatures = C2RustWrapper::addFeaturesToCargoToml(cargoTomlWithBinTarget, allRustFeatureAtoms);
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

        // Prepend lib.rs header info for binary target
        if (binaryTargetConfig)
        {
            try
            {
                const std::filesystem::path binRsPath = outputDir / binaryTargetConfig->second;
                if (std::filesystem::exists(binRsPath))
                {
                    const std::string header = C2RustWrapper::genLibRsHeader();
                    std::string binContent = Hayroll::loadFileToString(binRsPath);
                    std::string newContent = header + "\n" + binContent;
                    Hayroll::saveStringToFile(newContent, binRsPath);
                    SPDLOG_INFO("Prepended lib.rs header to binary target: {}", binRsPath.string());
                }
                else
                {
                    SPDLOG_WARN("Binary target path does not exist for header prepend: {}", binRsPath.string());
                }
            }
            catch (const std::exception & e)
            {
                SPDLOG_WARN("Failed to prepend lib.rs header to binary target: {}", e.what());
            }
        }

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
