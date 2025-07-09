#include <iostream>
#include <filesystem>

#include <spdlog/spdlog.h>
#include "json.hpp"

#include "Util.hpp"
#include "TempDir.hpp"
#include "CompileCommand.hpp"
#include "MakiWrapper.hpp"

int main(const int argc, const char* argv[])
{
    using namespace Hayroll;
    using json = nlohmann::json;

    // Take two arguments:
    // 1. Path to the compile_commands.json file.
    // 2. Directory of the target project (sources outside of this directory will not be processed).
    if (argc != 3)
    {
        std::cerr << "Usage: " << argv[0] << " <compile_commands.json path> <project directory>" << std::endl;
        return 1;
    }

    std::filesystem::path compileCommandsJsonPath = argv[1];
    std::filesystem::path projectDir = argv[2];
    compileCommandsJsonPath = std::filesystem::canonical(compileCommandsJsonPath);
    projectDir = std::filesystem::canonical(projectDir);

    std::string compileCommandsJsonStr = loadFileToString(compileCommandsJsonPath);
    json compileCommandsJson = json::parse(compileCommandsJsonStr);
    std::vector<CompileCommand> compileCommands = CompileCommand::fromCompileCommandsJson(compileCommandsJson);

    // MakiWrapper makiWrapper(compileCommands, projectDir);
    // makiWrapper.run();

    return 0;
}
