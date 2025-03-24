#ifndef HAYROLL_TREESITTERCPREPROC_HPP
#define HAYROLL_TREESITTERCPREPROC_HPP

#include "TreeSitter.hpp"

namespace ts
{
    #include "tree_sitter/tree-sitter-c-preproc.h"
} // namespace ts

namespace Hayroll
{

class TSLanguageCPreproc : public TSLanguage
{
public:
    TSLanguageCPreproc()
        : TSLanguage(ts::tree_sitter_c_preproc())
    {
    }
};

} // namespace Hayroll

#endif // HAYROLL_TREESITTERCPREPROC_HPP