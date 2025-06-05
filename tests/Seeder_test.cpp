// Reads Maki's .cpp2c invocation summary file and generates the instrumentation task
// You should first run Maki to get the .cpp2c file

// Overall process: xxxInfo -> Tag -> InstrumentationTask -> awk command
// xxxInfo: Direct mapping from Maki's .ccp2c format.
// Tag: Selected info from xxxInfo to be inserted into C code.
// InstrumentationTask: What to insert (serialized and escaped Tag) and where to insert, in a format that is easy to convert to awk commands.

#include "SymbolicExecutor.hpp"
#include "LineMatcher.hpp"
#include "Seeder.hpp"

int main(const int argc, const char* argv[])
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

    SymbolicExecutor executor
    (
        "../../libmcs/libm/mathf/sinhf.c",
        "../../libmcs/",
        {"../../libmcs/", "../../libmcs/libm/include/"}
    );
    std::filesystem::path includedFilename = "../../libmcs/libm/mathf/sinhf.included.c";
    std::vector<std::string> includePathStrs = {"../../libmcs/", "../../libmcs/libm/include/"};

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

    auto [lineMap, inverseLineMap] = LineMatcher::run(includedFilename, includeTree, includePaths);
    
    std::filesystem::path cpp2cFilePath = "../../libmcs/macro_invocation_analyses/all_results.cpp2c";
    std::filesystem::path srcPath = "../../libmcs/libm/mathf/sinhf.c";
    std::filesystem::path dstPath = "../../libmcs/libm/mathf/sinhf.included.c";
    const std::vector<int> & lineMapRootOnly = lineMap[includeTree];

    std::cout << "Line map root only:\n";
    for (int i = 0; i < lineMapRootOnly.size(); ++i)
    {
        std::cout << "    " << i << " -> " << lineMapRootOnly[i] << "\n";
    }

    // Copy dstPath to a temporary file
    std::filesystem::path tmpDstPath = tmpPath / "sinhf.included.c";
    std::filesystem::copy(dstPath, tmpDstPath, std::filesystem::copy_options::overwrite_existing);

    Seeder::run
    (
        cpp2cFilePath,
        srcPath,
        tmpDstPath,
        lineMapRootOnly
    );

    std::cout << "Seeder completed. Instrumentation tasks generated in " << tmpDstPath << std::endl;

    return 0;
}
