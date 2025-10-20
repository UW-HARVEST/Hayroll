#ifndef HAYROLL_MACROEXPANDER_HPP
#define HAYROLL_MACROEXPANDER_HPP

#include <string>
#include <vector>
#include <tuple>
#include <variant>
#include <ranges>
#include <map>
#include <algorithm>
#include <cctype>

#include <z3++.h>

#include <spdlog/spdlog.h>

#include "TreeSitter.hpp"
#include "TreeSitterCPreproc.hpp"
#include "Util.hpp"
#include "SymbolTable.hpp"
#include "ProgramPoint.hpp"

namespace Hayroll
{

class MacroExpander
{
public:
    MacroExpander(const CPreproc & lang, z3::context * ctx)
        : lang(lang), parser(lang), ctx(ctx), 
          constExpr0(ctx->int_val(0)), constExpr1(ctx->int_val(1))
    {
        // Initialize constant tokens
        auto && [tree, tokens] = parseIntoPreprocTokens("0 1 ! defined");
        assert(tokens.isSymbol(lang.preproc_tokens_s));
        tempTokensTree = std::move(tree);
        std::vector<TSNode> tokenNodes = lang.tokensToTokenVector(tokens);
        constToken0 = tokenNodes[0];
        constToken1 = tokenNodes[1];
        constTokenNot = tokenNodes[2];
        constTokenDefined = tokenNodes[3];
    }

    enum class Prepend
    {
        None,
        Defined,
        NotDefined
    };

