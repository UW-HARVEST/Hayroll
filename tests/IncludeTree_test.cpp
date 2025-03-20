#include <iostream>
#include <filesystem>
#include <fstream>

#include <spdlog/spdlog.h>
#include "subprocess.hpp"

#include "IncludeTree.hpp"
#include "TempDir.hpp"
#include "IncludeResolver.hpp"

#ifdef CLANG_EXE
const char * clang_exe_path = CLANG_EXE;
#else
const char * clang_exe_path = "clang";
#endif

int main(int argc, char **argv)
{
    using namespace Hayroll;

    spdlog::set_level(spdlog::level::debug);

    IncludeResolver resolver(clang_exe_path, {});

    std::cout << "Predefined macros:\n" << resolver.getPredefinedMacros() << std::endl;

    auto tmpDir = TempDir();
    auto tmpPath = tmpDir.getPath();
    std::filesystem::create_directories(tmpPath / "nonsense");

    auto headerPath = tmpPath / "test.h";
    std::ofstream headerFile(headerPath);
    headerFile << "#include <stdio.h>\n";
    headerFile.close();

    auto srcPath = tmpPath / "test.c";
    std::ofstream srcFile(srcPath);
    srcFile << "#ifdef A\n"
            "#include \"test.h\"\n"
            "#else\n"
            "#include \"nonsense/../test.h\"\n"
            "#endif\n";
    srcFile.close();

    auto root = IncludeTree::make(0, srcPath);
    auto node = root;

    // (isSystemInclude, includeName)
    std::vector<std::pair<bool, std::string>> includes = {
        { false, "nonsense/../test.h" },
        { true, "stdio.h" },
        { true, "bits/types.h" },
        { true, "bits/timesize.h" },
        { false, "bits/wordsize.h" }, // This is wrong, but just testing parent paths
    };

    for (const auto& [isSystemInclude, includeName] : includes)
    {
        auto ancestorDirs = node->getAncestorDirs();
        std::cout << "Ancestor dirs: \n";
        for (const auto& dir : ancestorDirs)
        {
            std::cout << dir << "\n";
        }
        auto includePath = resolver.resolveInclude(isSystemInclude, includeName, ancestorDirs);
        std::cout << "Resolved include path: " << includePath << std::endl;
        node->addChild(0, includePath);
        node = node->children[0];
    }

    std::cout << root->toString() << std::endl;

    return 0;
}
