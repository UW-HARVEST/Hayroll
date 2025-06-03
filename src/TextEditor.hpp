// Text editor that allows insertion, erasion, and modification of text at specific lines and columns.
// Commits all changes to a file at once.

#ifndef HAYROLL_TEXTEDITOR_HPP
#define HAYROLL_TEXTEDITOR_HPP

#include <string>
#include <vector>
#include <ranges>

#include "Util.hpp"

namespace Hayroll
{

class TextEditor
{
public:

    TextEditor(std::string_view text)
        : text(text)
    {
    }

    enum EditType
    {
        Insert,
        Modify
    };

    struct Edit
    {
        EditType type;
        std::size_t ln;
        std::size_t col;
        std::string_view content;

        Edit(EditType t, std::size_t l, std::size_t c, std::string_view cont)
            : type(t), ln(l), col(c), content(cont)
        {
        }

        bool operator<(const Edit & other) const
        {
            if (ln != other.ln) return ln < other.ln;
            if (col != other.col) return col < other.col;
            return type < other.type;
        }
    };

    void insert(std::size_t line, std::size_t column, std::string_view content)
    {
        edits.emplace_back(EditType::Insert, line, column, content);
    }

    void modify(std::size_t line, std::size_t column, std::string_view content)
    {
        edits.emplace_back(EditType::Modify, line, column, content);
    }

    // Equivalent to changing the text to space characters
    void erase(std::size_t line, std::size_t column, std::size_t length)
    {
        if (length == 0) return;
        std::string spaces(length, ' ');
        edits.emplace_back(EditType::Modify, line, column, spaces);
    }

    std::string text;
    std::vector<Edit> edits;

    std::string commit()
    {
        // Ln and col number start from 1
        std::vector<std::string> lines;
        lines.push_back(""); // Padding line 0
        std::size_t pos = 0;
        while (pos < text.size())
        {
            std::size_t nextPos = text.find('\n', pos);
            if (nextPos == std::string::npos)
            {
                lines.push_back(text.substr(pos));
                break;
            }
            lines.push_back(text.substr(pos, nextPos - pos));
            pos = nextPos + 1; // Skip the newline character
        }

        // Sort edits by line and column
        std::sort(edits.begin(), edits.end());

        // Apply last edit first to avoid conflicts
        for (const auto & edit : std::views::reverse(edits))
        {
            switch (edit.type)
            {
                case EditType::Insert:
                {
                    if (edit.ln < lines.size())
                    {
                        std::string & line = lines[edit.ln];
                        if (edit.col - 1 < line.size())
                        {
                            // Insert content at specified column
                            // Note: edit.col is 1-based, so we adjust by subtracting 1
                            if (edit.col - 1 > line.size())
                            {
                                // If column is out of bounds, append at the end
                                line.append(edit.content);
                            }
                            else
                            {
                                line.insert(edit.col - 1, edit.content);
                            }
                        }
                        else
                        {
                            // Pad with spaces if column is out of bounds
                            if (edit.col - 1 > line.size())
                            {
                                line.resize(edit.col - 1, ' ');
                            }
                            line.append(edit.content);
                        }
                    }
                    else
                    {
                        // If line is out of bounds, append new lines
                        lines.resize(edit.ln + 1);
                        std::string & line = lines[edit.ln];
                        // Pad with spaces if column is out of bounds
                        if (edit.col - 1 > line.size())
                        {
                            line.resize(edit.col - 1, ' ');
                        }
                        line.append(edit.content);
                    }
                    break;
                }
                case EditType::Modify:
                {
                    if (edit.ln < lines.size())
                    {
                        std::string & line = lines[edit.ln];
                        if (edit.col - 1 < line.size())
                        {
                            // Modify content at specified column
                            // Note: edit.col is 1-based, so we adjust by subtracting 1
                            if (edit.col - 1 + edit.content.size() > line.size())
                            {
                                // If modification exceeds line length, pad with spaces
                                line.resize(edit.col - 1 + edit.content.size(), ' ');
                            }
                            line.replace(edit.col - 1, edit.content.size(), edit.content);
                        }
                        else
                        {
                            // Pad with spaces if column is out of bounds
                            if (edit.col - 1 > line.size())
                            {
                                line.resize(edit.col - 1, ' ');
                            }
                            line.append(edit.content);
                        }
                    }
                    else
                    {
                        // If line is out of bounds, append new lines
                        lines.resize(edit.ln + 1);
                        std::string & line = lines[edit.ln];
                        // Pad with spaces if column is out of bounds
                        for (std::size_t i = line.size(); i < edit.col - 1; ++i)
                        {
                            line += ' ';
                        }
                        line.append(edit.content);
                    }
                    break;
                }
            }
        }

        // Join lines into a single string
        std::string result;
        for (std::size_t i = 1; i < lines.size(); ++i) // Start from 1 to skip padding line
        {
            result += lines[i] + '\n';
        }
        text = result;
        edits.clear(); // Clear edits after committing
        return text;
    }
};

} // namespace Hayroll

#endif // HAYROLL_TEXTEDITOR_HPP
