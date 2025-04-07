// A chained hashmap symbol table that holds macro difinitions

#ifndef HAYROLL_SYMBOLTABLE_HPP
#define HAYROLL_SYMBOLTABLE_HPP

#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include <variant>
#include <optional>
#include <sstream>

#include <spdlog/spdlog.h>

#include "TreeSitter.hpp"
#include "Util.hpp"

namespace Hayroll
{

class SymbolTable;
using SymbolTablePtr = std::shared_ptr<SymbolTable>;
using ConstSymbolTablePtr = std::shared_ptr<const SymbolTable>;

// Object-like macro symbols, e.g. #define HAYROLL 1
class ObjectSymbol
{
public:
    std::string name;
    TSNode body; // preproc_tokens, can be a null node
};

// Function-like macro symbols, e.g. #define HAYROLL(x) x + 1
class FunctionSymbol
{
public:
    std::string name;
    std::vector<std::string> params;
    TSNode body; // preproc_tokens, can be a null node
};

// Undefined symbols, e.g. #undef HAYROLL
class UndefinedSymbol
{
public:
    std::string name;
};

// Used for marking symbols that have been expanded, to avoid infinite recursion
// Instead of not expanding them, we raise an error, because if such a symbol is later symbolized,
// it will seem like -D can change its value, which is not true
class ExpandedSymbol
{
public:
    std::string name;
};

using Symbol = std::variant<ObjectSymbol, FunctionSymbol, UndefinedSymbol, ExpandedSymbol>;


// Chained hashmap symbol table that holds macro definitions.
// Shares parents as an immutable data structure.
class SymbolTable
    : public std::enable_shared_from_this<SymbolTable>
{
public:

    // Constructor
    // A SymbolTable object shall only be managed by a shared_ptr
    static SymbolTablePtr make(ConstSymbolTablePtr parent = nullptr)
    {
        SPDLOG_DEBUG("Creating symbol table");
        auto table = std::make_shared<SymbolTable>();
        table->parent = parent;
        return table;
    }

    SymbolTablePtr makeChild() const
    {
        return make(shared_from_this());
    }

    // Define a symbol in the current table
    // The defined symbol can be a "UndefinedSymbol" or an "ExpandedSymbol" too
    void define(Symbol && symbol)
    {
        std::string name = std::visit([](const auto & s) { return s.name; }, symbol);
        symbols[name] = std::move(symbol);
    }

    // Lookup a symbol in the current table and its parent tables
    std::optional<const Symbol *> lookup(std::string_view name) const
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
                    [&ss](const ObjectSymbol & s) { ss << s.name << " -> " << s.body.text(); },
                    [&ss](const FunctionSymbol & s) { ss << s.name << "(";
                        for (size_t i = 0; i < s.params.size(); i++)
                        {
                            ss << s.params[i];
                            if (i < s.params.size() - 1)
                            {
                                ss << ", ";
                            }
                        }
                        ss << ") -> " << s.body.text();
                    },
                    [&ss](const UndefinedSymbol & s) { ss << "undefined"; },
                    [&ss](const ExpandedSymbol & s) { ss << "expanded"; }
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
    std::unordered_map<std::string, Symbol, TransparentStringHash, TransparentStringEqual> symbols;
    ConstSymbolTablePtr parent;
};

// A top-level symbol table wrapper used for expanding macros
// Undefines symbols in prevention of recursive expansion
// Not intended for generating child symbol tables or being passd to other functions
class UndefStackSymbolTable
{
public:
    UndefStackSymbolTable(const ConstSymbolTablePtr & symbolTable)
        : symbolTable(symbolTable)
    {
        SPDLOG_DEBUG("Creating UndefStackSymbolTable");
    }

    void pushExpanded(std::string_view name)
    {
        // Actually we use ExpandedSymbol instead of UndefinedSymbol
        undefStack.emplace_back(ExpandedSymbol{std::string(name)});
    }

    void pushExpanded(std::string && name)
    {
        undefStack.emplace_back(ExpandedSymbol{std::move(name)});
    }

    void pop()
    {
        undefStack.pop_back();
    }

    std::optional<const Symbol *> lookup(std::string_view name) const
    {
        // size
        auto it = std::find_if(undefStack.rbegin(), undefStack.rend(), [&name](const Symbol & s)
        {
            assert(std::holds_alternative<ExpandedSymbol>(s));
            const ExpandedSymbol & expanded = std::get<ExpandedSymbol>(s);
            return expanded.name == name;
        });
        if (it != undefStack.rend())
        {
            return &*it;
        }
        // Check the parent symbol table
        return symbolTable->lookup(name);
    }

private:
    const ConstSymbolTablePtr & symbolTable;
    std::vector<Symbol> undefStack;
};

} // namespace Hayroll

#endif // HAYROLL_SYMBOLTABLE_HPP
