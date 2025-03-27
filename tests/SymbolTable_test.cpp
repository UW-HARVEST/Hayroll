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

    SymbolTablePtr symbolTable = SymbolTable::make();
    symbolTable->define(ObjectSymbol{"x", "1"});
    symbolTable->define(FunctionSymbol{"f", {"x"}, "x + 1"});

    auto x = symbolTable->lookup("x");
    if (x.has_value())
    {
        const Symbol & symbol = *x.value();
        std::cout << std::visit([](const auto & s) { return s.name; }, symbol) << std::endl;
    }

    // Parse the predefined macros

    IncludeResolver resolver(clang_exe_path, {});

    TSParser parser{CPreproc()};

    std::string predefinedMacros = resolver.getPredefinedMacros();

    std::cout << "Predefined macros:\n" << predefinedMacros << std::endl;
    
    TSTree tree = parser.parseString(predefinedMacros);

    // Walk the tree and add the macros to the symbol table
    // The tree will be like:
    // translation_unit [0, 0] - [2, 0]
    //     preproc_def [0, 0] - [1, 0]
    //         #define [0, 0] - [0, 7]
    //         name: identifier [0, 8] - [0, 28]
    //         value: preproc_arg [0, 29] - [0, 51]
    //     preproc_def [1, 0] - [2, 0]
    //         #define [1, 0] - [1, 7]
    //         name: identifier [1, 8] - [1, 29]
    //         value: preproc_arg [1, 30] - [1, 47]

    for (TSNode node : tree.rootNode().iterateChildren())
    {
        assert(node.isNamed());

        TSNode nameNode = node.childByFieldName("name");
        TSNode valueNode = node.childByFieldName("value");

        assert(!nameNode.isNull());
        std::string name = nameNode.text();

        std::string value;
        if (!valueNode.isNull())
        {
            value = valueNode.text();
        }

        symbolTable->define(ObjectSymbol{name, value});
    }

    std::cout << symbolTable->toString() << std::endl;

    return 0;
}
