#ifndef HAYROLL_INCLUDETREE_HPP
#define HAYROLL_INCLUDETREE_HPP

#include <filesystem>
#include <memory>
#include <vector>
#include <string_view>
#include <optional>
#include <sstream>

namespace Hayroll::IncludeTree
{

namespace fs = std::filesystem;

struct IncludeTree
    : std::enable_shared_from_this<IncludeTree>
{
    fs::path path;
    std::vector<std::shared_ptr<IncludeTree>> children;
    std::weak_ptr<IncludeTree> parent;

    static std::shared_ptr<IncludeTree> make()
    {
        return std::make_shared<IncludeTree>();
    }

    static std::shared_ptr<IncludeTree> make(const fs::path &path)
    {
        std::shared_ptr<IncludeTree> node = std::make_shared<IncludeTree>();
        node->path = path;
        return node;
    }

    // Parse a "clang -H" output into a tree
    // An example of the output is:
    // . /usr/bin/../lib/gcc/x86_64-linux-gnu/11/../../../../include/c++/11/iostream
    // .. /usr/bin/../lib/gcc/x86_64-linux-gnu/11/../../../../include/x86_64-linux-gnu/c++/11/bits/c++config.h
    // ... /usr/bin/../lib/gcc/x86_64-linux-gnu/11/../../../../include/x86_64-linux-gnu/c++/11/bits/os_defines.h
    // .... /usr/include/features.h
    // ..... /usr/include/features-time64.h
    // ...... /usr/include/x86_64-linux-gnu/bits/wordsize.h
    // ...... /usr/include/x86_64-linux-gnu/bits/timesize.h
    // ....... /usr/include/x86_64-linux-gnu/bits/wordsize.h
    // ..... /usr/include/stdc-predef.h
    // ..... /usr/include/x86_64-linux-gnu/sys/cdefs.h
    // ...... /usr/include/x86_64-linux-gnu/bits/wordsize.h
    // ...... /usr/include/x86_64-linux-gnu/bits/long-double.h
    // ..... /usr/include/x86_64-linux-gnu/gnu/stubs.h
    // ...... /usr/include/x86_64-linux-gnu/gnu/stubs-64.h
    // ... /usr/bin/../lib/gcc/x86_64-linux-gnu/11/../../../../include/x86_64-linux-gnu/c++/11/bits/cpu_defines.h
    // ... /usr/bin/../lib/gcc/x86_64-linux-gnu/11/../../../../include/c++/11/pstl/pstl_config.h
    // .. /usr/bin/../lib/gcc/x86_64-linux-gnu/11/../../../../include/c++/11/ostream
    // This whole multi-line string is a single input
    // A single dot represents top-level include
    // Keep the "../" in the path in order to match header strings that include "../"s
    static std::shared_ptr<IncludeTree> parseTreeFromString(const std::string_view src)
    {
        std::shared_ptr<IncludeTree> root = make("./src.c");
        std::shared_ptr<IncludeTree> previous = root;
        std::istringstream iss(src.data());
        std::string line;
        size_t depthPrev = 0;
        while (std::getline(iss, line))
        {
            if (line.empty())
            {
                continue;
            }
            size_t depth = 0;
            while (line[depth] == '.')
            {
                depth++;
            }
            assert(depth > 0);
            // depth + 1 to skip the space
            std::string_view path_str(line.data() + depth + 1, line.size() - depth - 1);
            std::shared_ptr<IncludeTree> target = previous;
            while (depth <= depthPrev)
            {
                target = target->parent.lock();
                depthPrev--;
            }
            assert(depth == depthPrev + 1);
            std::shared_ptr<IncludeTree> child = make(path_str);
            child->parent = target;
            target->children.push_back(child);
            previous = child;
            depthPrev = depth;
        }
        return root;
    }

    // Test if the given string path (spelt header name) is a suffix of the current path
    // header could include the path to the file, or just the file name
    // e.g. "z3++.h" or "z3/z3++.h" or "../z3/z3++.h"
    bool endsWith(const std::string_view header) const
    {
        return path.string().ends_with(header);
    }

    std::optional<std::shared_ptr<IncludeTree>> findChildren(const std::string_view header) const
    {
        for (const auto &child : children)
        {
            if (child->endsWith(header))
            {
                return child;
            }
        }
        return std::nullopt;
    }

    // Merge the other tree into this tree
    // Takes ownership of the other tree
    // Force using move semantics in function signature to indicate that the other tree will be moved
    void merge(std::shared_ptr<IncludeTree>&& other)
    {
        for (auto& child : other->children)
        {
            // Do not use findChildren here
            // The include path must be identical to merge
            bool merged = false;
            for (auto& thisChild : children)
            {
                if (thisChild->path == child->path)
                {
                    thisChild->merge(std::move(child));
                    merged = true;
                    break;
                }
            }
            if (!merged)
            {
                children.push_back(std::move(child));
            }
        }
        other->children.clear();
    }

    std::string toString(size_t depth = 0) const
    {
        std::stringstream ss;
        if (depth > 0)
        {
            for (size_t i = 0; i < depth; i++)
            {
                ss << ".";
            }
            ss << " ";
            ss << path.string() + "\n";
        }
        for (const auto &child : children)
        {
            ss << child->toString(depth + 1);
        }
        return ss.str();
    }
};

} // namespace Hayroll::IncludeTree

#endif // HAYROLL_INCLUDETREE_HPP
