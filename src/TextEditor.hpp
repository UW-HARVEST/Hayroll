// Text editor that allows insertion, erasion, and modification of text at 1-based line and column indices.
// Commits all changes to a string at once, ensuring that edits do not conflict with each other,
// as long as edits do not overlap.

#ifndef HAYROLL_TEXTEDITOR_HPP
#define HAYROLL_TEXTEDITOR_HPP

#include <string>
#include <vector>
#include <ranges>

namespace Hayroll
{

class TextEditor
{
public:
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
        std::string content;
        int priority; // Lower value means before

        Edit(EditType type, std::size_t ln, std::size_t col, std::string_view content, int priority = 0)
            : type(type), ln(ln), col(col), content(content), priority(priority)
        {
        }

        bool operator<(const Edit & other) const
        {
            if (ln != other.ln) return ln < other.ln;
            if (col != other.col) return col < other.col;
            if (type != other.type) return type < other.type;
            return priority < other.priority;
        }
    };

    std::string text;
    std::vector<std::string> lines;
    std::vector<Edit> edits;

    TextEditor(std::string_view t)
        : text(t)
    {
        // Ln and col number start from 1
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
    }

    void insert(std::size_t ln, std::size_t col, std::string_view content, int priority = 0)
    {
        edits.emplace_back(EditType::Insert, ln, col, content, priority);
    }

    void modify(std::size_t ln, std::size_t col, std::string_view content, int priority = 0)
    {
        edits.emplace_back(EditType::Modify, ln, col, content, priority);
    }

    // Equivalent to changing the text to space characters
    void erase(std::size_t ln, std::size_t col, std::size_t length, int priority = 0)
    {
        if (length == 0) return;
        std::string spaces(length, ' ');
        edits.emplace_back(EditType::Modify, ln, col, spaces, priority);
    }

    std::string get(std::size_t ln, std::size_t col, std::size_t length) const
    {
        if (ln >= lines.size())
        {
            throw std::out_of_range(std::format
            (
                "Line out of range: target {}:{}, limit {}",
                ln, col, lines.size()
            ));
        }
        if (col == 0 || col > lines[ln].size())
        {
            throw std::out_of_range(std::format
            (
                "Column out of range: target {}:{}, limit {}",
                ln, col, lines[ln].size()
            ));
        }
        
        std::size_t start = col - 1; // Convert to 0-based index
        std::size_t end = start + length;
        if (end > lines[ln].size())
        {
            end = lines[ln].size();
        }
        return lines[ln].substr(start, end - start);
    }

    std::string commit()
    {
        // Sort edits by line and column
        std::sort(edits.begin(), edits.end());

        // Apply last edit first to avoid conflicts
        for (const Edit & edit : std::views::reverse(edits))
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
