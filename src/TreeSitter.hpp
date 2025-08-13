// C++ wrapper for Tree-sitter C API
// C API: https://github.com/tree-sitter/tree-sitter/blob/master/lib/include/tree_sitter/api.h

#ifndef HAYROLL_TREESITTER_HPP
#define HAYROLL_TREESITTER_HPP

#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <stdexcept>
#include <optional>
#include <cassert>
#include <vector>
#include <ranges>

#include <iostream>

#include <boost/stacktrace.hpp>
#include <spdlog/spdlog.h>

#include "Util.hpp"

namespace ts
{
    #include "tree_sitter/api.h"
} // namespace ts

namespace Hayroll
{

using TSRange = ts::TSRange;
using TSStateId = ts::TSStateId;
using TSFieldId = ts::TSFieldId;
using TSSymbol = ts::TSSymbol;
using TSSymbolType = ts::TSSymbolType;

namespace TSUtils
{
    std::string freeCstrToString(const char *cstr);
    TSRange freeTSRangePtrToTSRange(const ts::TSRange *range);
} // namespace TSUtils

struct TSPoint : public ts::TSPoint
{
    TSPoint() = default;
    TSPoint(uint32_t row, uint32_t column) : ts::TSPoint{row, column} {}
    TSPoint(const ts::TSPoint & point) : ts::TSPoint{point} {}

    operator std::string() const
    {
        return std::format("{}:{}", row + 1, column + 1);
    }

    std::string toString() const
    {
        return std::string(*this);
    }

    std::partial_ordering operator<=>(const TSPoint & other) const
    {
        if (row < other.row) return std::partial_ordering::less;
        if (row > other.row) return std::partial_ordering::greater;
        if (column < other.column) return std::partial_ordering::less;
        if (column > other.column) return std::partial_ordering::greater;
        return std::partial_ordering::equivalent;
    }
};

class TSTreeCursor;
class TSTreeCursorIterateChildren;
class TSTreeCursorIterateDescendants;

class TSLanguage
{
public:
    TSLanguage(const ts::TSLanguage * language);

    TSLanguage(const TSLanguage & src);
    TSLanguage(TSLanguage && src) = default;
    TSLanguage & operator=(const TSLanguage & src);
    TSLanguage & operator=(TSLanguage && src) = default;
    ~TSLanguage() = default;

    operator const ts::TSLanguage *() const;
    const ts::TSLanguage * get();

    uint32_t symbolCount() const;
    uint32_t stateCount() const;
    TSSymbol symbolForName(const std::string & name, bool isNamed) const;
    uint32_t fieldCount() const;
    std::string fieldNameForId(ts::TSFieldId id) const;
    ts::TSFieldId fieldIdForName(const std::string & name) const;
    std::vector<TSSymbol> supertypes() const;
    std::vector<TSSymbol> subtypes(ts::TSSymbol supertype) const;
    std::string symbolName(ts::TSSymbol symbol) const;
    TSSymbolType symbolType(ts::TSSymbol symbol) const;
    uint32_t languageVersion() const;
    uint32_t abiVersion() const;
    const ts::TSLanguageMetadata *metadata() const;
    ts::TSStateId nextState(ts::TSStateId state, ts::TSSymbol symbol) const;
    std::string name() const;

private:
    std::unique_ptr<const ts::TSLanguage, decltype(&ts::ts_language_delete)> language;
};

class TSNode
{
public:
    TSNode(const ts::TSNode & node, const std::string * source);
    TSNode();

    operator ts::TSNode() const;

    std::string type() const;
    TSSymbol symbol() const;
    const TSLanguage language() const;

    std::string grammarType() const;
    TSSymbol grammarSymbol() const;

    uint32_t startByte() const;
    TSPoint startPoint() const;
    uint32_t endByte() const;
    TSPoint endPoint() const;

    std::string sExpression() const;

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
    TSNode descendantForPointRange(ts::TSPoint start, ts::TSPoint end) const;
    TSNode namedDescendantForByteRange(uint32_t start, uint32_t end) const;
    TSNode namedDescendantForPointRange(ts::TSPoint start, ts::TSPoint end) const;

    void edit(const ts::TSInputEdit *edit);
    bool eq(const ts::TSNode & other) const;
    bool operator==(const TSNode & other) const;

    bool isNull() const;
    operator bool() const;
    bool isNamed() const;
    bool isMissing() const;
    bool isExtra() const;
    bool hasChanges() const;
    bool hasError() const;
    bool isError() const;
    TSStateId parseState() const;
    TSStateId nextParseState() const;
    TSNode parent() const;
    TSNode childWithDescendant(ts::TSNode descendant) const;

