#include <iostream>

#include <spdlog/spdlog.h>

#include "LinemarkerEraser.hpp"

int main(int argc, char **argv)
{
    using namespace Hayroll;

    std::filesystem::path srcPath = "../../libmcs/libm/mathf/sinhf.included.c";
    std::string source = loadFileToString(srcPath);
    std::string result = LinemarkerEraser::run(source);
    std::cout << "Source:\n" << source << "\n";
    std::cout << "Result:\n" << result << "\n";

    // Check if the result is as expected

    CPreproc lang;
    ASTBank astBank{lang};

    const Hayroll::TSTree & tree = astBank.addAnonymousSource(std::string(result));
    TSNode rootNode = tree.rootNode();

    // Iterate over all linemarkers and replace them with spaces
    for (const TSNode & node : rootNode.iterateDescendants())
    {
        if (node.isSymbol(lang.preproc_line_s))
        {
            std::size_t ln = node.startPoint().row + 1; // Convert to 1-based line number
            std::size_t col = node.startPoint().column + 1; // Convert to 1-based column number
            std::size_t length = node.length();
            std::cout << "Found linemarker at line " << ln << ", column " << col << ", length " << length << "\n";
            return 1;
        }
    }
    return 0;
}
