// Test playground, not functional

use std::env;
use std::fs;
use syntax;
use syntax::ast::{self, AstNode};
use syntax::SourceFile;
// use syntax::SyntaxNode;
use syntax::ted;

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        eprintln!("Usage: {} <rust_source_file>", args[0]);
        std::process::exit(1);
    }

    let file_path = &args[1];

    let source_code = fs::read_to_string(file_path).expect("Failed to read the Rust source file");

    let parse = SourceFile::parse(&source_code, syntax::Edition::Edition2021);
    if !parse.errors().is_empty() {
        eprintln!("Syntax errors found:");
        for error in parse.errors() {
            eprintln!(" - {}", error);
        }
    }

    let syntax_tree = parse.syntax_node().clone_for_update();

    // Find all name or variable uses in the file, rename them as ORIGINAL_NAME_renamed
    // Replace it in the syntax tree
    // Then print the new source code
    // Walk the syntax tree to find all names
    let mut names_to_replace: Vec<ast::Name> = Vec::new();
    let mut name_refs_to_replace: Vec<ast::NameRef> = Vec::new();
    for node in syntax_tree.descendants() {
        if let Some(name) = ast::Name::cast(node.clone()) {
            names_to_replace.push(name);
        } else if let Some(name_ref) = ast::NameRef::cast(node.clone()) {
            name_refs_to_replace.push(name_ref);
        }
    }

    for name in names_to_replace {
        let original_name = name.text().to_string();
        let new_name = format!("{}_renamed", original_name);
        let new_name_node = ast::make::name(&new_name).clone_for_update();
        ted::replace(name.syntax(), new_name_node.syntax());
    }
    for name_ref in name_refs_to_replace {
        let original_name = name_ref.text().to_string();
        let new_name = format!("{}_renamed", original_name);
        let new_name_node = ast::make::name_ref(&new_name).clone_for_update();
        ted::replace(name_ref.syntax(), new_name_node.syntax());
    }

    let new_source_code = syntax_tree.to_string();
    println!("{}", new_source_code);
}
