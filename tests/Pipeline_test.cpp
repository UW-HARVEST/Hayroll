#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <optional>
#include <stdexcept>

#include <spdlog/spdlog.h>
#include "json.hpp"

#include "Pipeline.hpp"
#include "TempDir.hpp"
#include "CompileCommand.hpp"
#include "subprocess.hpp"

int main()
{
    using namespace Hayroll;

    spdlog::set_level(spdlog::level::debug);

    std::string testSrcString = R"(
#define EXPR_MACRO_ADD(x, y) ((x) + (y))
#define STMT_MACRO_INCR(x) do { (x)++; } while (0)
#define STMT_MACRO_INCR(x) (x)++
#define STMT_MACRO_DECR(x) (x)--
#define DECL_MACRO_INT int a;
#define STRUCT_ASSIGN .x = 0, .y = 1
#define STMT_BEFORE_RETURN()\
    do {\
    a++;\
    } while (0)

struct Point {
    int x;
    int y;
};

#define HALF_IF if (a < 0x00100000) {

DECL_MACRO_INT;

#if !defined(COND_MACRO_1) && !defined(COND_MACRO_2)
    #error "Either COND_MACRO_1 or COND_MACRO_2 must be defined"
#endif

int main()
{
    struct Point p = {STRUCT_ASSIGN};

    a =
    #ifdef COND_MACRO_1
    1
    #endif
    #ifdef COND_MACRO_2
    2
    #endif
    ;

    #ifdef COND_MACRO_3
    STMT_MACRO_INCR(a);
    #elif defined(COND_MACRO_4)
    STMT_MACRO_DECR(a);
    #endif
    
    #ifndef COND_MACRO_3
    a++;
        #ifdef COND_MACRO_5
        a+=5;
        #endif
        #ifdef COND_MACRO_6
        a+=6;
        #endif
    a++;
    #endif

    int b = EXPR_MACRO_ADD(3, a);

    HALF_IF
        b = 1;
    } else {
        b = 2;
    }

    STMT_BEFORE_RETURN();

    return EXPR_MACRO_ADD(b, 5);
}
)";

    struct TestConfig
    {
        bool keepSrcLoc;
        bool concreteMode;
    };

    for (const auto & [keepSrcLoc, concreteMode] : {TestConfig{false, false}, TestConfig{true, false}, TestConfig{false, true}})
    {
        SPDLOG_INFO("=== Running test with keepSrcLoc = {}, concreteMode = {} ===", keepSrcLoc, concreteMode);

        // Create a temporary project directory and output directory
        TempDir projTmp;
        TempDir outTmp;
        std::filesystem::path projDir = projTmp.getPath();
        std::filesystem::path outDir = outTmp.getPath();

        // Save the test source file into the project directory
        std::filesystem::path srcPath = projDir / "test.c";
        {
            std::ofstream ofs(srcPath);
            ofs << testSrcString;
        }

        std::string compileCommandsStr = R"(
        [
            {
                "arguments": [
                    "/usr/bin/gcc",
                    "-c",
                    "-std=c99",
                    "-O0",
                    "-DCOND_MACRO_1",
                    "-o",
                    "build/test.o",
                    "test.c"
                ],
                "directory": ")" + projDir.string() + R"(",
                "file": ")" + srcPath.string() + R"(",
                "output": ")" + (projDir / "build/test.o").string() + R"("
            }
        ]
        )";

        nlohmann::json compileCommandsJson = nlohmann::json::parse(compileCommandsStr);

        std::filesystem::path compileCommandsPath = projDir / "compile_commands.json";
        {
            std::ofstream ofs(compileCommandsPath);
            ofs << compileCommandsJson.dump(2);
        }

        const std::string binaryName = srcPath.stem().string();

        // Invoke the pipeline
        int returnCode = Hayroll::Pipeline::run
        (
            compileCommandsPath,
            outDir,
            projDir,
            std::nullopt, // symbolicMacroWhitelist
            concreteMode,
            true, // enableInline
            keepSrcLoc,
            1,
            binaryName
        );


        if (returnCode != 0)
        {
            return returnCode;
        }

        // Concrete mode follows one path, so there is exactly one DefineSet and it is empty.
        if (concreteMode)
        {
            const std::string defineSetsStr = loadFileToString(outDir / "src" / "test.defset.txt");
            if (defineSetsStr != "// DefineSet 0\n\n")
            {
                SPDLOG_ERROR("Concrete mode expected one empty DefineSet, got:\n{}", defineSetsStr);
                return 1;
            }
        }

        const std::string outDirStr = outDir.string();

        // Concrete mode builds the C code with -DCOND_MACRO_1 as given, so the Rust code has no Cargo feature for it.
        std::vector<std::string> featureArgs;
        if (!concreteMode) featureArgs = {"--features", "defCOND_MACRO_1"};

        auto runCargoCommand = [&](const std::string & cargoSubcommand, int expectedRet)
        {
            std::vector<std::string> args = {"cargo", cargoSubcommand, "--quiet"};
            args.insert(args.end(), featureArgs.begin(), featureArgs.end());
            subprocess::Popen proc
            (
                args,
                subprocess::output{subprocess::PIPE},
                subprocess::error{subprocess::PIPE},
                subprocess::cwd{outDirStr.c_str()}
            );
            auto [out, err] = proc.communicate();
            int ret = proc.retcode();
            if (ret != expectedRet)
            {
                std::string commandLine;
                for (const std::string & arg : args)
                {
                    if (!commandLine.empty()) commandLine.push_back(' ');
                    commandLine += arg;
                }
                SPDLOG_ERROR("{} failed with exit code {} (expected {}). stdout:\n{}\nstderr:\n{}",
                    commandLine,
                    ret,
                    expectedRet,
                    out.buf.data(),
                    err.buf.data()
                );
                throw std::runtime_error("cargo command failed");
            }
        };

        runCargoCommand("build", 0);
        runCargoCommand("run", 6);

        SPDLOG_INFO("=== Test with keepSrcLoc = {}, concreteMode = {} passed ===", keepSrcLoc, concreteMode);
    }

    return 0;
}
