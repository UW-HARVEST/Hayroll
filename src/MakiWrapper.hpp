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

namespace Hayroll
{

class MakiWrapper
{
public:
    static std::filesystem::path MakiDir;
    static std::filesystem::path MakiLibcpp2cPath;
    static std::filesystem::path MakiAnalysisScriptPath;

    static std::string runCpp2c
    (
        const std::vector<CompileCommand> & compileCommands,
        std::filesystem::path projDir,
        int numThreads = 16
    )
    {
        // Example
        // ../Maki/evaluation/analyze_macro_invocations_in_program.py "../Maki/build/lib/libcpp2c.so" "./compile_commands.json" "./" "./macro_invocation_analyses/" 16
        // <script> <cpp2c.so> <compileCommandsJsonPath> <projDir> <outputDir> <numThreads>

        TempDir compileCommandsDir;
        // This is fake, just so that Maki reads compile_commands.json from the current directory.
        std::filesystem::path compileCommandsDirPath = compileCommandsDir.getPath();
        std::filesystem::path compileCommandsPath = compileCommandsDirPath / "compile_commands.json";
        // Write compile_commands.json to projDir
        saveStringToFile
        (
            CompileCommand::compileCommandsToJson(compileCommands).dump(4),
            compileCommandsPath
        );
        
        projDir = std::filesystem::canonical(projDir);
        
        TempDir outputDir;

        SPDLOG_DEBUG
        (
            "Issuing command: {} {} {} {} {} {}",
            MakiAnalysisScriptPath.string(),
            MakiLibcpp2cPath.string(),
            compileCommandsPath.string(),
            projDir.string(),
            outputDir.getPath().string(),
            std::to_string(numThreads)
        );
        subprocess::Popen cpp2c
        (
            {
                MakiAnalysisScriptPath.string(),
                MakiLibcpp2cPath.string(),
                compileCommandsPath.string(),
                projDir.string(),
                outputDir.getPath().string(),
                std::to_string(numThreads)
            },
            subprocess::output{subprocess::PIPE},
            subprocess::error{subprocess::PIPE}
        );

        // Wait for the process to finish
        cpp2c.communicate();

        // Should appear: outputDir/all_results.cpp2c
        // Confirm that the file exists and return its content
        std::filesystem::path cpp2cFilePath = outputDir.getPath() / "all_results.cpp2c";
        if (!std::filesystem::exists(cpp2cFilePath))
        {
            throw std::runtime_error("Maki cpp2c did not produce the expected output file: " + cpp2cFilePath.string());
        }

        return loadFileToString(cpp2cFilePath);
    }
};

std::filesystem::path MakiWrapper::MakiDir = MAKI_DIR;
std::filesystem::path MakiWrapper::MakiLibcpp2cPath = MakiDir / "build/lib/libcpp2c.so";
std::filesystem::path MakiWrapper::MakiAnalysisScriptPath = MakiDir / "evaluation/analyze_macro_invocations_in_program.py";

} // namespace Hayroll

#endif // HAYROLL_MAKIWRAPPER_HPP
