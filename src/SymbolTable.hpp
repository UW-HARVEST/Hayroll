#ifndef HAYROLL_SYMBOLTABLE_HPP
#define HAYROLL_SYMBOLTABLE_HPP

#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include <variant>
#include <optional>
#include <sstream>

#include "Util.hpp"
#include "TreeSitter.hpp"
#include "tree_sitter/tree-sitter-c-preproc.h"

namespace Hayroll
{

class SymbolTable;
using SymbolTablePtr = std::shared_ptr<SymbolTable>;
using ConstSymbolTablePtr = std::shared_ptr<const SymbolTable>;

class ObjectSymbol
{
public:
    std::string name;
    std::string spelling;

    std::string expand() const
    {
        return spelling;
    }
};

class FunctionSymbol
{
public:
    std::string name;
    std::vector<std::string> params;
    std::string body;

    std::string expand(const std::vector<std::string> & args) const
    {
        // TODO: Implement with tree-sitter
        return "";
    }
};

class UndefinedSymbol
{
public:
    std::string name;
};

using Symbol = std::variant<ObjectSymbol, FunctionSymbol, UndefinedSymbol>;


// Tree-shaped symbol table
class SymbolTable
    : public std::enable_shared_from_this<SymbolTable>
{
public:
    static SymbolTablePtr make(ConstSymbolTablePtr parent = nullptr)
    {
        auto table = std::make_shared<SymbolTable>();
        table->parent = parent;
        return table;
    }

    void define(Symbol && symbol)
    {
        std::string name = std::visit([](const auto & s) { return s.name; }, symbol);
        symbols[name] = symbol;
    }

    std::optional<const Symbol *> lookup(const std::string & name) const
    {
        auto it = symbols.find(name);
        if (it != symbols.end())
        {
            return &it->second;
        }
        if (parent)
        {
            return parent->lookup(name);
        }
        // Missing: unknown symbol, should create a symbolic value
        return std::nullopt;
    }

    std::string toString() const
    {
        std::stringstream ss;
        for (const auto & [name, symbol] : symbols)
        {
            std::visit
            (
                overloaded
                {
                    [&ss](const ObjectSymbol & s) { ss << s.name << " -> " << s.spelling; },
                    [&ss](const FunctionSymbol & s) { ss << s.name << "(";
                        for (size_t i = 0; i < s.params.size(); i++)
                        {
                            ss << s.params[i];
                            if (i < s.params.size() - 1)
                            {
                                ss << ", ";
                            }
                        }
                        ss << ") -> " << s.body;
                    },
                    [&ss](const UndefinedSymbol & s) { ss << "undefined"; }
                },
                symbol
            );
            ss << "\n";
        }
        ss << "----------------" << "\n";
        if (parent)
        {
            ss << parent->toString();
        }
        return ss.str();
    }

private:
    std::unordered_map<std::string, Symbol> symbols;
    ConstSymbolTablePtr parent;
};

} // namespace Hayroll

#endif // HAYROLL_SYMBOLTABLE_HPP
