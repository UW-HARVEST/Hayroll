// Reads Maki's .cpp2c invocation summary file and generates the instrumentation task
// You should first run Maki to get the .cpp2c file

// Overall process: xxxInfo -> InvocationTag -> InstrumentationTask -> awk command
// xxxInfo: Direct mapping from Maki's .ccp2c format.
// InvocationTag: Selected info from xxxInfo to be inserted into C code.
// InstrumentationTask: What to insert (serialized and escaped InvocationTag) and where to insert, in a format that is easy to convert to awk commands.

#include <iostream>
#include <fstream>
#include <filesystem>

#include "SymbolicExecutor.hpp"
#include "LineMatcher.hpp"
#include "Seeder.hpp"
#include "CompileCommand.hpp"
#include "RewriteIncludesWrapper.hpp"
#include "MakiWrapper.hpp"

int main(const int argc, const char* argv[])
{
    using namespace Hayroll;

    spdlog::set_level(spdlog::level::debug);

    TempDir tmpDir(false);
    std::filesystem::path tmpPath = tmpDir.getPath();

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

    std::vector<std::tuple<SymbolicExecutor, std::string, std::vector<std::string>>> tasks;

    SymbolicExecutor executor
    (
        LibmcsDir / "libm/mathf/sinhf.c",
        LibmcsDir,
        {LibmcsDir , LibmcsDir / "libm/include/"}
    );
    std::filesystem::path cuPath = LibmcsDir / "libm/mathf/sinhf.cu.c";

    std::vector<std::filesystem::path> includePaths = command.getIncludePaths();
    
    Warp endWarp = executor.run();
    PremiseTree * premiseTree = executor.scribe.borrowTree();
    IncludeTreePtr includeTree = executor.includeTree;
    const CPreproc & lang = executor.lang;
    
    premiseTree->refine();
    std::cout << "Refined premise tree:\n";
    std::cout << premiseTree->toString() << std::endl;
    
    std::cout << "Include tree:\n";
    std::cout << includeTree->toString() << std::endl;
    
    std::string cuStr = RewriteIncludesWrapper::runRewriteIncludes(command);

    std::cout << "Rewritten source code CU file:\n" << cuStr << std::endl;

    auto [lineMap, inverseLineMap] = LineMatcher::run(cuStr, includeTree, includePaths);
    
    // Copy dstPath to a temporary file
    std::filesystem::path tmpDstPath = tmpPath / "sinhf.cu.c";

    std::string cpp2cStr = MakiWrapper::runCpp2cOnCu(command);
    auto [cpp2cInvocations, cpp2cRanges] = parseCpp2cSummary(cpp2cStr);

    std::cout << "Maki analysis completed." << std::endl;
    std::cout << "cpp2cStr:\n" << cpp2cStr << std::endl;

    auto [output, seedingReport] = Seeder::run(cpp2cInvocations, cpp2cRanges, cuStr, lineMap, inverseLineMap);

    // Save the source file to the temporary directory
    std::ofstream tmpDstFile(tmpDstPath);
    if (!tmpDstFile.is_open())
    {
        throw std::runtime_error("Error: Could not open temporary destination file " + tmpDstPath.string());
    }
    tmpDstFile << output;
    tmpDstFile.close();

    std::cout << "Seeder completed. Instrumented CU file saved to: " << tmpDstPath << std::endl;

    return 0;
}
