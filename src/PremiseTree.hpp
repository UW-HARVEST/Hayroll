#ifndef HAYROLL_PREMISETREE_HPP
#define HAYROLL_PREMISETREE_HPP

#include <memory>
#include <variant>
#include <unordered_map>

#include <spdlog/spdlog.h>

#include "z3++.h"

#include "Util.hpp"
#include "IncludeTree.hpp"
#include "TreeSitter.hpp"

namespace Hayroll
{

struct PremiseTree;
using PremiseTreePtr = std::shared_ptr<PremiseTree>;
using ConstPremiseTreePtr = std::shared_ptr<const PremiseTree>;

// A tree that keeps track of the premises of an #if/#else body or a macro expansion in C code.
// A translation_unit node (top node of a file) also has a premise tree node.
struct PremiseTree
    : public std::enable_shared_from_this<PremiseTree>
{
    ProgramPoint programPoint;
    // For #if/#else bodies, there is only one premise needed for entering the body.
    z3::expr premise;
    // For macro expansions, for each different definition of the macro, we need a different premise,
    // Which means "what conditions you need for the macro to be expanded using this definition".
    // When using this map, use emplace or insert instead of operator[], because z3::expr is not default constructible.
    std::unordered_map<ProgramPoint, z3::expr, ProgramPoint::Hasher> macroPremises;

    std::vector<PremiseTreePtr> children;
    std::weak_ptr<const PremiseTree> parent;

    static PremiseTreePtr make
    (
        const ProgramPoint & programPoint,
        const z3::expr & premise,
        ConstPremiseTreePtr parent = nullptr
    )
    {
        PremiseTreePtr tree = std::make_shared<PremiseTree>
        (
            programPoint,
            premise,
            parent
        );
        return tree;
    }

    void addChild
    (
        const ProgramPoint & programPoint,
        const z3::expr & premise
    )
    {
        children.push_back(make(programPoint, premise, shared_from_this()));
    }

    // Users shall not use this constructor directly.
    // This it meant for std::make_shared<PremiseTree> to work.
    PremiseTree
    (
        const ProgramPoint & programPoint,
        const z3::expr & premise,
        ConstPremiseTreePtr parent
    )
        : programPoint(programPoint), premise(premise), parent(parent)
    {
    }
};

} // namespace Hayroll

#endif // HAYROLL_PREMISETREE_HPP
