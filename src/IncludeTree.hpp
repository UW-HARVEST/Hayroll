#ifndef HAYROLL_INCLUDETREE_HPP
#define HAYROLL_INCLUDETREE_HPP

#include <filesystem>
#include <memory>
#include <map>
#include <string_view>
#include <optional>
#include <sstream>

namespace Hayroll
{

struct IncludeTree;
using IncludeTreePtr = std::shared_ptr<IncludeTree>;
using ConstIncludeTreePtr = std::shared_ptr<const IncludeTree>;

struct IncludeTree
    : public std::enable_shared_from_this<IncludeTree>
{
public:
    int line; // Line number of the include in its parent file
    std::filesystem::path path;
    std::map<int, IncludeTreePtr> children; // Line number to IncludeTreePtr
    std::weak_ptr<IncludeTree> parent;

    static IncludeTreePtr make
    (
        int line,
        const std::filesystem::path & path,
        IncludeTreePtr parent = nullptr
    )
    {
        auto tree = std::make_shared<IncludeTree>();
        tree->line = line;
        tree->path = path;
        tree->parent = parent;
        return tree;
    }

    void addChild(int line, const std::filesystem::path & path)
    {
        children[line] = make(line, path, shared_from_this());
    }

    // Test if the given string path (spelt header name) is a suffix of the current path
    // header could include the path to the file, or just the file name
    // e.g. "z3++.h" or "z3/z3++.h" or "../z3/z3++.h"
    bool endsWith(const std::string_view header) const
    {
        return path.string().ends_with(header);
    }

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

} // namespace Hayroll::IncludeTree

#endif // HAYROLL_INCLUDETREE_HPP
