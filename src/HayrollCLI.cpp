#include <iostream>
#include <filesystem>
#include <thread>

#include <spdlog/spdlog.h>
#include "CLI11.hpp"

#include "Pipeline.hpp"

int main(const int argc, const char* argv[])
{
    using namespace Hayroll;

    std::size_t hardwareThreads = std::thread::hardware_concurrency();
    if (hardwareThreads == 0) hardwareThreads = 2;
    if (hardwareThreads > 16) hardwareThreads = 16; // Limit to 16 threads to avoid memory thrashin

    // Default logging level (can be raised with -v / -vv)
    spdlog::set_level(spdlog::level::info);

    std::filesystem::path compileCommandsJsonPath;
    std::filesystem::path outputDir;
    std::size_t jobs = 0;
    std::filesystem::path projDir;
    int verbose = 0;

    try
    {
        CLI::App app
        {
            "Hayroll pipeline (supports C2Rust compatibility mode with the 'transpile' subcommand)\n"
            "Patterns:\n 1) hayroll <compile_commands.json> <output_dir> [opts]\n 2) hayroll transpile <input_folder> -o <output_dir> [opts]"
        };
        app.set_help_flag("-h,--help", "Show help");

        // Shared options
        app.add_option("-p,--project-dir", projDir,
            "Project directory (defaults to folder containing compile_commands.json)")
            ->default_str("");
        app.add_option("-j,--jobs", jobs, "Worker threads")
            ->default_val(hardwareThreads);
        app.add_flag("-v,--verbose", verbose,
            "Increase verbosity (-v=debug, -vv=trace)")
            ->default_val(0);

        // Main (default) pattern positionals
        app.add_option("compile_commands", compileCommandsJsonPath, "Path to compile_commands.json");
        app.add_option("output_dir", outputDir, "Output directory");

        // Subcommand: transpile
        std::filesystem::path transpileInputFolder;
        CLI::App * subTranspile = app.add_subcommand("transpile", "C2Rust compatibility mode (expects <input_folder> and -o)");
        subTranspile->add_option("input_folder", transpileInputFolder, "Input folder containing compile_commands.json")
            ->required()
            ->check(CLI::ExistingDirectory);
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
            std::filesystem::path inputFolder = transpileInputFolder;
            compileCommandsJsonPath = inputFolder / "compile_commands.json";
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

        return Pipeline::run
        (
            compileCommandsJsonPath,
            outputDir,
            projDir,
            jobs
        );
    }
    catch (const std::exception & e)
    {
        std::cerr << "Argument parse error: " << e.what() << std::endl;
        return 1;
    }
}
