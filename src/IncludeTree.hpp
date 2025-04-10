// Tree representation of the include hierarchy for a comilation unit.

#ifndef HAYROLL_INCLUDETREE_HPP
#define HAYROLL_INCLUDETREE_HPP

#include <filesystem>
#include <memory>
#include <map>
#include <string_view>
#include <optional>
#include <sstream>

#include "Util.hpp"
#include "TreeSitter.hpp"

namespace Hayroll
{

struct IncludeTree;
using IncludeTreePtr = std::shared_ptr<IncludeTree>;
using ConstIncludeTreePtr = std::shared_ptr<const IncludeTree>;

struct IncludeTree
    : public std::enable_shared_from_this<IncludeTree>
{
public:
uint32_t line; // Line number of the include in its parent file
    std::filesystem::path path;
    
    std::map<int, IncludeTreePtr> children; // Line number to IncludeTreePtr
    std::weak_ptr<const IncludeTree> parent;

    // Constructor
    // An IncludeTree object shall only be managed by a shared_ptr
    static IncludeTreePtr make
    (
        uint32_t line,
        const std::filesystem::path & path,
        ConstIncludeTreePtr parent = nullptr
    )
    {
        auto tree = std::make_shared<IncludeTree>();
        tree->line = line;
        tree->path = path;
        tree->parent = parent;
        return tree;
    }

    // Add a child IncludeTree object to the current one
    void addChild(uint32_t line, const std::filesystem::path & path)
    {
        children[line] = make(line, path, shared_from_this());
    }

    // Test if the given string path (spelt header name) is a suffix of the current path.
    // The arg "header" could include the path to the file, or just the file name.
    // e.g. "z3++.h" or "z3/z3++.h" or "../z3/z3++.h".
    bool endsWith(const std::string_view header) const
    {
        return path.string().ends_with(header);
    }

    // Get a vector of ancestor directories
    // This is useful for resolving user includes
    std::vector<std::filesystem::path> getAncestorDirs() const
    {
        // Peel off the filenames from the path
        std::vector<std::filesystem::path> dirs;
        auto node = shared_from_this();
        while (node)
        {
            auto dir = node->path.parent_path();
            dirs.push_back(dir);
            node = node->parent.lock();
        }
        return dirs;
    }

    std::string toString(size_t depth = 0) const
    {
        std::stringstream ss;
        for (size_t i = 0; i < depth; i++)
        {
            ss << ".";
        }
        ss << " ";

        // parentPath:lineNumber -> path
        if (auto parent = this->parent.lock())
        {
            ss << parent->path.string() << ":" << line << " -> ";
        }
        ss << path.string() << "\n";

        for (const auto &[line, child] : children)
        {
            ss << child->toString(depth + 1);
        }
        return ss.str();
    }
};

// A macro program point in the include tree. 
// In cases where a file is included multiple times,
// different inclusion instances contain different macro program points.
struct ProgramPoint
{
    ConstIncludeTreePtr includeTree;
    TSNode node;

    bool operator<=>(const ProgramPoint & other) const = default;

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

} // namespace Hayroll::IncludeTree

#endif // HAYROLL_INCLUDETREE_HPP
