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

std::string_view symbolName(const Symbol & symbol)
{
    return std::visit([](const auto & s) { return s.name; }, symbol);
}

const ProgramPoint & symbolProgramPoint(const Symbol & symbol)
{
    if (std::holds_alternative<ObjectSymbol>(symbol))
    {
        return std::get<ObjectSymbol>(symbol).def;
    }
    else if (std::holds_alternative<FunctionSymbol>(symbol))
    {
        return std::get<FunctionSymbol>(symbol).def;
    }
    else assert(false);
}

const TSNode & symbolBody(const Symbol & symbol)
{
    if (std::holds_alternative<ObjectSymbol>(symbol))
    {
        return std::get<ObjectSymbol>(symbol).body;
    }
    else if (std::holds_alternative<FunctionSymbol>(symbol))
    {
        return std::get<FunctionSymbol>(symbol).body;
    }
    else assert(false);
}

class SymbolSegment;
using SymbolSegmentPtr = std::shared_ptr<SymbolSegment>;
using ConstSymbolSegmentPtr = std::shared_ptr<const SymbolSegment>;

// Shared hashmap storage for the symbol table.
// Represents a continuous segment of #define/#undef statements.
class SymbolSegment
    : public std::enable_shared_from_this<SymbolSegment>
{
public:
    // Constructor
    // A SymbolSegment object shall only be managed by a shared_ptr.
    static SymbolSegmentPtr make()
    {
        totalSymbolSegments++;
        
        return std::make_shared<SymbolSegment>();
    }

    // Define a symbol in the segment.
    void define(Symbol && symbol)
    {
        totalSymbols++;

        std::string_view name = std::visit([](const auto & s) { return s.name; }, symbol);
        symbols.insert_or_assign(name, std::move(symbol));
    }

    // Lookup a symbol in the segment.
    std::optional<const Symbol *> lookup(std::string_view name) const
    {
        auto it = symbols.find(name);
        if (it != symbols.end())
        {
            return &it->second;
        }
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
            if (maxEntries > 0 && count >= maxEntries)
            {
                ss << "...\n";
                break;
            }
        }
        return ss.str();
    }

    static int totalSymbolSegments;
    static int totalSymbols;

private:
    std::unordered_map<std::string_view, Symbol, TransparentStringHash, TransparentStringEqual> symbols;
};

int SymbolSegment::totalSymbolSegments = 0;
int SymbolSegment::totalSymbols = 0;

// Chained hashmap symbol table that holds macro definitions.
// Shares parents as an immutable data structure.
class SymbolTable
    : public std::enable_shared_from_this<SymbolTable>
{
public:
    // Constructor
    // A SymbolTable object shall only be managed by a shared_ptr
    static SymbolTablePtr make
    (
        SymbolSegmentPtr symbols = SymbolSegment::make(),
        ConstSymbolTablePtr parent = nullptr
    )
    {
        totalSymbolTables++;

        auto table = std::make_shared<SymbolTable>();
        table->symbols = symbols;
        table->parent = parent;
        return table;
    }

    // Create a child that binds to the provied SymbolSegmentPtr
    SymbolTablePtr define(SymbolSegmentPtr segment)
    {
        totalSymbolTables++;
        return makeChild(segment);
    }

    // Force define a symbol in the current SymbolSegment
    SymbolTablePtr forceDefine(Symbol && symbol)
    {
        symbols->define(std::move(symbol));
        return shared_from_this();
    }

    // Lookup a symbol in the current table and its parent tables
    std::optional<const Symbol *> lookup(std::string_view name) const
    {
        if (std::optional<const Symbol *> symbol = symbols->lookup(name))
        {
            return symbol;
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
        return symbols->toString(maxEntries);
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
    
    static int totalSymbolTables;

private:
    SymbolSegmentPtr symbols;
    ConstSymbolTablePtr parent;

    SymbolTablePtr makeChild(SymbolSegmentPtr segment)
    {
        return make(segment, shared_from_this());
    }
};

int SymbolTable::totalSymbolTables = 0;

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
