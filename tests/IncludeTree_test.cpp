#include <iostream>
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

    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <source file path>" << std::endl;
        return 1;
    }

    std::string srcPath = argv[1];

    // clang -H -fsyntax-only {srcPath}
    subprocess::Popen proc(
        {clang_exe_path, "-H", "-fsyntax-only", srcPath},
        subprocess::output{subprocess::PIPE},
        subprocess::error{subprocess::PIPE}
    );
    auto [out, err] = proc.communicate();

    std::cout << "Input Include Tree : \n" << err.buf.data() << std::endl;
    // Data length
    std::cout << "Data Length : \n" << err.length << std::endl;
    std::string_view line(err.buf.data(), err.length);
    auto root = IncludeTree::parseTreeFromString(line);
    std::cout << "Parsed Include Tree : \n" << root->toString() << std::endl;
    return 0;
}
