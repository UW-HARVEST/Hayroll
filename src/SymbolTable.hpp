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
#include "IncludeTree.hpp"
#include "ProgramPoint.hpp"
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
    std::string_view name;
    ProgramPoint def;
    TSNode body; // preproc_tokens, can be a null node
};

// Function-like macro symbols, e.g. #define HAYROLL(x) x + 1
class FunctionSymbol
{
public:
    std::string_view name;
    ProgramPoint def;
    std::vector<std::string> params;
    TSNode body; // preproc_tokens, can be a null node
};

// Undefined symbols, e.g. #undef HAYROLL
class UndefinedSymbol
{
public:
    std::string_view name;
};

// Used for marking symbols that have been expanded, to avoid infinite recursion
// Instead of not expanding them, we raise an error, because if such a symbol is later symbolized,
// it will seem like -D can change its value, which is not true
class ExpandedSymbol
{
public:
    std::string_view name;
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
        auto table = std::make_shared<SymbolTable>();
        table->parent = parent;
        table->immutable = false;
        return table;
    }

    // Make this symbol table immutable so any changes will create a new child table
    // This is meant to be called when the execution reaches a branch
    void makeImmutable()
    {
        SPDLOG_DEBUG("Making symbol table immutable");
        immutable = true;
    }

    // Define a symbol in either the current table if it is mutable,
    // or in a new child table if it is immutable.
    // The defined symbol can be a "UndefinedSymbol" or an "ExpandedSymbol" too.
    // The user must assign the SymbolTablePtr back to itself. 
    SymbolTablePtr define(Symbol && symbol)
    {
        if (immutable)
        {
            SPDLOG_DEBUG("Defining symbol in immutable table, creating child");
            return makeChild()->define(std::move(symbol));;
        }
        else
        {
            std::string_view name = std::visit([](const auto & s) { return s.name; }, symbol);
            symbols.insert_or_assign(name, std::move(symbol));
            return shared_from_this();
        }
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

    std::string toString(int maxEntries = 10) const
    {
        std::stringstream ss;
        int count = 0;
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
                    [&ss](const UndefinedSymbol & s) { ss << s.name << " -> <UNDEFINED>"; },
                    [&ss](const ExpandedSymbol & s) { ss << s.name << " -> <EXPANDED>"; }
                },
                symbol
            );
            ss << "\n";
            count++;
            if (count >= maxEntries)
            {
                ss << "...\n";
                break;
            }
        }
        return ss.str();
    }

    std::string toStringFull() const
    {
        std::stringstream ss;
        ss << toString();
        ss << "----------------" << "\n";
        if (parent)
        {
            ss << parent->toStringFull();
        }
        return ss.str();
    }

private:
    std::unordered_map<std::string_view, Symbol, TransparentStringHash, TransparentStringEqual> symbols;
    ConstSymbolTablePtr parent;
    bool immutable;

    SymbolTablePtr makeChild() const
    {
        return make(shared_from_this());
    }
};

// A top-level symbol table wrapper used for expanding macros
// Undefines symbols in prevention of recursive expansion
// Not intended for generating child symbol tables or being passd to other functions
class UndefStackSymbolTable
{
public:
    UndefStackSymbolTable(const ConstSymbolTablePtr & symbolTable)
        : symbolTable(symbolTable)
    { }

    // Force using std::string_view to avoid implicit conversion from std::string.
    // This is for reminding the user that the name is not owned by the symbol table,
    // so the user is in charge of making sure it's alive.
    template<typename std_string_view>
    requires std::is_same_v<std_string_view, std::string_view>
    void pushExpanded(std_string_view name)
    {
        // Actually we use ExpandedSymbol instead of UndefinedSymbol
        undefStack.emplace_back(ExpandedSymbol{name});
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
        if (symbolTable) return symbolTable->lookup(name);
        return std::nullopt;
    }

private:
    const ConstSymbolTablePtr & symbolTable;
    std::vector<Symbol> undefStack;
};

} // namespace Hayroll

#endif // HAYROLL_SYMBOLTABLE_HPP