    // Expand and symbolize the expression
    z3::expr symbolizeToBoolExpr
    (
        const std::vector<TSNode> & tokens,
        const ConstSymbolTablePtr & symbolTable = nullptr,
        Prepend prepend = Prepend::None
    )
    {
        std::vector<TSNode> tokensPrepended;
        switch (prepend)
        {
            case Prepend::None:
                break;
            case Prepend::Defined:
                tokensPrepended = tokens;
                tokensPrepended.insert(tokensPrepended.begin(), constTokenDefined);
                break;
            case Prepend::NotDefined:
                tokensPrepended = tokens;
                tokensPrepended.insert(tokensPrepended.begin(), constTokenDefined);
                tokensPrepended.insert(tokensPrepended.begin(), constTokenNot);
                break;
        }
        std::vector<TSNode> expandedTokens = expandPreprocTokens(prepend == Prepend::None ? tokens : tokensPrepended, symbolTable);

        StringBuilder expandedStrBuilder;
        for (const TSNode & token : expandedTokens)
        {
            expandedStrBuilder.append(token.textView());
            expandedStrBuilder.append(" ");
        }
        std::string expandedStr = expandedStrBuilder.str();
        // Parse the string into an expression
        auto [exprTree, exprNode] = parseIntoExpression(expandedStr);
        if (!exprNode) return ctx->bool_val(false);
        // Symbolize the expression
        return int2bool(symbolizeExpression(exprNode));
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

        // Push the tokens into the stack in reverse order
        auto pushTokensAndUndef = [this, &stack, &symbolTable](const std::vector<TSNode> & tokens, std::string_view name = "")
        {
            bool undefBit = false;
            if (!name.empty())
            {
                symbolTable.pushExpanded(name);
                undefBit = true;
            }
            for (const auto & token : tokens | std::views::reverse)
            {
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
            for (const auto & token : tokens.iterateChildren() | std::views::reverse)
            {
                stack.emplace_back(token, undefBit);
                undefBit = false;
            }
        };

        // Push the tokens into the stack
        pushTokensAndUndef(tokens);

        while (!stack.empty())
        {
            std::string stackStr;
            for (const auto & [token, shouldPopUndef] : stack | std::views::reverse)
            {
                stackStr += token.textView();
                stackStr += " ";
            }
            auto [token, shouldPopUndef] = stack.back();
            stack.pop_back();

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
                                    throw std::runtime_error(std::format("Unbalanced parenthesis in function-like macro {}", name));
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
                        throw std::runtime_error(std::format("Recursive expansion of macro {}", name));
                    }
                    else
                    {
                        // Print the symbol type
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
                                throw std::runtime_error(std::format("Expected an identifier inside preproc_defined_literal"));
                            }

                            if (stack.empty())
                            {
                                throw std::runtime_error(std::format("Unbalanced parenthesis in preproc_defined_literal"));
                            }

                            auto && [token3, shouldPopUndef3] = stack.back();
                            stack.pop_back();
                            
                            if (token3.textView() != ")")
                            {
                                throw std::runtime_error(std::format("Unbalanced parenthesis in preproc_defined_literal"));
                            }
                            // We don't need the ")" if the whole thing is replaced
                            if (!replaced) buffer.push_back(token3);
                            if (shouldPopUndef3) symbolTable.pop();
                        }
                    }
                    else
                    {
                        // Error: expected an identifier after preproc_defined_literal
                        throw std::runtime_error(std::format("Expected an identifier after preproc_defined_literal"));
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

    // Collect all definitons used for a nested expansion.
    // Used to make sure our premise collection for multi-defined macro expansion is correct.
    std::vector<ProgramPoint> collectNestedExpansionDefinitions
    (
        const TSNode & token,
        const ConstSymbolTablePtr & symbolTable
    )
    {
        std::vector<ProgramPoint> collection;
        std::vector<TSNode> workList = {token};

        while (!workList.empty())
        {
            TSNode current = workList.back();
            workList.pop_back();

            if (!current.isSymbol(lang.identifier_s)) continue;

            std::optional<const Hayroll::Symbol *> symbol = symbolTable->lookup(current.textView());
            if (symbol.has_value())
            {
                const Symbol & sym = *symbol.value();
                if (std::holds_alternative<ObjectSymbol>(sym) || std::holds_alternative<FunctionSymbol>(sym))
                {
                    ProgramPoint defProgramPoint = symbolProgramPoint(sym);
                    if (std::find(collection.begin(), collection.end(), defProgramPoint) != collection.end())
                    {
                        // Already collected this definition, skip it to avoid infinite loops
                        continue;
                    }
                    collection.push_back(std::move(defProgramPoint));
                    const TSNode & body = symbolBody(sym);
                    if (body)
                    {
                        for (const TSNode & token : body.iterateChildren())
                        {
                            workList.push_back(token);
                        }
                    }
                }
                else if (std::holds_alternative<UndefinedSymbol>(sym))
                {
                    // Do nothing, undefined symbols do not have a body
                }
                else assert(false);
            }
        }

        return collection;
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
            throw std::runtime_error(std::format("Function-like macro {} called with {} arguments, expected {}", funcSymbol.name, args.size(), funcSymbol.params.size()));
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
                    buffer.insert(buffer.end(), arg.begin(), arg.end());
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
        #if DEBUG
            std::string expandedMacro;
            for (const TSNode & token : buffer)
            {
                expandedMacro += token.text() + " ";
            }
        #endif

        return buffer;
    }

    // Symbolize all identifiers in a preprocessor expression node
    // The expression must have been expanded and parsed with parseIntoExpression
    // This function will not lookup the symbol table, it assumes that known symbols are already replaced
    z3::expr symbolizeExpression(const TSNode & node)
    {
        // All possible expression kinds
        //     identifier,
        //     call_expression
        //     number_literal,
        //     char_literal,
        //     preproc_defined,
        //     unary_expression
        //     binary_expression
        //     parenthesized_expression
        //     conditional_expression

        // | Symbolic Expr | Evaluation     |
        // | defined A     | defA? 1 : 0    |
        // | A             | defA? valA : 0 |

        if (node.isSymbol(lang.identifier_s))
        {
            // Do not look up the symbol in the symbol table
            // Any symbol at this time will be treated as a symbolic value
            std::string_view name = node.textView();
            std::string defName = std::format("def{}", name);
            std::string valName = std::format("val{}", name);

            z3::expr def = ctx->bool_const(defName.c_str());
            z3::expr val = ctx->int_const(valName.c_str());

            // Create the expression
            z3::expr iteExpr = z3::ite(def, val, constExpr0);
            return iteExpr;
        }
        else if (node.isSymbol(lang.call_expression_s))
        {
            // There should be no call expression. It should have been expanded
            throw std::runtime_error(std::format("Unexpected call expression while symbolizing expression {}", node.textView()));
        }
        else if (node.isSymbol(lang.number_literal_s))
        {
            std::string spelling = node.text();
            std::string numberString = parseIntegerLiteralToDecimal(spelling);
            z3::expr val = ctx->int_val(numberString.c_str());
            return val;
        }
        else if (node.isSymbol(lang.char_literal_s))
        {
            throw std::runtime_error(std::format("Unexpected char literal while symbolizing expression {}", node.textView()));
        }
        else if (node.isSymbol(lang.preproc_defined_s))
        {
            // | Symbolic Expr | Evaluation     |
            // | defined A     | defA? 1 : 0    |

            TSNode idNode = node.childByFieldId(lang.preproc_defined_s.name_f);
            assert(idNode.isSymbol(lang.identifier_s));
            std::string_view name = idNode.textView();
            std::string defName = std::format("def{}", name);

            z3::expr def = ctx->bool_const(defName.c_str());
            z3::expr defExpr = bool2int(def);
            return defExpr;
        }
        else if (node.isSymbol(lang.unary_expression_s))
        {
            // Z(!, not)
            // Z(~, bnot)
            // Z(-, neg)
            // Z(+, pos)

            TSNode opNode = node.childByFieldId(lang.unary_expression_s.operator_f);
            TSNode argNode = node.childByFieldId(lang.unary_expression_s.argument_f);
            z3::expr arg = symbolizeExpression(argNode);
            if (opNode.textView() == lang.unary_expression_s.not_o)
            {
                z3::expr notExpr = bool2int(!int2bool(arg));
                return notExpr;
            }
            else if (opNode.textView() == lang.unary_expression_s.bnot_o)
            {
                // bitwise not
                z3::expr bnotExpr = z3::bv2int(~z3::int2bv(BIT_WIDTH, arg), true);
                return bnotExpr;
            }
            else if (opNode.textView() == lang.unary_expression_s.neg_o)
            {
                z3::expr negExpr = -arg;
                return negExpr;
            }
            else if (opNode.textView() == lang.unary_expression_s.pos_o)
            {
                return arg;
            }
            else assert(false);
        }
        else if (node.isSymbol(lang.binary_expression_s))
        {
            // Z(+, add)
            // Z(-, sub)
            // Z(*, mul)
            // Z(/, div)
            // Z(%, mod)
            // Z(||, or)
            // Z(&&, and)
            // Z(|, bor)
            // Z(^, bxor)
            // Z(&, band)
            // Z(==, eq)
            // Z(!=, neq)
            // Z(>, gt)
            // Z(>=, ge)
            // Z(<=, le)
            // Z(<, lt)
            // Z(<<, lsh)
            // Z(>>, rsh)

            TSNode opNode = node.childByFieldId(lang.binary_expression_s.operator_f);
            TSNode leftNode = node.childByFieldId(lang.binary_expression_s.left_f);
            TSNode rightNode = node.childByFieldId(lang.binary_expression_s.right_f);

            z3::expr left = symbolizeExpression(leftNode);
            z3::expr right = symbolizeExpression(rightNode);

            if (opNode.textView() == lang.binary_expression_s.add_o)
            {
                z3::expr addExpr = left + right;
                return addExpr;
            }
            else if (opNode.textView() == lang.binary_expression_s.sub_o)
            {
                z3::expr subExpr = left - right;
                return subExpr;
            }
            else if (opNode.textView() == lang.binary_expression_s.mul_o)
            {
                z3::expr mulExpr = left * right;
                return mulExpr;
            }
            else if (opNode.textView() == lang.binary_expression_s.div_o)
            {
                z3::expr divExpr = left / right;
                return divExpr;
            }
            else if (opNode.textView() == lang.binary_expression_s.mod_o)
            {
                z3::expr modExpr = left % right;
                return modExpr;
            }
            else if (opNode.textView() == lang.binary_expression_s.or_o)
            {
                z3::expr orExpr = bool2int(int2bool(left) || int2bool(right));
                return orExpr;
            }
            else if (opNode.textView() == lang.binary_expression_s.and_o)
            {
                z3::expr andExpr = bool2int(int2bool(left) && int2bool(right));
                return andExpr;
            }
            else if (opNode.textView() == lang.binary_expression_s.bor_o)
            {
                z3::expr borExpr = z3::bv2int(z3::int2bv(BIT_WIDTH, left) | z3::int2bv(BIT_WIDTH, right), true);
                return borExpr;
            }
            else if (opNode.textView() == lang.binary_expression_s.bxor_o)
            {
                z3::expr bxorExpr = z3::bv2int(z3::int2bv(BIT_WIDTH, left) ^ z3::int2bv(BIT_WIDTH, right), true);
                return bxorExpr;
            }
            else if (opNode.textView() == lang.binary_expression_s.band_o)
            {
                z3::expr bandExpr = z3::bv2int(z3::int2bv(BIT_WIDTH, left) & z3::int2bv(BIT_WIDTH, right), true);
                return bandExpr;
            }
            else if (opNode.textView() == lang.binary_expression_s.eq_o)
            {
                z3::expr eqExpr = bool2int(left == right);
                return eqExpr;
            }
            else if (opNode.textView() == lang.binary_expression_s.neq_o)
            {
                z3::expr neqExpr = bool2int(left != right);
                return neqExpr;
            }
            else if (opNode.textView() == lang.binary_expression_s.gt_o)
            {
                z3::expr gtExpr = bool2int(left > right);
                return gtExpr;
            }
            else if (opNode.textView() == lang.binary_expression_s.ge_o)
            {
                z3::expr geExpr = bool2int(left >= right);
                return geExpr;
            }
            else if (opNode.textView() == lang.binary_expression_s.le_o)
            {
                z3::expr leExpr = bool2int(left <= right);
                return leExpr;
            }
            else if (opNode.textView() == lang.binary_expression_s.lt_o)
            {
                z3::expr ltExpr = bool2int(left < right);
                return ltExpr;
            }
            else if (opNode.textView() == lang.binary_expression_s.lsh_o)
            {
                z3::expr lshExpr = z3::bv2int(z3::shl(z3::int2bv(BIT_WIDTH, left), z3::int2bv(BIT_WIDTH, right)), true);
                return lshExpr;
            }
            else if (opNode.textView() == lang.binary_expression_s.rsh_o)
            {
                z3::expr rshExpr = z3::bv2int(z3::ashr(z3::int2bv(BIT_WIDTH, left), z3::int2bv(BIT_WIDTH, right)), true);
                return rshExpr;
            }
            else assert(false);
        }
        else if (node.isSymbol(lang.parenthesized_expression_s))
        {
            // Parenthesized expression, just return the inner expression
            TSNode innerNode = node.childByFieldId(lang.parenthesized_expression_s.expr_f);
            z3::expr innerExpr = symbolizeExpression(innerNode);
            return innerExpr;
        }
        else if (node.isSymbol(lang.conditional_expression_s))
        {
            TSNode condNode = node.childByFieldId(lang.conditional_expression_s.condition_f);
            TSNode trueNode = node.childByFieldId(lang.conditional_expression_s.consequence_f);
            TSNode falseNode = node.childByFieldId(lang.conditional_expression_s.alternative_f);

            z3::expr condExpr = symbolizeExpression(condNode);
            z3::expr trueExpr = symbolizeExpression(trueNode);
            z3::expr falseExpr = symbolizeExpression(falseNode);

            z3::expr condIteExpr = z3::ite(int2bool(condExpr), trueExpr, falseExpr);
            return condIteExpr;
        }
        else assert(false);
    }

    z3::expr int2bool(const z3::expr & expr)
    {
        assert(expr.is_int());
        return z3::ite(expr != 0, ctx->bool_val(true), ctx->bool_val(false));
    }

    z3::expr bool2int(const z3::expr & expr)
    {
        assert(expr.is_bool());
        return z3::ite(expr, constExpr1, constExpr0);
    }

    // Parse a string into preprocessor tokens
    // Returns (tree, tokens)
    std::tuple<TSTree, TSNode> parseIntoPreprocTokens(std::string_view source)
    {
        if (source.empty()) return { TSTree(), TSNode() };
        std::string ifSource = std::format("#if {}\n#endif\n", source);
        TSTree tree = parser.parseString(std::move(ifSource));
        TSNode root = tree.rootNode(); // translation_unit
        TSNode tokens = root.firstChildForByte(0).childByFieldId(lang.preproc_if_s.condition_f);
        assert(tokens.isSymbol(lang.preproc_tokens_s));
        return { std::move(tree), std::move(tokens) };
    }

    // Parse a string into a preprocessor expression
    // Returns (tree, expr)
    std::tuple<TSTree, TSNode> parseIntoExpression(std::string_view source)
    {
        if (source.empty()) return { TSTree(), TSNode() };
        std::string evalSource = std::format("#eval {}\n#endeval\n", source);
        TSTree tree = parser.parseString(std::move(evalSource));
        TSNode root = tree.rootNode(); // translation_unit
        TSNode expr = root.firstChildForByte(0).childByFieldId(lang.preproc_eval_s.expr_f);
        return { std::move(tree), std::move(expr) };
    }

private:
    const CPreproc & lang;
    TSParser parser;

    z3::context * ctx;

    // Cache for the ownership of temporarily parsed trees
    TSTree tempTokensTree;
    TSNode constToken0;
    TSNode constToken1;
    TSNode constTokenNot;
    TSNode constTokenDefined;

    // The bit width of the symbolic values
    const int BIT_WIDTH = 32;
    z3::expr constExpr0;
    z3::expr constExpr1;

    static bool isIntegerSuffixChar(char c)
    {
        switch (c)
        {
            case 'u':
            case 'U':
            case 'l':
            case 'L':
            case 'w':
            case 'W':
                return true;
            default:
                return false;
        }
    }

    static int digitValueForBase(char c, int base)
    {
        if (c >= '0' && c <= '9')
        {
            int val = c - '0';
            if (val < base) return val;
        }
        if (base > 10)
        {
            char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            int span = base - 10;
            if (lower >= 'a' && lower < 'a' + span)
            {
                return 10 + (lower - 'a');
            }
        }
        return -1;
    }

    static std::string parseIntegerLiteralToDecimal(std::string_view literal)
    {
        if (literal.empty())
        {
            throw std::runtime_error("Empty number literal");
        }

        std::size_t pos = 0;
        bool negative = false;
        if (literal[pos] == '+' || literal[pos] == '-')
        {
            negative = literal[pos] == '-';
            pos++;
            if (pos >= literal.size())
            {
                throw std::runtime_error("Sign without digits in number literal");
            }
        }

        int base = 10;
        if (pos + 1 < literal.size() && literal[pos] == '0')
        {
            char prefix = literal[pos + 1];
            if (prefix == 'x' || prefix == 'X')
            {
                base = 16;
                pos += 2;
            }
            else if (prefix == 'b' || prefix == 'B')
            {
                base = 2;
                pos += 2;
            }
        }

        std::string digits;
        digits.reserve(literal.size() - pos);
        for (; pos < literal.size(); ++pos)
        {
            char c = literal[pos];
            if (c == '\'')
            {
                continue;
            }
            if (c == '.' || c == 'e' || c == 'E' || c == 'p' || c == 'P')
            {
                throw std::runtime_error(std::string("Floating-point literal not supported: ") + std::string(literal));
            }

            int digit = digitValueForBase(c, base);
            if (digit >= 0)
            {
                digits.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            }
            else
            {
                break;
            }
        }

        if (digits.empty())
        {
            throw std::runtime_error(std::string("Failed to parse number literal: ") + std::string(literal));
        }

        for (; pos < literal.size(); ++pos)
        {
            char c = literal[pos];
            if (!isIntegerSuffixChar(c))
            {
                throw std::runtime_error(std::string("Unexpected suffix in number literal: ") + std::string(literal));
            }
        }

        std::vector<int> decimalDigits{0};
        for (char c : digits)
        {
            int digitValue;
            if (c >= '0' && c <= '9')
            {
                digitValue = c - '0';
            }
            else
            {
                digitValue = 10 + (static_cast<int>(c) - 'A');
            }

            int carry = digitValue;
            for (std::size_t i = 0; i < decimalDigits.size(); ++i)
            {
                int temp = decimalDigits[i] * base + carry;
                decimalDigits[i] = temp % 10;
                carry = temp / 10;
            }
            while (carry > 0)
            {
                decimalDigits.push_back(carry % 10);
                carry /= 10;
            }
        }

        while (decimalDigits.size() > 1 && decimalDigits.back() == 0)
        {
            decimalDigits.pop_back();
        }

        std::string decimalString;
        decimalString.reserve(decimalDigits.size() + (negative ? 1 : 0));
        for (auto it = decimalDigits.rbegin(); it != decimalDigits.rend(); ++it)
        {
            decimalString.push_back(static_cast<char>('0' + *it));
        }

        if (negative && decimalString != "0")
        {
            decimalString.insert(decimalString.begin(), '-');
        }

        return decimalString;
    }
};

} // namespace Hayroll

#endif // HAYROLL_MACROEXPANDER_HPP
