#include <string>
#include <vector>
#include <tuple>
#include <variant>
#include <ranges>
#include <map>

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
        // Initialize constant tokens
        auto && [tree0, token0] = parseIntoPreprocTokens("0");
        treeOwnershipCache.emplace_back(std::move(tree0));
        constToken0 = token0;

        auto && [tree1, token1] = parseIntoPreprocTokens("1");
        treeOwnershipCache.emplace_back(std::move(tree1));
        constToken1 = token1;
    }

    std::vector<TSNode> expandPreprocTokens(const std::vector<TSNode> & tokens, const ConstSymbolTablePtr & baseSymbolTable)
    {
        // We process the flat token stream using a stack
        // Tokens are pushed into the stack in reverse order, so tokens on the left are at the top of the stack
        // The tokens are then popped from the stack, processed, and appended to the output string
        //
        // Working with the stack there is another helper data structure: UndefStackSymbolTable
        // A segment of the beginning of the token stream (which was just expanded) should be banned from
        // expanding the macro they were expanded from, and that info is recoded in the UndefStackSymbolTable
        // I mentionend "a segment", but there actually is a stack of segments (if there are nested macros)
        //
        // As we pop from the stack, we care about two types of atoms: identifier, and preproc_defined_literal
        // identifier: look up in the symbol table
        //     if defined as ObjectSymbol: push its tokens into the stack, add its name to the undef stack
        //     if defined as FunctionSymbol: look forward for paring parenthesis
        //         if found paring parenthesis: call expandFunctionLikeMacro, then treat the returned tokens as ObjectSymbol
        //         if not found any parenthesis: leave as is (according to the GCC manual)
        //         if parenthesis are not balanced: error
        //     if undefined: replace with 0
        //     if expanded: error
        //         In theory, this should just be left as is, but putting an unexpanded macro in an #if later will cause an error
        //         And, symbolizing it will be a mistake, because -D-ing it will not change its value
        //         This implementation does cause false positives, e.g., when the "left as is" symbol is swallowed
        //         by a function-like macro (as argument) that does not use it, and thus does not show in the output
        //     if unknown: leave as is, it will be turned into a symbolic value
        // preproc_defined_literal:
        //     look forward for:
        //         an identifier: look up in the symbol table
        //             if defined as ObjectSymbol: replace with 1
        //             if defined as FunctionSymbol: replace with 1
        //             if undefined: replace with 0
        //             if expanded: replace with 1
        //             if unknown: leave as is, it will be turned into a symbolic value
        //         a pair of parenthesis: keep looking for an identifier inside
        //             nesting is not allowed, there must be a single identifier inside, then do what we did above
        //         end of stream: leave as is (undefined behaviour)

        // Core stack
        // (token, shouldPopUndefStackAfterThisToken)
        std::vector<std::pair<TSNode, bool>> stack;

        // Undef stack symbol table
        UndefStackSymbolTable symbolTable(baseSymbolTable);

        // Output buffer
        std::vector<TSNode> buffer;

        // // Cache for the ownership of temporarily parsed trees
        // // We need to make sure the tree is alive when we use its nodes
        // std::vector<TSTree> treeOwnershipCache;

        // // For debugging
        // for (TSNode token : tokens.iterateChildren())
        // {
        //     SPDLOG_DEBUG("Token: {}", token.textView());
        // }
        // for (TSNode token : tokens.iterateChildren() | std::views::reverse)
        // {
        //     SPDLOG_DEBUG("Reversing token: {}", token.textView());
        // }

        // Push the tokens into the stack in reverse order
        auto pushTokensAndUndef = [this, &stack, &symbolTable](const std::vector<TSNode> & tokens, std::string_view name = "")
        {
            bool undefBit = false;
            if (!name.empty())
            {
                symbolTable.pushExpanded(name);
                undefBit = true;
            }
            for (auto it = tokens.rbegin(); it != tokens.rend(); ++it)
            {
                const TSNode & token = *it;
                stack.emplace_back(token, undefBit);
                undefBit = false;
            }
        };

        auto pushTokensNodeAndUndef = [this, &stack, &symbolTable](const TSNode & tokens, std::string_view name = "")
        {
            bool undefBit = false;
            if (!name.empty())
            {
                symbolTable.pushExpanded(name);
                undefBit = true;
            }
            TSTreeCursorIterateChildren iterateChildren = tokens.iterateChildren();
            for (auto it = iterateChildren.rbegin(); it != iterateChildren.rend(); ++it)
            {
                const TSNode & token = *it;
                stack.emplace_back(token, undefBit);
                undefBit = false;
            }
        };

        // Push the tokens into the stack
        pushTokensAndUndef(tokens);

        while (!stack.empty())
        {
            std::string stackStr;
            for (auto it = stack.rbegin(); it != stack.rend(); ++it)
            {
                const TSNode & token = it->first;
                stackStr += token.textView();
                stackStr += " ";
            }
            SPDLOG_DEBUG("Stack: {}", stackStr);

            auto [token, shouldPopUndef] = stack.back();
            stack.pop_back();

            SPDLOG_DEBUG("Inspecting token {}", token.textView());

            if (token.isSymbol(lang.identifier_s))
            {
                std::string_view name = token.textView();
                std::optional<const Hayroll::Symbol *> symbol = symbolTable.lookup(name);

                if (symbol.has_value())
                {
                    const Symbol & sym = *symbol.value();
                    if (!std::holds_alternative<ExpandedSymbol>(sym))
                    {
                        // All other kinds of symbols are stored in the base symbol table
                        // So popping the UndefStackSymbolTable would not invalidate the symbol
                        // However, ExpandedSymbol will be invalidated if we pop it now
                        if (shouldPopUndef) symbolTable.pop();
                    }
                    if (std::holds_alternative<ObjectSymbol>(sym))
                    {

                        // Object-like macro, push its tokens into the stack
                        const ObjectSymbol & objSymbol = std::get<ObjectSymbol>(sym);
                        const TSNode & body = objSymbol.body;
                        SPDLOG_DEBUG("Expanding object-like macro {}, body: {}", name, body.textView());
                        if (body) pushTokensNodeAndUndef(body, name);
                        // else do nothing, the macro is empty
                    }
                    else if (std::holds_alternative<FunctionSymbol>(sym))
                    {
                        // Function-like macro, look for the arguments
                        // Note: if no parenthesis are found, we leave the token as is
                        const FunctionSymbol & funcSymbol = std::get<FunctionSymbol>(sym);
                        
                        if (stack.empty())
                        {
                            // Not expanded as a function-like macro, leave as is
                            buffer.push_back(token);
                        }
                        else
                        {
                            auto && [token1Peek, shouldPopUndef1Peek] = stack.back();
                            // Just peek, don't pop
                            if (token1Peek.textView() != "(")
                            {
                                // Not expanded as a function-like macro, leave as is
                                buffer.push_back(token);
                            }
                            else
                            {
                                // Find paring parenthesis and extract the arguments
                                std::vector<std::vector<TSNode>> args;
                                args.push_back({}); // Ready to push the first argument
                                size_t parenDepth = 0;
                                do
                                {
                                    // Pop the token
                                    auto [token1, shouldPopUndef1] = stack.back();
                                    stack.pop_back();
                                    // We only care about: "(", ")" (always), and "," (only when parenDepth = 1)
                                    if (token1.textView() == "(")
                                    {
                                        if (parenDepth != 0) args.back().push_back(token1);
                                        parenDepth++;
                                    }
                                    else if (token1.textView() == ")")
                                    {
                                        parenDepth--;
                                        if (parenDepth != 0) args.back().push_back(token1);
                                    }
                                    else if (parenDepth == 1 && token1.textView() == ",")
                                    {
                                        // End of argument, start a new one
                                        args.push_back({});
                                    }
                                    else
                                    {
                                        // Push the token into the current argument
                                        args.back().push_back(token1);
                                    }
                                    if (shouldPopUndef1) symbolTable.pop();
                                } while (parenDepth > 0 && !stack.empty());
                                if (parenDepth != 0)
                                {
                                    // Error: unbalanced parenthesis
                                    throw std::runtime_error(fmt::format("Unbalanced parenthesis in function-like macro {}", name));
                                }

                                // Important note:
                                // There exist cases where an UndefStackSymbolTable boundary cuts into the
                                // middle of a function-like macro call, or even between tokens of the same
                                // argument. The correct way to handle this is, when expanding the argument,
                                // bring with each token the UndefStackSymbolTable state when it was popped. 
                                // However, we did not implement this, because (1) that is expensive to 
                                // compute, (2) that is hard to implement, and (3) we assume that if a self-
                                // referencing macro appears in the middle of a function-like macro call, 
                                // it will be captured later after the expanded function-like macro is re-
                                // pushed into the stack and re-processed for further expansion possibilities.
                                // We slacked the self-referencing check by just giving the baseSymbolTable
                                // to expandFunctionLikeMacro
                                std::vector<TSNode> expanded = expandFunctionLikeMacro(args, funcSymbol, baseSymbolTable);
                                
                                // Push the expanded tokens into the stack
                                pushTokensAndUndef(expanded, name);
                            }
                        }
                    }
                    else if (std::holds_alternative<UndefinedSymbol>(sym))
                    {
                        // Undefined macro, replace with 0
                        buffer.push_back(constToken0);
                    }
                    else if (std::holds_alternative<ExpandedSymbol>(sym))
                    {
                        if (shouldPopUndef) symbolTable.pop();
                        // Expanded macro, error
                        throw std::runtime_error(fmt::format("Recursive expansion of macro {}", name));
                    }
                    else
                    {
                        // Print the symbol type
                        // SPDLOG_DEBUG("Symbol type: {}", std::visit([](const auto & s) { return s.name; }, sym));
                        assert(false);
                    }
                }
                else
                {
                    // Unknown symbol, leave as is, it will be turned into a symbolic value
                    buffer.push_back(token);
                }
            }
            else if (token.isSymbol(lang.preproc_defined_literal_s))
            {
                // We will never need to lookup the preproc_defined_literal symbol
                if (shouldPopUndef) symbolTable.pop();
                if (stack.empty())
                {
                    // Not expanded, leave as is
                    buffer.push_back(token);
                }
                else
                {
                    auto && [token1, shouldPopUndef1] = stack.back();
                    stack.pop_back();

                    auto handleIdentifier = 
                    [this, &buffer, &symbolTable, token = std::move(token)]
                    (const TSNode & tokenX, bool shouldPopUndefX, const std::optional<const TSNode *> leftParenthesis = std::nullopt) -> bool
                    {
                        bool replaced = false;
                        std::string_view name = tokenX.textView();
                        std::optional<const Hayroll::Symbol *> symbol = symbolTable.lookup(name);
                        if (symbol.has_value())
                        {
                            const Symbol & sym = *symbol.value();
                            if (std::holds_alternative<ObjectSymbol>(sym) || std::holds_alternative<FunctionSymbol>(sym) || std::holds_alternative<ExpandedSymbol>(sym))
                            {
                                buffer.push_back(constToken1);
                            }
                            else if (std::holds_alternative<UndefinedSymbol>(sym))
                            {
                                buffer.push_back(constToken0);
                            }
                            else assert(false);
                            replaced = true;
                        }
                        else
                        {
                            // Unknown symbol, leave as is, it will be turned into a symbolic value
                            buffer.push_back(token);
                            if (leftParenthesis)
                            {
                                buffer.push_back(*leftParenthesis.value());
                            }
                            buffer.push_back(tokenX);
                            replaced = false;
                        }
                        // Use of symbol is done, pop the undef stack
                        if (shouldPopUndefX) symbolTable.pop();
                        return replaced;
                    };
                    
                    if (token1.isSymbol(lang.identifier_s))
                    {
                        handleIdentifier(token1, shouldPopUndef1);
                    }
                    else if (token1.textView() == "(")
                    {
                        // We will never need to lookup a parenthesis symbol
                        if (shouldPopUndef1) symbolTable.pop();
                        // Look for the identifier inside the parenthesis
                        if (stack.empty())
                        {
                            // Not expanded, leave as is
                        }
                        else
                        {
                            auto && [token2, shouldPopUndef2] = stack.back();
                            stack.pop_back();
                            bool replaced = false;
                            if (token2.isSymbol(lang.identifier_s))
                            {
                                replaced = handleIdentifier(token2, shouldPopUndef2, &token1);
                            }
                            else
                            {
                                // Error: expected an identifier inside preproc_defined_literal
                                throw std::runtime_error(fmt::format("Expected an identifier inside preproc_defined_literal"));
                            }

                            if (stack.empty())
                            {
                                throw std::runtime_error(fmt::format("Unbalanced parenthesis in preproc_defined_literal"));
                            }

                            auto && [token3, shouldPopUndef3] = stack.back();
                            stack.pop_back();
                            
                            if (token3.textView() != ")")
                            {
                                throw std::runtime_error(fmt::format("Unbalanced parenthesis in preproc_defined_literal"));
                            }
                            // We don't need the ")" if the whole thing is replaced
                            if (!replaced) buffer.push_back(token3);
                            if (shouldPopUndef3) symbolTable.pop();
                        }
                    }
                    else
                    {
                        // Error: expected an identifier after preproc_defined_literal
                        throw std::runtime_error(fmt::format("Expected an identifier after preproc_defined_literal"));
                    }
                }
            }
            else
            {
                // Other tokens, just append to the output
                buffer.push_back(token);
                if (shouldPopUndef) symbolTable.pop();
            }
        } // while stack

        return buffer;
    }

    std::vector<TSNode> expandFunctionLikeMacro
    (
        const std::vector<std::vector<TSNode>> & args,
        const FunctionSymbol & funcSymbol,
        const ConstSymbolTablePtr symbolTable
    )
    {
        // Check if the number of arguments is correct
        if (args.size() != funcSymbol.params.size())
        {
            throw std::runtime_error(fmt::format("Function-like macro {} called with {} arguments, expected {}", funcSymbol.name, args.size(), funcSymbol.params.size()));
        }

        // Expand the arguments w.r.t. the symbol table, and store them in a new symbol table
        std::unordered_map<std::string, std::vector<TSNode>, TransparentStringHash, TransparentStringEqual> argTable;
        for (size_t i = 0; i < args.size(); i++)
        {
            const std::vector<TSNode> & arg = args[i];
            argTable[funcSymbol.params[i]] = expandPreprocTokens(arg, symbolTable);
        }
        // Make sure all arg names are unique
        assert(argTable.size() == args.size());

        SPDLOG_DEBUG("Expanding function-like macro {} with body {}", funcSymbol.name, funcSymbol.body.textView());
        
        std::vector<TSNode> buffer;
        // Expand the function-like macro body
        for (const TSNode & token : funcSymbol.body.iterateChildren())
        {
            if (token.isSymbol(lang.identifier_s))
            {
                std::string_view name = token.textView();
                auto it = argTable.find(name);
                if (it != argTable.end())
                {
                    // Found an argument, push its tokens into the buffer
                    const std::vector<TSNode> & arg = it->second;
                    for (const TSNode & argToken : arg)
                    {
                        buffer.push_back(argToken);
                    }
                }
                else
                {
                    // Not an argument, just append the token
                    buffer.push_back(token);
                }
            }
            else
            {
                // Other tokens, just append to the output
                buffer.push_back(token);
            }
        }

        // Print the expanded function-like macro for debugging
        std::string expandedMacro;
        for (const TSNode & token : buffer)
        {
            expandedMacro += token.text() + " ";
        }
        SPDLOG_DEBUG("Expanded function-like macro {}: {}", funcSymbol.name, expandedMacro);

        return buffer;
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
        std::string evalSource = "#evalexpr " + std::string(source) + "\n#endevalexpr\n";
        TSTree tree = parser.parseString(std::move(evalSource));
        TSNode root = tree.rootNode(); // translation_unit
        TSNode expr = root.firstChildForByte(0).childByFieldId(lang.preproc_evalexpr_s.expr_f);
        return { std::move(tree), std::move(expr) };
    }

private:
    const CPreproc & lang;
    TSParser parser;

    // Cache for the ownership of temporarily parsed trees
    std::vector<TSTree> treeOwnershipCache;
    TSNode constToken0;
    TSNode constToken1;
};

} // namespace Hayroll

#endif // HAYROLL_MACROEXPANDER_HPP
