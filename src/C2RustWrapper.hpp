#ifndef HAYROLL_C2RUSTWRAPPER_HPP
#define HAYROLL_C2RUSTWRAPPER_HPP

#include <string>
#include <filesystem>
#include <set>
#include <sstream>
#include <tuple>

#include <spdlog/spdlog.h>
#include "subprocess.hpp"
#include "json.hpp"
#include "toml.hpp"

#include "Util.hpp"
#include "TempDir.hpp"
#include "CompileCommand.hpp"
#include "RewriteIncludesWrapper.hpp"
#include "LinemarkerEraser.hpp"

namespace Hayroll
{

class C2RustWrapper
{
public:
    // Call C2Rust to transpile a single seeded CU string
    // Return the transpiled Rust string and the corresponding Cargo.toml content
    static std::tuple<std::string, std::string> transpile
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
        std::string seededCuNolmStr = LinemarkerEraser::run(seededCuStr);
        saveStringToFile(seededCuNolmStr, inputFilePath);

        CompileCommand newCompileCommand = compileCommand
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
        SPDLOG_TRACE("Saved compile_commands.json to: {}\n content:\n{}",
                     compileCommandsPath.string(),
                     CompileCommand::compileCommandsToJson({newCompileCommand}).dump(4));
        
        TempDir outputDir;
        std::filesystem::path outputDirPath = outputDir.getPath();

        SPDLOG_TRACE
        (
            "Issuing command: {} {} {} {} {}",
            C2RustExe.string(),
            "transpile",
            "--reorganize-definitions",
            "--emit-build-files",
            compileCommandsPath.string(),
            "--output-dir", outputDirPath.string()
        );

        subprocess::Popen c2rustProc
        (
            {
                C2RustExe.string(),
                "transpile",
                "--reorganize-definitions",
                "--emit-build-files",
                compileCommandsPath.string(),
                "--output-dir", outputDirPath.string()
            },
            subprocess::output{subprocess::PIPE},
            subprocess::error{subprocess::PIPE}
        );

        // Wait for the process to finish
        auto [out, err] = c2rustProc.communicate();

        // Print out the output and error streams
        SPDLOG_TRACE("C2Rust stdout:\n{}", out.buf.data());
        SPDLOG_TRACE("C2Rust stderr:\n{}", err.buf.data());

        // Should appear: outputDir/src/input.seeded.cu.rs
        // Confirm that the file exists and return its content
        std::filesystem::path rustFilePath = outputDirPath / "src/input_seeded_cu.rs"; // It replaces '.' with '_'
        if (!std::filesystem::exists(rustFilePath))
        {
            std::ostringstream oss;
            oss << "C2Rust did not produce the expected output file: " << rustFilePath.string()
                << "\nOutput:\n" << out.buf.data()
                << "\nError:\n" << err.buf.data();
            throw std::runtime_error(oss.str());
        }

        std::string rustCode = loadFileToString(rustFilePath);
        std::string cargoToml = loadFileToString(outputDirPath / "Cargo.toml");
        return {rustCode, cargoToml};
    }

    static std::string mergeCargoTomls(const std::vector<std::string> & cargoTomls)
    {
        if (cargoTomls.empty()) return "";

        // Parse the first Cargo.toml as the base
        toml::ordered_value baseToml = toml::parse_str<toml::ordered_type_config>(cargoTomls[0]);

        for (size_t i = 1; i < cargoTomls.size(); ++i)
        {
            toml::ordered_value nextToml = toml::parse_str<toml::ordered_type_config>(cargoTomls[i]);

            // Merge [dependencies]
            if (nextToml.contains("dependencies"))
            {
                if (!baseToml.contains("dependencies"))
                {
                    baseToml["dependencies"] = toml::table{};
                }
                for (const auto& [key, value] : nextToml["dependencies"].as_table())
                {
                    baseToml["dependencies"][key] = value;
                }
            }
        }

        return toml::format(baseToml);
    }

    // Call C2Rust with --emit-build-files to generate build files
    // Modify lib.rs so it corresponds to Hayroll's output structure
    // Modify Cargo.toml so it contains additional features
    // build.rs, Cargo.toml, lib.rs, rust-toolchain.toml
    static std::tuple<std::string, std::string, std::string, std::string> generateBuildFiles
    (
        const std::vector<CompileCommand> & compileCommands,
        const std::set<std::string> & additionalFeatures = {}
    )
    {
        // Note: Simple version as requested: just dump all compile commands to a json file,
        // call c2rust once with --emit-build-files, and then read the four files
        // from the output directory root without any modification.

        // Prepare a temporary directory to host compile_commands.json
        TempDir compileCommandsDir;
        std::filesystem::path compileCommandsDirPath = compileCommandsDir.getPath();
        std::filesystem::path compileCommandsPath = compileCommandsDirPath / "compile_commands.json";

        // Serialize compile commands directly to JSON
        // Using the intrusive nlohmann::json converters defined in CompileCommand
        nlohmann::json jsonCommands = CompileCommand::compileCommandsToJson(compileCommands);
        saveStringToFile(jsonCommands.dump(4), compileCommandsPath);
        SPDLOG_TRACE("Saved compile_commands.json to: {}\n content:\n{}",
                     compileCommandsPath.string(), jsonCommands.dump(4));

        // Prepare output directory
        TempDir outputDir;
        std::filesystem::path outputDirPath = outputDir.getPath();

        SPDLOG_TRACE
        (
            "Issuing command: {} {} {} {} {} {} {}",
            C2RustExe.string(),
            "transpile",
            "--reorganize-definitions",
            "--emit-build-files",
            compileCommandsPath.string(),
            "--output-dir",
            outputDirPath.string()
        );

        // Invoke c2rust
        subprocess::Popen c2rustProc
        (
            {
                C2RustExe.string(),
                "transpile",
                "--reorganize-definitions",
                "--emit-build-files",
                compileCommandsPath.string(),
                "--output-dir", outputDirPath.string()
            },
            subprocess::output{subprocess::PIPE},
            subprocess::error{subprocess::PIPE}
        );

        auto [out, err] = c2rustProc.communicate();
        SPDLOG_TRACE("C2Rust stdout:\n{}", out.buf.data());
        SPDLOG_TRACE("C2Rust stderr:\n{}", err.buf.data());

        // Expected files at the output directory root (simple version)
        std::filesystem::path buildRsPath = outputDirPath / "build.rs";
        std::filesystem::path cargoTomlPath = outputDirPath / "Cargo.toml";
        std::filesystem::path libRsPath = outputDirPath / "lib.rs";
        std::filesystem::path rustToolchainTomlPath = outputDirPath / "rust-toolchain.toml";

        // Verify existence and load contents
        auto requireFile = [&](const std::filesystem::path & p) -> std::string
        {
            if (!std::filesystem::exists(p))
            {
                std::ostringstream oss;
                oss << "C2Rust did not produce the expected output file: " << p.string()
                    << "\nOutput:\n" << out.buf.data()
                    << "\nError:\n" << err.buf.data();
                throw std::runtime_error(oss.str());
            }
            return loadFileToString(p);
        };

        std::string buildRs = requireFile(buildRsPath);
        std::string cargoToml = requireFile(cargoTomlPath);
        std::string libRs = requireFile(libRsPath);
        std::string rustToolchainToml = requireFile(rustToolchainTomlPath);

        return {buildRs, cargoToml, libRs, rustToolchainToml};
    }
};

} // namespace Hayroll

#endif // HAYROLL_C2RUSTWRAPPER_HPP
