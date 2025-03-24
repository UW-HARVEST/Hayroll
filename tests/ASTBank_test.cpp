#include <iostream>

#include "ASTBank.hpp"
#include "IncludeTree.hpp"
#include "IncludeResolver.hpp"
#include "SymbolTable.hpp"
#include "TreeSitter.hpp"
#include "TreeSitterCPreproc.hpp"


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

    IncludeTreePtr includeRoot = IncludeTree::make(0, srcPath);
    IncludeTreePtr includeNode = includeRoot;

    // (isSystemInclude, includeName)
    std::vector<std::pair<bool, std::string>> includes =
    {
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

    CPreproc lang = CPreproc();

    ASTBank astBank(lang);

    for (const auto & [isSystemInclude, includeName] : includes)
    {
        auto ancestorDirs = includeNode->getAncestorDirs();
        auto includePath = resolver.resolveInclude(isSystemInclude, includeName, ancestorDirs);
        std::cout << "Resolved include path: " << includePath << std::endl;
        includeNode->addChild(0, includePath);
        includeNode = includeNode->children[0];
        astBank.addFile(includePath);
    }

    // Go from the last node to the root (excluding the root), printing the included files
    for (auto it = includeNode; it->parent.lock(); it = it->parent.lock())
    {
        std::cout << "Included file: " << it->path << std::endl;
        std::cout << std::flush;
        const auto & [source, ast] = astBank.find(it->path);
        TSNode root = ast.rootNode();
        std::cout << root.sExpression() << std::endl;
        for (TSNode node : root.iterateChildren())
        {
            if (node.symbol() == lang.ifndef_s)
            {
                // std::cout << node.childByFieldName("name").text(source) << std::endl;
                std::cout << node.childByFieldId(lang.ifndef_s.name_f).text(source) << std::endl;
            }
        }
        std::cout << std::endl;
    }

    std::cout << includeRoot->toString() << std::endl;

    return 0;
}