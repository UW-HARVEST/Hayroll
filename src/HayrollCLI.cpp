#include <iostream>
#include <filesystem>
#include <thread>
#include <optional>

#include <spdlog/spdlog.h>
#include "CLI11.hpp"
#include "json.hpp"

#include "Pipeline.hpp"

int main(const int argc, const char* argv[])
{
    using namespace Hayroll;
    using nlohmann::json;

    std::size_t hardwareThreads = std::thread::hardware_concurrency();
    if (hardwareThreads == 0) hardwareThreads = 2;
    if (hardwareThreads > 16) hardwareThreads = 16; // Limit to 16 threads to avoid memory thrashin

    // Default logging level (can be raised with -v / -vv)
    spdlog::set_level(spdlog::level::info);

    std::filesystem::path compileCommandsJsonPath;
    std::filesystem::path outputDir;
    std::size_t jobs = 0;
    std::filesystem::path projDir;
    std::filesystem::path symbolicMacroWhitelistPath;
    bool enableInline = false;
    int verbose = 0;
    std::string binaryTargetName;

    try
    {
        CLI::App app
        {
            "Hayroll pipeline (supports C2Rust compatibility mode with the 'transpile' subcommand)\n"
            "Patterns:\n 1) hayroll <compile_commands.json> <output_dir> [opts]\n 2) hayroll transpile <compile_commands.json> -o <output_dir> [opts]"
        };
        app.set_help_flag("-h,--help", "Show help");

        // Shared options
        app.add_option("-p,--project-dir", projDir,
            "Project directory (defaults to folder containing compile_commands.json)")
            ->default_str("");
        app.add_option("-w,--whitelist", symbolicMacroWhitelistPath,
            "Path to symbolic macro whitelist json file, which defines which macros are allowed to be symbolically executed")
            ->default_str("");
        app.add_option("-j,--jobs", jobs, "Worker threads")
            ->default_val(hardwareThreads);
        app.add_flag("-i,--inline", enableInline,
            "Enable inline macro expansion")
            ->default_val(false);
        app.add_flag("-v,--verbose", verbose,
            "Increase verbosity (-v=debug, -vv=trace)")
            ->default_val(0);
        app.add_option(
            "-b,--binary",
            binaryTargetName,
            "Emit a Cargo [[bin]] entry using the main() from the specified translation unit "
            "(pass the file name without extension)")
            ->default_str("");

        // Main (default) pattern positionals
        app.add_option("compile_commands", compileCommandsJsonPath, "Path to compile_commands.json");
        app.add_option("output_dir", outputDir, "Output directory");

        // Subcommand: transpile
        std::filesystem::path transpileCompileCommands;
        CLI::App * subTranspile = app.add_subcommand("transpile", "C2Rust compatibility mode (expects <compile_commands.json> and -o)");
        subTranspile->add_option("compile_commands", transpileCompileCommands, "Path to compile_commands.json")
            ->required()
            ->check(CLI::ExistingFile);
        subTranspile->add_option("-o,--output-dir", outputDir,
            "Output directory")
            ->required();

        app.require_subcommand(0, 1);

        try
        {
            app.parse(argc, argv);
        }
        catch (const CLI::Error & e)
        {
            return app.exit(e);
        }

        if (subTranspile->parsed())
        {
            compileCommandsJsonPath = transpileCompileCommands;
        }
        else
        {
            // Normal mode requires two positionals
            if (compileCommandsJsonPath.empty() || outputDir.empty())
            {
                std::cerr << "Error: expected <compile_commands.json> <output_dir>.\n" << app.help() << std::endl;
                return 1;
            }
        }

        // Apply verbosity
        switch (verbose)
        {
            case 1: spdlog::set_level(spdlog::level::debug); break;
            case 2: spdlog::set_level(spdlog::level::trace); break;
            default: spdlog::set_level(spdlog::level::info); break;
        }

        compileCommandsJsonPath = std::filesystem::canonical(compileCommandsJsonPath);
        // Wipe the output directory if it exists
        if (std::filesystem::exists(outputDir))
        {
            std::filesystem::remove_all(outputDir);
        }
        std::filesystem::create_directories(outputDir);
        outputDir = std::filesystem::canonical(outputDir);

        if (!projDir.empty())
        {
            projDir = std::filesystem::canonical(projDir);
        }
        else
        {
            projDir = std::filesystem::canonical(compileCommandsJsonPath.parent_path());
            SPDLOG_INFO("Project directory not given, defaulting to: {}", projDir.string());
        }

        std::optional<std::vector<std::string>> symbolicMacroWhitelist = std::nullopt;
        if (!symbolicMacroWhitelistPath.empty())
        {
            json symbolicMacroWhitelistJson;
            std::ifstream whitelistFile(symbolicMacroWhitelistPath);
            if (!whitelistFile)
            {
                std::cerr << "Failed to open symbolic macro whitelist file: " << symbolicMacroWhitelistPath << std::endl;
                return 1;
            }
            whitelistFile >> symbolicMacroWhitelistJson;
            symbolicMacroWhitelist = symbolicMacroWhitelistJson.get<std::vector<std::string>>();
        }

        std::optional<std::string> binaryTarget = std::nullopt;
        if (!binaryTargetName.empty())
        {
            binaryTarget = binaryTargetName;
        }

        return Pipeline::run
        (
            compileCommandsJsonPath,
            outputDir,
            projDir,
            symbolicMacroWhitelist,
            enableInline,
            jobs,
            binaryTarget
        );
    }
    catch (const std::exception & e)
    {
        std::cerr << "Argument parse error: " << e.what() << std::endl;
        return 1;
    }
}
