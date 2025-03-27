#include <string>
#include <vector>
#include <tuple>

#include "TreeSitter.hpp"
#include "TreeSitterCPreproc.hpp"
#include "Util.hpp"
#include "SymbolTable.hpp"

#ifndef HAYROLL_MACROEXPANDER_HPP
#define HAYROLL_MACROEXPANDER_HPP

namespace Hayroll
{

class MacroExpander
{
public:
    MacroExpander(const CPreproc & lang)
        : lang(lang), parser(lang)
    {
    }

    std::string expandPreprocTokens(const TSNode & tokens, const ConstSymbolTablePtr & symbolTable)
    {
        assert(tokens.isSymbol(lang.preproc_tokens_s));
        // We care about three types of atoms: identifier, call_expression, and preproc_defined
        // identifier: look up in the symbol table
        //     if defined as ObjectSymbol: replace with the value
        //     if defined as FunctionSymbol: error
        //     if undefined: replace with 0
        //     if unknown: leave as is, it will be turned into a symbolic value
        // call_expression: look up in the symbol table
        //     if defined as ObjectSymbol: error
        //     if defined as FunctionSymbol: return expandFunctionLikeMacro()
        //     if undefined: error
        //     if unknown: error
        // preproc_defined: look up in the symbol table
        //     if defined: replace with 1
        //     if undefined: replace with 0
        //     if unknown: leave as is, it will be turned into a symbolic value
        // We walk through the descendants of the tokens node, expanding as we go
        // Whenever seeing an expandable node, we expand it and skip its descendants

        // For std::string segments, we std::move them into the bufferOwnershipCache, and store a std::string_view in buffer
        // For std::string_view segments, we store them in buffer
        StringBuilder buffer;

        uint32_t handledByte = tokens.startByte();
        TSTreeCursor cursor = tokens.cursor();
        cursor.preorderNext();
        
        while (true)
        {
            TSNode node = cursor.currentNode();
            if (node.isSymbol(lang.identifier_s) || node.isSymbol(lang.call_expression_s) || node.isSymbol(lang.preproc_defined_s))
            {
                uint32_t startByte = node.startByte();
                if (startByte > handledByte)
                {
                    // Handle source range that we skipped
                    buffer.append(tokens.getSource().substr(handledByte, startByte - handledByte));
                    handledByte = startByte;
                }

                if (node.isSymbol(lang.identifier_s))
                {
                    std::string_view name = node.textView();
                    std::optional<const Hayroll::Symbol *> symbol = symbolTable->lookup(name);
                    if (symbol.has_value())
                    {
                        const Symbol & sym = *symbol.value();
                        std::string_view replacement;
                        if (std::holds_alternative<ObjectSymbol>(sym))
                        {
                            replacement = std::get<ObjectSymbol>(sym).spelling;
                        }
                        else if (std::holds_alternative<FunctionSymbol>(sym))
                        {
                            throw std::runtime_error(fmt::format("Function-like macro {} used as an object-like macro", name));
                        }
                        else if (std::holds_alternative<UndefinedSymbol>(sym))
                        {
                            replacement = "0";
                        }
                        else assert(false);
                        buffer.append(replacement);
                    }
                    else
                    {
                        // Unknown symbol, leave as is
                        buffer.append(name);
                    }
                }
                else if (node.isSymbol(lang.call_expression_s))
                {
                    TSNode function = node.childByFieldId(lang.call_expression_s.function_f);
                    assert(function.isSymbol(lang.identifier_s));
                    std::string_view name = function.textView();
                    std::optional<const Hayroll::Symbol *> symbol = symbolTable->lookup(name);
                    if (symbol.has_value())
                    {
                        const Symbol & sym = *symbol.value();
                        if (std::holds_alternative<ObjectSymbol>(sym))
                        {
                            throw std::runtime_error(fmt::format("Object-like macro {} used as a function-like macro", name));
                        }
                        else if (std::holds_alternative<FunctionSymbol>(sym))
                        {
                            const FunctionSymbol & funcSymbol = std::get<FunctionSymbol>(sym);
                            std::string expanded = expandFunctionLikeMacro(node, funcSymbol, symbolTable);
                            buffer.append(std::move(expanded));
                        }
                        else if (std::holds_alternative<UndefinedSymbol>(sym))
                        {
                            throw std::runtime_error(fmt::format("Undefined function-like macro {} used", name));
                        }
                        else assert(false);
                    }
                    else
                    {
                        // Unknown symbol, error
                        throw std::runtime_error(fmt::format("Unknown function-like macro {} used", name));
                    }
                }
                else if (node.isSymbol(lang.preproc_defined_s))
                {
                    TSNode nameNode = node.childByFieldId(lang.preproc_defined_s.name_f);
                    std::string_view name = nameNode.textView();
                    std::optional<const Hayroll::Symbol *> symbol = symbolTable->lookup(name);
                    if (symbol.has_value())
                    {
                        const Symbol & sym = *symbol.value();
                        if (std::holds_alternative<ObjectSymbol>(sym) || std::holds_alternative<FunctionSymbol>(sym))
                        {
                            buffer.append("1");
                        }
                        else if (std::holds_alternative<UndefinedSymbol>(sym))
                        {
                            buffer.append("0");
                        }
                        else assert(false);
                    }
                    else
                    {
                        // Unknown symbol, leave as is
                        buffer.append(name);
                    }
                }
                else assert(false);

                handledByte = node.endByte();
                if (!cursor.preorderSkip()) break;
            } // Nodes we care about
            else
            {
                // Source ranges that we don't care about
                if (!cursor.preorderNext()) break;
            }    
        }
        
        size_t endByte = tokens.endByte();
        if (handledByte < endByte)
        {
            buffer.append(tokens.getSource().substr(handledByte, endByte - handledByte));
            handledByte = endByte;
        }

        return buffer.str();
    }

