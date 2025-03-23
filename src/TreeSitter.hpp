#ifndef HAYROLL_TREE_SITTER_HPP
#define HAYROLL_TREE_SITTER_HPP

#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <stdexcept>
#include <optional>
#include <cassert>

namespace ts
{
#include "tree_sitter/api.h"
} // namespace ts

namespace Hayroll
{

using TSPoint = ts::TSPoint;
using TSRange = ts::TSRange;
using TSStateId = ts::TSStateId;
using TSFieldId = ts::TSFieldId;

namespace TSUtils
{
    std::string freeCstrToString(const char *cstr);
    TSRange freeTSRangePtrToTSRange(const ts::TSRange *range);
} // namespace TSUtils

class TSTreeCursor;
class TSTreeCursorIterateChildren;

class TSNode
{
public:
    TSNode(ts::TSNode node);

    operator ts::TSNode() const;
    TSNode get();

    std::string type() const;
    ts::TSSymbol symbol() const;
    const ts::TSLanguage * language() const;

    std::string grammarType() const;
    ts::TSSymbol grammarSymbol() const;

    uint32_t startByte() const;
    TSPoint startPoint() const;
    uint32_t endByte() const;
    TSPoint endPoint() const;

    uint32_t childCount() const;
    uint32_t namedChildCount() const;
    TSNode child(uint32_t index) const;
    TSNode namedChild(uint32_t index) const;

    TSNode nextSibling() const;
    TSNode prevSibling() const;
    TSNode nextNamedSibling() const;
    TSNode prevNamedSibling() const;

    TSNode firstChildForByte(uint32_t byte) const;
    TSNode firstNamedChildForByte(uint32_t byte) const;
    uint32_t descendantCount() const;
    TSNode descendantForByteRange(uint32_t start, uint32_t end) const;
    TSNode descendantForPointRange(TSPoint start, TSPoint end) const;
    TSNode namedDescendantForByteRange(uint32_t start, uint32_t end) const;
    TSNode namedDescendantForPointRange(TSPoint start, TSPoint end) const;

    void edit(const ts::TSInputEdit *edit);
    bool operator==(const TSNode & other) const;

    bool isNull() const;
    bool isNamed() const;
    bool isMissing() const;
    bool isExtra() const;
    bool hasChanges() const;
    bool hasError() const;
    bool isError() const;
    TSStateId parseState() const;
    TSStateId nextParseState() const;
    TSNode parent() const;
    TSNode childWithDescendant(TSNode descendant) const;

    std::string fieldNameForChild(uint32_t child_index) const;
    std::string fieldNameForNamedChild(uint32_t named_child_index) const;
    TSNode childByFieldName(const std::string & name) const;
    TSNode childByFieldId(TSFieldId field_id) const;

    // Helper functions
    std::string text(std::string_view source) const;
    TSTreeCursor cursor() const;
    TSTreeCursorIterateChildren iterateChildren() const;
private:
    ts::TSNode node;
};

class TSTree
{
public:
    TSTree(ts::TSTree * tree);

    operator ts::TSTree *();
    operator const ts::TSTree *() const;
    ts::TSTree * get();

    TSNode rootNode() const;
    TSNode rootNodeWithOffset(uint32_t offsetBytes, TSPoint offsetExtent) const;
    const ts::TSLanguage * language() const;

    std::tuple<ts::TSRange, uint32_t> includedRanges() const;
    void edit(const ts::TSInputEdit *edit);
    std::tuple<ts::TSRange, uint32_t> changedRanges(const TSTree & oldTree) const;
    void printDotGraph(int fileDescriptor) const;
private:
    std::unique_ptr<ts::TSTree, decltype(&ts::ts_tree_delete)> tree;
};

class TSParser
{
public:
    TSParser(const ts::TSLanguage * language);
    TSParser(ts::TSParser * parser);

    operator ts::TSParser *();
    operator const ts::TSParser *() const;
    ts::TSParser * get();

    const ts::TSLanguage * language() const;
    bool setLanguage(const ts::TSLanguage * language);

    TSTree parseString(std::string_view source);
    void reset();
private:
    std::unique_ptr<ts::TSParser, decltype(&ts::ts_parser_delete)> parser;
};

class TSQuery
{
public:
    TSQuery(const ts::TSLanguage * language, std::string_view source);

