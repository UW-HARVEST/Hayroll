#include <iostream>
#include <filesystem>

#include <spdlog/spdlog.h>
#include "json.hpp"

#include "Util.hpp"
#include "TempDir.hpp"
#include "CompileCommand.hpp"
#include "RewriteIncludesWrapper.hpp"
#include "C2RustWrapper.hpp"
#include "LinemarkerEraser.hpp"

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

    std::string cuStr = RewriteIncludesWrapper::runRewriteIncludes(command);
    std::string cuStrNoLm = LinemarkerEraser::run(cuStr);
    std::string result = C2RustWrapper::transpile(cuStrNoLm, command);

    std::cout << "C2Rust output:\n" << result << std::endl;

    return 0;
}
