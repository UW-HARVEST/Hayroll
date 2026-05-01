#include <cassert>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <stdexcept>
#include <string>

#include <spdlog/spdlog.h>
#include "json.hpp"

#include "Pipeline.hpp"
#include "TempDir.hpp"
#include "subprocess.hpp"

int main()
{
    using namespace Hayroll;

    spdlog::set_level(spdlog::level::debug);

    TempDir projTmp;
    TempDir outTmp;
    const std::filesystem::path projDir = projTmp.getPath();
    const std::filesystem::path outDir = outTmp.getPath();

    const std::filesystem::path includeOneDir = projDir / "config_one";
    const std::filesystem::path includeTwoDir = projDir / "config_two";
    std::filesystem::create_directories(includeOneDir);
    std::filesystem::create_directories(includeTwoDir);

    Hayroll::saveStringToFile("#define CONFIG_VALUE 11\n", includeOneDir / "config_value.h");
    Hayroll::saveStringToFile("#define CONFIG_VALUE 22\n", includeTwoDir / "config_value.h");

    const std::filesystem::path srcPath = projDir / "main.c";
    Hayroll::saveStringToFile(
R"(#include "config_value.h"

#if !defined(CFG_ONE) && !defined(CFG_TWO)
#error "Either CFG_ONE or CFG_TWO must be defined"
#endif

int main(void)
{
    return CONFIG_VALUE;
}
)",
        srcPath
    );

    const std::string compileCommandsStr = R"(
    [
        {
            "arguments": [
                "/usr/bin/gcc",
                "-c",
                "-std=c99",
                "-O0",
                "-DCFG_ONE",
                "-I)" + includeOneDir.string() + R"(",
                "-o",
                "build/main-one.o",
                "main.c"
            ],
            "directory": ")" + projDir.string() + R"(",
            "file": ")" + srcPath.string() + R"(",
            "output": ")" + (projDir / "build/main-one.o").string() + R"("
        },
        {
            "arguments": [
                "/usr/bin/gcc",
                "-c",
                "-std=c99",
                "-O2",
                "-DCFG_TWO",
                "-I)" + includeTwoDir.string() + R"(",
                "-o",
                "build/main-two.o",
                "main.c"
            ],
            "directory": ")" + projDir.string() + R"(",
            "file": ")" + srcPath.string() + R"(",
            "output": ")" + (projDir / "build/main-two.o").string() + R"("
        }
    ]
    )";

    const nlohmann::json compileCommandsJson = nlohmann::json::parse(compileCommandsStr);
    const std::filesystem::path compileCommandsPath = projDir / "compile_commands.json";
    Hayroll::saveStringToFile(compileCommandsJson.dump(2), compileCommandsPath);

    const int returnCode = Hayroll::Pipeline::run(
        compileCommandsPath,
        outDir,
        projDir,
        std::nullopt,
        true,
        false,
        1,
        std::string("main")
    );
    if (returnCode != 0)
    {
        return returnCode;
    }

    const std::filesystem::path variant0PremiseTree = outDir / "src/main.variant0.premise_tree.txt";
    const std::filesystem::path variant1PremiseTree = outDir / "src/main.variant1.premise_tree.txt";
    const std::filesystem::path variant0Cu = outDir / "src/main.variant0.0.cu.c";
    const std::filesystem::path variant1Cu = outDir / "src/main.variant1.0.cu.c";
    assert(std::filesystem::exists(variant0PremiseTree));
    assert(std::filesystem::exists(variant1PremiseTree));
    assert(std::filesystem::exists(variant0Cu));
    assert(std::filesystem::exists(variant1Cu));

    const std::string variant0CuStr = Hayroll::loadFileToString(variant0Cu);
    const std::string variant1CuStr = Hayroll::loadFileToString(variant1Cu);
    assert(variant0CuStr.contains("11"));
    assert(variant1CuStr.contains("22"));
    assert(variant0CuStr != variant1CuStr);

    const std::string outDirStr = outDir.string();
    const auto runCargoCommand =
        [&outDirStr](const std::initializer_list<std::string> & args, int expectedRet)
        {
            subprocess::Popen proc(
                args,
                subprocess::output{subprocess::PIPE},
                subprocess::error{subprocess::PIPE},
                subprocess::cwd{outDirStr.c_str()}
            );
            auto [out, err] = proc.communicate();
            const int ret = proc.retcode();
            if (ret != expectedRet)
            {
                std::string commandLine;
                for (const std::string & arg : args)
                {
                    if (!commandLine.empty()) commandLine.push_back(' ');
                    commandLine += arg;
                }
                SPDLOG_ERROR(
                    "{} failed with exit code {} (expected {}). stdout:\n{}\nstderr:\n{}",
                    commandLine,
                    ret,
                    expectedRet,
                    out.buf.data(),
                    err.buf.data()
                );
                throw std::runtime_error("cargo command failed");
            }
        };

    runCargoCommand({"cargo", "build", "--quiet", "--features", "defCFG_ONE"}, 0);
    runCargoCommand({"cargo", "run", "--quiet", "--features", "defCFG_ONE"}, 11);

    return 0;
}
