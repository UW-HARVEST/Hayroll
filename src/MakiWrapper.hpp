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

// Extra code ranges to require Maki to analyze.
// Used for conditional compilation.
struct CodeRangeAnalysisTask
{
    std::string name;
    int beginLine;
    int beginCol;
    int endLine;
    int endCol;
    std::string extraInfo;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(CodeRangeAnalysisTask, name, beginLine, beginCol, endLine, endCol, extraInfo)
};

class MakiWrapper
{
public:
    static std::filesystem::path MakiDir;
    static std::filesystem::path MakiLibcpp2cPath;
    static std::filesystem::path MakiAnalysisScriptPath;

    // Automatically aggregate each compile command into a single compilation unit file,
    // erase its line markers, and save it to a temporary directory.
    // and then run Maki's cpp2c on it.
    // This gives an absolute line:col number w.r.t. the CU file.
    static std::string runCpp2cOnCu
    (
        const CompileCommand & compileCommand,
        const std::vector<CodeRangeAnalysisTask> & codeRanges = {},
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

        return runCpp2c(newCompileCommand, cuDirPath, codeRanges, numThreads);
    }

private:
    static std::string runCpp2c
    (
        const CompileCommand & compileCommand,
        std::filesystem::path projDir,
        const std::vector<CodeRangeAnalysisTask> & codeRanges = {},
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
        std::filesystem::path codeRangesPath = tempDirPath / "code_ranges.json";
        // Write compile_commands.json to projDir
        saveStringToFile
        (
            CompileCommand::compileCommandsToJson({compileCommand}).dump(4),
            compileCommandsPath
        );
        SPDLOG_TRACE("Saved compile_commands.json to: {}\n content:\n{}",
                     compileCommandsPath.string(),
                     CompileCommand::compileCommandsToJson({compileCommand}).dump(4));

        // Write code ranges to a file if provided
        if (!codeRanges.empty())
        {
            nlohmann::json codeRangesJson = nlohmann::json(codeRanges);
            saveStringToFile(codeRangesJson.dump(4), codeRangesPath);
            SPDLOG_TRACE("Saved code_ranges.json to: {}\n content:\n{}",
                         codeRangesPath.string(),
                         codeRangesJson.dump(4));
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

        if (!codeRanges.empty())
        {
            args.push_back(codeRangesPath.string());
        }

        SPDLOG_TRACE
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
        SPDLOG_TRACE("Maki cpp2c output:\n{}", out.buf.data());
        SPDLOG_TRACE("Maki cpp2c error:\n{}", err.buf.data());

        // Should appear: outputDir/all_results.cpp2c
        // Confirm that the file exists and return its content
        std::filesystem::path cpp2cFilePath = outputDir.getPath() / "all_results.cpp2c";
        if (!std::filesystem::exists(cpp2cFilePath))
        {
            std::ostringstream oss;
            oss << "Maki cpp2c did not produce the expected output file: " << cpp2cFilePath.string()
                << "\nOutput:\n" << out.buf.data()
                << "\nError:\n" << err.buf.data();
            throw std::runtime_error(oss.str());
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
