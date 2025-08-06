#ifndef HAYROLL_PROGRAMPOINT_HPP
#define HAYROLL_PROGRAMPOINT_HPP

#include <string>
#include <optional>

#include "Util.hpp"

#include "TreeSitter.hpp"
#include "IncludeTree.hpp"

namespace Hayroll
{

// A macro program point in the include tree. 
// In cases where a file is included multiple times,
// different inclusion instances contain different macro program points.
struct ProgramPoint
{
    IncludeTreePtr includeTree;
    TSNode node;

    std::string toString() const
    {
        if (!node) return std::format("{}:EOF", includeTree->path.string());
        return std::format
        (
            "{}:{}~{} {}",
            includeTree->path.string(),
            node.startPoint().toString(),
            node.endPoint().toString(),
            node.type()
        );
    }

    std::string toStringFull() const
    {
        // Print the whole include tree
        return std::format("{}\n{}\n", includeTree->stacktrace(), toString());
    }

    ProgramPoint parent() const
    {
        // In the same file, just do parent for the TSNode.
        if (TSNode parentNode = node.parent())
        {
            return ProgramPoint{includeTree, parentNode};
        }
        // In a different file, we need to find the parent include tree.
        IncludeTreePtr parentIncludeTree = includeTree->parent.lock();
        assert(parentIncludeTree);
        assert(includeTree->includeNode);
        // If we have a node in the parent file, we can use it to find the parent include tree.
        return ProgramPoint{parentIncludeTree, includeTree->includeNode};
    }

    ProgramPoint nextSibling() const
    {
        return ProgramPoint{includeTree, node.nextSibling()};
    }

    ProgramPoint firstChild() const
    {
        return ProgramPoint{includeTree, node.firstChildForByte(0)};
    }

    operator bool() const
    {
        // TSNode can be null (meaning EOF), but IncludeTree cannot.
        return includeTree != nullptr;
    }

    bool operator==(const ProgramPoint & other) const
    {
        return includeTree == other.includeTree && node == other.node;
    }

    bool contains(const ProgramPoint & other) const
    {
        if (includeTree == other.includeTree)
        {
            return node.startByte() <= other.node.startByte() && node.endByte() >= other.node.endByte();
        }
        if (other.includeTree->isContainedBy(node))
        {
            return true;
        }
        return false;
    }

    struct Hasher
    {
        std::size_t operator()(const ProgramPoint & programPoint) const noexcept
        {
            std::size_t h1 = std::hash<decltype(programPoint.includeTree)>()(programPoint.includeTree);
            std::size_t h2 = TSNode::Hasher{}(programPoint.node);
            return h1 ^ (h2 << 1);
        }
    };
};

} // namespace Hayroll

#endif // HAYROLL_PROGRAMPOINT_HPP
