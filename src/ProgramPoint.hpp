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
    ConstIncludeTreePtr includeTree;
    TSNode node;

    std::string toString() const
    {
        if (!node) return std::format("{}:EOF", includeTree->path.string());
        return std::format
        (
            "{}:{}:{}~{}:{}",
            includeTree->path.string(),
            node.startPoint().row + 1,
            node.startPoint().column + 1,
            node.endPoint().row + 1,
            node.endPoint().column + 1
        );
    }

    std::string toStringFull() const
    {
        // Print the whole include tree
        return std::format("{}\n{}\n", includeTree->toString(), toString());
    }

    ProgramPoint parent(std::optional<TSNode> includeNodeInParentFile = std::nullopt) const
    {
        // In the same file, just do parent for the TSNode.
        if (TSNode parentNode = node.parent())
        {
            return ProgramPoint{includeTree, parentNode};
        }
        // In a different file, we need to find the parent include tree.
        ConstIncludeTreePtr parentIncludeTree = includeTree->parent.lock();
        assert(parentIncludeTree);
        assert(includeNodeInParentFile.has_value());
        // If we have a node in the parent file, we can use it to find the parent include tree.
        return ProgramPoint{parentIncludeTree, includeNodeInParentFile.value()};
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

}; // namespace Hayroll

#endif // HAYROLL_PROGRAMPOINT_HPP
