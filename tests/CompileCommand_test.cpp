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
    json compileCommandsJson = R"(
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
                "libm/mathf/sinhf.seed.cu.c"
            ],
            "directory": "/home/husky/libmcs",
            "file": "/home/husky/libmcs/libm/mathf/sinhf.seed.cu.c",
            "output": "/home/husky/libmcs/build-x86_64-linux-gnu/obj/libm/mathf/sinhf.o"
        }
    ]
    )"_json;

    std::vector<CompileCommand> commands = CompileCommand::fromCompileCommandsJson(compileCommandsJson);
    assert(commands.size() == 1);
    CompileCommand &command = commands[0];    
    assert(command.directory == "/home/husky/libmcs");
    assert(command.file == "/home/husky/libmcs/libm/mathf/sinhf.seed.cu.c");
    assert(command.output == "/home/husky/libmcs/build-x86_64-linux-gnu/obj/libm/mathf/sinhf.o");
    std::vector<std::filesystem::path> includePaths = command.getIncludePaths();
    assert(includePaths.size() == 4);
    assert(includePaths[0] == "/home/husky/libmcs/libm/include");
    assert(includePaths[1] == "/home/husky/libmcs/libm/common");
    assert(includePaths[2] == "/home/husky/libmcs/libm/mathd/internal");
    assert(includePaths[3] == "/home/husky/libmcs/libm/mathf/internal");

    return 0;
}
