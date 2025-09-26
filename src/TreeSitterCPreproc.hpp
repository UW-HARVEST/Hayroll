// Specialized TSLanguage class for the tree-sitter-c_preproc language
// Also encodes the symbolized grammar for the language

#ifndef HAYROLL_TREESITTERCPREPROC_HPP
#define HAYROLL_TREESITTERCPREPROC_HPP

#include <vector>

#include "TreeSitter.hpp"

namespace ts
{
    #include "tree_sitter/tree-sitter-c-preproc.h"
} // namespace ts

namespace Hayroll
{

class CPreproc : public TSLanguage
{
public:
    // X-macro that defines the symbols and fields of the tree-sitter-c_preproc language
    // In this way we don't need to use string literals in the code
    // X: start of symbol (syntactic kind)
    // XX: end of symbol
    // Y: field
    // Z: operator
    #define C_PREPROC_GRAMMAR \
        X(translation_unit) XX \
        X(block_items) XX \
        X(preproc_arg) XX \
        X(argument_list) \
            Y(argument) \
        XX \
        X(binary_expression) \
            Y(left) \
            Y(operator) \
            Y(right) \
            Z(+, add) \
            Z(-, sub) \
            Z(*, mul) \
            Z(/, div) \
            Z(%, mod) \
            Z(||, or) \
            Z(&&, and) \
            Z(|, bor) \
            Z(^, bxor) \
            Z(&, band) \
            Z(==, eq) \
            Z(!=, neq) \
            Z(>, gt) \
            Z(>=, ge) \
            Z(<=, le) \
            Z(<, lt) \
            Z(<<, lsh) \
            Z(>>, rsh) \
        XX \
        X(preproc_call) \
            Y(directive) \
            Y(argument) \
        XX \
        X(call_expression) \
            Y(function) \
            Y(arguments) \
        XX \
        X(conditional_expression) \
            Y(condition) \
            Y(consequence) \
            Y(alternative) \
        XX \
        X(preproc_def) \
            Y(name) \
            Y(value) \
        XX \
        X(preproc_defined) \
            Y(name) \
        XX \
        X(preproc_directive) XX \
        X(preproc_elif) \
            Y(condition) \
            Y(body) \
            Y(alternative) \
        XX \
        X(preproc_elifdef) \
            Y(name) \
            Y(body) \
            Y(alternative) \
        XX \
        X(preproc_elifndef) \
            Y(name) \
            Y(body) \
            Y(alternative) \
        XX \
        X(preproc_else) \
            Y(body) \
        XX \
        X(preproc_error) \
            Y(message) \
        XX \
        X(preproc_function_def) \
            Y(name) \
            Y(parameters) \
            Y(value) \
        XX \
        X(preproc_if) \
            Y(condition) \
            Y(body) \
            Y(alternative) \
        XX \
        X(preproc_ifdef) \
            Y(name) \
            Y(body) \
            Y(alternative) \
        XX \
        X(preproc_ifndef) \
            Y(name) \
            Y(body) \
            Y(alternative) \
        XX \
        X(preproc_include) \
            Y(path) \
        XX \
        X(preproc_include_next) \
            Y(path) \
        XX \
        X(preproc_line) \
            Y(line_number) \
            Y(filename) \
            Y(flag) \
        XX \
        X(preproc_params) \
            Y(parameter) \
        XX \
        X(preproc_tokens) \
            Y(token) \
        XX \
        X(c_tokens) \
            Y(token) \
        XX \
        X(comment) XX \
        X(parenthesized_expression) \
            Y(expr) \
        XX \
        X(unary_expression) \
            Y(operator) \
            Y(argument) \
            Z(!, not) \
            Z(~, bnot) \
            Z(-, neg) \
            Z(+, pos) \
        XX \
        X(preproc_undef) \
            Y(name) \
        XX \
        X(preproc_eval) \
            Y(expr) \
        XX \
        X(number_literal) XX \
        X(char_literal) XX \
        X(preproc_defined_literal) XX \
        X(identifier) XX \
        X(string_literal) \
            Y(content) \
        XX \
        X(system_lib_string) \
            Y(content) \
        XX \
        X(string_content) XX \

    #define X(sym) , sym##_s({ .str = #sym, .tsSymbol = symbolForName(#sym, true)
    #define Y(fld) , .fld##_f = { .str = #fld, .tsFieldId = fieldIdForName(#fld) }
    #define Z(op, name)
    #define XX })
    CPreproc()
        : TSLanguage(ts::tree_sitter_c_preproc())
        C_PREPROC_GRAMMAR
    {
    }
    #undef XX
    #undef Z
    #undef Y
    #undef X
    // Exapmle:
    // , ifndef_s({ .str = "preproc_ifndef", .tsSymbol = symbolForName("preproc_ifndef", true) , .name_f = { .str = "name", .tsFieldId = fieldIdForName("name") } , .body_f = { .str = "body", .tsFieldId = fieldIdForName("body") } , .alternative_f = { .str = "alternative", .tsFieldId = fieldIdForName("alternative") } })

    #define X(sym) struct type_##sym##_s { std::string str; TSSymbol tsSymbol; operator TSSymbol() const { return tsSymbol; }
    #define Y(fld) const struct type_##fld##_f { std::string str; TSFieldId tsFieldId; operator TSFieldId() const { return tsFieldId; } } fld##_f;
    #define Z(op, name) const std::string name##_o = #op;
    #define XX };
    C_PREPROC_GRAMMAR
    #undef XX
    #undef Z
    #undef Y
    #undef X
    // Exapmle:
    // struct type_ifndef_s
    // {
    //     std::string str;
    //     TSSymbol tsSymbol;
    //     operator TSSymbol() const
    //     {
    //         return tsSymbol;
    //     }
    //     const struct type_name_f
    //     {
    //         std::string str;
    //         TSFieldId tsFieldId;
    //         operator TSFieldId() const
    //         {
    //             return tsFieldId;
    //         }
    //     } name_f;
    //     const struct type_body_f
    //     {
    //         std::string str;
    //         TSFieldId tsFieldId;
    //         operator TSFieldId() const
    //         {
    //             return tsFieldId;
    //         }
    //     } body_f;
    //     const struct type_alternative_f
    //     {
    //         std::string str;
    //         TSFieldId tsFieldId;
    //         operator TSFieldId() const
    //         {
    //             return tsFieldId;
    //         }
    //     } alternative_f;
    //     // Only for operators
    //     const std::string add_o = "+";
    // };

    #define X(sym) const type_##sym##_s sym##_s;
    #define Y(fld)
    #define Z(op, name)
    #define XX
    C_PREPROC_GRAMMAR
    #undef XX
    #undef Z
    #undef Y
    #undef X
    // Exapmle:
    // const struct type_ifndef_s ifndef_s;

    #undef C_PREPROC_GRAMMAR

    std::vector<TSNode> tokensToTokenVector(const TSNode & tokens) const
    {
        assert(tokens.isSymbol(preproc_tokens_s));
        std::vector<TSNode> result;
        for (const TSNode & token : tokens.iterateChildren())
        {
            result.emplace_back(token);
        }
        return result;
    }
};

} // namespace Hayroll

#endif // HAYROLL_TREESITTERCPREPROC_HPP