    operator ts::TSQuery *();
    operator const ts::TSQuery *() const;
    ts::TSQuery * get();

    uint32_t patternCount() const;
    uint32_t captureCount() const;
    uint32_t stringCount() const;
    uint32_t startByteForPattern(uint32_t patternIndex) const;
    uint32_t endByteForPattern(uint32_t patternIndex) const;
private:
    std::unique_ptr<ts::TSQuery, decltype(&ts::ts_query_delete)> query;
};

class TSTreeCursor
{
public:
    TSTreeCursor(const TSTreeCursor & src);
    TSTreeCursor(const ts::TSNode & node);

    operator ts::TSTreeCursor() const;
    operator ts::TSTreeCursor *();
    operator const ts::TSTreeCursor *() const;
    void reset(TSNode node);
    void resetTo(const TSTreeCursor & src);
    TSNode currentNode() const;
    std::string currentFieldName() const;
    ts::TSFieldId currentFieldId() const;
    bool gotoParent();
    bool gotoNextSibling();
    bool gotoPreviousSibling();
    bool gotoFirstChild();
    bool gotoLastChild();
    void gotoDescendant(uint32_t goalDescendantIndex);
    uint32_t currentDescendantIndex() const;
    uint32_t currentDepth() const;
    int64_t gotoFirstChildForByte(uint32_t goalByte);
    int64_t gotoFirstChildForPoint(TSPoint goalPoint);

    // Helper functions
    TSTreeCursorIterateChildren iterateChildren() const;
private:
    ts::TSTreeCursor cursor;
    std::unique_ptr<ts::TSTreeCursor, decltype(&ts::ts_tree_cursor_delete)> cursorPtr;
};

// Class to iterate over children of a TSTreeCursor's current node.
class TSTreeCursorIterateChildren
{
public:
    // Nested iterator class
    class Iterator
    {
    public:
        using value_type = TSNode;
        using difference_type = std::ptrdiff_t;
        using pointer = TSNode *;
        using reference = TSNode;
        using iterator_category = std::input_iterator_tag;

        // Default constructor (end iterator)
        Iterator() : cursor(std::nullopt) {}

        // Constructor with a given TSTreeCursor state and valid flag
        explicit Iterator(TSTreeCursor && cursor)
            : cursor(std::move(cursor)) {}

        // Dereference operator returns the current node.
        TSNode operator *() const
        {
            assert(cursor.has_value());
            return cursor.value().currentNode();
        }

        // Pre-increment: move to the next sibling child.
        Iterator & operator++()
        {
            assert(cursor.has_value());
            if (!cursor.value().gotoNextSibling())
            {
                cursor = std::nullopt;
            }
            return *this;
        }

        // Post-increment.
        Iterator operator++(int)
        {
            Iterator copy = *this;
            ++(*this);
            return copy;
        }

        // Equality: iterators are equal if both are invalid (i.e. at the end) or 
        // if both are valid and their current nodes compare equal.
        bool operator==(const Iterator & other) const
        {
            if (!cursor.has_value() && !other.cursor.has_value())
                return true;
            if (cursor.has_value() && other.cursor.has_value())
                return cursor.value().currentNode() == other.cursor.value().currentNode();
            return false;
        }

        // Inequality operator.
        bool operator!=(const Iterator & other) const
        {
            return !(*this == other);
        }

    private:
        std::optional<TSTreeCursor> cursor;
    };

    // Constructor: store a copy of the parent's cursor.
    explicit TSTreeCursorIterateChildren(const TSTreeCursor & parentCursor)
        : parentCursor(parentCursor) {}

    // begin() returns an iterator positioned at the first child.
    Iterator begin() const
    {
        TSTreeCursor childCursor = parentCursor;
        if (!childCursor.gotoFirstChild())
            return end();
        return Iterator(std::move(childCursor));
    }

