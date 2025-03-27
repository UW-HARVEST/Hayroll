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
};

class FunctionSymbol
{
public:
    std::string name;
    std::vector<std::string> params;
    std::string body;
};

class UndefinedSymbol
{
public:
    std::string name;
};

// Used for marking symbols that have been expanded, to avoid infinite recursion
// Why not undefine it? You can do "#define A defined A", and then "#if A" should still be true
class ExpandedSymbol
{
public:
    std::string name;
};

using Symbol = std::variant<ObjectSymbol, FunctionSymbol, UndefinedSymbol, ExpandedSymbol>;


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

    SymbolTablePtr makeChild() const
    {
        return make(shared_from_this());
    }

    void define(Symbol && symbol)
    {
        std::string name = std::visit([](const auto & s) { return s.name; }, symbol);
        symbols[name] = symbol;
    }

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

    struct TransparentStringHash
    {
        using is_transparent = void;
    
        size_t operator()(const std::string & s) const
        {
            return std::hash<std::string>{}(s);
        }
        size_t operator()(std::string_view sv) const
        {
            return std::hash<std::string_view>{}(sv);
        }
    };
    
    struct TransparentStringEqual
    {
        using is_transparent = void;
    
        bool operator()(const std::string & lhs, const std::string & rhs) const
        {
            return lhs == rhs;
        }
        bool operator()(const std::string & lhs, std::string_view rhs) const
        {
            return lhs == rhs;
        }
        bool operator()(std::string_view lhs, const std::string & rhs) const
        {
            return lhs == rhs;
        }
    };

private:
    std::unordered_map<std::string, Symbol, TransparentStringHash, TransparentStringEqual> symbols;
    ConstSymbolTablePtr parent;
};

} // namespace Hayroll

#endif // HAYROLL_SYMBOLTABLE_HPP
