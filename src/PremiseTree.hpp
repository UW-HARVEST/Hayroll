#ifndef HAYROLL_PREMISETREE_HPP
#define HAYROLL_PREMISETREE_HPP

#include <memory>
#include <variant>
#include <unordered_map>

#include <z3++.h>

#include <spdlog/spdlog.h>

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

    std::string toString(size_t depth = 0) const
    {
        std::string str = std::format
        (
            "{}{} {}",
            std::string(depth * 4, ' '),
            programPoint.toString(),
            premise.to_string()
        );
        for (const PremiseTreePtr & child : children)
        {
            str += "\n" + child->toString(depth + 1);
        }
        return str;
    }

    // Simplify premises of all descendants.
    void refine()
    {
        premise = simplifyOrOfAnd(premise);

        for (const PremiseTreePtr & child : children)
        {
            // child->premise = premise && child->premise;
            child->refine();
        }
    }

    // Find the smallest premise tree node that contains the target program point.
    const PremiseTree * findEnclosingNode(const ProgramPoint & target) const
    {
        assert(programPoint.contains(target));
        for (const PremiseTreePtr & child : children)
        {
            if (child->programPoint.contains(target))
            {
                return child->findEnclosingNode(target);
            }
        }
        return this;
    }
};

// A helper class that takes down info during symbolic execution to build the premise tree.
class PremiseTreeScribe
{
public:
    PremiseTreeScribe()
        : tree(nullptr), map(), init(false)
    {
    }

    PremiseTreeScribe(const ProgramPoint & programPoint, const z3::expr & premise)
        : tree(PremiseTree::make(programPoint, premise)), map(), init(true)
    {
        map.insert_or_assign(programPoint, tree.get());
    }

    // If a premise tree node for the given program point already exists, disjunct the premise with the existing one.
    // Otherwise, create a new premise tree node and add the premise to it, automatically finding the parent node.
    // When the parent node is in a different file, includeNodeInParentFile must be provided.
    PremiseTree * addPremiseOrCreateChild
    (
        const ProgramPoint & programPoint,
        const z3::expr & premise,
        std::optional<TSNode> includeNodeInParentFile = std::nullopt
    )
    {
        if (!init) return nullptr;
        SPDLOG_DEBUG(std::format("Scribe adding premise: \n Program point: {}\n Premise: {}", programPoint.toString(), premise.to_string()));
        if (auto it = map.find(programPoint); it != map.end())
        {
            PremiseTree * treeNode = it->second;
            treeNode->premise = treeNode->premise || premise;
            SPDLOG_DEBUG(std::format("New premise: {}", treeNode->premise.to_string()));
            return treeNode;
        }
        // Keep going to parent until such program point has a corresponding premise tree node.
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
            ancestor = ancestor.parent(includeNodeInParentFile);
            assert(ancestor);
        }

        PremiseTree * newTree = parent->addChild(programPoint, premise);
        map.insert_or_assign(programPoint, newTree);

        SPDLOG_DEBUG(std::format("Created new premise tree node: {}", newTree->toString()));
        SPDLOG_DEBUG(std::format("Parent premise tree node: {}", parent->toString()));
        SPDLOG_DEBUG(std::format("New premise: {}", newTree->premise.to_string()));

        return newTree;
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
    PremiseTreePtr tree;
    // A mapping from the program point to the premise tree node.
    // It does not need ownership so it's using raw pointers.
    std::unordered_map<ProgramPoint, PremiseTree *, ProgramPoint::Hasher> map;
    bool init;
};

} // namespace Hayroll

#endif // HAYROLL_PREMISETREE_HPP
