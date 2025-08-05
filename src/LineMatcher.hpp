#ifndef HAYROLL_LINEMATCHER_HPP
#define HAYROLL_LINEMATCHER_HPP

#include <string>
#include <optional>
#include <vector>
#include <variant>
#include <filesystem>
#include <map>
#include <format>

#include <z3++.h>

#include <spdlog/spdlog.h>

#include "Util.hpp"
#include "TreeSitter.hpp"
#include "TreeSitterCPreproc.hpp"
#include "IncludeResolver.hpp"
#include "IncludeTree.hpp"
#include "ASTBank.hpp"

namespace Hayroll
{

// A compilation unit preprocessed with -frewrite-includes flag would include all #include directives
// that are reached by this concrete execution of the preprocessor, leaving all other macros unexpanded.
// LineMatcher maches the lines of the original source code with the lines of the preprocessed source code.
class LineMatcher
{
public:
    static
    std::pair
    <
        std::unordered_map<IncludeTreePtr, std::vector<int>>, // lineMap
        std::vector<std::pair<IncludeTreePtr, int>> // inverseLineMap
    >
    run
    (
        std::string cuStr, // Compilation unit source code as a string
        IncludeTreePtr includeTree, // IncludeTree from a previous symbolic execution
        const std::vector<std::filesystem::path> & includePaths = {}
    )
    {
        const CPreproc lang{CPreproc()};
        IncludeResolver includeResolver{ClangExe, includePaths};
        ASTBank astBank{lang};
        const TSTree & tree = astBank.addAnonymousSource(std::move(cuStr));
        const TSNode root = tree.rootNode();
        assert(root.isSymbol(lang.translation_unit_s));

        int cuTotalLines = root.endPoint().row + 1;

        // IncludeTree -> line number in original source -> line number in compilation unit file
        std::unordered_map<IncludeTreePtr, std::vector<int>> lineMap;
        // Inverse mapping: line number in compilation unit file -> (IncludeTreePtr, line number in original source)
        std::vector<std::pair<IncludeTreePtr, int>> inverseLineMap(cuTotalLines + 1, {nullptr, 0});

        std::vector<TSNode> linemarkers;
        // Get all #line directives in the tree
        for (TSNode node : root.iterateDescendants())
        {
            if (node.isSymbol(lang.preproc_line_s))
            {
                linemarkers.push_back(node);
            }
        }
        linemarkers.push_back(TSNode{}); // Sentinel

        IncludeTreePtr lastIncludeTree = includeTree;
        TSNode lastLinemarker;
        for (const TSNode & linemarker : linemarkers)
        {
            if (!lastLinemarker)
            {
                lastLinemarker = linemarker;
                continue;
            }

            int lastSrcLine = std::stoi(lastLinemarker.childByFieldId(lang.preproc_line_s.line_number_f).text());
            std::string_view lastPath = lastLinemarker
                .childByFieldId(lang.preproc_line_s.filename_f)
                .childByFieldId(lang.string_literal_s.content_f)
                .textView();
            std::filesystem::path lastCanonicalPath = *includeResolver.resolveUserInclude(lastPath, lastIncludeTree->getAncestorDirs());
            if (lastCanonicalPath != lastIncludeTree->path)
            {
                // We are in a file that was concretely executed, and thus not in the include tree.
                // There is no need to map the lines.
                lastLinemarker = linemarker;
                continue;
            }

            int lastCuLine = lastLinemarker.startPoint().row + 1;
            int thisCuLine = linemarker ? linemarker.startPoint().row + 1 : cuTotalLines;

            if (!lineMap.contains(lastIncludeTree))
            {
                lineMap[lastIncludeTree] = std::vector<int>(1024, 0);
            }

            std::vector<int> & lines = lineMap[lastIncludeTree];
            while (lines.size() <= lastSrcLine)
            {
                lines.resize(lines.size() * 2);
            }
            // t <= thisCuLine : give one more line to lastCuLine
            for (int s = lastSrcLine, t = lastCuLine + 1; t <= thisCuLine; ++s, ++t)
            {
                lines[s] = t;
                inverseLineMap[t] = {lastIncludeTree, s};
            }

            if (!linemarker) break; // Sentinel reached, no more linemarkers

            // Handle file jumps
            TSNode thisFlagNode = linemarker.childByFieldId(lang.preproc_line_s.flag_f);
            if (!thisFlagNode)
            {
                lastLinemarker = linemarker;
                continue;
            }
            int thisFlag = std::stoi(thisFlagNode.text());
            std::string_view thisPath = linemarker
                .childByFieldId(lang.preproc_line_s.filename_f)
                .childByFieldId(lang.string_literal_s.content_f)
                .textView();
            std::filesystem::path thisCanonicalPath = *includeResolver.resolveUserInclude(thisPath, lastIncludeTree->getAncestorDirs());
            if (thisFlag == 1)
            {
                // Jump into a new file
                // last -> # 8 "libm/include/math.h"
                // this -> # 1 "libm/include/config.h" 1
                for (const auto & [includeNode, childIncludeTree] : lastIncludeTree->children)
                {
                    if (includeNode.startPoint().row + 1 == lastSrcLine && childIncludeTree->path == thisCanonicalPath)
                    {
                        lastIncludeTree = childIncludeTree;
                        break;
                    }
                }
                // There might not be a matching child include tree.
                // That happens when the included file is not in the include tree (concretely executed).
                // In that case, we will just leave the lastIncludeTree as it is.
            }
            if (thisFlag == 2)
            {
                // Return to the previous file
                // last -> # 18 "libm/include/config.h"
                // this -> # 9 "libm/include/math.h" 2
                IncludeTreePtr parentIncludeTree = lastIncludeTree->parent.lock();
                assert(parentIncludeTree);
                if (parentIncludeTree->path == thisCanonicalPath)
                {
                    lastIncludeTree = parentIncludeTree;
                }
                // Else we are in a file that was concretely executed, and thus not in the include tree.
                // No special handling is needed.
            }
            // No special handling for flags 3 and 4.

            lastLinemarker = linemarker;
            continue;
        }

        // Prune extra 0s from the end of the line map
        for (auto & [includeTree, lines] : lineMap)
        {
            while (!lines.empty() && lines.back() == 0)
            {
                lines.pop_back();
            }
        }

        return {std::move(lineMap), std::move(inverseLineMap)};
    }
};

} // namespace Hayroll

#endif // HAYROLL_LINEMATCHER_HPP
