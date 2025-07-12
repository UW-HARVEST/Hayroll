#ifndef HAYROLL_C2RUSTWRAPPER_HPP
#define HAYROLL_C2RUSTWRAPPER_HPP

#include <string>
#include <filesystem>

#include <spdlog/spdlog.h>
#include "subprocess.hpp"
#include "json.hpp"

#include "Util.hpp"
#include "TempDir.hpp"
#include "CompileCommand.hpp"
#include "RewriteIncludesWrapper.hpp"

namespace Hayroll
{

class C2RustWrapper
{
public:
    static std::string runC2Rust
    (
        std::string_view seededCuStr,
        const CompileCommand & compileCommand
    )
    {
        // Example
        // c2rust transpile --output-dir ./c2rust_output ./compile_commands.json
        // --output-dir: Path to output directory. Rust sources will be emitted in DIR/src/ and build files will be emitted in DIR/

        TempDir inputDir;
        std::filesystem::path inputDirPath = inputDir.getPath();
        std::filesystem::path inputFilePath = inputDirPath / "input.seeded.cu.c";
        saveStringToFile(seededCuStr, inputFilePath);

        CompileCommand newCompileCommand = compileCommand
            .withUpdatedDirectory(inputDirPath)
            .withUpdatedFile(inputFilePath);

        TempDir compileCommandsDir;
        // This is fake, just so that C2Rust reads compile_commands.json from the current directory.
        std::filesystem::path compileCommandsDirPath = compileCommandsDir.getPath();
        std::filesystem::path compileCommandsPath = compileCommandsDirPath / "compile_commands.json";
        // Write compile_commands.json to projDir
        saveStringToFile
        (
            CompileCommand::compileCommandsToJson({newCompileCommand}).dump(4),
            compileCommandsPath
        );
        SPDLOG_DEBUG("Saved compile_commands.json to: {}\n content:\n{}",
                     compileCommandsPath.string(),
                     CompileCommand::compileCommandsToJson({newCompileCommand}).dump(4));
        
        TempDir outputDir;
        std::filesystem::path outputDirPath = outputDir.getPath();

        SPDLOG_DEBUG
        (
            "Issuing command: {} {} {} {} {}",
            C2RustExe.string(),
            "transpile",
            compileCommandsPath.string(),
            "--output-dir", outputDirPath.string()
        );

        subprocess::Popen c2rustProc
        (
            {
                C2RustExe.string(),
                "transpile",
                compileCommandsPath.string(),
                "--output-dir", outputDirPath.string()
            },
            subprocess::output{subprocess::PIPE},
            subprocess::error{subprocess::PIPE}
        );

        // Wait for the process to finish
        auto [out, err] = c2rustProc.communicate();

        // Print out the output and error streams
        SPDLOG_DEBUG("C2Rust stdout:\n{}", out.buf.data());
        SPDLOG_DEBUG("C2Rust stderr:\n{}", err.buf.data());

        // Should appear: outputDir/src/input.seeded.cu.rs
        // Confirm that the file exists and return its content
        std::filesystem::path rustFilePath = outputDirPath / "src/input_seeded_cu.rs"; // It replaces '.' with '_'
        if (!std::filesystem::exists(rustFilePath))
        {
            throw std::runtime_error("C2Rust did not produce the expected output file: " + rustFilePath.string());
        }

        return loadFileToString(rustFilePath);
    }
};

} // namespace Hayroll

#endif // HAYROLL_C2RUSTWRAPPER_HPP
