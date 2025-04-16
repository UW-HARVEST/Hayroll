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

    struct Iterator
    {
    public:
        using difference_type = std::ptrdiff_t;
        using value_type = ConstIncludeTreePtr;
        using iterator_concept = std::forward_iterator_tag;

        Iterator() : currentNode(nullptr), atEnd(true) {}

        explicit Iterator(ConstIncludeTreePtr node, bool atEnd = false)
            : currentNode(std::move(node)), atEnd(atEnd) {}

        ConstIncludeTreePtr operator*() const
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
                ConstIncludeTreePtr parent = currentNode->parent.lock();
                while (parent)
                {
                    auto it = parent->children.find(currentNode->line);
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
        ConstIncludeTreePtr currentNode;
        bool atEnd;
    };

    Iterator begin() const
    {
        return Iterator(shared_from_this());
    }

    Iterator end() const
    {
        return Iterator(nullptr, true);
    }
};
static_assert(std::forward_iterator<IncludeTree::Iterator>);
static_assert(std::ranges::forward_range<IncludeTree>);

} // namespace Hayroll::IncludeTree

#endif // HAYROLL_INCLUDETREE_HPP
