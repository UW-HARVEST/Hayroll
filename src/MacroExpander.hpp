#include <string>
#include <vector>
#include <tuple>

#include <spdlog/spdlog.h>

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
        //     if defined as ObjectSymbol: return expandObjectLikeMacro()
        //     if defined as FunctionSymbol: error
        //     if undefined: replace with 0
        //     if expanded: leave as is, no recursion
        //     if unknown: leave as is, it will be turned into a symbolic value
        // call_expression: look up in the symbol table
        //     if defined as ObjectSymbol: error
        //     if defined as FunctionSymbol: return expandFunctionLikeMacro()
        //     if undefined: error
        //     if expanded: leave as is, no recursion
        //     if unknown: error
        // preproc_defined: look up in the symbol table
        //     if defined (as anything): replace with 1
        //     if undefined: replace with 0
        //     if expanded: this means the symbol was defined before, so replace with 1
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
                        if (std::holds_alternative<ObjectSymbol>(sym))
                        {
                            const ObjectSymbol & objSymbol = std::get<ObjectSymbol>(sym);
                            std::string expanded = expandObjectLikeMacro(node, objSymbol, symbolTable);
                            buffer.append(std::move(expanded));
                        }
                        else if (std::holds_alternative<FunctionSymbol>(sym))
                        {
                            throw std::runtime_error(fmt::format("Function-like macro {} used as an object-like macro", name));
                        }
                        else if (std::holds_alternative<UndefinedSymbol>(sym))
                        {
                            buffer.append("0");
                        }
                        else if (std::holds_alternative<ExpandedSymbol>(sym))
                        {
                            // Leave as is
                            buffer.append(name);
                        }
                        else assert(false);
                    }
                    else
                    {
                        // Unknown symbol, leave as is, it will be turned into a symbolic value
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
                        else if (std::holds_alternative<ExpandedSymbol>(sym))
                        {
                            // Leave as is
                            buffer.append(node.textView());
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
                        if (std::holds_alternative<ObjectSymbol>(sym) || std::holds_alternative<FunctionSymbol>(sym) || std::holds_alternative<ExpandedSymbol>(sym))
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
                        // Unknown symbol, leave as is, it will be turned into a symbolic value
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

        if (funcSymbol.body.empty()) return "";

        // Make sure there funcSymbol accepts the right number of arguments
        TSNode argList = call.childByFieldId(lang.call_expression_s.arguments_f);
        assert(argList.isSymbol(lang.argument_list_s));

        // Parse the function body into tokens
        auto [tree, bodyTokens] = parseIntoPreprocTokens(funcSymbol.body);

        // Create a temp symbol table
        
        // Define the arguments in the temp symbol table
        if (!funcSymbol.params.empty())
        {
            SymbolTablePtr tempSymbolTable = symbolTable->makeChild();
            size_t i = 0;
            for (TSNode arg : argList.iterateChildren())
            {
                // TODO: filter with field name
                if (!arg.isSymbol(lang.preproc_tokens_s)) continue;
                std::string argName = funcSymbol.params[i];
                std::string expandedArg = expandPreprocTokens(arg, tempSymbolTable);
                tempSymbolTable->define(ObjectSymbol{std::move(argName), std::move(expandedArg)});
                i++;
            }
            if (i != funcSymbol.params.size())
            {
                throw std::runtime_error(fmt::format("Function-like macro {} called with {} arguments, expected {}", funcSymbol.name, i, funcSymbol.params.size()));
            }

            return expandPreprocTokens(bodyTokens, tempSymbolTable);
        }

        return expandPreprocTokens(bodyTokens, symbolTable);
    }

    std::string expandObjectLikeMacro
    (
        const TSNode & identifier,
        const ObjectSymbol & objSymbol,
        const ConstSymbolTablePtr & symbolTable
    )
    {
        assert(identifier.isSymbol(lang.identifier_s));

        if (objSymbol.spelling.empty()) return "";

        // Parse the object-like macro body into tokens
        auto [tree, bodyTokens] = parseIntoPreprocTokens(objSymbol.spelling);

        // Create a temp symbol table
        SymbolTablePtr tempSymbolTable = symbolTable->makeChild();
        // Mark this object-like macro as being expanded
        // This will not overwrite the original object symbol because we are using a child symbol table
        tempSymbolTable->define(ExpandedSymbol{objSymbol.name});

        return expandPreprocTokens(bodyTokens, tempSymbolTable);
    }

    // Parse a string into preprocessor tokens
    // Returns (tree, tokens)
    // Source must not be blank (\s*)
    std::tuple<TSTree, TSNode> parseIntoPreprocTokens(std::string_view source)
    {
        std::string ifSource = "#if " + std::string(source) + "\n#endif\n";
        TSTree tree = parser.parseString(std::move(ifSource));
        TSNode root = tree.rootNode(); // translation_unit
        TSNode tokens = root.firstChildForByte(0).childByFieldId(lang.preproc_if_s.condition_f);
        assert(tokens.isSymbol(lang.preproc_tokens_s));
        return { std::move(tree), std::move(tokens) };
    }

    // Parse a string into a preprocessor expression
    // Returns (tree, expr)
    // Source must not be blank (\s*)
    std::tuple<TSTree, TSNode> parseIntoExpression(std::string_view source)
    {
        std::string evalSource = "#eval " + std::string(source) + "\n#endeval\n";
        TSTree tree = parser.parseString(std::move(evalSource));
        TSNode root = tree.rootNode(); // translation_unit
        TSNode expr = root.firstChildForByte(0).childByFieldId(lang.preproc_eval_s.expr_f);
        return { std::move(tree), std::move(expr) };
    }

private:
    const CPreproc & lang;
    TSParser parser;
};

} // namespace Hayroll

#endif // HAYROLL_MACROEXPANDER_HPP
