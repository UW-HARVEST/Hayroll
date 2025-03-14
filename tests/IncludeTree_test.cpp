#include <iostream>
#include <filesystem>
#include <fstream>

#include "subprocess.hpp"

#include "IncludeTree.hpp"

#ifdef CLANG_EXE
const char * clang_exe_path = CLANG_EXE;
#else
const char * clang_exe_path = "clang";
#endif

int main(int argc, char **argv)
{
    using namespace Hayroll::IncludeTree;

    auto tmpPath = std::filesystem::temp_directory_path() / "hayroll_IncTree_test";
    std::filesystem::remove_all(tmpPath);
    std::filesystem::create_directories(tmpPath);
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

    // clang -H -fsyntax-only {srcPath}
    subprocess::Popen proc1
    (
        {clang_exe_path, "-H", "-fsyntax-only", srcPath},
        subprocess::output{subprocess::PIPE},
        subprocess::error{subprocess::PIPE}
    );
    auto [out1, err1] = proc1.communicate();

    std::cout << "Input Include Tree 1: \n" << err1.buf.data() << std::endl;
    std::string_view hierarchy1(err1.buf.data(), err1.length);
    auto root1 = IncludeTree::parseTreeFromString(hierarchy1);
    std::cout << "Parsed Include Tree 1: \n" << root1->toString() << std::endl;

    subprocess::Popen proc2
    (
        {clang_exe_path, "-H", "-fsyntax-only", srcPath, "-DA"},
        subprocess::output{subprocess::PIPE},
        subprocess::error{subprocess::PIPE}
    );
    auto [out2, err2] = proc2.communicate();

    std::cout << "Input Include Tree 2: \n" << err2.buf.data() << std::endl;
    std::string_view hierarchy2(err2.buf.data(), err2.length);
    auto root2 = IncludeTree::parseTreeFromString(hierarchy2);
    std::cout << "Parsed Include Tree 2: \n" << root2->toString() << std::endl;

    root1->merge(std::move(root2));
    std::cout << "Merged Include Tree: \n" << root1->toString() << std::endl;

    // Find includes: nonsense/../test.h, stdio.h, bits/types.h, bits/timesize.h, bits/wordsize.h

    auto node1 = root1->findInclude("nonsense/../test.h").value();
    std::cout << "Found include for nonsense/../test.h: " << node1->path.string() << std::endl;
    auto node2 = node1->findInclude("stdio.h").value();
    std::cout << "Found include for stdio.h: " << node2->path.string() << std::endl;
    auto node3 = node2->findInclude("bits/types.h").value();
    std::cout << "Found include for bits/types.h: " << node3->path.string() << std::endl;
    auto node4 = node3->findInclude("bits/timesize.h").value();
    std::cout << "Found include for bits/timesize.h: " << node4->path.string() << std::endl;
    auto node5 = node4->findInclude("bits/wordsize.h").value();
    std::cout << "Found include for bits/wordsize.h: " << node5->path.string() << std::endl;

    std::filesystem::remove_all(tmpPath);

    return 0;
}
