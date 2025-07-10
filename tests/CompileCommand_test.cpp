#include <iostream>
#include <filesystem>

#include <spdlog/spdlog.h>
#include "json.hpp"

#include "Util.hpp"
#include "TempDir.hpp"
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
                "-DLIBMCS_FPU_DAZ",
                "-DLIBMCS_WANT_COMPLEX",
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

    std::vector<CompileCommand> commands = CompileCommand::fromCompileCommandsJson(compileCommandsJson);
    assert(commands.size() == 1);
    CompileCommand &command = commands[0];
    assert(command.directory == LibmcsDir);
    assert(command.file == LibmcsDir / "libm/mathf/sinhf.c");
    assert(command.output == LibmcsDir / "build-x86_64-linux-gnu/obj/libm/mathf/sinhf.o");
    std::vector<std::filesystem::path> includePaths = command.getIncludePaths();
    assert(includePaths.size() == 5);
    assert(includePaths[0] == LibmcsDir); // Include the command's directory as well.
    assert(includePaths[1] == LibmcsDir / "libm/include");
    assert(includePaths[2] == LibmcsDir / "libm/common");
    assert(includePaths[3] == LibmcsDir / "libm/mathd/internal");
    assert(includePaths[4] == LibmcsDir / "libm/mathf/internal");

    return 0;
}
