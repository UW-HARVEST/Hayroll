#ifndef HAYROLL_TREESITTERCPREPROC_HPP
#define HAYROLL_TREESITTERCPREPROC_HPP

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
    #define C_PREPROC_GRAMMAR \
        X(arg) XX \
        X(argument_list) \
            Y(argument) \
        XX \
        X(binary_expression) \
            Y(left) \
            Y(operator) \
            Y(right) \
        XX \
        X(call) \
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
        X(def) \
            Y(name) \
            Y(value) \
        XX \
        X(defined) XX \
        X(directive) XX \
        X(elif) \
            Y(condition) \
            Y(body) \
            Y(alternative) \
        XX \
        X(elifdef) \
            Y(name) \
            Y(body) \
            Y(alternative) \
        XX \
        X(elifndef) \
            Y(name) \
            Y(body) \
            Y(alternative) \
        XX \
        X(else) \
            Y(body) \
        XX \
        X(error) \
            Y(message) \
        XX \
        X(eval) \
            Y(expression) \
        XX \
        X(expression) XX \
        X(function_def) \
            Y(name) \
            Y(parameters) \
            Y(value) \
        XX \
        X(if) \
            Y(condition) \
            Y(body) \
            Y(alternative) \
        XX \
        X(ifdef) \
            Y(name) \
            Y(body) \
            Y(alternative) \
        XX \
        X(ifndef) \
            Y(name) \
            Y(body) \
            Y(alternative) \
        XX \
        X(include) \
            Y(path) \
        XX \
        X(include_next) \
            Y(path) \
        XX \
        X(line) \
            Y(line_number) \
            Y(filename) \
        XX \
        X(params) \
            Y(parameter) \
        XX \
        X(parenthesized_expression) XX \
        X(unary_expression) \
            Y(operator) \
            Y(argument) \
        XX \
        X(undef) \
            Y(name) \
        XX

    #define X(sym) , sym##_s({ .str = "preproc_"#sym, .tsSymbol = symbolForName("preproc_"#sym, true)
    #define Y(fld) , .fld##_f = { .str = #fld, .tsFieldId = fieldIdForName(#fld) }
    #define XX })
    CPreproc()
        : TSLanguage(ts::tree_sitter_c_preproc())
        C_PREPROC_GRAMMAR
    {
    }
    #undef XX
    #undef Y
    #undef X
    // Exapmle:
    // , ifndef_s({ .str = "preproc_ifndef", .tsSymbol = symbolForName("preproc_ifndef", true) , .name_f = { .str = "name", .tsFieldId = fieldIdForName("name") } , .body_f = { .str = "body", .tsFieldId = fieldIdForName("body") } , .alternative_f = { .str = "alternative", .tsFieldId = fieldIdForName("alternative") } })

    #define X(sym) struct type_##sym##_s { std::string str; TSSymbol tsSymbol; operator TSSymbol() const { return tsSymbol; }
    #define Y(fld) const struct type_##fld##_f { std::string str; TSFieldId tsFieldId; operator TSFieldId() const { return tsFieldId; } } fld##_f;
    #define XX };
    C_PREPROC_GRAMMAR
    #undef XX
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
    // };

    #define X(sym) const type_##sym##_s sym##_s;
    #define Y(fld)
    #define XX
    C_PREPROC_GRAMMAR
    #undef XX
    #undef Y
    #undef X
    // Exapmle:
    // const struct type_ifndef_s ifndef_s;

    #undef C_PREPROC_GRAMMAR
};

} // namespace Hayroll

#endif // HAYROLL_TREESITTERCPREPROC_HPP
