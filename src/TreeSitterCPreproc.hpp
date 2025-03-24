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
    #define SYMBOL_LIST \
        X(preproc_arg) \
        X(preproc_argument_list) \
        X(preproc_binary_expression) \
        X(preproc_call) \
        X(preproc_call_expression) \
        X(preproc_conditional_expression) \
        X(preproc_def) \
        X(preproc_defined) \
        X(preproc_directive) \
        X(preproc_elif) \
        X(preproc_elifdef) \
        X(preproc_elifndef) \
        X(preproc_else) \
        X(preproc_error) \
        X(preproc_eval) \
        X(preproc_expression) \
        X(preproc_function_def) \
        X(preproc_if) \
        X(preproc_ifdef) \
        X(preproc_ifndef) \
        X(preproc_include) \
        X(preproc_include_next) \
        X(preproc_line) \
        X(preproc_params) \
        X(preproc_parenthesized_expression) \
        X(preproc_unary_expression) \
        X_END(preproc_undef)

    #define X(name) name(symbolForName(#name, true)),
    #define X_END(name) name(symbolForName(#name, true))
    CPreproc()
        : TSLanguage(ts::tree_sitter_c_preproc()),
        SYMBOL_LIST
    {
    }
    #undef X_END
    #undef X

    #define X(name) const TSSymbol name;
    #define X_END(name) const TSSymbol name;
    SYMBOL_LIST
    #undef X_END
    #undef X

    #undef SYMBOL_LIST
};

} // namespace Hayroll

#endif // HAYROLL_TREESITTERCPREPROC_HPP
