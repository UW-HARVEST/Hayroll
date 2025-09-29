use std::collections::{HashMap, HashSet};

use ide_db::{base_db::{SourceDatabase, SourceRootDatabase}, source_change::SourceChangeBuilder, EditionedFileId};
use tracing_subscriber::{fmt, prelude::*, EnvFilter};

use ide::{Edition, RootDatabase};
use syntax::{
    ast::{self, HasAttrs, SourceFile},
    AstNode, AstToken, SyntaxElement, SyntaxNode, SyntaxToken, T,
    syntax_editor::Position
};
use vfs::FileId;

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
        .unwrap();
    Position::before(&first_node)
}

pub fn bot_pos(source_file: &ast::SourceFile) -> Position {
    let last_node = source_file.syntax().children().last().unwrap();
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

// Collect all parsed `SourceFile` roots from the database without using VFS.
//
// Strategy:
// - Gather all SourceRootIds by walking the crate graph's root files.
// - For each SourceRoot, iterate all files and parse them to `ast::SourceFile`.
//
// Note: we don't filter by file extension here; non-Rust files will parse to empty/near-empty trees
// and will be naturally ignored by later passes that expect Rust syntax nodes.
pub fn collect_syntax_roots_from_db(db: &RootDatabase) -> HashMap<FileId, SourceFile> {
    let graph = db.crate_graph();
    let mut source_root_ids = HashSet::new();
    for krate in graph.iter() {
        let root_file = graph[krate].root_file_id;
        source_root_ids.insert(db.file_source_root(root_file));
    }

    let mut out = HashMap::new();
    for sr_id in source_root_ids {
        let sr = db.source_root(sr_id);
        // Iterate all files in this source root; parse and record their trees.
        // Depending on the RA version, the `SourceRoot` may expose iteration via `iter()` over FileId.
        for file_id in sr.iter() {
            let tree = db.parse(EditionedFileId::current_edition(file_id)).tree();
            out.insert(file_id, tree);
        }
    }
    out
}


pub fn stmt_is_hayroll_tag(stmt: &ast::Stmt) -> bool {
    // Strategy: look for any byte string literal inside the stmt whose decoded contents
    // parse as JSON and contain { "hayroll": true }.
    // This matches instrumentation like:
    // *(b"{\"astKind\":\"Stmts\",... ,\"hayroll\":true,...}\0" as *const u8 as *const libc::c_char);
    for element in stmt.syntax().descendants_with_tokens() {
        if let Some(token) = element.clone().into_token() {
            if let Some(byte_str) = ast::ByteString::cast(token) {
                let content = match byte_str.value() {
                    Ok(cow) => String::from_utf8_lossy(&cow).to_string(),
                    Err(_) => continue,
                };
                let content = content.trim_end_matches(char::from(0));
                if let Ok(val) = serde_json::from_str::<serde_json::Value>(&content) {
                    if val.get("hayroll").and_then(|v| v.as_bool()) == Some(true) {
                        return true;
                    }
                }
            }
        }
    }
    false
}

// A helper structure to manage multiple SourceChangeBuilders keyed by FileId.
// It provides a facade mirroring a subset of SourceChangeBuilder's API, routing
// calls to the appropriate underlying builder based on the file being edited.
pub struct SourceChangeBuilderSet {
    builders: HashMap<FileId, SourceChangeBuilder>,
    root_to_file: HashMap<syntax::SyntaxNode, FileId>,
}

impl SourceChangeBuilderSet {
    pub fn new() -> Self { Self { builders: HashMap::new(), root_to_file: HashMap::new() } }

    // Pre-populate a builder per file, each with a full mutable tree initialized.
    pub fn from_syntax_roots(syntax_roots: &HashMap<FileId, ast::SourceFile>) -> Self {
        let mut set = SourceChangeBuilderSet::new();
        for (file_id, source_file) in syntax_roots {
            let mut builder = SourceChangeBuilder::new(*file_id);
            // Initialize mutable tree for that file (mirrors previous helper behavior)
            builder.make_mut(source_file.clone());
            let root = source_file.syntax().clone();
            // let ptr = syntax::SyntaxNodePtr::new(&root);
            set.root_to_file.insert(root, *file_id);
            set.builders.insert(*file_id, builder);
        }
        set
    }

    pub fn get(&mut self, file_id: FileId) -> &mut SourceChangeBuilder {
        self.builders.get_mut(&file_id).expect("No builder for file id (did you forget from_syntax_roots?)")
    }

    pub fn builder_mut(&mut self, file_id: FileId) -> &mut SourceChangeBuilder { self.get(file_id) }

    // Attempt to derive the file id from an arbitrary node by walking to its immutable root.
    // NOTE: This works only for nodes from the original syntax trees (immutable roots). After
    // a node is cloned_for_update() the root changes and resolution may fail.
    fn file_id_of_node(&self, node: &syntax::SyntaxNode) -> Option<FileId> {
        let root = node.ancestors().last().unwrap_or_else(|| node.clone());
        self.root_to_file.get(&root).copied()
    }

    #[allow(dead_code)]
    pub fn builder_mut_for_node(&mut self, node: &syntax::SyntaxNode) -> &mut SourceChangeBuilder {
        let file_id = self.file_id_of_node(node).expect("Unable to resolve FileId from node root");
        self.get(file_id)
    }

    // Mirror SourceChangeBuilder::make_editor (independent of which builder).
    #[allow(dead_code)]
    pub fn make_editor(&self, node: &syntax::SyntaxNode) -> syntax::syntax_editor::SyntaxEditor {
        syntax::syntax_editor::SyntaxEditor::new(
            node.ancestors().last().unwrap_or_else(|| node.clone())
        )
    }

    // Route add_file_edits to the correct underlying builder.
    #[allow(dead_code)]
    pub fn add_file_edits(&mut self, file_id: FileId, editor: syntax::syntax_editor::SyntaxEditor) {
        self.builder_mut(file_id).add_file_edits(file_id, editor);
    }

    // Force a commit on every underlying builder without consuming them. Since
    // SourceChangeBuilder::commit is private, we trigger it indirectly by
    // calling edit_file with the same file_id, which internally performs a commit.
    pub fn commit(&mut self) {
        for (_fid, builder) in self.builders.iter_mut() {
            let fid = builder.file_id;
            builder.edit_file(fid); // self-commit
        }
    }

    // Convenience: infer file id from the node itself.
    pub fn make_mut<N: syntax::AstNode>(&mut self, node: N) -> N {
        let file_id = self.file_id_of_node(node.syntax()).expect("Unable to resolve FileId from node");
        self.get(file_id).make_mut(node)
    }

    // Route make_syntax_mut to the appropriate builder.
    #[allow(dead_code)]
    pub fn make_syntax_mut(&mut self, node: syntax::SyntaxNode) -> syntax::SyntaxNode {
        let file_id = self.file_id_of_node(&node).expect("Unable to resolve FileId from node");
        self.get(file_id).make_syntax_mut(node)
    }

    // Finish all builders, merging their SourceChanges.
    pub fn finish(mut self) -> ide::SourceChange {
        self.commit();
        self.builders.into_iter().fold(ide::SourceChange::default(), |acc, (_fid, builder)| {
            let change = builder.finish();
            acc.merge(change)
        })
    }
}
