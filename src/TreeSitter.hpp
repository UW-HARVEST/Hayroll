#ifndef HAYROLL_TREE_SITTER_HPP
#define HAYROLL_TREE_SITTER_HPP

#include <memory>
#include <string>
#include <string_view>

namespace ts
{
#include "tree_sitter/api.h"
} // namespace ts

namespace Hayroll
{

using TSPoint = ts::TSPoint;
using TSRange = ts::TSRange;

namespace TSUtils
{
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

class TSNode
{
public:
    TSNode(ts::TSNode node)
        : node(node)
    {
    }

    operator ts::TSNode()
    {
        return node;
    }

    TSNode get()
    {
        return node;
    }

    std::string type() const
    {
        return TSUtils::freeCstrToString(ts::ts_node_type(node));
    }

    ts::TSSymbol symbol() const
    {
        return ts::ts_node_symbol(node);
    }

    const ts::TSLanguage * language() const
    {
        return ts::ts_node_language(node);
    }

    std::string grammarType() const
    {
        return TSUtils::freeCstrToString(ts::ts_node_grammar_type(node));
    }

    ts::TSSymbol grammarSymbol() const
    {
        return ts::ts_node_grammar_symbol(node);
    }

    uint32_t startByte() const
    {
        return ts::ts_node_start_byte(node);
    }

    TSPoint startPoint() const
    {
        return ts::ts_node_start_point(node);
    }

    uint32_t endByte() const
    {
        return ts::ts_node_end_byte(node);
    }

    TSPoint endPoint() const
    {
        return ts::ts_node_end_point(node);
    }

    uint32_t childCount() const
    {
        return ts::ts_node_child_count(node);
    }

    uint32_t namedChildCount() const
    {
        return ts::ts_node_named_child_count(node);
    }

    TSNode child(uint32_t index) const
    {
        return ts::ts_node_child(node, index);
    }

    TSNode namedChild(uint32_t index) const
    {
        return ts::ts_node_named_child(node, index);
    }

    TSNode nextSibling() const
    {
        return ts::ts_node_next_sibling(node);
    }

    TSNode prevSibling() const
    {
        return ts::ts_node_prev_sibling(node);
    }

    TSNode nextNamedSibling() const
    {
        return ts::ts_node_next_named_sibling(node);
    }

    TSNode prevNamedSibling() const
    {
        return ts::ts_node_prev_named_sibling(node);
    }

    TSNode firstChildForByte(uint32_t byte) const
    {
        return ts::ts_node_first_child_for_byte(node, byte);
    }

    TSNode firstNamedChildForByte(uint32_t byte) const
    {
        return ts::ts_node_first_named_child_for_byte(node, byte);
    }

    uint32_t descendantCount() const
    {
        return ts::ts_node_descendant_count(node);
    }

    TSNode descendantForByteRange(uint32_t start, uint32_t end) const
    {
        return ts::ts_node_descendant_for_byte_range(node, start, end);
    }

    TSNode descendantForPointRange(TSPoint start, TSPoint end) const
    {
        return ts::ts_node_descendant_for_point_range(node, start, end);
    }

    TSNode namedDescendantForByteRange(uint32_t start, uint32_t end) const
    {
        return ts::ts_node_named_descendant_for_byte_range(node, start, end);
    }

    TSNode namedDescendantForPointRange(TSPoint start, TSPoint end) const
    {
        return ts::ts_node_named_descendant_for_point_range(node, start, end);
    }

    void edit(const ts::TSInputEdit *edit)
    {
        ts::ts_node_edit(&node, edit);
    }

    bool operator==(const TSNode &other) const
    {
        return ts::ts_node_eq(node, other.node);
    }

    bool isNull() const
    {
        return ts::ts_node_is_null(node);
    }

    bool isNamed() const
    {
        return ts::ts_node_is_named(node);
    }

    bool isMissing() const
    {
        return ts::ts_node_is_missing(node);
    }

    bool isExtra() const
    {
        return ts::ts_node_is_extra(node);
    }

