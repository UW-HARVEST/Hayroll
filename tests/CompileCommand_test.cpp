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

    std::string libmcsDirStr = LIBMCS_DIR;
    std::filesystem::path libmcsDir(libmcsDirStr);

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
            "directory": ")" + libmcsDirStr + R"(",
            "file": ")" + libmcsDirStr + R"(/libm/mathf/sinhf.c",
            "output": ")" + libmcsDirStr + R"(/build-x86_64-linux-gnu/obj/libm/mathf/sinhf.o"
        }
    ]
    )";
    json compileCommandsJson = json::parse(compileCommandsStr);

    std::vector<CompileCommand> commands = CompileCommand::fromCompileCommandsJson(compileCommandsJson);
    assert(commands.size() == 1);
    CompileCommand &command = commands[0];
    assert(command.directory == libmcsDir);
    assert(command.file == libmcsDir / "libm/mathf/sinhf.c");
    assert(command.output == libmcsDir / "build-x86_64-linux-gnu/obj/libm/mathf/sinhf.o");
    std::vector<std::filesystem::path> includePaths = command.getIncludePaths();
    assert(includePaths.size() == 4);
    assert(includePaths[0] == libmcsDir / "libm/include");
    assert(includePaths[1] == libmcsDir / "libm/common");
    assert(includePaths[2] == libmcsDir / "libm/mathd/internal");
    assert(includePaths[3] == libmcsDir / "libm/mathf/internal");

    return 0;
}
