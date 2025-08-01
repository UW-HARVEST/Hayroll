#ifndef HAYROLL_MAKIWRAPPER_HPP
#define HAYROLL_MAKIWRAPPER_HPP

#include <string>
#include <filesystem>

#include <spdlog/spdlog.h>
#include "subprocess.hpp"
#include "json.hpp"

#include "Util.hpp"
#include "TempDir.hpp"
#include "CompileCommand.hpp"
#include "RewriteIncludesWrapper.hpp"
#include "LinemarkerEraser.hpp"

namespace Hayroll
{

class MakiWrapper
{
public:
    static std::filesystem::path MakiDir;
    static std::filesystem::path MakiLibcpp2cPath;
    static std::filesystem::path MakiAnalysisScriptPath;

    // Extra code interval to require Maki to analyze.
    // Used for conditional compilation intervals.
    struct CodeIntervalAnalysisTask
    {
        std::string name;
        int beginLine;
        int beginCol;
        int endLine;
        int endCol;
        std::string extraInfo;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(CodeIntervalAnalysisTask, name, beginLine, beginCol, endLine, endCol, extraInfo)
    };

    // Automatically aggregate each compile command into a single compilation unit file,
    // erase its line markers, and save it to a temporary directory.
    // and then run Maki's cpp2c on it.
    // This gives an absolute line:col number w.r.t. the CU file.
    static std::string runCpp2cOnCu
    (
        const CompileCommand & compileCommand,
        const std::vector<CodeIntervalAnalysisTask> & codeIntervals = {},
        int numThreads = 16
    )
    {
        TempDir cuDir;
        std::filesystem::path cuDirPath = cuDir.getPath();
        // Update the command to use the CU file as the source
        std::string cuStr = RewriteIncludesWrapper::runRewriteIncludes(compileCommand);
        std::string cuNolmStr = LinemarkerEraser::run(cuStr);
        CompileCommand newCompileCommand = compileCommand
            .withUpdatedDirectory(cuDirPath)
            .withUpdatedExtension(".cu.c");
        saveStringToFile(cuNolmStr, newCompileCommand.file);

        return runCpp2c(newCompileCommand, cuDirPath, codeIntervals, numThreads);
    }

private:
    static std::string runCpp2c
    (
        const CompileCommand & compileCommand,
        std::filesystem::path projDir,
        const std::vector<CodeIntervalAnalysisTask> & codeIntervals = {},
        int numThreads = 16
    )
    {
        // Example
        // ../Maki/evaluation/analyze_macro_invocations_in_program.py "../Maki/build/lib/libcpp2c.so" "./compile_commands.json" "./" "./macro_invocation_analyses/" 16
        // <script> <cpp2c.so> <compileCommandsJsonPath> <projDir> <outputDir> <numThreads>

        TempDir tempDir;
        // This is fake, just so that Maki reads compile_commands.json from the current directory.
        std::filesystem::path tempDirPath = tempDir.getPath();
        std::filesystem::path compileCommandsPath = tempDirPath / "compile_commands.json";
        std::filesystem::path codeIntervalsPath = tempDirPath / "code_intervals.json";
        // Write compile_commands.json to projDir
        saveStringToFile
        (
            CompileCommand::compileCommandsToJson({compileCommand}).dump(4),
            compileCommandsPath
        );
        SPDLOG_DEBUG("Saved compile_commands.json to: {}\n content:\n{}",
                     compileCommandsPath.string(),
                     CompileCommand::compileCommandsToJson({compileCommand}).dump(4));

        // Write code intervals to a file if provided
        if (!codeIntervals.empty())
        {
            nlohmann::json codeIntervalsJson = nlohmann::json(codeIntervals);
            saveStringToFile(codeIntervalsJson.dump(4), codeIntervalsPath);
            SPDLOG_DEBUG("Saved code_intervals.json to: {}\n content:\n{}",
                         codeIntervalsPath.string(),
                         codeIntervalsJson.dump(4));
        }
        
        projDir = std::filesystem::canonical(projDir);
        
        TempDir outputDir;

        std::vector<std::string> args =
        {
            MakiAnalysisScriptPath.string(),
            MakiLibcpp2cPath.string(),
            compileCommandsPath.string(),
            projDir.string(),
            outputDir.getPath().string(),
            std::to_string(numThreads)
        };

        if (!codeIntervals.empty())
        {
            args.push_back(codeIntervalsPath.string());
        }

        SPDLOG_DEBUG
        (
            "Issuing command: {}",
            [&args]() {
                std::ostringstream oss;
                for (size_t i = 0; i < args.size(); ++i) {
                    if (i > 0) oss << " ";
                    oss << args[i];
                }
                return oss.str();
            }()
        );
        
        subprocess::Popen cpp2c
        (
            args,
            subprocess::output{subprocess::PIPE},
            subprocess::error{subprocess::PIPE}
        );

        // Wait for the process to finish
        auto [out, err] = cpp2c.communicate();

        // Print out the output and error streams
        SPDLOG_DEBUG("Maki cpp2c output:\n{}", out.buf.data());
        SPDLOG_DEBUG("Maki cpp2c error:\n{}", err.buf.data());

        // Should appear: outputDir/all_results.cpp2c
        // Confirm that the file exists and return its content
        std::filesystem::path cpp2cFilePath = outputDir.getPath() / "all_results.cpp2c";
        if (!std::filesystem::exists(cpp2cFilePath))
        {
            throw std::runtime_error("Maki cpp2c did not produce the expected output file: " + cpp2cFilePath.string());
        }

        std::string cpp2cStr = loadFileToString(cpp2cFilePath);

        if (cpp2cStr.empty())
        {
            throw std::runtime_error("Maki cpp2c produced an empty output file.");
        }

        return cpp2cStr;
    }
};

std::filesystem::path MakiWrapper::MakiDir = Hayroll::MakiDir;
std::filesystem::path MakiWrapper::MakiLibcpp2cPath = MakiDir / "build/lib/libcpp2c.so";
std::filesystem::path MakiWrapper::MakiAnalysisScriptPath = MakiDir / "evaluation/analyze_macro_invocations_in_program.py";

} // namespace Hayroll

#endif // HAYROLL_MAKIWRAPPER_HPP
