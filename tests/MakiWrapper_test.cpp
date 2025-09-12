#include <iostream>
#include <filesystem>

#include <spdlog/spdlog.h>
#include "json.hpp"

#include "Util.hpp"
#include "TempDir.hpp"
#include "MakiWrapper.hpp"
#include "CompileCommand.hpp"

int main(int argc, char **argv)
{
    using namespace Hayroll;
    using json = nlohmann::json;

    spdlog::set_level(spdlog::level::debug);

    std::string compileCommandsStr = R"(
    [
        {
            "arguments": [
                "/usr/bin/gcc",
                "-c",
                "-Wall",
                "-std=c99",
                "-pedantic",
                "-Wextra",
                "-frounding-math",
                "-g",
                "-fno-builtin",
                "-Ilibm/include",
                "-Ilibm/common",
                "-Ilibm/mathd/internal",
                "-Ilibm/mathf/internal",
                "-o",
                "build-x86_64-linux-gnu/obj/libm/mathf/sinhf.o",
                "libm/mathf/sinhf.c"
            ],
            "directory": ")" + LibmcsDir.string() + R"(",
            "file": ")" + LibmcsDir.string() + R"(/libm/mathf/sinhf.c",
            "output": ")" + LibmcsDir.string() + R"(/build-x86_64-linux-gnu/obj/libm/mathf/sinhf.o"
        }
    ]
    )";
    json compileCommandsJson = json::parse(compileCommandsStr);

    CodeRangeAnalysisTask codeRangeAnalysisTask = 
    {
        .name = "Test Range",
        .beginLine = 7193,
        .beginCol = 5,
        .endLine = 7193,
        .endCol = 19,
        .extraInfo = "This is a test code range for Maki analysis."
    };
    
    std::vector<CompileCommand> compileCommands = CompileCommand::fromCompileCommandsJson(compileCommandsJson);
    std::vector<CodeRangeAnalysisTask> codeRanges = {codeRangeAnalysisTask};

    std::string cpp2cCuStr = MakiWrapper::runCpp2cOnCu(compileCommands[0], codeRanges);

    std::cout << "Maki cpp2c on CU output:\n" << cpp2cCuStr << std::endl;

    return 0;
}