    bool hasChanges() const
    {
        return ts::ts_node_has_changes(node);
    }

    bool hasError() const
    {
        return ts::ts_node_has_error(node);
    }

    bool isError() const
    {
        return ts::ts_node_is_error(node);
    }

    ts::TSStateId parseState() const
    {
        return ts::ts_node_parse_state(node);
    }

    ts::TSStateId nextParseState() const
    {
        return ts::ts_node_next_parse_state(node);
    }

    TSNode parent() const
    {
        return ts::ts_node_parent(node);
    }

    TSNode childWithDescendant(TSNode descendant) const
    {
        return ts::ts_node_child_with_descendant(node, descendant);
    }

    std::string fieldNameForChild(uint32_t child_index) const
    {
        return TSUtils::freeCstrToString(ts::ts_node_field_name_for_child(node, child_index));
    }

    std::string fieldNameForNamedChild(uint32_t named_child_index) const
    {
        return TSUtils::freeCstrToString(ts::ts_node_field_name_for_named_child(node, named_child_index));
    }

    TSNode childByFieldName(const std::string & name) const
    {
        return ts::ts_node_child_by_field_name(node, name.c_str(), name.size());
    }

    TSNode childByFieldId(ts::TSFieldId field_id) const
    {
        return ts::ts_node_child_by_field_id(node, field_id);
    }

    // Helper functions

    std::string text(std::string_view source) const
    {
        return std::string(source.substr(startByte(), endByte() - startByte()));
    }

private:
    ts::TSNode node;
};

class TSTree
{
public:
    TSTree(ts::TSTree * tree)
        : tree(tree)
    {
    }

    ~TSTree()
    {
        ts::ts_tree_delete(tree);
    }

    operator ts::TSTree *()
    {
        return tree;
    }

    ts::TSTree * get()
    {
        return tree;
    }

    TSNode rootNode() const
    {
        return ts::ts_tree_root_node(tree);
    }

    TSNode rootNodeWithOffset(uint32_t offsetBytes, TSPoint offsetExtent) const
    {
        return ts::ts_tree_root_node_with_offset(tree, offsetBytes, offsetExtent);
    }

    const ts::TSLanguage * language() const
    {
        return ts::ts_tree_language(tree);
    }

    std::tuple<ts::TSRange, uint32_t> includedRanges() const
    {
        uint32_t * length;
        TSRange range = TSUtils::freeTSRangePtrToTSRange(ts::ts_tree_included_ranges(tree, length));
        return {range, *length};
    }

    void edit(const ts::TSInputEdit *edit)
    {
        ts::ts_tree_edit(tree, edit);
    }

    std::tuple<ts::TSRange, uint32_t> changedRanges(const ts::TSTree * oldTree) const
    {
        uint32_t * length;
        TSRange range = TSUtils::freeTSRangePtrToTSRange(ts::ts_tree_get_changed_ranges(oldTree, tree, length));
        return {range, *length};
    }

    void printDotGraph(int fileDescriptor) const
    {
        ts::ts_tree_print_dot_graph(tree, fileDescriptor);
    }

private:
    ts::TSTree * tree;
};

class TSParser
{
public:
    TSParser(const TSLanguage * language)
        : parser(ts::ts_parser_new())
    {
        ts::ts_parser_set_language(parser, language);
    }

    TSParser(ts::TSParser * parser)
        : parser(parser)
    {
    }

    ~TSParser()
    {
        ts::ts_parser_delete(parser);
    }

    operator ts::TSParser *()
    {
        return parser;
    }

    ts::TSParser * get()
    {
        return parser;
    }

    const ts::TSLanguage *language() const
    {
        return ts::ts_parser_language(parser);
    }

    bool setLanguage(const ts::TSLanguage * language)
    {
        return ts::ts_parser_set_language(parser, language);
    }

    TSTree parseString(const std::string & str)
    {
        return ts::ts_parser_parse_string(parser, nullptr, str.c_str(), str.size());
    }

