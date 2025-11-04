#ifndef HAYROLL_RUSTREFACTORWRAPPER_HPP
#define HAYROLL_RUSTREFACTORWRAPPER_HPP

#include <filesystem>
#include <functional>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/spdlog.h>
#include "subprocess.hpp"
#include "json.hpp"

#include "Util.hpp"
#include "TempDir.hpp"

namespace Hayroll
{
class RustRefactorWrapper
{
public:
    struct ToolConfig
    {
        std::string toolName;
        std::filesystem::path executable;
        std::size_t workingDirIndex{0};
        std::size_t outputDirIndex{0};
        std::function<std::vector<std::string>(const std::vector<std::filesystem::path> &)> buildArgs;
        std::string_view cargoToml{dummyCargoToml};
    };

    static inline const std::string dummyCargoToml = R"(
[package]
name = "test"
version = "0.1.0"
edition = "2021"

[[bin]]
name = "test"
path = "src/main.rs"
)";

    static std::string runReaper(std::string_view seededRustStr)
    {
        ToolConfig config;
        config.toolName = "Reaper";
        config.executable = HayrollReaperExe;
        config.workingDirIndex = 0;
        config.outputDirIndex = 0;
        config.buildArgs = [](const std::vector<std::filesystem::path> & paths)
        {
            return std::vector<std::string>{paths[0].string()};
        };
        return runTool(config, {seededRustStr});
    }

    static std::string runMerger(std::string_view reapedRustStrBase, std::string_view reapedRustStrPatch)
    {
        ToolConfig config;
        config.toolName = "Merger";
        config.executable = HayrollMergerExe;
        config.workingDirIndex = 0;
        config.outputDirIndex = 0;
        config.buildArgs = [](const std::vector<std::filesystem::path> & paths)
        {
            return std::vector<std::string>{paths[0].string(), paths[1].string()};
        };
        return runTool(config, {reapedRustStrBase, reapedRustStrPatch});
    }

    static std::string runInliner(std::string_view rustStr)
    {
        ToolConfig config;
        config.toolName = "Inliner";
        config.executable = HayrollInlinerExe;
        config.workingDirIndex = 0;
        config.outputDirIndex = 0;
        config.buildArgs = [](const std::vector<std::filesystem::path> & paths)
        {
            return std::vector<std::string>{paths[0].string()};
        };
        return runTool(config, {rustStr});
    }

    static std::string runCleaner(std::string_view rustStr)
    {
        ToolConfig config;
        config.toolName = "Cleaner";
        config.executable = HayrollCleanerExe;
        config.workingDirIndex = 0;
        config.outputDirIndex = 0;
        config.buildArgs = [](const std::vector<std::filesystem::path> & paths)
        {
            return std::vector<std::string>{paths[0].string()};
        };
        return runTool(config, {rustStr});
    }

private:
    static std::string runTool(const ToolConfig & config, std::initializer_list<std::string_view> inputs)
    {
        if (inputs.size() == 0)
        {
            throw std::invalid_argument(config.toolName + " requires at least one input file.");
        }
        if (!config.buildArgs)
        {
            throw std::invalid_argument(config.toolName + " configuration missing buildArgs callback.");
        }

        std::vector<std::string_view> inputVec(inputs.begin(), inputs.end());
        std::vector<TempDir> tempDirs;
        tempDirs.reserve(inputVec.size());
        std::vector<std::filesystem::path> tempPaths;
        tempPaths.reserve(inputVec.size());
        std::vector<std::filesystem::path> inputPaths;
        inputPaths.reserve(inputVec.size());

        for (std::size_t i = 0; i < inputVec.size(); ++i)
        {
            tempDirs.emplace_back();
            const std::filesystem::path & dirPath = tempDirs.back().getPath();
            tempPaths.emplace_back(dirPath);
            const std::filesystem::path cargoPath = dirPath / "Cargo.toml";
            const std::filesystem::path inputPath = dirPath / "src/main.rs";
            saveStringToFile(config.cargoToml, cargoPath);
            saveStringToFile(inputVec[i], inputPath);
            inputPaths.emplace_back(inputPath);
        }

        if (config.workingDirIndex >= tempPaths.size())
        {
            throw std::invalid_argument("workingDirIndex out of range for " + config.toolName);
        }
        if (config.outputDirIndex >= inputPaths.size())
        {
            throw std::invalid_argument("outputDirIndex out of range for " + config.toolName);
        }

        std::vector<std::string> args = config.buildArgs(tempPaths);
        std::vector<std::string> processArgs;
        processArgs.reserve(args.size() + 1);
        processArgs.push_back(config.executable.string());
        processArgs.insert(processArgs.end(), args.begin(), args.end());

        std::string commandPreview;
        for (const std::string & arg : processArgs)
        {
            if (!commandPreview.empty()) commandPreview.push_back(' ');
            commandPreview += arg;
        }
        SPDLOG_TRACE("Issuing command: {}", commandPreview);

        const std::string workingDir = tempPaths[config.workingDirIndex].string();
        subprocess::Popen process
        (
            processArgs,
            subprocess::output{subprocess::PIPE},
            subprocess::error{subprocess::PIPE},
            subprocess::cwd{workingDir.c_str()}
        );

        auto [out, err] = process.communicate();
        SPDLOG_TRACE("{} stdout:\n{}", config.toolName, out.buf.data());
        SPDLOG_TRACE("{} stderr:\n{}", config.toolName, err.buf.data());

        const int retcode = process.retcode();
        if (retcode != 0)
        {
            SPDLOG_ERROR("{} exited with code {}", config.toolName, retcode);
            std::string errStr = std::string(out.buf.data()) + '\n' + std::string(err.buf.data());
            throw std::runtime_error(config.toolName + " failed (exit code " + std::to_string(retcode) + "): " + errStr);
        }

        const std::filesystem::path & outputPath = inputPaths[config.outputDirIndex];
        std::string output = loadFileToString(outputPath);
        if (output.empty())
        {
            throw std::runtime_error(config.toolName + " produced an empty output file.");
        }
        return output;
    }
};

} // namespace Hayroll

#endif // HAYROLL_RUSTREFACTORWRAPPER_HPP
