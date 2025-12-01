#ifndef HAYROLL_C2RUSTWRAPPER_HPP
#define HAYROLL_C2RUSTWRAPPER_HPP

#include <string>
#include <filesystem>
#include <set>
#include <sstream>
#include <map>
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

        // Normalize package/lib metadata for downstream build stability.
        if (!baseToml.contains("package"))
        {
            baseToml["package"] = toml::table{};
        }
        baseToml["package"]["name"] = "hayroll_out";
        baseToml["package"]["authors"] = toml::array{std::string{"Hayroll"}};

        if (!baseToml.contains("lib"))
        {
            baseToml["lib"] = toml::table{};
        }
        baseToml["lib"]["name"] = "hayroll_out";

        return toml::format(baseToml);
    }

    static std::string addFeaturesToCargoToml
    (
        const std::string & cargoToml,
        const std::set<std::string> & rustFeatureAtoms
    )
    {
        if (rustFeatureAtoms.empty()) return cargoToml;

        toml::ordered_value tomlVal = toml::parse_str<toml::ordered_type_config>(cargoToml);

        // Add [features] section if not present
        if (!tomlVal.contains("features"))
        {
            tomlVal["features"] = toml::table{};
        }

        // Ensure "default" feature exists but remains empty
        tomlVal["features"]["default"] = toml::array{};

        // Add each feature atom as its own feature entry if not already present
        for (const auto & atom : rustFeatureAtoms)
        {
            if (!tomlVal["features"].contains(atom))
            {
                tomlVal["features"][atom] = toml::array{};
            }
        }

        return toml::format(tomlVal);
    }

    static std::string genRustToolchainToml()
    {
        const std::string rustToolchainToml =
R"(
[toolchain]
channel = "nightly-2023-04-15"
components = ["rustfmt"]
)";
        return rustToolchainToml;
    }
    
    static std::string genBuildRs()
    {
        const std::string buildRs =
R"(
#[cfg(all(unix, not(target_os = "macos")))]
fn main() {
    // add unix dependencies below
    // println!("cargo:rustc-flags=-l readline");
}

#[cfg(target_os = "macos")]
fn main() {
    // add macos dependencies below
    // println!("cargo:rustc-flags=-l edit");
}
)";
        return buildRs;
    }

    static std::string genLibRs
    (
        const std::filesystem::path & projDir,
        const std::vector<CompileCommand> & compileCommands
    )
    {
        // Header copied as-is
        const char * header =
R"(
#![allow(dead_code)]
#![allow(mutable_transmutes)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(non_upper_case_globals)]
#![allow(unused_assignments)]
#![allow(unused_mut)]
#![feature(register_tool)]
#![register_tool(c2rust)]
#![feature(extern_types)]
#![feature(c_variadic)]
)";

        // Build a directory tree from relative paths of input files
        struct Node
        {
            std::map<std::string, Node> dirs;   // sorted keys
            std::set<std::string> files;        // leaf module names (sorted)
        };

        Node root; // represents the content under src/

        // Deduplicate by normalized relative path (directory + stem)
        std::set<std::filesystem::path> uniqueRelPaths;

        for (const CompileCommand & cmd : compileCommands)
        {
            CompileCommand sanitizedCmd = cmd.withSanitizedPaths(projDir);
            std::filesystem::path rel;
            try
            {
                rel = std::filesystem::relative(sanitizedCmd.file, projDir);
            }
            catch (...)
            {
                // If relative fails (different roots), fall back to filename only
                rel = sanitizedCmd.file.filename();
            }

            // Normalize path (remove extension)
            std::filesystem::path relNoExt = rel;
            relNoExt.replace_extension("");
            uniqueRelPaths.insert(relNoExt);
        }

        for (const auto & relNoExt : uniqueRelPaths)
        {
            Node * cur = &root;
            // Walk components; all but the last are directories; last is the module (file stem)
            std::vector<std::string> comps;
            for (const auto & part : relNoExt)
            {
                comps.push_back(part.string());
            }
            if (comps.empty()) continue;
            for (size_t i = 0; i < comps.size() - 1; ++i)
            {
                std::string dir = comps[i];
                cur = &cur->dirs[dir];
            }
            std::string stem = comps.back();
            cur->files.insert(stem);
        }

        // Emit nested modules under top-level src
        std::ostringstream oss;
        oss << header;
        oss << "pub mod src {\n";

        std::function<void(const Node&, int, const std::string&)> emit = [&](const Node & node, int depth, const std::string & modName)
        {
            auto indent = [&](int d){ return std::string(static_cast<size_t>(d) * 4, ' '); };

            // First emit subdirectories as nested modules
            for (const auto & kv : node.dirs)
            {
                const std::string & name = kv.first;
                const Node & child = kv.second;
                oss << indent(depth) << "pub mod " << name << " {\n";
                emit(child, depth + 1, name);
                oss << indent(depth) << "} // mod " << name << "\n";
            }
            // Then emit leaf files as modules
            for (const auto & fileStem : node.files)
            {
                oss << indent(depth) << "pub mod " << fileStem << ";\n";
            }
        };

        emit(root, 1, "src");
        oss << "} // mod src\n";

        return oss.str();
    }
};

} // namespace Hayroll

#endif // HAYROLL_C2RUSTWRAPPER_HPP
