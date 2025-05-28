#include <iostream>

#include <z3++.h>

#include <spdlog/spdlog.h>

#include "Util.hpp"
#include "SymbolicExecutor.hpp"
#include "LineMatcher.hpp"

int main(int argc, char **argv)
{
    using namespace Hayroll;

    spdlog::set_level(spdlog::level::debug);

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
                "../../libmcs/libm/mathf/sinhf.c",
                "../../libmcs/",
                {"../../libmcs/", "../../libmcs/libm/include/"}
            ),
            "../../libmcs/libm/mathf/sinhf.included.c",
            std::vector<std::string>{"../../libmcs/", "../../libmcs/libm/include/"}
        )
    );
    
    for (auto &[executor, includedFilename, includePathStrs] : tasks)
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

        std::unordered_map<Hayroll::IncludeTreePtr, std::vector<int>> lineMap = LineMatcher::run(includedFilename, includeTree, includePaths);

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
    }

    return 0;
}
