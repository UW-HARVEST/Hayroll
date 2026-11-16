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
    )"; // output is optional
    json compileCommandsJson = json::parse(compileCommandsStr);

    std::vector<CompileCommand> commands = CompileCommand::fromCompileCommandsJson(compileCommandsJson);
    assert(commands.size() == 1);
    CompileCommand &command = commands[0];
    assert(command.directory == LibmcsDir);
    assert(command.file == LibmcsDir / "libm/mathf/sinhf.c");
    std::vector<std::filesystem::path> includePaths = command.getIncludePaths();
    assert(includePaths.size() == 5);
    assert(includePaths[0] == LibmcsDir); // Include the command's directory as well.
    assert(includePaths[1] == LibmcsDir / "libm/include");
    assert(includePaths[2] == LibmcsDir / "libm/common");
    assert(includePaths[3] == LibmcsDir / "libm/mathd/internal");
    assert(includePaths[4] == LibmcsDir / "libm/mathf/internal");

    assert(command.getMacroDirectives() == "#define LIBMCS_FPU_DAZ 1\n#define LIBMCS_WANT_COMPLEX 1\n");

    // All -D and -U forms, in command-line order.
    CompileCommand macroCommand = command;
    macroCommand.arguments =
    {
        "/usr/bin/gcc",
        "-c",
        "-DVALUE=3",
        "-D",
        "SPLIT=4",
        "-DEMPTY=",
        "-DFUNC(x)=((x)+1)",
        "-DSTR=\"a=b\"",
        "-UVALUE",
        "-U",
        "SPLIT",
        "sinhf.c"
    };
    assert
    (
        macroCommand.getMacroDirectives() ==
        "#define VALUE 3\n"
        "#define SPLIT 4\n"
        "#define EMPTY \n"
        "#define FUNC(x) ((x)+1)\n"
        "#define STR \"a=b\"\n"
        "#undef VALUE\n"
        "#undef SPLIT\n"
    );

    CompileCommand updatedCommandCu = command
        .withUpdatedFilePathPrefix("/tmp/hayroll", LibmcsDir)
        .withUpdatedFileExtension(".cu.c");
    assert(updatedCommandCu.directory == LibmcsDir); // Directory is unchanged.
    assert(updatedCommandCu.file == "/tmp/hayroll/libm/mathf/sinhf.cu.c");

    CompileCommand updatedCommandSeedCu = updatedCommandCu.withUpdatedFileExtension(".seed.cu.c");
    assert(updatedCommandSeedCu.directory == LibmcsDir); // Directory is unchanged.
    assert(updatedCommandSeedCu.file == "/tmp/hayroll/libm/mathf/sinhf.seed.cu.c");

    return 0;
}
