use tracing_subscriber::{fmt, prelude::*, EnvFilter};

use ide::Edition;
use syntax::{
    ast::{self, HasAttrs, SourceFile},
    ted::Position,
    AstNode, AstToken, SyntaxElement, SyntaxNode, SyntaxToken, T,
};

// Public logging initialization
pub fn init_logging() {
    let fmt_layer = fmt::layer()
        .with_thread_ids(true)
        .with_thread_names(true)
        .with_target(false);

    let filter = EnvFilter::try_from_default_env().unwrap_or_else(|_| EnvFilter::new("info"));

    tracing_subscriber::registry().with(filter).with(fmt_layer).init();
}

// Create an AST node from a string
pub fn ast_from_text<N: AstNode>(text: &str) -> N {
    let parse = SourceFile::parse(text, Edition::CURRENT);
    let node = match parse.tree().syntax().descendants().find_map(N::cast) {
        Some(it) => it,
        None => {
            let node = std::any::type_name::<N>();
            // Print out all descendants for debugging
            eprintln!("Failed to parse node of type {node} from text {text}");
            for descendant in parse.tree().syntax().descendants() {
                eprintln!("  - {}", descendant);
            }
            panic!("Failed to make ast node `{node}` from text {text}")
        }
    };
    let node = node.clone_subtree();
    assert_eq!(node.syntax().text_range().start(), 0.into());
    node
}

// Create an expression from a string
pub fn expr_from_text(text: &str) -> ast::Expr {
    ast_from_text(&format!("const C: () = {text};"))
}

// Get a mutable dollar token from a macro_rules
pub fn get_dollar_token_mut() -> SyntaxToken {
    let macro_rules = ast_from_text::<ast::MacroRules>("macro_rules! M {($x:expr) => {};}");
    // find a dollar token in the parsed macro_rules
    let dollar_token = macro_rules
        .syntax()
        .descendants_with_tokens()
        .find(|element| element.kind() == T![$])
        .unwrap()
        .into_token()
        .unwrap();
    let dollar_parent_node = dollar_token.parent().unwrap().clone_for_update();
    // find the dollar token in the dollar_parent_node that is now mutable
    let dollar_token_mut = dollar_parent_node
        .descendants_with_tokens()
        .find(|element| element.kind() == T![$])
        .unwrap()
        .into_token()
        .unwrap();
    dollar_token_mut
}

pub fn get_empty_line_element_mut() -> SyntaxElement {
    let empty_line = ast::make::tokens::whitespace("\n");
    syntax::NodeOrToken::Token(empty_line)
}

pub fn top_pos(source_file: &ast::SourceFile) -> Position {
    let first_node = source_file
        .syntax()
        .children()
        .find(|element| !ast::Attr::can_cast(element.kind()))
        .unwrap()
        .clone();
    Position::before(&first_node)
}

pub fn bot_pos(source_file: &ast::SourceFile) -> Position {
    let last_node = source_file.syntax().children().last().unwrap().clone();
    Position::after(&last_node)
}

// Takes a node and keeps climbing up to the parent node until the node itself is of the given kind
pub fn parent_until_kind_and_cond<T>(node: &impl ast::AstNode, condition: fn(&T) -> bool) -> Option<T>
where
    T: ast::AstNode,
{
    let mut node = node.syntax().clone();
    while !(T::can_cast(node.kind()) && condition(&T::cast(node.clone()).unwrap())) {
        node = node.parent()?;
    }
    Some(T::cast(node)?)
}

// Takes a node and keeps climbing up to the parent node until the node itself is of the given kind
pub fn parent_until_kind<T>(node: &impl ast::AstNode) -> Option<T>
where
    T: ast::AstNode,
{
    parent_until_kind_and_cond(node, |_| true)
}

// Takes a node and returns the parent node until the parent node satisfies the condition
#[allow(dead_code)]
pub fn parent_until(node: SyntaxNode, condition: fn(SyntaxNode) -> bool) -> Option<SyntaxNode> {
    let mut node = node;
    while !condition(node.clone()) {
        node = node.parent()?;
    }
    Some(node)
}

// Keep climbing up the AST from the given node until we find the source file
pub fn get_source_file(node: &impl ast::AstNode) -> ast::SourceFile {
    parent_until_kind::<ast::SourceFile>(node).unwrap()
}

#[derive(Clone, PartialEq, Eq)]
pub struct LnCol {
    pub line: u32,
    pub col: u32,
}

impl LnCol {
    pub fn new(line: u32, col: u32) -> Self {
        LnCol { line, col }
    }

    pub fn from_cu_ln_col(cu_ln_col: &str) -> Self {
        let mut parts = cu_ln_col.split(':');
        let line: u32 = parts.next().unwrap().parse().unwrap();
        let col: u32 = parts.next().unwrap().parse().unwrap();
        LnCol::new(line, col)
    }

    // Return true if this LnCol is within [start, end] using line/column containment semantics
    pub fn is_within(&self, range: &std::ops::RangeInclusive<LnCol>) -> bool {
        if self.line < range.start().line || self.line > range.end().line {
            return false;
        }
        if self.line == range.start().line && self.col < range.start().col {
            return false;
        }
        if self.line == range.end().line && self.col > range.end().col {
            return false;
        }
        true
    }
}

// Find all ast::Item in a SourceFile, who has a #[c2rust::src_loc = "l:c"] attribute within a range
pub fn find_items_in_range(
    source_file: &ast::SourceFile,
    range: std::ops::RangeInclusive<LnCol>,
) -> Vec<ast::Item> {
    source_file
        .syntax()
        .descendants()
        .filter_map(ast::Item::cast)
        .filter(|item| {
            item.attrs().any(|attr| {
                attr.meta().map_or(false, |meta| {
                    meta.path().map_or(false, |path| path.to_string() == "c2rust::src_loc")
                        && meta.expr().map_or(false, |expr| {
                            ast::String::cast(expr.syntax().first_token().unwrap())
                                .map_or(false, |string| {
                                    let cu_loc = string.value();
                                    let loc = LnCol::from_cu_ln_col(&cu_loc.as_ref().unwrap());
                                    loc.is_within(&range)
                                })
                        })
                })
            })
        })
        .collect()
}
