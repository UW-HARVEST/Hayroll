#ifndef HAYROLL_TREE_SITTER_HPP
#define HAYROLL_TREE_SITTER_HPP

#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <stdexcept>

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
    bool operator==(const TSNode &other) const;

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
private:
    ts::TSNode node;
};

class TSTree
{
public:
    TSTree(ts::TSTree * tree);
    ~TSTree();

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
    ts::TSTree * tree;
};

class TSParser
{
public:
    TSParser(const ts::TSLanguage * language);
    TSParser(ts::TSParser * parser);
    ~TSParser();

    operator ts::TSParser *();
    operator const ts::TSParser *() const;
    ts::TSParser * get();

    const ts::TSLanguage * language() const;
    bool setLanguage(const ts::TSLanguage * language);

    TSTree parseString(std::string_view source);
    void reset();
private:
    ts::TSParser * parser;
};

class TSQuery
{
public:
    TSQuery(const ts::TSLanguage * language, std::string_view source);
    ~TSQuery();

    operator ts::TSQuery *();
    operator const ts::TSQuery *() const;
    ts::TSQuery * get();

    uint32_t patternCount() const;
    uint32_t captureCount() const;
    uint32_t stringCount() const;
    uint32_t startByteForPattern(uint32_t patternIndex) const;
    uint32_t endByteForPattern(uint32_t patternIndex) const;
private:
    ts::TSQuery * query;
};

class TSTreeCursor
{
public:
    TSTreeCursor(ts::TSTreeCursor cursor);
    TSTreeCursor(ts::TSNode node);