    std::string expandFunctionLikeMacro
    (
        const TSNode & call,
        const FunctionSymbol & funcSymbol,
        const ConstSymbolTablePtr & symbolTable // Avoid cost of reference counting
    )
    {
        assert(call.isSymbol(lang.call_expression_s));

        // Make sure there funcSymbol accepts the right number of arguments
        TSNode argList = call.childByFieldId(lang.call_expression_s.arguments_f);
        assert(argList.isSymbol(lang.argument_list_s));
        if (argList.childCount() != funcSymbol.params.size())
        {
            throw std::runtime_error("Function-like macro " + funcSymbol.name + " called with the wrong number of arguments");
        }

        // Parse the function body into tokens
        auto [tree, tokens] = parseIntoPreprocTokens(funcSymbol.body);

        // Create a temp symbol table with the arguments defined, if any
        if (!funcSymbol.params.empty())
        {
            SymbolTablePtr tempSymbolTable = SymbolTable::make(symbolTable);
            size_t i = 0;
            for (TSNode arg : argList.iterateChildren())
            {
                assert(arg.isSymbol(lang.preproc_tokens_s));
                std::string argName = funcSymbol.params[i];
                std::string argValue = arg.text();
                tempSymbolTable->define(ObjectSymbol{std::move(argName), std::move(argValue)});
                i++;
            }
            // Expand the tokens with the temp symbol table
            return expandPreprocTokens(tokens, tempSymbolTable);
        }
        else
        {
            // No arguments, expand as if it were an object-like macro
            return expandPreprocTokens(tokens, symbolTable);
        }
    }

    std::tuple<TSTree, TSNode> parseIntoPreprocTokens(std::string_view source)
    {
        std::string ifSource = "#if " + std::string(source) + "\n#endif\n";
        TSTree tree = parser.parseString(std::move(ifSource));
        TSNode root = tree.rootNode();
        TSNode tokens = root.childByFieldId(lang.preproc_if_s.condition_f);
        assert(tokens.isSymbol(lang.preproc_tokens_s));
        return { std::move(tree), std::move(tokens) };
    }

    std::tuple<TSTree, TSNode> parseIntoExpression(std::string_view source)
    {
        std::string evalSource = "#eval " + std::string(source) + "\n#endeval\n";
        TSTree tree = parser.parseString(std::move(evalSource));
        TSNode root = tree.rootNode();
        TSNode expr = root.childByFieldId(lang.preproc_eval_s.expr_f);
        return { std::move(tree), std::move(expr) };
    }

private:
    const CPreproc & lang;
    TSParser parser;
};

} // namespace Hayroll

#endif // HAYROLL_MACROEXPANDER_HPP
