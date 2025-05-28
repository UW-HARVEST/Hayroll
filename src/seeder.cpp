// Reads Maki's .cpp2c invocation summary file and generates the instrumentation task
// You should first run Maki to get the .cpp2c file

// Overall process: xxxInfo -> Tag -> InstrumentationTask -> awk command
// xxxInfo: Direct mapping from Maki's .ccp2c format.
// Tag: Selected info from xxxInfo to be inserted into C code.
// InstrumentationTask: What to insert (serialized and escaped Tag) and where to insert, in a format that is easy to convert to awk commands.

#include "Seeder.hpp"

int main(const int argc, const char* argv[])
{
    using namespace Hayroll;

    // Take the first argument as the .cpp2c invocation summary file. Check if it is provided.
    // Take the second argument as the source path. Check if it is provided.
    //     Other files other than the souce path will not be instrumented.
    std::string cpp2cFilePathStr;
    std::string projectRootDirStr;

    if (argc != 3)
    {
        std::cerr << "Usage: " << argv[0] << " <cpp2c invocation summary file> <source path>" << std::endl;
        return 1;
    }
    cpp2cFilePathStr = argv[1];
    projectRootDirStr = argv[2];

    std::filesystem::path srcPath(projectRootDirStr);
    srcPath = std::filesystem::canonical(srcPath);

    std::filesystem::path cpp2cFilePath(cpp2cFilePathStr);
    cpp2cFilePath = std::filesystem::canonical(cpp2cFilePath);

    Seeder::run(cpp2cFilePath, srcPath, srcPath);

    return 0;
}