    std::string fieldNameForChild(uint32_t child_index) const;
    std::string fieldNameForNamedChild(uint32_t named_child_index) const;
    TSNode childByFieldName(const std::string & name) const;
    TSNode childByFieldId(ts::TSFieldId field_id) const;

    // Helper functions
    bool isSymbol(ts::TSSymbol symbol) const;
    const std::string & getSource() const;
    std::string_view textView() const;
    std::string text() const;
    TSTreeCursor cursor() const;
    TSTreeCursorIterateChildren iterateChildren() const;
    TSTreeCursorIterateDescendants iterateDescendants() const;
    size_t length() const;
    TSNode preorderNext() const;
    TSNode preorderSkip() const;
    std::partial_ordering operator<=>(const TSNode & other) const;

    struct Hasher
    {
        std::size_t operator()(const TSNode & node) const noexcept;
    };
private:
    ts::TSNode node;
    const std::string * source;

    void assertNonNull() const;
};

class TSTree
{
public:
    TSTree(ts::TSTree * tree, std::string_view source);
    TSTree(ts::TSTree * tree, std::string && source);
    TSTree();

    TSTree(const TSTree &) = delete;
    TSTree(TSTree &&) = default;
    TSTree & operator=(const TSTree &) = delete;
    TSTree & operator=(TSTree &&) = default;
    ~TSTree() = default;

    operator ts::TSTree *();
    operator const ts::TSTree *() const;
    ts::TSTree * get();

    TSNode rootNode() const;
    TSNode rootNodeWithOffset(uint32_t offsetBytes, ts::TSPoint offsetExtent) const;
    const TSLanguage language() const;

    std::tuple<ts::TSRange, uint32_t> includedRanges() const;
    void edit(const ts::TSInputEdit *edit);
    std::tuple<ts::TSRange, uint32_t> changedRanges(const ts::TSTree * oldTree) const;
    void printDotGraph(int fileDescriptor) const;

    // Helper functions
    const std::string & getSource() const;
private:
    std::unique_ptr<ts::TSTree, decltype(&ts::ts_tree_delete)> tree;
    std::unique_ptr<const std::string> sourcePtr; // To make sure std::string_views in its nodes are valid after moving

    void assertNoError() const;
};

class TSParser
{
public:
    TSParser(const ts::TSLanguage * language);
    TSParser(ts::TSParser * parser);

    TSParser(const TSParser &) = delete;
    TSParser(TSParser &&) = default;
    TSParser & operator=(const TSParser &) = delete;
    TSParser & operator=(TSParser &&) = default;
    ~TSParser() = default;

    operator ts::TSParser *();
    operator const ts::TSParser *() const;
    ts::TSParser * get();

    const TSLanguage language() const;
    bool setLanguage(const ts::TSLanguage * language);

    TSTree parseString(std::string_view source);
    TSTree parseString(std::string && source);
    void reset();
private:
    std::unique_ptr<ts::TSParser, decltype(&ts::ts_parser_delete)> parser;
};

class TSQuery
{
public:
    TSQuery(const ts::TSLanguage * language, std::string_view source);

    TSQuery(const TSQuery &) = delete;
    TSQuery(TSQuery &&) = default;
    TSQuery & operator=(const TSQuery &) = delete;
    TSQuery & operator=(TSQuery &&) = default;
    ~TSQuery() = default;

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
    TSTreeCursor(const TSNode & node);

    TSTreeCursor(const TSTreeCursor & src);
    TSTreeCursor(TSTreeCursor && src);
    TSTreeCursor & operator=(const TSTreeCursor & src);
    TSTreeCursor & operator=(TSTreeCursor && src);
    ~TSTreeCursor() = default;

    operator ts::TSTreeCursor() const;
    operator ts::TSTreeCursor *();
    operator const ts::TSTreeCursor *() const;
    void reset(const TSNode & node);
    void resetTo(const TSTreeCursor & src);
    TSNode currentNode() const;
    std::string currentFieldName() const;
    TSFieldId currentFieldId() const;
    bool gotoParent();
    bool gotoNextSibling();
    bool gotoPreviousSibling();
    bool gotoFirstChild();
    bool gotoLastChild();
    void gotoDescendant(uint32_t goalDescendantIndex);
    uint32_t currentDescendantIndex() const;
    uint32_t currentDepth() const;
    int64_t gotoFirstChildForByte(uint32_t goalByte);
    int64_t gotoFirstChildForPoint(ts::TSPoint goalPoint);

