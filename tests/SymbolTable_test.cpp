#include <iostream>
#include <string>
#include <optional>
#include <variant>
#include <memory>

#include <spdlog/spdlog.h>

#include "TreeSitter.hpp"
#include "TreeSitterCPreproc.hpp"
#include "SymbolTable.hpp"
#include "IncludeResolver.hpp"

#ifdef CLANG_EXE
const char * clang_exe_path = CLANG_EXE;
#else
const char * clang_exe_path = "clang";
#endif

int main()
{
    using namespace Hayroll;

    spdlog::set_level(spdlog::level::debug);

    IncludeTreePtr includeTree = IncludeTree::make(TSNode{}, "<PREDEFINED_MACROS>");
    SymbolTablePtr symbolTable = SymbolTable::make();
    IncludeResolver resolver(clang_exe_path, {});
    CPreproc lang = CPreproc();
    TSParser parser(lang);

    std::string predefinedMacros = resolver.getPredefinedMacros();

    std::cout << "Predefined macros:\n" << predefinedMacros << std::endl;
    
    TSTree tree = parser.parseString(predefinedMacros);

    // Walk the tree and add the macros to the symbol table
    // The tree will be like:
    // translation_unit [0, 0] - [358, 0]
    //     preproc_def [0, 0] - [1, 0]
    //         name: identifier [0, 8] - [0, 13]
    //         value: preproc_tokens [0, 14] - [0, 15]
    //             token: number_literal [0, 14] - [0, 15]
    //     preproc_def [1, 0] - [2, 0]
    //         name: identifier [1, 8] - [1, 24]
    //         value: preproc_tokens [1, 25] - [1, 26]
    //             token: number_literal [1, 25] - [1, 26]

    for (TSNode node : tree.rootNode().iterateChildren())
    {
        assert(node.isNamed());

        TSNode nameNode = node.childByFieldId(lang.preproc_def_s.name_f);

        assert(!nameNode.isNull());
        std::string_view name = nameNode.textView();

        // valueNode can be an invalid node, but wo store it as-is
        symbolTable = symbolTable->define(ObjectSymbol{name, {includeTree, node}});
    }

    std::cout << symbolTable->toString() << std::endl;

    return 0;
}
