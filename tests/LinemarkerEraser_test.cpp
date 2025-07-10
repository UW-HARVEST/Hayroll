#include <iostream>

#include <spdlog/spdlog.h>
#include "json.hpp"

#include "RewriteIncludesWrapper.hpp"
#include "LinemarkerEraser.hpp"

int main(int argc, char **argv)
{
    using namespace Hayroll;
    using json = nlohmann::json;

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
    std::vector<CompileCommand> compileCommands = CompileCommand::fromCompileCommandsJson(compileCommandsJson);
    assert(compileCommands.size() == 1);
    const CompileCommand & compileCommand = compileCommands[0];

    std::string cuStr = RewriteIncludesWrapper::runRewriteIncludes(compileCommand);
    std::string result = LinemarkerEraser::run(cuStr);
    std::cout << "CU Source:\n" << cuStr << "\n";
    std::cout << "Result:\n" << result << "\n";

    // Check if the result is as expected

    CPreproc lang;
    ASTBank astBank{lang};

    const Hayroll::TSTree & tree = astBank.addAnonymousSource(std::string(result));
    TSNode rootNode = tree.rootNode();

    // Iterate over all linemarkers and replace them with spaces
    for (const TSNode & node : rootNode.iterateDescendants())
    {
        if (node.isSymbol(lang.preproc_line_s))
        {
            std::size_t ln = node.startPoint().row + 1; // Convert to 1-based line number
            std::size_t col = node.startPoint().column + 1; // Convert to 1-based column number
            std::size_t length = node.length();
            std::cout << "Found linemarker at line " << ln << ", column " << col << ", length " << length << "\n";
            return 1;
        }
    }
    return 0;
}
