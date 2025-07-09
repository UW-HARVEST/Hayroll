#include <iostream>
#include <filesystem>

#include <spdlog/spdlog.h>
#include "json.hpp"

#include "Util.hpp"
#include "TempDir.hpp"
#include "CompileCommand.hpp"
#include "RewriteIncludesWrapper.hpp"
#include "TreeSitterCPreproc.hpp"
#include "ASTBank.hpp"

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

    std::string cuStr = RewriteIncludesWrapper::runRewriteIncludes(command);

    std::cout << "Rewritten includes output:\n" << cuStr << std::endl;

    // TreeSitterCPreproc should be able to parse the output.
    CPreproc lang;
    ASTBank astBank{lang};
    const TSTree & tree = astBank.addAnonymousSource(std::move(cuStr));

    // Print out all #include directives in the preprocessed output.
    std::cout << "All #include in this compilation unit:\n";
    for (const TSNode & node : tree.rootNode().iterateDescendants())
    {
        if (node.isSymbol(lang.preproc_include_s))
        {
            std::cout << node.textView() << std::endl;
        }
    }

    return 0;
}
