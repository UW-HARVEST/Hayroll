#include <iostream>

#include <z3++.h>

#include <spdlog/spdlog.h>
#include "json.hpp"

#include "Util.hpp"
#include "SymbolicExecutor.hpp"
#include "LineMatcher.hpp"
#include "TempDir.hpp"
#include "CompileCommand.hpp"
#include "RewriteIncludesWrapper.hpp"

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
    std::vector<CompileCommand> compileCommands = CompileCommand::fromCompileCommandsJson(compileCommandsJson);
    assert(compileCommands.size() == 1);
    const CompileCommand & compileCommand = compileCommands[0];
    std::string cuStr = RewriteIncludesWrapper::runRewriteIncludes(compileCommand);

    TempDir tmpDir(false);
    std::filesystem::path tmpPath = tmpDir.getPath();

    auto saveSource = [&tmpPath](const std::string & source, const std::string & filename) -> std::filesystem::path
    {
        std::filesystem::path srcPath = tmpPath / filename;
        std::ofstream srcFile(srcPath);
        srcFile << source;
        srcFile.close();
        return srcPath;
    };

    std::vector<std::tuple<SymbolicExecutor, std::string, std::vector<std::string>>> tasks;

    tasks.push_back(
        std::tuple<SymbolicExecutor, std::string, std::vector<std::string>>
        (
            SymbolicExecutor
            (
                LibmcsDir / "libm/mathf/sinhf.c",
                LibmcsDir / "",
                {LibmcsDir / "", LibmcsDir / "libm/include/"}
            ),
            cuStr,
            std::vector<std::string>{LibmcsDir / "", LibmcsDir / "libm/include/"}
        )
    );
    
    for (auto &[executor, cuStr, includePathStrs] : tasks)
    {
        std::vector<std::filesystem::path> includePaths(includePathStrs.begin(), includePathStrs.end());

        Warp endWarp = executor.run();
        PremiseTree * premiseTree = executor.scribe.borrowTree();
        IncludeTreePtr includeTree = executor.includeTree;
        const CPreproc & lang = executor.lang;

        premiseTree->refine();
        std::cout << "Refined premise tree:\n";
        std::cout << premiseTree->toString() << std::endl;

        std::cout << "Include tree:\n";
        std::cout << includeTree->toString() << std::endl;

        auto [lineMap, inverseLineMap] = LineMatcher::run(cuStr, includeTree, includePaths);

        std::cout << "Line map:\n";
        for (const auto & [includeTreeNode, lines] : lineMap)
        {
            std::cout << includeTreeNode->stacktrace() << "\n";
            for (size_t i = 0; i < lines.size(); ++i)
            {
                if (lines[i] != 0)
                {
                    std::cout << "    " << i << " -> " << lines[i] << "\n";
                }
            }
        }

        std::cout << "Inverse line map:\n";
        for (size_t i = 1; i < inverseLineMap.size(); ++i)
        {
            const auto &[includeTreeNode, srcLine] = inverseLineMap[i];
            if (srcLine != 0)
            {
                std::cout << i << " -> " << includeTreeNode->stacktrace() << ": " << srcLine << "\n";
            }
        }

        // Check if lineMap and inverseLineMap are consistent
        // Note: this check will not work backwards, i.e. every line in inverseLineMap must be present in lineMap,
        // because -frewrite-includes may introduce extra lines in the compilation unit file. 
        for (const auto & [includeTreeNode, lines] : lineMap)
        {
            for (size_t srcLn = 0; srcLn < lines.size(); ++srcLn)
            {
                int cuLn = lines[srcLn];
                if (cuLn == 0) continue; // Skip empty lines
                auto [expectedIncludeTreeNode, expectedSrcLn] = inverseLineMap[cuLn];
                if (expectedIncludeTreeNode != includeTreeNode || expectedSrcLn != srcLn)
                {
                    throw std::runtime_error
                    (
                        "Inconsistent line mapping: "
                        + includeTreeNode->stacktrace() + ": " + std::to_string(srcLn)
                        + " -> " + std::to_string(cuLn)
                        + " but expected " + expectedIncludeTreeNode->stacktrace() + ": " + std::to_string(expectedSrcLn)
                    );
                }
            }
        }
    }

    return 0;
}
