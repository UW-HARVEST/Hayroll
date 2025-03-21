#include <iostream>
#include <string>
#include <optional>
#include <variant>

#include "SymbolTable.hpp"

using namespace Hayroll;

int main()
{
    SymbolTablePtr symbolTable = SymbolTable::make();
    symbolTable->define(ObjectSymbol{"x", "1"});
    symbolTable->define(FunctionSymbol{"f", {"x"}, "x + 1"});

    auto x = symbolTable->lookup("x");
    if (x.has_value())
    {
        const Symbol & symbol = *x.value();
        std::cout << std::visit([](const auto & s) { return s.name; }, symbol) << std::endl;
    }
}
