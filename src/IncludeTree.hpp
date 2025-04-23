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
    : public std::enable_shared_from_this<IncludeTree>,
      public std::ranges::view_interface<IncludeTree>
{
public:
    TSNode includeNode; // The node in the parent file AST that includes this file
    std::filesystem::path path;
    bool isSystemInclude = false; // True if the include is a system include, i.e., concretely executed and not in the astBank
    
    std::map<TSNode, IncludeTreePtr> children; // Line number -> IncludeTreePtr
    std::weak_ptr<IncludeTree> parent;

    // Constructor
    // An IncludeTree object shall only be managed by a shared_ptr
    static IncludeTreePtr make
    (
        const TSNode & includeNode,
        const std::filesystem::path & path,
        IncludeTreePtr parent = nullptr,
        bool isSystemInclude = false
    )
    {
        auto tree = std::make_shared<IncludeTree>();
        tree->includeNode = includeNode;
        tree->path = path;
        tree->parent = parent;
        tree->isSystemInclude = isSystemInclude;
        return tree;
    }

    // Add a child IncludeTree object to the current one
    // Do not use canonicalized path, as the ".."s may be part of the include name in source code
    IncludeTreePtr addChild(TSNode includeNode, const std::filesystem::path & path, bool isSystemInclude = false)
    {
        children[includeNode] = make(includeNode, path, shared_from_this(), isSystemInclude);
        return children[includeNode];
    }

    // Test if the given string path (spelt header name) is a suffix of the current path.
    // The arg "header" could include the path to the file, or just the file name.
    // e.g. "z3++.h" or "z3/z3++.h" or "../z3/z3++.h".
    bool endsWith(const std::string_view header) const
    {
        return path.string().ends_with(header);
    }

    bool isAncestorOf(const ConstIncludeTreePtr & child) const
    {
        // Check if the child is in the current tree
        if (child == shared_from_this()) return true;
        // Check if the child is in the parent tree
        if (ConstIncludeTreePtr parent = child->parent.lock())
        {
            return parent->isAncestorOf(shared_from_this());
        }
        return false;
    }

    bool isContainedBy(const TSNode & node) const
    {
        for (TSNode ancestorNode = includeNode; ancestorNode; ancestorNode = ancestorNode.parent())
        {
            if (ancestorNode == node)
            {
                return true;
            }
        }
        if (ConstIncludeTreePtr parent = this->parent.lock())
        {
            return parent->isContainedBy(node);
        }
        return false;
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

    // Print the entire subtree
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
            ss << parent->path.string() << ":";
            if (includeNode)
            {
                ss << std::format
                (
                    "{}:{}",
                    parent->path.string(),
                    includeNode.startPoint().column + 1,
                    includeNode.startPoint().row + 1
                );
            }
            else
            {
                ss << "EOF";
            }
            ss << " -> ";
        }
        ss << path.string() << "\n";

        for (const auto &[line, child] : children)
        {
            ss << child->toString(depth + 1);
        }
        return ss.str();
    }

    // Print all the way up to the root
    std::string stacktrace() const
    {
        std::stringstream ss;
        TSNode prevIncludeNode = includeNode;
        ConstIncludeTreePtr node = shared_from_this();
        while (node)
        {
            ss << node->path.string() << ":";
            if (prevIncludeNode)
            {
                ss << std::format
                (
                    "{}:{}",
                    prevIncludeNode.startPoint().row + 1,
                    prevIncludeNode.startPoint().column + 1
                );
            }
            else
            {
                ss << "EOF";
            }
            prevIncludeNode = node->includeNode;
            node = node->parent.lock();
        }
        return ss.str();
    }

    struct Iterator
    {
    public:
        using difference_type = std::ptrdiff_t;
        using value_type = IncludeTreePtr;
        using iterator_concept = std::forward_iterator_tag;

        Iterator() : currentNode(nullptr), atEnd(true) {}

        explicit Iterator(IncludeTreePtr node, bool atEnd = false)
            : currentNode(std::move(node)), atEnd(atEnd) {}

        IncludeTreePtr operator*() const
        {
            assert(!atEnd);
            return currentNode;
        }

        Iterator & operator++()
        {
            assert(!atEnd);
            if (!currentNode->children.empty())
            {
                currentNode = currentNode->children.begin()->second;
            }
            else
            {
                IncludeTreePtr parent = currentNode->parent.lock();
                while (parent)
                {
                    auto it = parent->children.find(currentNode->includeNode);
                    ++it;
                    if (it != parent->children.end())
                    {
                        currentNode = it->second;
                        return *this;
                    }
                    currentNode = parent;
                    parent = parent->parent.lock();
                }
                atEnd = true;
            }
            return *this;
        }

        Iterator operator++(int)
        {
            Iterator copy = *this;
            ++(*this);
            return copy;
        }

        bool operator==(const Iterator &other) const
        {
            if (atEnd && other.atEnd) return true;
            if (atEnd != other.atEnd) return false;
            return currentNode == other.currentNode;
        }

    private:
        IncludeTreePtr currentNode;
        bool atEnd;
    };

    Iterator begin()
    {
        return Iterator(shared_from_this());
    }

    Iterator end()
    {
        return Iterator(nullptr, true);
    }
};
static_assert(std::forward_iterator<IncludeTree::Iterator>);
static_assert(std::ranges::forward_range<IncludeTree>);

} // namespace Hayroll::IncludeTree

#endif // HAYROLL_INCLUDETREE_HPP
