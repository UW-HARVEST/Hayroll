#include <iostream>
#include <string>
#include <optional>
#include <variant>
#include <memory>

#include "tree_sitter/tree-sitter-c-preproc.h"

#include "TreeSitter.hpp"
#include "SymbolTable.hpp"
#include "IncludeResolver.hpp"

using namespace Hayroll;

// int main()
// {
//     // Create a parser.
//     TSParser *parser = ts_parser_new();
  
//     // Set the parser's language (JSON in this case).
//     ts_parser_set_language(parser, tree_sitter_json());
  
//     // Build a syntax tree based on source code stored in a string.
//     const char *source_code = "[1, null]";
//     TSTree *tree = ts_parser_parse_string(
//       parser,
//       NULL,
//       source_code,
//       strlen(source_code)
//     );
  
//     // Get the root node of the syntax tree.
//     TSNode root_node = ts_tree_root_node(tree);
  
//     // Get some child nodes.
//     TSNode array_node = ts_node_named_child(root_node, 0);
//     TSNode number_node = ts_node_named_child(array_node, 0);
  
//     // Check that the nodes have the expected types.
//     assert(strcmp(ts_node_type(root_node), "document") == 0);
//     assert(strcmp(ts_node_type(array_node), "array") == 0);
//     assert(strcmp(ts_node_type(number_node), "number") == 0);
  
//     // Check that the nodes have the expected child counts.
//     assert(ts_node_child_count(root_node) == 1);
//     assert(ts_node_child_count(array_node) == 5);
//     assert(ts_node_named_child_count(array_node) == 2);
//     assert(ts_node_child_count(number_node) == 0);
  
//     // Print the syntax tree as an S-expression.
//     char *string = ts_node_string(root_node);
//     printf("Syntax tree: %s\n", string);
  
//     // Free all of the heap-allocated memory.
//     free(string);
//     ts_tree_delete(tree);
//     ts_parser_delete(parser);
//     return 0;
// }

// You can initialize a cursor from any node:

// TSTreeCursor ts_tree_cursor_new(TSNode);
// You can move the cursor around the tree:

// bool ts_tree_cursor_goto_first_child(TSTreeCursor *);
// bool ts_tree_cursor_goto_next_sibling(TSTreeCursor *);
// bool ts_tree_cursor_goto_parent(TSTreeCursor *);
// These methods return true if the cursor successfully moved and false if there was no node to move to.

// You can always retrieve the cursor's current node, as well as the field name that is associated with the current node.


// TSNode ts_tree_cursor_current_node(const TSTreeCursor *);
// const char *ts_tree_cursor_current_field_name(const TSTreeCursor *);
// TSFieldId ts_tree_cursor_current_field_id(const TSTreeCursor *);

#ifdef CLANG_EXE
const char * clang_exe_path = CLANG_EXE;
#else
const char * clang_exe_path = "clang";
#endif

int main()
{
    SymbolTablePtr symbolTable = SymbolTable::make();
    symbolTable->define(ObjectSymbol{"x", "1"});
    symbolTable->define(FunctionSymbol{"f", {"x"}, "x + 1"});

    auto x = symbolTable->lookup("x");
    if (x.has_value())
    {
        const Symbol & symbol = *x.value();
        std::cout << std::visit([](const auto & s) { return s.name; }, symbol) << std::endl;
    }

    // Parse the predefined macros

    IncludeResolver resolver(clang_exe_path, {});

    TSParser parser(tree_sitter_c_preproc());

    std::string predefinedMacros = resolver.getPredefinedMacros();

    std::cout << "Predefined macros:\n" << predefinedMacros << std::endl;
    
    TSTree tree = parser.parseString(predefinedMacros);

    // Walk the tree and add the macros to the symbol table
    // The tree will be like:
    // translation_unit [0, 0] - [2, 0]
    //     preproc_def [0, 0] - [1, 0]
    //         #define [0, 0] - [0, 7]
    //         name: identifier [0, 8] - [0, 28]
    //         value: preproc_arg [0, 29] - [0, 51]
    //     preproc_def [1, 0] - [2, 0]
    //         #define [1, 0] - [1, 7]
    //         name: identifier [1, 8] - [1, 29]
    //         value: preproc_arg [1, 30] - [1, 47]

    TSTreeCursor cursor(tree.rootNode());
    cursor.gotoFirstChild();
    while (true)
    {
        TSNode node = cursor.currentNode();

        assert(node.isNamed());

        TSNode nameNode = node.childByFieldName("name");
        TSNode valueNode = node.childByFieldName("value");

        assert(!nameNode.isNull());
        uint32_t nameStart = nameNode.startByte();
        uint32_t nameEnd = nameNode.endByte();
        std::string name = predefinedMacros.substr(nameStart, nameEnd - nameStart);

        std::string value;
        if (!valueNode.isNull())
        {
            uint32_t valueStart = valueNode.startByte();
            uint32_t valueEnd = valueNode.endByte();
            value = predefinedMacros.substr(valueStart, valueEnd - valueStart);
        }

        symbolTable->define(ObjectSymbol{std::move(name), std::move(value)});

        if (!cursor.gotoNextSibling())
        {
            break;
        }
    }

    std::cout << symbolTable->toString() << std::endl;

    return 0;
}
