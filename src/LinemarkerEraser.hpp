// Replaces linemarker text with spaces
#ifndef HAYROLL_LINEMARKERERASER_HPP
#define HAYROLL_LINEMARKERERASER_HPP

#include <string>

#include "TreeSitter.hpp"
#include "TreeSitterCPreproc.hpp"
#include "ASTBank.hpp"
#include "TextEditor.hpp"

namespace Hayroll
{

class LinemarkerEraser
{
public:
    static std::string run(std::string_view text)
    {
        TextEditor editor(text);
        CPreproc lang;
        ASTBank astBank{lang};

        const Hayroll::TSTree & tree = astBank.addAnonymousSource(std::string(text));
        TSNode rootNode = tree.rootNode();

        // Iterate over all linemarkers and replace them with spaces
        for (const TSNode & node : rootNode.iterateDescendants())
        {
            if (!node.isSymbol(lang.preproc_line_s)) continue;
            std::size_t ln = node.startPoint().row + 1; // Convert to 1-based line number
            std::size_t col = node.startPoint().column + 1; // Convert to 1-based column number
            std::size_t length = node.length();
            editor.erase(ln, col, ln, col + length);
        }
        return editor.commit();
    }
};

} // namespace Hayroll

#endif // HAYROLL_LINEMARKERERASER_HPP
