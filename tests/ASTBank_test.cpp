#include <iostream>

#include "tree_sitter/tree-sitter-c-preproc.h"

#include "ASTBank.hpp"
#include "IncludeTree.hpp"
#include "IncludeResolver.hpp"
#include "SymbolTable.hpp"
#include "TreeSitter.hpp"


int main(int argc, char **argv)
{
    using namespace Hayroll;

    IncludeResolver resolver(CLANG_EXE, {});

    auto tmpDir = TempDir();
    auto tmpPath = tmpDir.getPath();

    auto srcPath = tmpPath / "test.c";
    std::ofstream srcFile(srcPath);
    srcFile << "#include <stdio.h>\n";
    srcFile.close();

    IncludeTreePtr root = IncludeTree::make(0, srcPath);
    IncludeTreePtr node = root;

    // (isSystemInclude, includeName)
    std::vector<std::pair<bool, std::string>> includes = {
        { true, "stdio.h" },
        { true, "bits/types.h" },
        { true, "bits/timesize.h" },
        // This is actually a systems include, but just to test parent paths
        { true, "bits/wordsize.h" },
        // Should produce an error internally, but it's ignored as expected
        // /usr/include/x86_64-linux-gnu/bits/typesizes.h:20:3: error: "Never include <bits/typesizes.h> directly; use <sys/types.h> instead."
        // # error "Never include <bits/typesizes.h> directly; use <sys/types.h> instead."
        //   ^
        { true, "bits/typesizes.h" },
    };

    ASTBank astBank(tree_sitter_c_preproc());

    for (const auto & [isSystemInclude, includeName] : includes)
    {
        auto ancestorDirs = node->getAncestorDirs();
        auto includePath = resolver.resolveInclude(isSystemInclude, includeName, ancestorDirs);
        std::cout << "Resolved include path: " << includePath << std::endl;
        node->addChild(0, includePath);
        node = node->children[0];
        astBank.addFile(includePath);
    }

    // Go from the last node to the root (excluding the root), printing the included files
    for (auto it = node; it->parent.lock(); it = it->parent.lock())
    {
        std::cout << "Included file: " << it->path << std::endl;
        std::cout << std::flush;
        const auto & [text, ast] = astBank.find(it->path);
        std::cout << text << std::endl;
        std::cout << ast.rootNode().sExpression() << std::endl;
    }

    std::cout << root->toString() << std::endl;

    return 0;
}