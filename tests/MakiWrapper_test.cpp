#include <iostream>
#include <filesystem>

#include <spdlog/spdlog.h>

#include "Util.hpp"
#include "TempDir.hpp"
#include "MakiWrapper.hpp"
#include "CompileCommand.hpp"

int main(int argc, char **argv)
{
    using namespace Hayroll;

    spdlog::set_level(spdlog::level::debug);

    std::filesystem::path libmcsDir(LIBMCS_DIR);
    libmcsDir = std::filesystem::canonical(libmcsDir);
    
    std::filesystem::path compileCommandsJsonPath = libmcsDir / "compile_commands.json";
    if (!std::filesystem::exists(compileCommandsJsonPath))
    {
        std::cerr << "Error: compile_commands.json not found at " << compileCommandsJsonPath.string() << std::endl;
        return 1;
    }
    std::string compileCommandsJsonStr = loadFileToString(compileCommandsJsonPath);
    nlohmann::json compileCommandsJson = nlohmann::json::parse(compileCommandsJsonStr);
    std::vector<CompileCommand> compileCommands = CompileCommand::fromCompileCommandsJson(compileCommandsJson);

    std::string cpp2cStr = MakiWrapper::runCpp2c
    (
        compileCommands,
        libmcsDir,
        16
    );

    std::cout << "Maki cpp2c output:\n" << cpp2cStr << std::endl;

    return 0;
}