    operator ts::TSTreeCursor() const;
    operator ts::TSTreeCursor *();
    operator const ts::TSTreeCursor *() const;
    void reset(TSNode node);
    void resetTo(const TSTreeCursor &src);
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
    TSTreeCursor copy() const;
private:
    ts::TSTreeCursor cursor;
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

// TsNode

TSNode::TSNode(ts::TSNode node)
    : node(node)
{
}

TSNode::operator ts::TSNode() const
{
    return node;
}

TSNode TSNode::get()
{
    return node;
}

std::string TSNode::type() const
{
    return TSUtils::freeCstrToString(ts::ts_node_type(node));
}

ts::TSSymbol TSNode::symbol() const
{
    return ts::ts_node_symbol(node);
}

const ts::TSLanguage * TSNode::language() const
{
    return ts::ts_node_language(node);
}

std::string TSNode::grammarType() const
{
    return TSUtils::freeCstrToString(ts::ts_node_grammar_type(node));
}

ts::TSSymbol TSNode::grammarSymbol() const
{
    return ts::ts_node_grammar_symbol(node);
}

uint32_t TSNode::startByte() const
{
    return ts::ts_node_start_byte(node);
}

TSPoint TSNode::startPoint() const
{
    return ts::ts_node_start_point(node);
}

uint32_t TSNode::endByte() const
{
    return ts::ts_node_end_byte(node);
}

TSPoint TSNode::endPoint() const
{
    return ts::ts_node_end_point(node);
}

uint32_t TSNode::childCount() const
{
    return ts::ts_node_child_count(node);
}

uint32_t TSNode::namedChildCount() const
{
    return ts::ts_node_named_child_count(node);
}

TSNode TSNode::child(uint32_t index) const
{
    return ts::ts_node_child(node, index);
}

TSNode TSNode::namedChild(uint32_t index) const
{
    return ts::ts_node_named_child(node, index);
}

TSNode TSNode::nextSibling() const
{
    return ts::ts_node_next_sibling(node);
}

TSNode TSNode::prevSibling() const
{
    return ts::ts_node_prev_sibling(node);
}

TSNode TSNode::nextNamedSibling() const
{
    return ts::ts_node_next_named_sibling(node);
}

TSNode TSNode::prevNamedSibling() const
{
    return ts::ts_node_prev_named_sibling(node);
}

TSNode TSNode::firstChildForByte(uint32_t byte) const
{
    return ts::ts_node_first_child_for_byte(node, byte);
}

TSNode TSNode::firstNamedChildForByte(uint32_t byte) const
{
    return ts::ts_node_first_named_child_for_byte(node, byte);
}

uint32_t TSNode::descendantCount() const
{
    return ts::ts_node_descendant_count(node);
}

TSNode TSNode::descendantForByteRange(uint32_t start, uint32_t end) const
{
    return ts::ts_node_descendant_for_byte_range(node, start, end);
}

TSNode TSNode::descendantForPointRange(TSPoint start, TSPoint end) const
{
    return ts::ts_node_descendant_for_point_range(node, start, end);
}

TSNode TSNode::namedDescendantForByteRange(uint32_t start, uint32_t end) const
{
    return ts::ts_node_named_descendant_for_byte_range(node, start, end);
}

TSNode TSNode::namedDescendantForPointRange(TSPoint start, TSPoint end) const
{
    return ts::ts_node_named_descendant_for_point_range(node, start, end);
}

void TSNode::edit(const ts::TSInputEdit *edit)
{
    ts::ts_node_edit(&node, edit);
}

bool TSNode::operator==(const TSNode &other) const
{
    return ts::ts_node_eq(node, other.node);
}

bool TSNode::isNull() const
{
    return ts::ts_node_is_null(node);
}

bool TSNode::isNamed() const
{
    return ts::ts_node_is_named(node);
}

bool TSNode::isMissing() const
{
    return ts::ts_node_is_missing(node);
}

bool TSNode::isExtra() const
{
    return ts::ts_node_is_extra(node);
}

bool TSNode::hasChanges() const
{
    return ts::ts_node_has_changes(node);
}

bool TSNode::hasError() const
{
    return ts::ts_node_has_error(node);
}

bool TSNode::isError() const
{
    return ts::ts_node_is_error(node);
}

TSStateId TSNode::parseState() const
{
    return ts::ts_node_parse_state(node);
}

TSStateId TSNode::nextParseState() const
{
    return ts::ts_node_next_parse_state(node);
}

TSNode TSNode::parent() const
{
    return ts::ts_node_parent(node);
}

TSNode TSNode::childWithDescendant(TSNode descendant) const
{
    return ts::ts_node_child_with_descendant(node, descendant);
}

std::string TSNode::fieldNameForChild(uint32_t child_index) const
{
    return TSUtils::freeCstrToString(ts::ts_node_field_name_for_child(node, child_index));
}

std::string TSNode::fieldNameForNamedChild(uint32_t named_child_index) const
{
    return TSUtils::freeCstrToString(ts::ts_node_field_name_for_named_child(node, named_child_index));
}

TSNode TSNode::childByFieldName(const std::string & name) const
{
    return ts::ts_node_child_by_field_name(node, name.c_str(), name.size());
}

TSNode TSNode::childByFieldId(ts::TSFieldId field_id) const
{
    return ts::ts_node_child_by_field_id(node, field_id);
}

std::string TSNode::text(std::string_view source) const
{
    return std::string(source.substr(startByte(), endByte() - startByte()));
}

TSTreeCursor TSNode::cursor() const
{
    return TSTreeCursor(node);
}

// TSTree

TSTree::TSTree(ts::TSTree * tree)
    : tree(tree)
{
}

TSTree::~TSTree()
{
    ts::ts_tree_delete(tree);
}

TSTree::operator ts::TSTree *()
{
    return tree;
}

TSTree::operator const ts::TSTree *() const
{
    return tree;
}

ts::TSTree * TSTree::get()
{
    return tree;
}

TSNode TSTree::rootNode() const
{
    return ts::ts_tree_root_node(tree);
}

TSNode TSTree::rootNodeWithOffset(uint32_t offsetBytes, TSPoint offsetExtent) const
{
    return ts::ts_tree_root_node_with_offset(tree, offsetBytes, offsetExtent);
}

const ts::TSLanguage * TSTree::language() const
{
    return ts::ts_tree_language(tree);
}

std::tuple<ts::TSRange, uint32_t> TSTree::includedRanges() const
{
    uint32_t * length;
    TSRange range = TSUtils::freeTSRangePtrToTSRange(ts::ts_tree_included_ranges(tree, length));
    return {range, *length};
}

void TSTree::edit(const ts::TSInputEdit *edit)
{
    ts::ts_tree_edit(tree, edit);
}

std::tuple<ts::TSRange, uint32_t> TSTree::changedRanges(const TSTree & oldTree) const
{
    uint32_t * length;
    TSRange range = TSUtils::freeTSRangePtrToTSRange(ts::ts_tree_get_changed_ranges(oldTree, tree, length));
    return {range, *length};
}

void TSTree::printDotGraph(int fileDescriptor) const
{
    ts::ts_tree_print_dot_graph(tree, fileDescriptor);
}

//
// TSParser 实现
//
TSParser::TSParser(const ts::TSLanguage * language)
    : parser(ts::ts_parser_new())
{
    ts::ts_parser_set_language(parser, language);
}

TSParser::TSParser(ts::TSParser * parser)
    : parser(parser)
{
}

TSParser::~TSParser()
{
    ts::ts_parser_delete(parser);
}

TSParser::operator ts::TSParser *()
{
    return parser;
}

inline TSParser::operator const ts::TSParser *() const
{
    return parser;
}

ts::TSParser * TSParser::get()
{
    return parser;
}

const ts::TSLanguage * TSParser::language() const
{
    return ts::ts_parser_language(parser);
}

bool TSParser::setLanguage(const ts::TSLanguage * language)
{
    return ts::ts_parser_set_language(parser, language);
}

TSTree TSParser::parseString(std::string_view str)
{
    return ts::ts_parser_parse_string(parser, nullptr, str.data(), str.size());
}

void TSParser::reset()
{
    ts::ts_parser_reset(parser);
}

// TSQuery

TSQuery::TSQuery(const ts::TSLanguage * language, std::string_view source)
{
    uint32_t error_offset;
    ts::TSQueryError error_type;
    query = ts::ts_query_new(language, source.data(), source.size(), &error_offset, &error_type);
    if (query == nullptr)
    {
        throw std::runtime_error("Failed to create query");
    }
}

TSQuery::~TSQuery()
{
    ts::ts_query_delete(query);
}

TSQuery::operator ts::TSQuery *()
{
    return query;
}

TSQuery::operator const ts::TSQuery *() const
{
    return query;
}

ts::TSQuery * TSQuery::get()
{
    return query;
}

uint32_t TSQuery::patternCount() const
{
    return ts::ts_query_pattern_count(query);
}

uint32_t TSQuery::captureCount() const
{
    return ts::ts_query_capture_count(query);
}

uint32_t TSQuery::stringCount() const
{
    return ts::ts_query_string_count(query);
}

uint32_t TSQuery::startByteForPattern(uint32_t patternIndex) const
{
    return ts::ts_query_start_byte_for_pattern(query, patternIndex);
}

uint32_t TSQuery::endByteForPattern(uint32_t patternIndex) const
{
    return ts::ts_query_end_byte_for_pattern(query, patternIndex);
}

// TSTreeCursor

TSTreeCursor::TSTreeCursor(ts::TSTreeCursor cursor)
    : cursor(cursor)
{
}

TSTreeCursor::TSTreeCursor(ts::TSNode node)
    : cursor(ts::ts_tree_cursor_new(node))
{
}

TSTreeCursor::operator ts::TSTreeCursor() const
{
    return cursor;
}

TSTreeCursor::operator ts::TSTreeCursor *()
{
    return &cursor;
}

TSTreeCursor::operator const ts::TSTreeCursor *() const
{
    return &cursor;
}

void TSTreeCursor::reset(TSNode node)
{
    ts::ts_tree_cursor_reset(&cursor, node);
}

void TSTreeCursor::resetTo(const TSTreeCursor & src)
{
    ts::ts_tree_cursor_reset_to(&cursor, src);
}

TSNode TSTreeCursor::currentNode() const
{
    return ts::ts_tree_cursor_current_node(&cursor);
}

std::string TSTreeCursor::currentFieldName() const
{
    const char *field_name = ts::ts_tree_cursor_current_field_name(&cursor);
    return field_name ? std::string(field_name) : std::string();
}

ts::TSFieldId TSTreeCursor::currentFieldId() const
{
    return ts::ts_tree_cursor_current_field_id(&cursor);
}

bool TSTreeCursor::gotoParent()
{
    return ts::ts_tree_cursor_goto_parent(&cursor);
}

bool TSTreeCursor::gotoNextSibling()
{
    return ts::ts_tree_cursor_goto_next_sibling(&cursor);
}

bool TSTreeCursor::gotoPreviousSibling()
{
    return ts::ts_tree_cursor_goto_previous_sibling(&cursor);
}

bool TSTreeCursor::gotoFirstChild()
{
    return ts::ts_tree_cursor_goto_first_child(&cursor);
}

bool TSTreeCursor::gotoLastChild()
{
    return ts::ts_tree_cursor_goto_last_child(&cursor);
}

void TSTreeCursor::gotoDescendant(uint32_t goalDescendantIndex)
{
    ts::ts_tree_cursor_goto_descendant(&cursor, goalDescendantIndex);
}

uint32_t TSTreeCursor::currentDescendantIndex() const
{
    return ts::ts_tree_cursor_current_descendant_index(&cursor);
}

uint32_t TSTreeCursor::currentDepth() const
{
    return ts::ts_tree_cursor_current_depth(&cursor);
}

int64_t TSTreeCursor::gotoFirstChildForByte(uint32_t goalByte)
{
    return ts::ts_tree_cursor_goto_first_child_for_byte(&cursor, goalByte);
}

int64_t TSTreeCursor::gotoFirstChildForPoint(TSPoint goalPoint)
{
    return ts::ts_tree_cursor_goto_first_child_for_point(&cursor, goalPoint);
}

TSTreeCursor TSTreeCursor::copy() const
{
    return ts::ts_tree_cursor_copy(&cursor);
}

} // namespace Hayroll

#endif // HAYROLL_TREE_SITTER_HPP