    // Helper functions
    bool preorderNext();
    bool preorderSkip();
    TSTreeCursorIterateChildren iterateChildren() const;
    TSTreeCursorIterateDescendants iterateDescendants() const;
private:
    ts::TSTreeCursor cursor;
    std::unique_ptr<ts::TSTreeCursor, decltype(&ts::ts_tree_cursor_delete)> cursorPtr;
    TSNode root;
};

// Class to iterate over children of a TSTreeCursor's current node.
class TSTreeCursorIterateChildren
    : public std::ranges::view_interface<TSTreeCursorIterateChildren>
{
public:
    // Nested iterator class
    class Iterator
    {
    public:
        using difference_type = std::ptrdiff_t;
        using value_type = TSNode;
        using iterator_concept = std::bidirectional_iterator_tag;

        Iterator() : cursor(std::nullopt), atEnd(true) {}

        // Constructor with a given TSTreeCursor state and valid flag
        explicit Iterator(TSTreeCursor && cursor, bool atEnd = false)
            : cursor(std::move(cursor)), atEnd(atEnd) {}

        // Dereference operator returns the current node.
        TSNode operator *() const
        {
            assert(!atEnd);
            return cursor->currentNode();
        }

        // Pre-increment: move to the next sibling child.
        Iterator & operator++()
        {
            assert(!atEnd);
            if (!cursor->gotoNextSibling())
            {
                cursor->gotoParent();
                atEnd = true;
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

        Iterator & operator--()
        {
            if (atEnd)
            {
                if (cursor->gotoLastChild())
                {
                    atEnd = false;
                }
            }
            else cursor->gotoPreviousSibling();

            return *this;
        }

        Iterator operator--(int)
        {
            Iterator copy = *this;
            --(*this);
            return copy;
        }

        // Equality: iterators are equal if both are invalid (i.e. at the end) or 
        // if both are valid and their current nodes compare equal.
        bool operator==(const Iterator & other) const
        {
            if (!cursor.has_value() && !other.cursor.has_value()) return true;
            if (atEnd != other.atEnd) return false;
            if (cursor.has_value() && other.cursor.has_value())
            {
                return cursor->currentNode() == other.cursor->currentNode();
            }
            return false;
        }

    private:
        std::optional<TSTreeCursor> cursor;
        bool atEnd;
        // if atEnd, cursor must be the parent node
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

    Iterator end() const
    {
        TSTreeCursor parentCursorCopy = parentCursor;
        return Iterator(std::move(parentCursorCopy), true);
    }

    std::reverse_iterator<Iterator> rbegin() const
    {
        return std::reverse_iterator<Iterator>(end());
    }

    std::reverse_iterator<Iterator> rend() const
    {
        return std::reverse_iterator<Iterator>(begin());
    }

private:
    TSTreeCursor parentCursor;
};
static_assert(std::bidirectional_iterator<TSTreeCursorIterateChildren::Iterator>);
static_assert(std::ranges::bidirectional_range<TSTreeCursorIterateChildren>);

// Class to iterate over all descendants (pre-order) of a TSTreeCursor's current node.
class TSTreeCursorIterateDescendants
    : public std::ranges::view_interface<TSTreeCursorIterateChildren>
{
public:
    // Nested iterator class.
    class Iterator
    {
    public:
        using difference_type = std::ptrdiff_t;
        using value_type = TSNode;
        using iterator_concept = std::forward_iterator_tag;

        // Default constructor (end iterator).
        Iterator() : cursor(std::nullopt) {}

        // Constructor with a given TSTreeCursor state and the root node.
        explicit Iterator(TSTreeCursor && cursor, const TSNode & root)
            : cursor(std::move(cursor)) {}

        // Dereference operator returns the current node.
        TSNode operator*() const
        {
            assert(cursor.has_value());
            return cursor->currentNode();
        }

        // Pre-increment: move to the next descendant in pre-order.
        Iterator & operator++()
        {
            assert(cursor.has_value());
            TSTreeCursor cur = std::move(cursor.value());
            // Use the preorderNext helper function to move to the next node.
            if (!cur.preorderNext()) cursor = std::nullopt;
            else cursor = std::move(cur);

            return *this;
        }

        // Post-increment.
        Iterator operator++(int)
        {
            Iterator copy = *this;
            ++(*this);
            return copy;
        }

        // Equality: iterators are equal if both are invalid (i.e., at the end)
        // or if both are valid and their current nodes compare equal.
        bool operator==(const Iterator & other) const
        {
            if (!cursor.has_value() && !other.cursor.has_value())
                return true;
            if (cursor.has_value() && other.cursor.has_value())
                return cursor->currentNode() == other.cursor->currentNode();
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
    explicit TSTreeCursorIterateDescendants(const TSTreeCursor & parentCursor)
        : parentCursor(parentCursor)
    {}

    // begin() returns an iterator positioned at the first descendant.
    Iterator begin() const
    {
        TSTreeCursor childCursor = parentCursor;
        TSNode root = childCursor.currentNode();
        if (!childCursor.gotoFirstChild())
            return end();
        return Iterator(std::move(childCursor), root);
    }

    // end() returns a default-constructed iterator (i.e. invalid).
    Iterator end() const
    {
        return Iterator();
    }

private:
    TSTreeCursor parentCursor;
};
static_assert(std::forward_iterator<TSTreeCursorIterateDescendants::Iterator>);
static_assert(std::ranges::forward_range<TSTreeCursorIterateDescendants>);

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


// TSLanguage

// Constructor
TSLanguage::TSLanguage(const ts::TSLanguage * language)
    : language(language, &ts::ts_language_delete)
{
}

TSLanguage::TSLanguage(const TSLanguage &src)
    : language(ts::ts_language_copy(src), &ts::ts_language_delete)
{
}

TSLanguage &TSLanguage::operator=(const TSLanguage &src)
{
    language.reset(ts::ts_language_copy(src));
    return *this;
}

TSLanguage::operator const ts::TSLanguage *() const
{
    return language.get();
}

const ts::TSLanguage * TSLanguage::get()
{
    return language.get();
}

// Get the number of distinct node types in the language.
uint32_t TSLanguage::symbolCount() const
{
    return ts::ts_language_symbol_count(*this);
}

// Get the number of valid states in this language.
uint32_t TSLanguage::stateCount() const
{
    return ts::ts_language_state_count(*this);
}

// Get the numerical id for the given node type string.
TSSymbol TSLanguage::symbolForName(const std::string & name, bool isNamed) const
{
    return ts::ts_language_symbol_for_name(*this, name.c_str(), name.size(), isNamed);
}

// Get the number of distinct field names in the language.
uint32_t TSLanguage::fieldCount() const
{
    return ts::ts_language_field_count(*this);
}

// Get the field name string for the given numerical id.
std::string TSLanguage::fieldNameForId(ts::TSFieldId id) const
{
    const char * fname = ts::ts_language_field_name_for_id(*this, id);
    return fname ? std::string( fname ) : std::string();
}

// Get the numerical id for the given field name string.
ts::TSFieldId TSLanguage::fieldIdForName(const std::string & name) const
{
    return ts::ts_language_field_id_for_name(*this, name.c_str(), static_cast< uint32_t >( name.size() ) );
}

// Get a list of all supertype symbols for the language.
std::vector<TSSymbol> TSLanguage::supertypes() const
{
    uint32_t length = 0;
    const ts::TSSymbol * symbols = ts::ts_language_supertypes(*this, & length );
    return std::vector<ts::TSSymbol>( symbols, symbols + length );
}

// Get a list of all subtype symbol ids for a given supertype symbol.
// See [`ts_language_supertypes`] for fetching all supertype symbols.
std::vector<TSSymbol> TSLanguage::subtypes(ts::TSSymbol supertype) const
{
    uint32_t length = 0;
    const TSSymbol * symbols = ts::ts_language_subtypes(*this, supertype, & length );
    return std::vector<TSSymbol>( symbols, symbols + length );
}

// Get a node type string for the given numerical id.
std::string TSLanguage::symbolName(ts::TSSymbol symbol) const
{
    const char * name = ts::ts_language_symbol_name(*this, symbol);
    return TSUtils::freeCstrToString(name);
}

// Check whether the given node type id belongs to named nodes, anonymous nodes,
// or a hidden nodes.
// See also [`ts_node_is_named`]. Hidden nodes are never returned from the API.
TSSymbolType TSLanguage::symbolType(ts::TSSymbol symbol) const
{
    return ts::ts_language_symbol_type(*this, symbol);
}

// @deprecated use [`ts_language_abi_version`] instead, this will be removed in 0.26.
// Get the ABI version number for this language. This version number is used
// to ensure that languages were generated by a compatible version of
// Tree-sitter.
// See also [`ts_parser_set_language`].
uint32_t TSLanguage::languageVersion() const
{
    return ts::ts_language_version(*this);
}

// Get the ABI version number for this language. This version number is used
// to ensure that languages were generated by a compatible version of
// Tree-sitter.
// See also [`ts_parser_set_language`].
uint32_t TSLanguage::abiVersion() const
{
    return ts::ts_language_abi_version(*this);
}

// Get the metadata for this language. This information is generated by the
// CLI, and relies on the language author providing the correct metadata in
// the language's `tree-sitter.json` file.
// See also [`TSMetadata`].
const ts::TSLanguageMetadata * TSLanguage::metadata() const
{
    return ts::ts_language_metadata(*this);
}

// Get the next parse state. Combine this with lookahead iterators to generate
// completion suggestions or valid symbols in error nodes. Use
// [`ts_node_grammar_symbol`] for valid symbols.
ts::TSStateId TSLanguage::nextState(ts::TSStateId state, ts::TSSymbol symbol) const
{
    return ts::ts_language_next_state(*this, state, symbol);
}

// Get the name of this language. This returns `NULL` in older parsers.
std::string TSLanguage::name() const
{
    const char * langName = ts::ts_language_name(*this);
    return TSUtils::freeCstrToString(langName);
}


// TSNode

// Constructor for TSNode
TSNode::TSNode(const ts::TSNode & node, const std::string * source)
    : node(node), source(source)
{
}

TSNode::TSNode()
{
    node =
    {
        .context = {0, 0, 0, 0},
        .id = nullptr,
        .tree = nullptr,
    };
    source = nullptr;
}

// Conversion operator: Get the underlying ts::TSNode.
TSNode::operator ts::TSNode() const
{
    return node;
}


// Get the node's type as a null-terminated string.
std::string TSNode::type() const
{
    const char * t = ts::ts_node_type(*this);
    if (t == nullptr) return std::string();
    return t;
}

// Get the node's type as a numerical id.
TSSymbol TSNode::symbol() const
{
    assertNonNull();
    return ts::ts_node_symbol(*this);
}

// Get the node's language.
const TSLanguage TSNode::language() const
{
    assertNonNull();
    return ts::ts_node_language(*this);
}

// Get the node's type as it appears in the grammar ignoring aliases as a null-terminated string.
std::string TSNode::grammarType() const
{
    assertNonNull();
    return ts::ts_node_grammar_type(*this);
}

// Get the node's type as a numerical id as it appears in the grammar ignoring aliases.
// This should be used in ts_language_next_state instead of ts_node_symbol.
TSSymbol TSNode::grammarSymbol() const
{
    assertNonNull();
    return ts::ts_node_grammar_symbol(*this);
}

// Get the node's start byte.
uint32_t TSNode::startByte() const
{
    assertNonNull();
    return ts::ts_node_start_byte(*this);
}

// Get the node's start position in terms of rows and columns.
TSPoint TSNode::startPoint() const
{
    assertNonNull();
    return ts::ts_node_start_point(*this);
}

// Get the node's end byte.
uint32_t TSNode::endByte() const
{
    assertNonNull();
    return ts::ts_node_end_byte(*this);
}

// Get the node's end position in terms of rows and columns.
TSPoint TSNode::endPoint() const
{
    assertNonNull();
    return ts::ts_node_end_point(*this);
}

// Get an S-expression representing the node as a string.
// This string is allocated with malloc and the caller is responsible for freeing it using free.
std::string TSNode::sExpression() const
{
    assertNonNull();
    return TSUtils::freeCstrToString(ts::ts_node_string(*this));
}

// Get the node's number of children.
uint32_t TSNode::childCount() const
{
    assertNonNull();
    return ts::ts_node_child_count(*this);
}

// Get the node's number of named children.
uint32_t TSNode::namedChildCount() const
{
    assertNonNull();
    return ts::ts_node_named_child_count(*this);
}

// Get the node's child at the given index, where zero represents the first child.
TSNode TSNode::child(uint32_t index) const
{
    assertNonNull();
    return { ts::ts_node_child(*this, index), source };
}

// Get the node's named child at the given index.
TSNode TSNode::namedChild(uint32_t index) const
{
    assertNonNull();
    return { ts::ts_node_named_child(*this, index), source };
}

// Get the node's next sibling.
TSNode TSNode::nextSibling() const
{
    assertNonNull();
    return { ts::ts_node_next_sibling(*this), source };
}

// Get the node's previous sibling.
TSNode TSNode::prevSibling() const
{
    assertNonNull();
    return { ts::ts_node_prev_sibling(*this), source };
}

// Get the node's next named sibling.
TSNode TSNode::nextNamedSibling() const
{
    assertNonNull();
    return { ts::ts_node_next_named_sibling(*this), source };
}

// Get the node's previous named sibling.
TSNode TSNode::prevNamedSibling() const
{
    assertNonNull();
    return { ts::ts_node_prev_named_sibling(*this), source };
}

// Get the node's first child that contains or starts after the given byte offset.
TSNode TSNode::firstChildForByte(uint32_t byte) const
{
    assertNonNull();
    return { ts::ts_node_first_child_for_byte(*this, byte), source };
}

// Get the node's first named child that contains or starts after the given byte offset.
TSNode TSNode::firstNamedChildForByte(uint32_t byte) const
{
    assertNonNull();
    return { ts::ts_node_first_named_child_for_byte(*this, byte), source };
}

// Get the node's number of descendants, including one for the node itself.
uint32_t TSNode::descendantCount() const
{
    assertNonNull();
    return ts::ts_node_descendant_count(*this);
}

// Get the smallest node within this node that spans the given range of bytes.
TSNode TSNode::descendantForByteRange(uint32_t start, uint32_t end) const
{
    assertNonNull();
    return { ts::ts_node_descendant_for_byte_range(*this, start, end), source };
}

// Get the smallest node within this node that spans the given range of (row, column) positions.
TSNode TSNode::descendantForPointRange(ts::TSPoint start, ts::TSPoint end) const
{
    assertNonNull();
    return { ts::ts_node_descendant_for_point_range(*this, start, end), source };
}

// Get the smallest named node within this node that spans the given range of bytes.
TSNode TSNode::namedDescendantForByteRange(uint32_t start, uint32_t end) const
{
    assertNonNull();
    return { ts::ts_node_named_descendant_for_byte_range(*this, start, end), source };
}

// Get the smallest named node within this node that spans the given range of (row, column) positions.
TSNode TSNode::namedDescendantForPointRange(ts::TSPoint start, ts::TSPoint end) const
{
    assertNonNull();
    return { ts::ts_node_named_descendant_for_point_range(*this, start, end), source };
}

// Edit the node to keep it in sync with source code that has been edited.
// This function is only rarely needed. When you edit a syntax tree with the ts_tree_edit function, all of the nodes that you retrieve from the tree afterward will already reflect the edit.
// You only need to use ts_node_edit when you have a TSNode instance that you want to keep and continue to use after an edit.
void TSNode::edit(const ts::TSInputEdit *edit)
{
    assertNonNull();
    ts::ts_node_edit(&node, edit);
}

// Check if two nodes are identical.
bool TSNode::eq(const ts::TSNode & other) const
{
    return ts::ts_node_eq(*this, other);
}

// Check if two nodes are identical.
bool TSNode::operator==(const TSNode & other) const
{
    return ts::ts_node_eq(*this, other) || !(*this) && !other;
}

// Check if the node is null.
// Functions like ts_node_child and ts_node_next_sibling will return a null node to indicate that no such node was found.
bool TSNode::isNull() const
{
    return ts::ts_node_is_null(*this) || source == nullptr;
}

TSNode::operator bool() const
{
    return !isNull();
}

// Check if the node is named.
// Named nodes correspond to named rules in the grammar, whereas anonymous nodes correspond to string literals in the grammar.
bool TSNode::isNamed() const
{
    assertNonNull();
    return ts::ts_node_is_named(*this);
}

// Check if the node is missing.
// Missing nodes are inserted by the parser in order to recover from certain kinds of syntax errors.
bool TSNode::isMissing() const
{
    assertNonNull();
    return ts::ts_node_is_missing(*this);
}

// Check if the node is extra.
// Extra nodes represent things like comments, which are not required the grammar, but can appear anywhere.
bool TSNode::isExtra() const
{
    assertNonNull();
    return ts::ts_node_is_extra(*this);
}

// Check if the syntax node has been edited.
bool TSNode::hasChanges() const
{
    assertNonNull();
    return ts::ts_node_has_changes(*this);
}

// Check if the node has any syntax errors.
bool TSNode::hasError() const
{
    assertNonNull();
    return ts::ts_node_has_error(*this);
}

// Check if the node is a syntax error.
bool TSNode::isError() const
{
    assertNonNull();
    return ts::ts_node_is_error(*this);
}

// Get this node's parse state.
TSStateId TSNode::parseState() const
{
    assertNonNull();
    return ts::ts_node_parse_state(*this);
}

// Get the parse state after this node.
TSStateId TSNode::nextParseState() const
{
    assertNonNull();
    return ts::ts_node_next_parse_state(*this);
}

// Get the node's immediate parent.
// Prefer ts_node_child_with_descendant for iterating over the node's ancestors.
TSNode TSNode::parent() const
{
    assertNonNull();
    return { ts::ts_node_parent(*this), source };
}

// Get the node that contains the given descendant.
// Note that this can return descendant itself.
TSNode TSNode::childWithDescendant(ts::TSNode descendant) const
{
    assertNonNull();
    return { ts::ts_node_child_with_descendant(*this, descendant), source };
}

// Get the field name for the node's child at the given index.
// Returns NULL if no field is found.
std::string TSNode::fieldNameForChild(uint32_t child_index) const
{
    assertNonNull();
    return TSUtils::freeCstrToString(ts::ts_node_field_name_for_child(*this, child_index));
}

// Get the field name for the node's named child at the given index.
// Returns NULL if no field is found.
std::string TSNode::fieldNameForNamedChild(uint32_t named_child_index) const
{
    assertNonNull();
    return TSUtils::freeCstrToString(ts::ts_node_field_name_for_named_child(*this, named_child_index));
}

// Get the node's child with the given field name.
TSNode TSNode::childByFieldName(const std::string & name) const
{
    assertNonNull();
    return { ts::ts_node_child_by_field_name(*this, name.c_str(), name.size()), source };
}

// Get the node's child with the given numerical field id.
// You can convert a field name to an id using the ts_language_field_id_for_name function.
TSNode TSNode::childByFieldId(ts::TSFieldId field_id) const
{
    assertNonNull();
    return { ts::ts_node_child_by_field_id(*this, field_id), source };
}

bool TSNode::isSymbol(ts::TSSymbol symbol) const
{
    return symbol == this->symbol();
}

const std::string & TSNode::getSource() const
{
    assertNonNull();
    return *source;
}

std::string_view TSNode::textView() const
{
    if (isNull()) return "";
    return std::string_view(getSource()).substr(startByte(), length());
}

// Helper function: Get the text of the node from the source.
std::string TSNode::text() const
{
    return std::string(textView());
}

// Create a tree cursor for this node.
TSTreeCursor TSNode::cursor() const
{
    assertNonNull();
    return TSTreeCursor(*this);
}

TSTreeCursorIterateChildren TSNode::iterateChildren() const
{
    return cursor().iterateChildren();
}

TSTreeCursorIterateDescendants TSNode::iterateDescendants() const
{
    return cursor().iterateDescendants();
}

size_t TSNode::length() const
{
    return endByte() - startByte();
}

TSNode TSNode::preorderNext() const
{
    assertNonNull();
    // Try to descend to the first child.
    if (TSNode child = firstChildForByte(0)) return child;
    // Otherwise, climb up to find a next sibling.
    return preorderSkip();
}

TSNode TSNode::preorderSkip() const
{
    assertNonNull();
    // Climb up to find a next sibling.
    for (TSNode up = *this; up; up = up.parent())
    {
        if (TSNode next = up.nextSibling()) return next;
    }
    // If we reach the root node, return null.
    return TSNode();
}

std::partial_ordering TSNode::operator<=>(const TSNode &other) const
{
    if (isNull() && other.isNull())
    {
        return std::partial_ordering::equivalent;
    }
    if (isNull())
    {
        return std::partial_ordering::less;
    }
    if (other.isNull())
    {
        return std::partial_ordering::greater;
    }
    if (getSource() != other.getSource()) // Must be the same tree
    {
        return std::partial_ordering::unordered;
    }
    // Compare start bytes
    if (startByte() < other.startByte())
    {
        return std::partial_ordering::less;
    }
    if (startByte() > other.startByte())
    {
        return std::partial_ordering::greater;
    }
    // Compare end bytes. The larger end byte is considered less.
    if (endByte() < other.endByte())
    {
        return std::partial_ordering::greater;
    }
    if (endByte() > other.endByte())
    {
        return std::partial_ordering::less;
    }
    return std::partial_ordering::equivalent;
}

std::size_t Hayroll::TSNode::Hasher::operator()(const TSNode & node) const noexcept
{
    std::size_t hash = 0;
    for (uint32_t c : node.node.context)
    {
        hash ^= std::hash<uint32_t>()(c) << 1;
    }
    hash ^= std::hash<const void *>()(node.node.id) << 1;
    hash ^= std::hash<const ts::TSTree *>()(node.node.tree) << 1;
    return hash;
}

void TSNode::assertNonNull() const
{
    #if DEBUG
        // Throw and print stack trace if the node is null.
        if (isNull())
        {
            std::cout << boost::stacktrace::stacktrace() << std::endl;
            std::cout << std::flush;
            throw std::runtime_error("TSNode is null");
        }
    #endif
}


// TSTree

// Construct a TSTree with the given ts::TSTree pointer.
TSTree::TSTree(ts::TSTree * tree, std::string_view source)
    : tree(tree, ts::ts_tree_delete), sourcePtr(std::make_unique<std::string>(source)) // Copy and own
{
    #if DEBUG
        assertNoError();
    #endif
}

TSTree::TSTree(ts::TSTree *tree, std::string && source)
    : tree(tree, ts::ts_tree_delete), sourcePtr(std::make_unique<std::string>(std::move(source))) // Move and own
{
    #if DEBUG
        assertNoError();
    #endif
}

TSTree::TSTree()
    : tree(nullptr, ts::ts_tree_delete), sourcePtr(nullptr)
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
    return { ts::ts_tree_root_node(*this), sourcePtr.get() };
}

// Get the root node of the syntax tree, with its position shifted by the given offset.
TSNode TSTree::rootNodeWithOffset(uint32_t offsetBytes, ts::TSPoint offsetExtent) const
{
    return { ts::ts_tree_root_node_with_offset(*this, offsetBytes, offsetExtent), sourcePtr.get() };
}

// Get the language that was used to parse the syntax tree.
const TSLanguage TSTree::language() const
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
std::tuple<ts::TSRange, uint32_t> TSTree::changedRanges(const ts::TSTree * oldTree) const
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

const std::string & TSTree::getSource() const
{
    assert(tree);
    return *sourcePtr;
}

// Make sure there is no ERROR node in this tree by doing a query for it.
// This is quite expensive, so it should only be used in debug builds.
void TSTree::assertNoError() const
{
    if (rootNode().hasError())
    {
        std::cerr << boost::stacktrace::stacktrace() << std::endl;
        throw std::runtime_error("Tree contains ERROR node");
    }
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
const TSLanguage TSParser::language() const
{
    return ts::ts_parser_language(*this);
}

// Set the language for the parser.
bool TSParser::setLanguage(const ts::TSLanguage * language)
{
    return ts::ts_parser_set_language(*this, language);
}

// Use the parser to parse a string and create a syntax tree.
TSTree TSParser::parseString(std::string_view source)
{
    return { ts::ts_parser_parse_string(*this, nullptr, source.data(), source.size()), source };
}

TSTree TSParser::parseString(std::string && source)
{
    return { ts::ts_parser_parse_string(*this, nullptr, source.data(), source.size()), source };
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

// Create a new tree cursor starting from the given node.
TSTreeCursor::TSTreeCursor(const TSNode & node)
    : cursor(ts::ts_tree_cursor_new(node)), cursorPtr(&cursor, ts::ts_tree_cursor_delete), root(node)
{
}

// Copy constructor.
TSTreeCursor::TSTreeCursor(const TSTreeCursor & src)
    : cursor(ts::ts_tree_cursor_copy(src)), cursorPtr(&cursor, ts::ts_tree_cursor_delete), root(src.root)
{
}

// Move constructor.
TSTreeCursor::TSTreeCursor(TSTreeCursor && src)
    : cursor(src.cursor), cursorPtr(&cursor, ts::ts_tree_cursor_delete), root(src.root)
{
    src.cursorPtr.release();
}

// Copy assignment operator.
TSTreeCursor & TSTreeCursor::operator=(const TSTreeCursor & src)
{
    if (this != &src)
    {
        cursor = ts::ts_tree_cursor_copy(src);
        cursorPtr.reset(&cursor);
        root = src.root;
    }
    return *this;
}

// Move assignment operator.
TSTreeCursor & TSTreeCursor::operator=(TSTreeCursor && src)
{
    if (this != &src)
    {
        cursor = src.cursor;
        cursorPtr.reset(&cursor);
        src.cursorPtr.release();
        root = src.root;
    }
    return *this;
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
void TSTreeCursor::reset(const TSNode & node)
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
    return { ts::ts_tree_cursor_current_node(*this), &root.getSource() };
}

// Get the field name of the tree cursor's current node.
std::string TSTreeCursor::currentFieldName() const
{
    const char *field_name = ts::ts_tree_cursor_current_field_name(*this);
    return field_name ? std::string(field_name) : std::string();
}

// Get the field id of the tree cursor's current node.
TSFieldId TSTreeCursor::currentFieldId() const
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
int64_t TSTreeCursor::gotoFirstChildForPoint(ts::TSPoint goalPoint)
{
    return ts::ts_tree_cursor_goto_first_child_for_point(*this, goalPoint);
}

// Go to the next node in a pre-order traversal of the tree.
bool TSTreeCursor::preorderNext()
{
    // Try to descend to the first child.
    if (gotoFirstChild()) return true;
    // Otherwise, climb up to find a next sibling.
    return preorderSkip();
}

// Skip the subtree of the current node.
bool TSTreeCursor::preorderSkip()
{
    // Climb up to find a next sibling.
    while (true)
    {
        if (gotoNextSibling()) return true;
        if (!gotoParent()) return false;
    }
}

TSTreeCursorIterateChildren TSTreeCursor::iterateChildren() const
{
    return TSTreeCursorIterateChildren(*this);
}

TSTreeCursorIterateDescendants TSTreeCursor::iterateDescendants() const
{
    return TSTreeCursorIterateDescendants(*this);
}

} // namespace Hayroll

#endif // HAYROLL_TREESITTER_HPP
