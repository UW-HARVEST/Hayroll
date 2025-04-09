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
using PremiseTreePtr = std::unique_ptr<PremiseTree>;
using ConstPremiseTreePtr = std::unique_ptr<const PremiseTree>;

// A tree that keeps track of the premises of an #if/#else body or a macro expansion in C code.
// A translation_unit node (top node of a file) also has a premise tree node.
struct PremiseTree
{
    ProgramPoint programPoint;
    // For #if/#else bodies, there is only one premise needed for entering the body.
    z3::expr premise;
    // For macro expansions, for each different definition of the macro, we need a different premise,
    // Which means "what conditions you need for the macro to be expanded using this definition".
    // When using this map, use emplace or insert instead of operator[], because z3::expr is not default constructible.
    std::unordered_map<ProgramPoint, z3::expr, ProgramPoint::Hasher> macroPremises;

    std::vector<PremiseTreePtr> children;
    const PremiseTree * parent;

    static PremiseTreePtr make
    (
        const ProgramPoint & programPoint,
        const z3::expr & premise,
        const PremiseTree * parent = nullptr
    )
    {
        PremiseTreePtr tree = std::make_unique<PremiseTree>
        (
            programPoint,
            premise,
            parent
        );
        return tree;
    }

    PremiseTree * addChild
    (
        const ProgramPoint & programPoint,
        const z3::expr & premise
    )
    {
        PremiseTreePtr child = PremiseTree::make(programPoint, premise, this);
        children.push_back(std::move(child));
        return children.back().get();
    }

    // Users shall not use this constructor directly.
    // This it meant for std::make_shared<PremiseTree> to work.
    PremiseTree
    (
        const ProgramPoint & programPoint,
        const z3::expr & premise,
        const PremiseTree * parent
    )
        : programPoint(programPoint), premise(premise), parent(parent)
    {
    }

    std::string toString() const
    {
        std::string str = programPoint.toString() + "\n" + premise.to_string();
        for (const PremiseTreePtr & child : children)
        {
            str += "\n" + child->toString();
        }
        return str;
    }
};

// A helper class that takes down info during symbolic execution and builds the premise tree.
class PremiseTreeScribe
{
public:
    PremiseTreeScribe(z3::context & ctx, const ProgramPoint & programPoint)
        : ctx(ctx), tree(PremiseTree::make(programPoint, ctx.bool_val(true)))
    {
    }

    // If a premise tree node for the given program point already exists, conjunct the premise with the current premise. 
    // Otherwise, create a new premise tree node and add the premise to it, automatically finding the parent node.
    // If the parent node is in a different file, includeNodeInParentFile must be provided.
    void addPremiseOrCreateChild
    (
        const ProgramPoint & programPoint,
        const z3::expr & premise,
        std::optional<TSNode> includeNodeInParentFile = std::nullopt
    )
    {
        if (auto it = map.find(programPoint); it != map.end())
        {
            PremiseTree * treeNode = it->second;
            treeNode->premise = treeNode->premise && premise;
            return;
        }
        // Keep goint to parent until such program point has a corresponding premise tree node.
        ProgramPoint ancestor = programPoint.parent(includeNodeInParentFile);
        PremiseTree * parent = nullptr;
        while (true)
        {
            if (auto it = map.find(ancestor); it != map.end())
            {
                parent = it->second;
                break;
            }
            // If we reach the root node, we can't find a parent.
            ancestor = ancestor.parent();
            assert(ancestor);
        }

        PremiseTree * newTree = parent->addChild(programPoint, premise);
        map.insert({programPoint, newTree});
    }

    PremiseTreePtr takeTree()
    {
        PremiseTreePtr result = std::move(tree);
        map.clear();
        return result;
    }

    PremiseTree * borrowTree()
    {
        return tree.get();
    }

private:
    z3::context & ctx;

    PremiseTreePtr tree;
    // A mapping from the program point to the premise tree node.
    // It does not need ownership so it's using raw pointers.
    std::unordered_map<ProgramPoint, PremiseTree *, ProgramPoint::Hasher> map;
};

} // namespace Hayroll

#endif // HAYROLL_PREMISETREE_HPP