    // end() returns a default-constructed iterator (i.e. invalid).
    Iterator end() const
    {
        return Iterator();
    }

private:
    TSTreeCursor parentCursor;
};

// ==== Implementations ====

namespace TSUtils {

std::string freeCstrToString(const char *cstr)
{
    std::string str(cstr);
    ts::free((void *)cstr);
    return str;
}

TSRange freeTSRangePtrToTSRange(const ts::TSRange *range)
{
    TSRange ret = *range;
    ts::free((void *)range);
    return ret;
}

} // namespace TSUtils


// TSNode

// Constructor for TSNode
TSNode::TSNode(ts::TSNode node)
    : node(node)
{
}

// Conversion operator: Get the underlying ts::TSNode.
TSNode::operator ts::TSNode() const
{
    return node;
}

// Returns the stored TSNode.
TSNode TSNode::get()
{
    return node;
}

// Get the node's type as a null-terminated string.
std::string TSNode::type() const
{
    return TSUtils::freeCstrToString(ts::ts_node_type(node));
}

// Get the node's type as a numerical id.
ts::TSSymbol TSNode::symbol() const
{
    return ts::ts_node_symbol(node);
}

// Get the node's language.
const ts::TSLanguage * TSNode::language() const
{
    return ts::ts_node_language(node);
}

// Get the node's type as it appears in the grammar ignoring aliases as a null-terminated string.
std::string TSNode::grammarType() const
{
    return TSUtils::freeCstrToString(ts::ts_node_grammar_type(node));
}

// Get the node's type as a numerical id as it appears in the grammar ignoring aliases.
ts::TSSymbol TSNode::grammarSymbol() const
{
    return ts::ts_node_grammar_symbol(node);
}

// Get the node's start byte.
uint32_t TSNode::startByte() const
{
    return ts::ts_node_start_byte(node);
}

// Get the node's start position in terms of rows and columns.
TSPoint TSNode::startPoint() const
{
    return ts::ts_node_start_point(node);
}

// Get the node's end byte.
uint32_t TSNode::endByte() const
{
    return ts::ts_node_end_byte(node);
}

// Get the node's end position in terms of rows and columns.
TSPoint TSNode::endPoint() const
{
    return ts::ts_node_end_point(node);
}

// Get the node's number of children.
uint32_t TSNode::childCount() const
{
    return ts::ts_node_child_count(node);
}

// Get the node's number of named children.
uint32_t TSNode::namedChildCount() const
{
    return ts::ts_node_named_child_count(node);
}

// Get the node's child at the given index, where zero represents the first child.
TSNode TSNode::child(uint32_t index) const
{
    return ts::ts_node_child(node, index);
}

// Get the node's named child at the given index.
TSNode TSNode::namedChild(uint32_t index) const
{
    return ts::ts_node_named_child(node, index);
}

// Get the node's next sibling.
TSNode TSNode::nextSibling() const
{
    return ts::ts_node_next_sibling(node);
}

// Get the node's previous sibling.
TSNode TSNode::prevSibling() const
{
    return ts::ts_node_prev_sibling(node);
}

// Get the node's next named sibling.
TSNode TSNode::nextNamedSibling() const
{
    return ts::ts_node_next_named_sibling(node);
}

// Get the node's previous named sibling.
TSNode TSNode::prevNamedSibling() const
{
    return ts::ts_node_prev_named_sibling(node);
}

// Get the node's first child that contains or starts after the given byte offset.
TSNode TSNode::firstChildForByte(uint32_t byte) const
{
    return ts::ts_node_first_child_for_byte(node, byte);
}

// Get the node's first named child that contains or starts after the given byte offset.
TSNode TSNode::firstNamedChildForByte(uint32_t byte) const
{
    return ts::ts_node_first_named_child_for_byte(node, byte);
}

// Get the node's number of descendants, including one for the node itself.
uint32_t TSNode::descendantCount() const
{
    return ts::ts_node_descendant_count(node);
}

// Get the smallest node within this node that spans the given range of bytes.
TSNode TSNode::descendantForByteRange(uint32_t start, uint32_t end) const
{
    return ts::ts_node_descendant_for_byte_range(node, start, end);
}

// Get the smallest node within this node that spans the given range of (row, column) positions.
TSNode TSNode::descendantForPointRange(TSPoint start, TSPoint end) const
{
    return ts::ts_node_descendant_for_point_range(node, start, end);
}

// Get the smallest named node within this node that spans the given range of bytes.
TSNode TSNode::namedDescendantForByteRange(uint32_t start, uint32_t end) const
{
    return ts::ts_node_named_descendant_for_byte_range(node, start, end);
}

// Get the smallest named node within this node that spans the given range of (row, column) positions.
TSNode TSNode::namedDescendantForPointRange(TSPoint start, TSPoint end) const
{
    return ts::ts_node_named_descendant_for_point_range(node, start, end);
}

// Edit the node to keep it in sync with source code that has been edited.
void TSNode::edit(const ts::TSInputEdit *edit)
{
    ts::ts_node_edit(&node, edit);
}

// Check if two nodes are identical.
bool TSNode::operator==(const TSNode & other) const
{
    return ts::ts_node_eq(node, other.node);
}

// Check if the node is null.
bool TSNode::isNull() const
{
    return ts::ts_node_is_null(node);
}

// Check if the node is named.
bool TSNode::isNamed() const
{
    return ts::ts_node_is_named(node);
}

// Check if the node is missing.
bool TSNode::isMissing() const
{
    return ts::ts_node_is_missing(node);
}

// Check if the node is extra.
bool TSNode::isExtra() const
{
    return ts::ts_node_is_extra(node);
}

// Check if the syntax node has been edited.
bool TSNode::hasChanges() const
{
    return ts::ts_node_has_changes(node);
}

// Check if the node has any syntax errors.
bool TSNode::hasError() const
{
    return ts::ts_node_has_error(node);
}

// Check if the node is a syntax error.
bool TSNode::isError() const
{
    return ts::ts_node_is_error(node);
}

// Get this node's parse state.
TSStateId TSNode::parseState() const
{
    return ts::ts_node_parse_state(node);
}

// Get the parse state after this node.
TSStateId TSNode::nextParseState() const
{
    return ts::ts_node_next_parse_state(node);
}

// Get the node's immediate parent.
TSNode TSNode::parent() const
{
    return ts::ts_node_parent(node);
}

// Get the node that contains the given descendant.
TSNode TSNode::childWithDescendant(TSNode descendant) const
{
    return ts::ts_node_child_with_descendant(node, descendant);
}

// Get the field name for the node's child at the given index.
std::string TSNode::fieldNameForChild(uint32_t child_index) const
{
    return TSUtils::freeCstrToString(ts::ts_node_field_name_for_child(node, child_index));
}

// Get the field name for the node's named child at the given index.
std::string TSNode::fieldNameForNamedChild(uint32_t named_child_index) const
{
    return TSUtils::freeCstrToString(ts::ts_node_field_name_for_named_child(node, named_child_index));
}

// Get the node's child with the given field name.
TSNode TSNode::childByFieldName(const std::string & name) const
{
    return ts::ts_node_child_by_field_name(node, name.c_str(), name.size());
}

// Get the node's child with the given numerical field id.
TSNode TSNode::childByFieldId(ts::TSFieldId field_id) const
{
    return ts::ts_node_child_by_field_id(node, field_id);
}

// Helper function: Get the text of the node from the source.
std::string TSNode::text(std::string_view source) const
{
    return std::string(source.substr(startByte(), endByte() - startByte()));
}

// Create a tree cursor for this node.
TSTreeCursor TSNode::cursor() const
{
    return TSTreeCursor(node);
}

TSTreeCursorIterateChildren TSNode::iterateChildren() const
{
    return cursor().iterateChildren();
}

// TSTree

// Construct a TSTree with the given ts::TSTree pointer.
TSTree::TSTree(ts::TSTree * tree)
    : tree(tree, ts::ts_tree_delete)
{
}

// Conversion operator: Get the underlying ts::TSTree pointer.
TSTree::operator ts::TSTree *()
{
    return tree.get();
}

// Conversion operator: Get the underlying const ts::TSTree pointer.
TSTree::operator const ts::TSTree *() const
{
    return tree.get();
}

// Returns the stored ts::TSTree pointer.
ts::TSTree * TSTree::get()
{
    return *this;
}

// Get the root node of the syntax tree.
TSNode TSTree::rootNode() const
{
    return ts::ts_tree_root_node(*this);
}

// Get the root node of the syntax tree, with its position shifted by the given offset.
TSNode TSTree::rootNodeWithOffset(uint32_t offsetBytes, TSPoint offsetExtent) const
{
    return ts::ts_tree_root_node_with_offset(*this, offsetBytes, offsetExtent);
}

// Get the language that was used to parse the syntax tree.
const ts::TSLanguage * TSTree::language() const
{
    return ts::ts_tree_language(*this);
}

// Get the array of included ranges that was used to parse the syntax tree.
std::tuple<ts::TSRange, uint32_t> TSTree::includedRanges() const
{
    uint32_t * length;
    TSRange range = TSUtils::freeTSRangePtrToTSRange(ts::ts_tree_included_ranges(*this, length));
    return {range, *length};
}

// Edit the syntax tree to keep it in sync with the edited source code.
void TSTree::edit(const ts::TSInputEdit *edit)
{
    ts::ts_tree_edit(*this, edit);
}

// Compare an old edited syntax tree to a new syntax tree, returning an array of changed ranges.
std::tuple<ts::TSRange, uint32_t> TSTree::changedRanges(const TSTree & oldTree) const
{
    uint32_t * length;
    TSRange range = TSUtils::freeTSRangePtrToTSRange(ts::ts_tree_get_changed_ranges(oldTree, *this, length));
    return {range, *length};
}

// Write a DOT graph describing the syntax tree to the given file descriptor.
void TSTree::printDotGraph(int fileDescriptor) const
{
    ts::ts_tree_print_dot_graph(*this, fileDescriptor);
}


// TSParser

// Create a new parser and set its language.
TSParser::TSParser(const ts::TSLanguage * language)
    : parser(ts::ts_parser_new(), ts::ts_parser_delete)
{
    ts::ts_parser_set_language(*this, language);
}

// Construct a TSParser from an existing ts::TSParser pointer.
TSParser::TSParser(ts::TSParser * parser)
    : parser(parser, ts::ts_parser_delete)
{
}

// Conversion operator: Get the underlying ts::TSParser pointer.
TSParser::operator ts::TSParser *()
{
    return parser.get();
}

// Conversion operator: Get the underlying const ts::TSParser pointer.
inline TSParser::operator const ts::TSParser *() const
{
    return parser.get();
}

// Returns the stored ts::TSParser pointer.
ts::TSParser * TSParser::get()
{
    return *this;
}

// Get the parser's current language.
const ts::TSLanguage * TSParser::language() const
{
    return ts::ts_parser_language(*this);
}

// Set the language for the parser.
bool TSParser::setLanguage(const ts::TSLanguage * language)
{
    return ts::ts_parser_set_language(*this, language);
}

// Use the parser to parse a string and create a syntax tree.
TSTree TSParser::parseString(std::string_view str)
{
    return ts::ts_parser_parse_string(*this, nullptr, str.data(), str.size());
}

// Reset the parser to start the next parse from the beginning.
void TSParser::reset()
{
    ts::ts_parser_reset(*this);
}


// TSQuery

// Create a new query from a source string and associate it with a language.
TSQuery::TSQuery(const ts::TSLanguage * language, std::string_view source)
    : query(nullptr, ts::ts_query_delete)
{
    uint32_t error_offset;
    ts::TSQueryError error_type;
    query.reset(ts::ts_query_new(language, source.data(), source.size(), &error_offset, &error_type));
    if (query == nullptr)
    {
        throw std::runtime_error("Failed to create query");
    }
}

// Conversion operator: Get the underlying ts::TSQuery pointer.
TSQuery::operator ts::TSQuery *()
{
    return query.get();
}

// Conversion operator: Get the underlying const ts::TSQuery pointer.
TSQuery::operator const ts::TSQuery *() const
{
    return query.get();
}

// Returns the stored ts::TSQuery pointer.
ts::TSQuery * TSQuery::get()
{
    return *this;
}

// Get the number of patterns in the query.
uint32_t TSQuery::patternCount() const
{
    return ts::ts_query_pattern_count(*this);
}

// Get the number of captures in the query.
uint32_t TSQuery::captureCount() const
{
    return ts::ts_query_capture_count(*this);
}

// Get the number of string literals in the query.
uint32_t TSQuery::stringCount() const
{
    return ts::ts_query_string_count(*this);
}

// Get the byte offset where the given pattern starts in the query's source.
uint32_t TSQuery::startByteForPattern(uint32_t patternIndex) const
{
    return ts::ts_query_start_byte_for_pattern(*this, patternIndex);
}

// Get the byte offset where the given pattern ends in the query's source.
uint32_t TSQuery::endByteForPattern(uint32_t patternIndex) const
{
    return ts::ts_query_end_byte_for_pattern(*this, patternIndex);
}


// TSTreeCursor

TSTreeCursor::TSTreeCursor(const TSTreeCursor & src)
    : cursor(ts::ts_tree_cursor_copy(src)), cursorPtr(&cursor, ts::ts_tree_cursor_delete)
{
}

// Create a new tree cursor starting from the given node.
TSTreeCursor::TSTreeCursor(const ts::TSNode & node)
    : cursor(ts::ts_tree_cursor_new(node)), cursorPtr(&cursor, ts::ts_tree_cursor_delete)
{
}

// Conversion operator: Get the underlying ts::TSTreeCursor.
TSTreeCursor::operator ts::TSTreeCursor() const
{
    return cursor;
}

// Conversion operator: Get a pointer to the underlying ts::TSTreeCursor.
TSTreeCursor::operator ts::TSTreeCursor *()
{
    return &cursor;
}

// Conversion operator: Get a const pointer to the underlying ts::TSTreeCursor.
TSTreeCursor::operator const ts::TSTreeCursor *() const
{
    return &cursor;
}

// Re-initialize the tree cursor to start at the given node.
void TSTreeCursor::reset(TSNode node)
{
    ts::ts_tree_cursor_reset(*this, node);
}

// Re-initialize the tree cursor to the same position as another cursor.
void TSTreeCursor::resetTo(const TSTreeCursor & src)
{
    ts::ts_tree_cursor_reset_to(*this, src);
}

// Get the tree cursor's current node.
TSNode TSTreeCursor::currentNode() const
{
    return ts::ts_tree_cursor_current_node(*this);
}

// Get the field name of the tree cursor's current node.
std::string TSTreeCursor::currentFieldName() const
{
    const char *field_name = ts::ts_tree_cursor_current_field_name(*this);
    return field_name ? std::string(field_name) : std::string();
}

// Get the field id of the tree cursor's current node.
ts::TSFieldId TSTreeCursor::currentFieldId() const
{
    return ts::ts_tree_cursor_current_field_id(*this);
}

// Move the cursor to the parent of its current node.
bool TSTreeCursor::gotoParent()
{
    return ts::ts_tree_cursor_goto_parent(*this);
}

// Move the cursor to the next sibling of its current node.
bool TSTreeCursor::gotoNextSibling()
{
    return ts::ts_tree_cursor_goto_next_sibling(*this);
}

// Move the cursor to the previous sibling of its current node.
bool TSTreeCursor::gotoPreviousSibling()
{
    return ts::ts_tree_cursor_goto_previous_sibling(*this);
}

// Move the cursor to the first child of its current node.
bool TSTreeCursor::gotoFirstChild()
{
    return ts::ts_tree_cursor_goto_first_child(*this);
}

// Move the cursor to the last child of its current node.
bool TSTreeCursor::gotoLastChild()
{
    return ts::ts_tree_cursor_goto_last_child(*this);
}

// Move the cursor to the nth descendant of the original node, where zero represents the original node itself.
void TSTreeCursor::gotoDescendant(uint32_t goalDescendantIndex)
{
    ts::ts_tree_cursor_goto_descendant(*this, goalDescendantIndex);
}

// Get the index of the cursor's current node among all descendants of the original node.
uint32_t TSTreeCursor::currentDescendantIndex() const
{
    return ts::ts_tree_cursor_current_descendant_index(*this);
}

// Get the depth of the cursor's current node relative to the original node.
uint32_t TSTreeCursor::currentDepth() const
{
    return ts::ts_tree_cursor_current_depth(*this);
}

// Move the cursor to the first child for the given byte offset.
int64_t TSTreeCursor::gotoFirstChildForByte(uint32_t goalByte)
{
    return ts::ts_tree_cursor_goto_first_child_for_byte(*this, goalByte);
}

// Move the cursor to the first child for the given (row, column) point.
int64_t TSTreeCursor::gotoFirstChildForPoint(TSPoint goalPoint)
{
    return ts::ts_tree_cursor_goto_first_child_for_point(*this, goalPoint);
}

TSTreeCursorIterateChildren TSTreeCursor::iterateChildren() const
{
    return TSTreeCursorIterateChildren(*this);
}

} // namespace Hayroll

#endif // HAYROLL_TREE_SITTER_HPP