    void reset()
    {
        ts::ts_parser_reset(parser);
    }

private:
    ts::TSParser * parser;
};

class TSQuery
{
public:
    TSQuery(const ts::TSLanguage * language, std::string_view source)
    {
        uint32_t error_offset;
        ts::TSQueryError error_type;
        query = ts::ts_query_new(language, source.data(), source.size(), &error_offset, &error_type);
        if (query == nullptr)
        {
            throw std::runtime_error("Failed to create query");
        }
    }

    ~TSQuery()
    {
        ts::ts_query_delete(query);
    }

    operator ts::TSQuery *()
    {
        return query;
    }

    ts::TSQuery * get()
    {
        return query;
    }

    uint32_t patternCount() const
    {
        return ts::ts_query_pattern_count(query);
    }

    uint32_t captureCount() const
    {
        return ts::ts_query_capture_count(query);
    }

    uint32_t stringCount() const
    {
        return ts::ts_query_string_count(query);
    }

    uint32_t startByteForPattern(uint32_t patternIndex) const
    {
        return ts::ts_query_start_byte_for_pattern(query, patternIndex);
    }

    uint32_t endByteForPattern(uint32_t patternIndex) const
    {
        return ts::ts_query_end_byte_for_pattern(query, patternIndex);
    }

private:
    ts::TSQuery * query;
};

class TSTreeCursor
{
public:
    TSTreeCursor(ts::TSTreeCursor cursor)
        : cursor(cursor)
    {
    }

    TSTreeCursor(TSNode node)
        : cursor(ts::ts_tree_cursor_new(node))
    {
    }

    operator ts::TSTreeCursor()
    {
        return cursor;
    }

    void reset(TSNode node)
    {
        ts::ts_tree_cursor_reset(&cursor, node);
    }

    void resetTo(const TSTreeCursor &src)
    {
        ts::ts_tree_cursor_reset_to(&cursor, &src.cursor);
    }

    TSNode currentNode() const
    {
        return ts::ts_tree_cursor_current_node(&cursor);
    }

    std::string currentFieldName() const
    {
        const char *field_name = ts::ts_tree_cursor_current_field_name(&cursor);
        return field_name ? std::string(field_name) : std::string();
    }

    ts::TSFieldId currentFieldId() const
    {
        return ts::ts_tree_cursor_current_field_id(&cursor);
    }

    bool gotoParent()
    {
        return ts::ts_tree_cursor_goto_parent(&cursor);
    }

    bool gotoNextSibling()
    {
        return ts::ts_tree_cursor_goto_next_sibling(&cursor);
    }

    bool gotoPreviousSibling()
    {
        return ts::ts_tree_cursor_goto_previous_sibling(&cursor);
    }

    bool gotoFirstChild()
    {
        return ts::ts_tree_cursor_goto_first_child(&cursor);
    }

    bool gotoLastChild()
    {
        return ts::ts_tree_cursor_goto_last_child(&cursor);
    }

    void gotoDescendant(uint32_t goal_descendant_index)
    {
        ts::ts_tree_cursor_goto_descendant(&cursor, goal_descendant_index);
    }

    uint32_t currentDescendantIndex() const
    {
        return ts::ts_tree_cursor_current_descendant_index(&cursor);
    }

    uint32_t currentDepth() const
    {
        return ts::ts_tree_cursor_current_depth(&cursor);
    }

    int64_t gotoFirstChildForByte(uint32_t goal_byte)
    {
        return ts::ts_tree_cursor_goto_first_child_for_byte(&cursor, goal_byte);
    }

    int64_t gotoFirstChildForPoint(TSPoint goal_point)
    {
        return ts::ts_tree_cursor_goto_first_child_for_point(&cursor, goal_point);
    }

    TSTreeCursor copy() const
    {
        return ts::ts_tree_cursor_copy(&cursor);
    }

private:
    ts::TSTreeCursor cursor;
};

} // namespace Hayroll

#endif // HAYROLL_TREE_SITTER_HPP
