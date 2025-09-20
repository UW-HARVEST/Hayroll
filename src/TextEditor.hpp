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
        Erase = 0,
        Modify = 1,
        Insert = 2,
        Append = 3
    };

    struct Edit
    {
        EditType type;
        std::size_t ln;
        std::size_t col;
        std::size_t lnEnd; // Only for Erase
        std::size_t colEnd; // Only for Erase
        std::string content; // Only for Insert and Modify
        int priority; // Lower value means lefter

        bool operator<(const Edit & other) const
        {
            // Modify before Insert
            if (type != other.type) return type < other.type;
            if (ln != other.ln) return ln > other.ln; // Later lines first
            if (col != other.col) return col > other.col; // Later columns first
            return priority > other.priority; // Lower value means lefter (executed later)
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
        edits.push_back
        (
            Edit
            {
                .type = EditType::Insert,
                .ln = ln,
                .col = col,
                .content = std::string(content),
                .priority = priority
            }
        );
    }

    void modify(std::size_t ln, std::size_t col, std::string_view content, int priority = 0)
    {
        edits.push_back
        (
            Edit
            {
                .type = EditType::Modify,
                .ln = ln,
                .col = col,
                .content = std::string(content),
                .priority = priority
            }
        );
    }

    // Replace with spaces
    void erase(std::size_t ln, std::size_t col, std::size_t lnEnd, std::size_t colEnd, int priority = 0)
    {
        edits.push_back
        (
            Edit
            {
                .type = EditType::Erase,
                .ln = ln,
                .col = col,
                .lnEnd = lnEnd,
                .colEnd = colEnd,
                .priority = priority
            }
        );
    }

    // Append new line at the end of the file
    void append(std::string_view content, int priority = 0)
    {
        edits.push_back
        (
            Edit
            {
                .type = EditType::Append,
                .content = std::string(content),
                .priority = priority
            }
        );
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

        for (const Edit & edit : edits)
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
                case EditType::Erase:
                {
                    if (edit.ln < lines.size())
                    {
                        std::string & line = lines[edit.ln];
                        if (edit.col - 1 < line.size())
                        {
                            // Erase content from specified column to lnEnd:colEnd
                            // Note: edit.col is 1-based, so we adjust by subtracting 1
                            std::size_t start = edit.col - 1;
                            std::size_t end;
                            if (edit.lnEnd < lines.size())
                            {
                                if (edit.colEnd - 1 < lines[edit.lnEnd].size())
                                {
                                    end = edit.colEnd - 1;
                                }
                                else
                                {
                                    end = lines[edit.lnEnd].size();
                                }
                            }
                            else
                            {
                                end = line.size();
                            }

                            if (edit.ln == edit.lnEnd)
                            {
                                // Single line erase
                                if (start < end)
                                {
                                    line.replace(start, end - start, std::string(end - start, ' '));
                                }
                            }
                            else
                            {
                                // Multi-line erase
                                // Erase from start to end of the first line
                                line.replace(start, line.size() - start, std::string(line.size() - start, ' '));
                                // Erase full lines in between
                                for (std::size_t l = edit.ln + 1; l < edit.lnEnd; ++l)
                                {
                                    if (l < lines.size())
                                    {
                                        lines[l] = std::string(lines[l].size(), ' ');
                                    }
                                }
                                // Erase from beginning to end on the last line
                                if (edit.lnEnd < lines.size())
                                {
                                    std::string & endLine = lines[edit.lnEnd];
                                    if (end > 0)
                                    {
                                        endLine.replace(0, end, std::string(end, ' '));
                                    }
                                }
                            }
                        }
                    }
                    break;
                }
                case EditType::Append:
                {
                    lines.push_back(edit.content);
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
