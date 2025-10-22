use std::collections::HashMap;
use std::ops::RangeInclusive;

use serde_json::{self};
use syntax::syntax_editor::Element;
use syntax::{
    ast::{self, edit_in_place::AttrsOwnerEdit, HasAttrs},
    syntax_editor::Position,
    ted::{self},
    AstNode, AstToken, SourceFile, SyntaxElement, SyntaxNode,
};
use tracing::{error, trace, warn};
use vfs::FileId;

use crate::util::*;

// HayrollTag literal in the source code
#[derive(Clone, Debug)]
pub struct HayrollTag {
    pub literal: ast::Literal,
    pub tag: serde_json::Value,
    pub file_id: FileId,
}

// Intermediate trait: any type that can expose an underlying HayrollTag
// Implementing this automatically grants a HayrollMeta implementation via the blanket impl below.
pub trait HasHayrollTag {
    fn hayroll_tag(&self) -> &HayrollTag;
}

impl HasHayrollTag for HayrollTag {
    fn hayroll_tag(&self) -> &HayrollTag {
        self
    }
}

// Trait abstraction for hayroll metadata (blanket impl provided for HasHayrollTag types)
pub trait HayrollMeta {
    fn seed_type(&self) -> String;
    fn is_invocation(&self) -> bool;
    fn is_conditional(&self) -> bool;
    fn is_arg(&self) -> bool;
    fn name(&self) -> String;
    fn arg_names(&self) -> Vec<String>;
    fn loc_begin(&self) -> String;
    #[allow(dead_code)]
    fn loc_end(&self) -> String;
    fn cu_ln_col_begin(&self) -> String;
    fn cu_ln_col_end(&self) -> String;
    fn loc_ref_begin(&self) -> String;
    fn can_be_fn(&self) -> bool;
    fn file_id(&self) -> FileId;
    fn is_lvalue(&self) -> bool;
    fn ast_kind(&self) -> String;
    fn begin(&self) -> bool;
    fn is_expr(&self) -> bool;
    fn is_stmt(&self) -> bool;
    fn is_stmts(&self) -> bool;
    fn is_decl(&self) -> bool;
    fn is_decls(&self) -> bool;
    fn is_placeholder(&self) -> bool;
    fn premise(&self) -> String;
    fn merged_variants(&self) -> Vec<String>;
    fn with_appended_merged_variants(&self, new_variant: &str) -> ast::Literal;
    fn with_updated_begin(&self, new_begin: bool) -> ast::Literal;
}

impl<T: HasHayrollTag> HayrollMeta for T {
    fn seed_type(&self) -> String {
        self.hayroll_tag().tag["seedType"]
            .as_str()
            .unwrap()
            .to_string()
    }
    fn is_invocation(&self) -> bool {
        self.seed_type() == "invocation"
    }
    fn is_conditional(&self) -> bool {
        self.seed_type() == "conditional"
    }
    fn is_arg(&self) -> bool {
        self.hayroll_tag().tag["isArg"] == true
    }
    fn name(&self) -> String {
        self.hayroll_tag().tag["name"].as_str().unwrap().to_string()
    }
    fn arg_names(&self) -> Vec<String> {
        self.hayroll_tag().tag["argNames"]
            .as_array()
            .unwrap()
            .iter()
            .map(|a| a.as_str().unwrap().to_string())
            .collect()
    }
    fn loc_begin(&self) -> String {
        self.hayroll_tag().tag["locBegin"]
            .as_str()
            .unwrap()
            .to_string()
    }
    fn loc_end(&self) -> String {
        self.hayroll_tag().tag["locEnd"]
            .as_str()
            .unwrap()
            .to_string()
    }
    fn cu_ln_col_begin(&self) -> String {
        self.hayroll_tag().tag["cuLnColBegin"]
            .as_str()
            .unwrap()
            .to_string()
    }
    fn cu_ln_col_end(&self) -> String {
        self.hayroll_tag().tag["cuLnColEnd"]
            .as_str()
            .unwrap()
            .to_string()
    }
    fn loc_ref_begin(&self) -> String {
        self.hayroll_tag().tag["locRefBegin"]
            .as_str()
            .unwrap()
            .to_string()
    }
    fn can_be_fn(&self) -> bool {
        self.hayroll_tag().tag["canBeFn"] == true
    }
    fn file_id(&self) -> FileId {
        self.hayroll_tag().file_id
    }
    fn is_lvalue(&self) -> bool {
        self.hayroll_tag().tag["isLvalue"] == true
    }
    fn ast_kind(&self) -> String {
        self.hayroll_tag().tag["astKind"]
            .as_str()
            .unwrap()
            .to_string()
    }
    fn begin(&self) -> bool {
        self.hayroll_tag().tag["begin"] == true
    }
    fn is_expr(&self) -> bool {
        self.ast_kind() == "Expr"
    }
    fn is_stmt(&self) -> bool {
        self.ast_kind() == "Stmt"
    }
    fn is_stmts(&self) -> bool {
        self.ast_kind() == "Stmts"
    }
    fn is_decl(&self) -> bool {
        self.ast_kind() == "Decl"
    }
    fn is_decls(&self) -> bool {
        self.ast_kind() == "Decls"
    }
    fn is_placeholder(&self) -> bool {
        self.hayroll_tag().tag["isPlaceholder"].as_bool().unwrap()
    }
    fn premise(&self) -> String {
        self.hayroll_tag().tag["premise"]
            .as_str()
            .unwrap()
            .to_string()
    }
    fn merged_variants(&self) -> Vec<String> {
        self.hayroll_tag().tag["mergedVariants"]
            .as_array()
            .unwrap()
            .iter()
            .map(|a| a.as_str().unwrap().to_string())
            .collect()
    }
    fn with_appended_merged_variants(&self, new_variant: &str) -> ast::Literal {
        // Clone and update mergedVariants
        let mut new_tag = self.hayroll_tag().tag.clone();
        let mut merged_variants = self.merged_variants();
        merged_variants.push(new_variant.to_string());
        new_tag["mergedVariants"] = serde_json::Value::Array(
            merged_variants
                .iter()
                .map(|s| serde_json::Value::String(s.clone()))
                .collect(),
        );

        // Serialize full JSON compactly
        let json = serde_json::to_string(&new_tag).unwrap();

        // Build a Rust byte string literal: b"{json}\0"
        // Escape for Rust string literal context
        let escaped: String = json.chars().flat_map(|c| c.escape_default()).collect();
        let literal_text = format!("b\"{}\\0\"", escaped);

        // Create an AST literal from the exact literal text
        ast::make::expr_literal(&literal_text)
    }
    fn with_updated_begin(&self, new_begin: bool) -> ast::Literal {
        // Clone and update begin
        let mut new_tag = self.hayroll_tag().tag.clone();
        new_tag["begin"] = serde_json::Value::Bool(new_begin);

        // Serialize full JSON compactly
        let json = serde_json::to_string(&new_tag).unwrap();

        // Build a Rust byte string literal: b"{json}\0"
        // Escape for Rust string literal context
        let escaped: String = json.chars().flat_map(|c| c.escape_default()).collect();
        let literal_text = format!("b\"{}\\0\"", escaped);

        // Create an AST literal from the exact literal text
        ast::make::expr_literal(&literal_text)
    }
}

// HayrollSeed is a tagged region in the source code
// This can be either a single expression or a span of statements
#[derive(Clone)]
pub enum HayrollSeed {
    Expr(HayrollTag),
    Stmts(HayrollTag, HayrollTag),
    Decls(HayrollTag),
}

impl HasHayrollTag for HayrollSeed {
    fn hayroll_tag(&self) -> &HayrollTag {
        self.first_tag()
    }
}

impl HayrollSeed {
    pub fn first_tag(&self) -> &HayrollTag {
        match self {
            HayrollSeed::Expr(tag) => tag,
            HayrollSeed::Stmts(tag_begin, _) => tag_begin,
            HayrollSeed::Decls(tag) => tag,
        }
    }

    // Returns immutable code region on the original AST
    // Useful for locating where to be replaced
    pub fn get_raw_code_region(&self, with_deref: bool) -> CodeRegion {
        match self {
            HayrollSeed::Expr(tag) => {
                let if_expr = parent_until_kind::<ast::IfExpr>(&tag.literal).unwrap();
                if with_deref && self.is_lvalue() {
                    let star_expr =
                        parent_until_kind_and_cond::<ast::PrefixExpr>(&if_expr, |prefix_expr| {
                            prefix_expr.op_kind().unwrap() == ast::UnaryOp::Deref
                        })
                        .unwrap();
                    CodeRegion::Expr(star_expr.into())
                } else {
                    CodeRegion::Expr(if_expr.into())
                }
            }
            HayrollSeed::Stmts(tag_begin, tag_end) => {
                let stmt_begin = parent_until_kind::<ast::Stmt>(&tag_begin.literal).unwrap();
                let stmt_end = parent_until_kind::<ast::Stmt>(&tag_end.literal).unwrap();
                let stmt_list = parent_until_kind::<ast::StmtList>(&stmt_begin).unwrap();
                let start_idx = stmt_list
                    .statements()
                    .position(|s| s == stmt_begin)
                    .unwrap();
                let end_idx = stmt_list.statements().position(|s| s == stmt_end).expect(&format!(
                    "Could not find end stmt in stmt list for Hayroll tag: {}",
                    tag_end.tag
                ));
                CodeRegion::Stmts {
                    parent: stmt_list,
                    range: start_idx..=end_idx,
                }
            }
            HayrollSeed::Decls(tag) => {
                // Collect all the Items in the SourceFile where the seed is
                // whose #[c2rust::src_loc = "l:c"] tags are within the cuLocBegin and cuLocEnd range in the tag
                let source_file = get_source_file(&tag.literal);
                let cu_loc_begin = LnCol::from_cu_ln_col(&self.cu_ln_col_begin());
                let cu_loc_end = LnCol::from_cu_ln_col(&self.cu_ln_col_end());
                let range = cu_loc_begin..=cu_loc_end;
                let items = find_items_in_range(&source_file, range);
                CodeRegion::Decls(items)
            }
        }
    }

    // Returns immutable code region on the original AST, not including the hayroll tag itself
    pub fn get_raw_code_region_inside_tag(&self) -> CodeRegion {
        let region = self.get_raw_code_region(false);
        match region {
            CodeRegion::Expr(expr) => {
                let if_expr = ast::IfExpr::cast(expr.syntax().clone()).unwrap();
                CodeRegion::Expr(if_expr.then_branch().unwrap().into())
            }
            CodeRegion::Stmts { parent, range } => {
                let new_start = range.start() + 1;
                let new_end = range.end() - 1;
                CodeRegion::Stmts {
                    parent,
                    range: new_start..=new_end,
                }
            }
            CodeRegion::Decls(_) => region,
        }
    }

    pub fn get_raw_decls_tag_item(&self) -> ast::Item {
        match self {
            HayrollSeed::Expr(_) => panic!("get_raw_decls_tag_item() is not applicable to Expr"),
            HayrollSeed::Stmts(_, _) => {
                panic!("get_raw_decls_tag_item() is not applicable to Stmts")
            }
            HayrollSeed::Decls(seed) => parent_until_kind::<ast::Item>(&seed.literal).unwrap(),
        }
    }

    pub fn ptr_or_base_type(&self) -> Option<ast::Type> {
        // lvalue: *if{}else{0 as *mut T} -> *mut T
        // rvalue: if{}else{*(0 as *mut T)} -> T
        match self {
            HayrollSeed::Expr(ref seed) => {
                let if_expr = parent_until_kind::<ast::IfExpr>(&seed.literal).unwrap();
                let else_branch = if_expr.else_branch().unwrap();
                let else_block = if let ast::ElseBranch::Block(block) = else_branch {
                    block
                } else {
                    panic!("Expected a block");
                };
                // Find the first ast::PtrType
                let ptr_type = else_block
                    .syntax()
                    .descendants()
                    .find_map(|element| ast::PtrType::cast(element))
                    .expect(&format!(
                        "Expected to find a PtrType in else branch for Hayroll tag: {}",
                        seed.tag
                    ));
                if self.is_lvalue() {
                    Some(syntax::ast::Type::PtrType(ptr_type))
                } else {
                    Some(ptr_type.ty().unwrap())
                }
            }
            HayrollSeed::Stmts(_, _) => None,
            HayrollSeed::Decls(_) => None,
        }
    }

    pub fn base_type(&self) -> Option<ast::Type> {
        match self.ptr_or_base_type() {
            Some(ast::Type::PtrType(ptr_type)) if self.is_lvalue() => ptr_type.ty(),
            Some(ty) => Some(ty),
            None => None,
        }
    }

    pub fn is_structurally_compatible_with(&self, other: &Self) -> bool {
        match (self, other) {
            (HayrollSeed::Expr(_), HayrollSeed::Expr(_)) => true,
            (HayrollSeed::Stmts(_, _), HayrollSeed::Stmts(_, _)) => true,
            (HayrollSeed::Decls(_), HayrollSeed::Decls(_)) => true,
            _ => false,
        }
    }

    pub fn is_type_compatible_with(&self, other: &Self) -> bool {
        self.is_structurally_compatible_with(other)
            && match (self, other) {
                (HayrollSeed::Expr(_), HayrollSeed::Expr(_)) => {
                    self.base_type().unwrap().to_string() == other.base_type().unwrap().to_string()
                }
                _ => true,
            }
    }
}

// A CodeRegion can be either a single expression, a span of statements,
// or a scattered set of items corresponding to C top-level declarations
#[derive(Clone)]
pub enum CodeRegion {
    // A single Expr
    Expr(ast::Expr),
    // A consecutive span of Stmts
    Stmts {
        parent: ast::StmtList,
        range: RangeInclusive<usize>,
    },
    // A list of possibly scattered Fn/Static/Struct/Union/Const/TypeAlias/...
    Decls(Vec<ast::Item>),
}

impl CodeRegion {
    pub fn clone_subtree(&self) -> CodeRegion {
        match self {
            CodeRegion::Expr(expr) => CodeRegion::Expr(expr.clone_subtree()),
            CodeRegion::Stmts { parent, range } => {
                let parent = parent.clone_subtree();
                CodeRegion::Stmts {
                    parent,
                    range: range.clone(),
                }
            }
            CodeRegion::Decls(decls) => {
                CodeRegion::Decls(decls.iter().map(|d| d.clone_subtree()).collect())
            }
        }
    }

    pub fn is_empty(&self) -> bool {
        match self {
            CodeRegion::Expr(_) => false,
            CodeRegion::Stmts { .. } => false,
            CodeRegion::Decls(decls) => decls.is_empty(),
        }
    }

    // LUB: Least Upper Bound, a single syntax node that contains all the elements in the region
    pub fn lub(&self) -> SyntaxNode {
        match self {
            CodeRegion::Expr(expr) => expr.syntax().clone(),
            CodeRegion::Stmts { parent, .. } => parent.syntax().clone(),
            CodeRegion::Decls(decls) => get_source_file(&decls[0]).syntax().clone(),
        }
    }

    // Takes a TreeMutator or a SourceChangeBuilder and returns a mutable CodeRegion
    pub fn make_mut_with_mutator(
        &self,
        mutator: &ide_db::source_change::TreeMutator,
    ) -> CodeRegion {
        match self {
            CodeRegion::Expr(expr) => CodeRegion::Expr(mutator.make_mut(expr)),
            CodeRegion::Stmts { parent, range } => {
                let parent_mut = mutator.make_mut(parent);
                CodeRegion::Stmts {
                    parent: parent_mut,
                    range: range.clone(),
                }
            }
            CodeRegion::Decls(decls) => {
                let mut new_decls = Vec::new();
                for decl in decls {
                    new_decls.push(mutator.make_mut(decl));
                }
                CodeRegion::Decls(new_decls)
            }
        }
    }

    #[allow(dead_code)]
    pub fn make_mut_with_builder(
        &self,
        builder: &mut ide_db::source_change::SourceChangeBuilder,
    ) -> CodeRegion {
        match self {
            CodeRegion::Expr(expr) => CodeRegion::Expr(builder.make_mut(expr.clone())),
            CodeRegion::Stmts { parent, range } => {
                let parent_mut = builder.make_mut(parent.clone());
                CodeRegion::Stmts {
                    parent: parent_mut,
                    range: range.clone(),
                }
            }
            CodeRegion::Decls(decls) => {
                let mut new_decls = Vec::new();
                for decl in decls {
                    new_decls.push(builder.make_mut(decl.clone()));
                }
                CodeRegion::Decls(new_decls)
            }
        }
    }

    pub fn make_mut_with_builder_set(&self, builder: &mut SourceChangeBuilderSet) -> CodeRegion {
        match self {
            CodeRegion::Expr(expr) => CodeRegion::Expr(builder.make_mut(expr.clone())),
            CodeRegion::Stmts { parent, range } => {
                let parent_mut = builder.make_mut(parent.clone());
                CodeRegion::Stmts {
                    parent: parent_mut,
                    range: range.clone(),
                }
            }
            CodeRegion::Decls(decls) => {
                let mut new_decls = Vec::new();
                for decl in decls {
                    new_decls.push(builder.make_mut(decl.clone()));
                }
                CodeRegion::Decls(new_decls)
            }
        }
    }

    #[allow(dead_code)]
    pub fn clone_for_update(&self) -> CodeRegion {
        match self {
            CodeRegion::Expr(expr) => CodeRegion::Expr(expr.clone_for_update()),
            CodeRegion::Stmts { parent, range } => {
                let parent = parent.clone_for_update();
                CodeRegion::Stmts {
                    parent,
                    range: range.clone(),
                }
            }
            CodeRegion::Decls(decls) => {
                CodeRegion::Decls(decls.iter().map(|d| d.clone_for_update()).collect())
            }
        }
    }

    // Peels tag from expr or stmts, does nothing for decls
    // The CodeRegion must align with that generated from HayrollSeed::get_raw_code_region
    // Returns immutable CodeRegion that is no longer part of the original syntax tree
    pub fn peel_tag(&self) -> CodeRegion {
        let mutator = ide_db::source_change::TreeMutator::new(&self.lub());
        let mut_region = match self {
            CodeRegion::Expr(expr) => {
                if let Some(if_expr) = ast::IfExpr::cast(expr.syntax().clone()) {
                    let then_branch = if_expr.then_branch().unwrap();
                    let then_branch_mut = mutator.make_mut(&then_branch);
                    CodeRegion::Expr(then_branch_mut.into())
                } else {
                    let star_expr = ast::PrefixExpr::cast(expr.syntax().clone()).unwrap();
                    let star_expr_mut = mutator.make_mut(&star_expr);
                    let mut if_or_paren_expr = star_expr.expr().unwrap();
                    while let Some(paren_expr) =
                        ast::ParenExpr::cast(if_or_paren_expr.syntax().clone())
                    {
                        if_or_paren_expr = paren_expr.expr().unwrap();
                    }
                    let if_expr = ast::IfExpr::cast(if_or_paren_expr.syntax().clone()).unwrap();
                    let then_branch = if_expr.then_branch().unwrap();
                    let if_expr_mut = mutator.make_mut(&if_expr);
                    let then_branch_mut = mutator.make_mut(&then_branch);
                    ted::replace(if_expr_mut.syntax(), then_branch_mut.syntax());
                    CodeRegion::Expr(star_expr_mut.into())
                }
            }
            CodeRegion::Stmts { parent, range } => {
                let parent_mut = mutator.make_mut(parent);
                let start = *range.start();
                let end = *range.end();
                let new_start = if start == end { start } else { start + 1 };
                let new_end = if end == 0 { 0 } else { end - 1 };
                CodeRegion::Stmts {
                    parent: parent_mut,
                    range: new_start..=new_end,
                }
            }
            CodeRegion::Decls(_) => self.make_mut_with_mutator(&mutator),
        };
        mut_region.clone_subtree()
    }

    // Give every decls item that is scoped in a `extern "C"` its own unique scope
    // Expr and stmts stay the same
    // Returns immutable CodeRegion that is no longer part of the original syntax tree
    pub fn individualize_decls(&self) -> CodeRegion {
        let mutator = ide_db::source_change::TreeMutator::new(&self.lub());
        let mut_region = match self {
            CodeRegion::Expr(_) => self.make_mut_with_mutator(&mutator),
            CodeRegion::Stmts { .. } => self.make_mut_with_mutator(&mutator),
            CodeRegion::Decls(items) => {
                let items_processed: Vec<ast::Item> = items
                    .into_iter()
                    .map(|item| {
                        if let Some(_) =
                            parent_until_kind_and_cond::<ast::ExternBlock>(item, |ext| {
                                ext.abi().map_or(false, |abi| {
                                    abi.abi_string()
                                        .map_or(false, |s| s.value().unwrap() == "C")
                                })
                            })
                        {
                            // Wrap the item in a new extern "C" block of its own
                            let new_extern_c_str =
                                format!("extern \"C\" {{\n    {}\n}}", item.to_string());
                            let new_extern_c = expr_from_text(&new_extern_c_str);
                            let new_extern_c = mutator.make_mut(&new_extern_c);
                            ast::Item::cast(new_extern_c.syntax().clone()).unwrap()
                        } else {
                            mutator.make_mut(item)
                        }
                    })
                    .collect::<Vec<_>>();
                CodeRegion::Decls(items_processed)
            }
        };
        mut_region.clone_subtree()
    }

    // Peel off any #[c2rust::src_loc = "..."] attributes in the code region (only decls could have these)
    // Returns immutable CodeRegion that is no longer part of the original syntax tree
    pub fn peel_c2rust_src_locs(&self) -> CodeRegion {
        let region = match self {
            CodeRegion::Expr(_) => self.clone(),
            CodeRegion::Stmts { .. } => self.clone(),
            CodeRegion::Decls(items) => {
                let items_processed: Vec<ast::Item> = items
                    .into_iter()
                    .map(|item| peel_c2rust_src_locs_from_item(&item))
                    .collect::<Vec<_>>();
                CodeRegion::Decls(items_processed)
            }
        };
        region.clone_subtree()
    }

    // Returns a range of syntax elements that represent the code region.
    // NOTE: Only valid for Expr and Stmts variants; Decls is not well-defined for syntax element ranges.
    pub fn syntax_element_range(&self) -> RangeInclusive<SyntaxElement> {
        match self {
            CodeRegion::Expr(expr) => {
                expr.syntax().syntax_element().clone()..=expr.syntax().syntax_element().clone()
            }
            CodeRegion::Stmts { parent, range } => {
                let start = parent.statements().nth(*range.start()).unwrap();
                let end = parent.statements().nth(*range.end()).unwrap();
                start.syntax().syntax_element().clone()..=end.syntax().syntax_element().clone()
            }
            CodeRegion::Decls(_) => {
                panic!("syntax_element_range is not well-defined for Decls variant - use syntax_element_vec instead");
            }
        }
    }

    // Returns a vector of syntax elements that represent the code region.
    pub fn syntax_element_vec(&self) -> Vec<SyntaxElement> {
        match self {
            CodeRegion::Expr(expr) => vec![expr.syntax().syntax_element().clone()],
            CodeRegion::Stmts { parent, range } => {
                parent
                    .statements()
                    .enumerate()
                    .filter(|(i, _)| range.contains(i))
                    .map(|(_, stmt)| stmt.syntax().syntax_element().clone())
                    // Attach a new line for each statement to preserve formatting
                    .flat_map(|se| vec![se, get_empty_line_element_mut()])
                    .collect::<Vec<SyntaxElement>>()
            }
            CodeRegion::Decls(decls) => decls
                .iter()
                .map(|d| d.syntax().syntax_element().clone())
                .collect(),
        }
    }

    pub fn position_after(&self) -> Position {
        match self {
            CodeRegion::Expr(expr) => Position::after(expr.syntax()),
            CodeRegion::Stmts { parent, range } => {
                let end = parent.statements().nth(*range.end()).unwrap();
                Position::after(end.syntax())
            }
            CodeRegion::Decls(decls) => {
                let last_decl = decls.last().unwrap();
                Position::after(last_decl.syntax())
            }
        }
    }
}

impl std::fmt::Display for CodeRegion {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            CodeRegion::Expr(expr) => write!(f, "{}", expr),
            CodeRegion::Stmts { parent, range } => {
                write!(
                    f,
                    "{}",
                    parent
                        .statements()
                        .enumerate()
                        .filter(|(i, _)| range.contains(i))
                        .map(|(_, stmt)| stmt.to_string())
                        .collect::<Vec<String>>()
                        .join("\n")
                )
            }
            CodeRegion::Decls(decls) => {
                write!(
                    f,
                    "{}",
                    decls
                        .iter()
                        .map(|d| d.to_string())
                        .collect::<Vec<String>>()
                        .join("\n")
                )
            }
        }
    }
}

// HayrollMacroInv is a macro invocation in AST representation
// It contains the CodeRegion of the expansion range and the CodeRegions of the arguments
#[derive(Clone)]
pub struct HayrollMacroInv {
    pub seed: HayrollSeed,
    // Args is a list of (argument name, list of HayrollSeed) pairs
    pub args: Vec<(String, Vec<HayrollSeed>)>,
}

impl HasHayrollTag for HayrollMacroInv {
    fn hayroll_tag(&self) -> &HayrollTag {
        self.seed.hayroll_tag()
    }
}

impl HayrollMacroInv {
    pub fn signature(&self) -> String {
        if self.seed.is_decl() || self.seed.is_decls() {
            return String::new();
        }

        let sanitize = |raw: &str| -> String {
            let trimmed = raw.rsplit("::").next().unwrap_or(raw);
            trimmed
                .chars()
                .filter(|c| c.is_ascii_alphanumeric() || *c == '_')
                .collect()
        };

        let mut parts: Vec<String> = Vec::new();

        if self.seed.is_expr() {
            parts.push(sanitize(&self.seed.base_type().unwrap().to_string()));
        }

        for (_, arg_regions) in &self.args {
            if arg_regions.iter().any(|seed| seed.is_stmt()) {
                parts.push("stmt".to_string());
                continue;
            }

            let first_seed = arg_regions.first().unwrap();
            parts.push(sanitize(&first_seed.base_type().unwrap().to_string()));
        }

        parts.join("_")
    }

    pub fn name_with_signature(&self) -> String {
        let mut parts: Vec<String> = Vec::new();
        parts.push(self.name());
        let signature = self.signature();
        if !signature.is_empty() {
            parts.push(signature);
        }
        parts.join("_")
    }

    // Replace the args tagged code regions into $argName, for generating macro definition
    // Returns immutable CodeRegion
    pub fn replace_arg_regions_into(
        &self,
        peel_tag: bool,
        return_inv_region_with_deref: bool,
        args_require_lvalue: &Vec<bool>,
        substitute: fn(&HayrollSeed) -> Vec<SyntaxElement>,
    ) -> CodeRegion {
        let mut delayed_tasks: Vec<Box<dyn FnOnce()>> = Vec::new();
        let region = self.seed.get_raw_code_region(return_inv_region_with_deref);
        let mutator = ide_db::source_change::TreeMutator::new(&region.lub());
        let region_mut = region.make_mut_with_mutator(&mutator);
        for ((_, arg_regions), requires_lvalue) in self.args.iter().zip(args_require_lvalue.iter())
        {
            for arg_region in arg_regions {
                let arg_code_region = arg_region.get_raw_code_region(!requires_lvalue);
                let arg_code_region_mut = arg_code_region.make_mut_with_mutator(&mutator);
                let arg_code_region_range = arg_code_region_mut.syntax_element_range();
                let new_tokens = substitute(arg_region);
                delayed_tasks.push(Box::new(move || {
                    ted::replace_all(arg_code_region_range, new_tokens);
                }));
            }
        }
        for task in delayed_tasks {
            task();
        }
        let region_immut = region_mut.clone_subtree();
        if peel_tag {
            region_immut.peel_tag()
        } else {
            region_immut
        }
    }

    pub fn macro_rules(&self) -> ast::MacroRules {
        let macro_name = self.name_with_signature();
        // arg format: ($x:expr) or ($x:stmt)
        let macro_args = self
            .args
            .iter()
            .map(|(arg_name, arg_regions)| {
                let arg_type = match arg_regions[0] {
                    HayrollSeed::Expr(_) => "expr",
                    HayrollSeed::Stmts(_, _) => "stmt",
                    HayrollSeed::Decls(_) => panic!("Decls not supported as macro arg"),
                };
                format!("${}:{}", arg_name, arg_type)
            })
            .collect::<Vec<String>>()
            .join(", ");
        let macro_body = match self.seed {
            HayrollSeed::Expr(_) | HayrollSeed::Stmts(_, _) => self.replace_arg_regions_into(
                true,
                true,
                &vec![false; self.args.len()],
                |arg_region| {
                    let name = arg_region.name();
                    let name_token = ast::make::tokens::ident(&name);
                    let name_node = name_token.parent().unwrap().clone_for_update();
                    let dollar_token_mut = get_dollar_token_mut();
                    vec![
                        syntax::NodeOrToken::Token(dollar_token_mut),
                        syntax::NodeOrToken::Node(name_node),
                    ]
                },
            ),
            HayrollSeed::Decls(_) => self
                .seed
                .get_raw_code_region(true)
                .individualize_decls()
                .peel_c2rust_src_locs(),
        };
        let macro_def = format!(
            "macro_rules! {}\n{{\n    ({}) => {{\n    {}\n    }}\n}}",
            macro_name, macro_args, macro_body
        );
        let macro_rules_node = ast_from_text::<ast::MacroRules>(&macro_def);
        macro_rules_node.clone_for_update()
    }

    pub fn macro_call(&self) -> ast::MacroCall {
        let macro_name = self.name_with_signature();
        let args_spelling: String = self
            .args
            .iter()
            .map(|(_, arg_regions)| {
                let arg_code_region = arg_regions[0].get_raw_code_region(true).peel_tag();
                arg_code_region.to_string()
            })
            .collect::<Vec<String>>()
            .join(", ");
        let macro_call = if self.is_expr() {
            format!("{}!({})", macro_name, args_spelling)
        } else {
            format!("{}!({});", macro_name, args_spelling)
        };
        ast_from_text::<ast::MacroCall>(&macro_call).clone_for_update()
    }

    pub fn fn_(&self, args_require_lvalue: &Vec<bool>) -> ast::Fn {
        let return_type: String = match self.seed.ptr_or_base_type() {
            Some(t) => " -> ".to_string() + &t.to_string(),
            None => "".to_string(),
        };
        let arg_with_types = self
            .args
            .iter()
            .zip(args_require_lvalue.iter())
            .filter_map(|((arg_name, arg_regions), requires_lvalue)| {
                if arg_regions.is_empty() {
                    warn!(macro_name = %self.name_with_signature(), arg = %arg_name, "argument is never used in macro");
                    None
                } else {
                    let t = if *requires_lvalue {
                        arg_regions[0].ptr_or_base_type().unwrap()
                    } else {
                        arg_regions[0].base_type().unwrap()
                    };
                    Some(format!("{}: {}", arg_name, t))
                }
            })
            .collect::<Vec<String>>()
            .join(", ");
        let fn_body =
            self.replace_arg_regions_into(true, false, args_require_lvalue, |arg_region| {
                let name = arg_region.name();
                let name_token = ast::make::tokens::ident(&name);
                let name_node = name_token.parent().unwrap().clone_for_update();
                vec![syntax::NodeOrToken::Node(name_node)]
            });
        let fn_ = format!(
            "unsafe fn {}({}){} {{\n    {}\n}}",
            self.name_with_signature(),
            arg_with_types,
            return_type,
            fn_body
        );
        ast_from_text::<ast::Fn>(&fn_).clone_for_update()
    }

    pub fn call_expr(&self, args_require_lvalue: &Vec<bool>) -> ast::Expr {
        let fn_name = self.name_with_signature();
        let args_spelling: String = self
            .args
            .iter()
            .zip(args_require_lvalue.iter())
            .filter_map(|((_, arg_regions), requires_lvalue)| {
                if arg_regions.is_empty() {
                    warn!(macro_name = %self.name_with_signature(), "an argument is never used in macro");
                    None
                } else {
                    let arg_code_region = arg_regions[0].get_raw_code_region(!requires_lvalue).peel_tag();
                    Some(arg_code_region.to_string())
                }
            })
            .collect::<Vec<String>>()
            .join(", ");
        let call_expr = format!("{}({})", fn_name, args_spelling);
        let call_expr = if self.is_lvalue() {
            format!("*{}", call_expr)
        } else {
            call_expr
        };
        expr_from_text(&call_expr).clone_for_update()
    }

    pub fn call_expr_or_stmt_mut(&self, args_require_lvalue: &Vec<bool>) -> SyntaxNode {
        let call_expr = self.call_expr(args_require_lvalue);
        if self.seed.is_expr() {
            call_expr.syntax().clone()
        } else {
            let stmt = ast::make::expr_stmt(call_expr).clone_for_update();
            stmt.syntax().clone()
        }
    }

    pub fn args_internally_structurally_compatible(&self) -> bool {
        self.args.iter().all(|(_, seeds)| {
            seeds.is_empty()
                || seeds
                    .iter()
                    .all(|seed| seed.is_structurally_compatible_with(&seeds[0]))
        })
    }

    pub fn args_internally_type_compatible(&self) -> bool {
        self.args.iter().all(|(_, seeds)| {
            seeds.is_empty()
                || seeds
                    .iter()
                    .all(|seed| seed.is_type_compatible_with(&seeds[0]))
        })
    }

    pub fn is_structurally_compatible_with(&self, other: &Self) -> bool {
        self.seed.is_structurally_compatible_with(&other.seed)
            && self.args_internally_structurally_compatible()
            && other.args_internally_structurally_compatible()
            && self.args.len() == other.args.len()
            && self
                .args
                .iter()
                .zip(other.args.iter())
                .all(|((_, seeds1), (_, seeds2))| {
                    if seeds1.is_empty() && seeds2.is_empty() {
                        true
                    } else if seeds1.is_empty() != seeds2.is_empty() {
                        false
                    } else {
                        seeds1[0].is_structurally_compatible_with(&seeds2[0])
                    }
                })
    }

    pub fn is_type_compatible_with(&self, other: &Self) -> bool {
        self.seed.is_type_compatible_with(&other.seed)
            && self.args_internally_type_compatible()
            && other.args_internally_type_compatible()
            && self.args.len() == other.args.len()
            && self
                .args
                .iter()
                .zip(other.args.iter())
                .all(|((_, seeds1), (_, seeds2))| {
                    if seeds1.is_empty() && seeds2.is_empty() {
                        true
                    } else if seeds1.is_empty() != seeds2.is_empty() {
                        false
                    } else {
                        seeds1[0].is_type_compatible_with(&seeds2[0])
                    }
                })
    }

    pub fn args_require_lvalue(&self) -> Vec<bool> {
        self.args
            .iter()
            .map(|(_, seeds)| seeds.is_empty() || seeds.iter().all(|seed| seed.is_lvalue()))
            .collect()
    }
}

pub struct HayrollMacroCluster {
    pub invocations: Vec<HayrollMacroInv>,
}

impl HasHayrollTag for HayrollMacroCluster {
    fn hayroll_tag(&self) -> &HayrollTag {
        self.invocations[0].hayroll_tag()
    }
}

impl HayrollMacroCluster {
    pub fn can_be_fn(&self) -> bool {
        self.invs_internally_type_compatible() && self.invocations.iter().all(|inv| inv.can_be_fn())
    }

    pub fn invs_internally_structurally_compatible(&self) -> bool {
        assert!(!self.invocations.is_empty());
        let first = &self.invocations[0];
        self.invocations
            .iter()
            .all(|inv| inv.is_structurally_compatible_with(&first))
    }

    pub fn invs_internally_type_compatible(&self) -> bool {
        assert!(!self.invocations.is_empty());
        let first = &self.invocations[0];
        self.invocations
            .iter()
            .all(|inv| inv.is_type_compatible_with(&first))
    }

    pub fn macro_rules(&self) -> ast::MacroRules {
        assert!(self.invs_internally_structurally_compatible());
        self.invocations[0].macro_rules()
    }

    pub fn fn_(&self) -> ast::Fn {
        assert!(self.invs_internally_type_compatible());
        self.invocations[0].fn_(&self.args_require_lvalue())
    }

    pub fn args_require_lvalue(&self) -> Vec<bool> {
        self.invocations
            .iter()
            .map(|inv| inv.args_require_lvalue())
            .fold(
                vec![true; self.invocations[0].args.len()],
                |acc, arg_reqs| {
                    acc.iter()
                        .zip(arg_reqs.iter())
                        .map(|(a, b)| *a && *b)
                        .collect()
                },
            )
    }
}

// HayrollMacroDB is a database of HayrollMacroInv collected from the source code
pub struct HayrollMacroDB {
    pub map: std::collections::HashMap<(String, String), HayrollMacroCluster>, // (refLoc, signature) -> cluster
}

impl HayrollMacroDB {
    pub fn new() -> Self {
        HayrollMacroDB {
            map: std::collections::HashMap::new(),
        }
    }

    pub fn from_hayroll_macro_invs(hayroll_macros: &Vec<HayrollMacroInv>) -> Self {
        let mut db = HayrollMacroDB::new();
        for mac in hayroll_macros.iter() {
            let loc_decl = mac.loc_ref_begin();
            let signature = mac.signature();
            let key = (loc_decl, signature);
            if !db.map.contains_key(&key) {
                db.map.insert(
                    key.clone(),
                    HayrollMacroCluster {
                        invocations: Vec::new(),
                    },
                );
            }
            db.map.get_mut(&key).unwrap().invocations.push(mac.clone());
        }
        db
    }
}

#[derive(Clone)]
pub struct HayrollConditionalMacro {
    pub seed: HayrollSeed,
}

impl HasHayrollTag for HayrollConditionalMacro {
    fn hayroll_tag(&self) -> &HayrollTag {
        self.seed.hayroll_tag()
    }
}

impl HayrollConditionalMacro {
    // Attach #[cfg(premise)] to every element in the code region
    // Returns a list of ted-style delayed tasks to be executed later
    pub fn attach_cfg_teds(&self, builder: &mut SourceChangeBuilderSet) -> Vec<Box<dyn FnOnce()>> {
        let mut teds: Vec<Box<dyn FnOnce()>> = Vec::new();
        // Force attaching cfg to a placeholder decl/decls is a hack
        // We do this because Maki frequently thinks a decl/decls is a placeholder
        // when other preprocessor directives "invade" the range
        if self.is_placeholder() && !(self.is_decl() || self.is_decls()) {
            return Vec::new();
        }
        let premise = self.premise();
        match self.seed {
            HayrollSeed::Expr(_) => {
                // Work on the underlying IfExpr (don't include outer deref for lvalues)
                let region = self.seed.get_raw_code_region(false);
                if let CodeRegion::Expr(expr) = &region {
                    // Expect the current expr to be an if-expr produced by instrumentation
                    let if_expr = ast::IfExpr::cast(expr.syntax().clone())
                        .expect("Expected IfExpr for conditional seed expr");
                    let then_branch = if_expr.then_branch().expect("IfExpr must have then branch");
                    let else_branch = if_expr.else_branch().expect("IfExpr must have else branch");
                    let then_text = then_branch.to_string();
                    let else_text = match else_branch {
                        ast::ElseBranch::Block(b) => b.to_string(),
                        ast::ElseBranch::IfExpr(e) => e.to_string(),
                    };
                    let new_expr_text = format!(
                        "{{ if cfg!({}) {} else {} }}",
                        // No extra braces around then and else branches because they are already blocks
                        // But provide extra braces around the whole if-expression to help replacement
                        premise,
                        then_text,
                        else_text
                    );
                    let new_expr_mut = expr_from_text(&new_expr_text).clone_for_update();
                    let _if_expr_mut = builder.make_mut(if_expr);
                    let then_branch_mut = builder.make_mut(then_branch);
                    teds.push(Box::new(move || {
                        ted::replace(then_branch_mut.syntax(), new_expr_mut.syntax());
                    }));
                } else {
                    panic!("Expected Expr region for conditional seed expr");
                }
            }
            HayrollSeed::Stmts(_, _) => {
                let region = self.seed.get_raw_code_region_inside_tag();
                // Print this code region for debugging
                if let CodeRegion::Stmts { parent, range } = &region {
                    for (i, stmt) in parent.statements().enumerate() {
                        if !range.contains(&i) {
                            continue;
                        }
                        let attr_text = format!("#[cfg({})]", premise);
                        let attr = ast_from_text::<ast::Attr>(&attr_text).clone_for_update();
                        match &stmt {
                            ast::Stmt::LetStmt(let_stmt) => {
                                let let_stmt_mut = builder.make_mut(let_stmt.clone());
                                teds.push(Box::new(move || {
                                    let_stmt_mut.add_attr(attr);
                                }));
                            }
                            ast::Stmt::Item(item_stmt) => {
                                let item_mut = builder.make_mut(item_stmt.clone());
                                teds.push(Box::new(move || {
                                    item_mut.add_attr(attr);
                                }));
                            }
                            ast::Stmt::ExprStmt(expr_stmt) => {
                                if !stmt_is_hayroll_tag(&stmt) {
                                    if let Some(expr) = expr_stmt.expr() {
                                        // First, try to attach the attr directly on any HasAttrs expression type.
                                        let attached = schedule_add_attr_on_expr_if_possible(
                                            builder,
                                            expr.clone(),
                                            attr.clone(),
                                            &mut teds,
                                        );
                                        if !attached {
                                            // Fallback: wrap with a ParenExpr and attach the attr to it.
                                            let paren_expr = expr_from_text("(1)");
                                            let paren_expr =
                                                ast::ParenExpr::cast(paren_expr.syntax().clone())
                                                    .unwrap();
                                            let paren_expr_mut = paren_expr.clone_for_update();
                                            paren_expr_mut.add_attr(attr.clone());
                                            let original_expr_mut = builder.make_mut(expr);
                                            teds.push(Box::new(move || {
                                                // If the parent of original_expr_mut is a paren expr with existing attrs,
                                                // (in case where other transformations have already wrapped it)
                                                // then attatch to that. Otherwise, create a new paren expr.
                                                if let Some(original_expr_mut_parent) =
                                                    original_expr_mut.syntax().parent()
                                                {
                                                    if let Some(existing_paren) =
                                                        ast::ParenExpr::cast(
                                                            original_expr_mut_parent.clone(),
                                                        )
                                                    {
                                                        if !existing_paren.attrs().next().is_none()
                                                        {
                                                            // Attach to existing paren expr
                                                            existing_paren.add_attr(attr.clone());
                                                            return;
                                                        }
                                                    }
                                                }
                                                // The sequence here is very tricky
                                                // parent{orig;}, paren() (detached)
                                                ted::replace(
                                                    original_expr_mut.syntax(),
                                                    paren_expr_mut.syntax(),
                                                );
                                                // parent{paren();}, orig (detached)
                                                ted::replace(
                                                    paren_expr_mut.expr().unwrap().syntax(),
                                                    original_expr_mut.syntax(),
                                                );
                                                // parent{paren(orig);}
                                            }));
                                        }
                                    } else {
                                        error!("ExprStmt has no expr: {}", expr_stmt);
                                    }
                                }
                            }
                        }
                    }
                } else {
                    panic!("Expected Stmts region for conditional seed stmts");
                }
            }
            HayrollSeed::Decls(_) => {
                let region = self.seed.get_raw_code_region(false);
                if region.is_empty() {
                    return Vec::new();
                }
                let region_mut = region.make_mut_with_builder_set(builder);
                if let CodeRegion::Decls(items_mut) = region_mut {
                    items_mut.iter().for_each(|item_mut| {
                        let attr_text = format!("#[cfg({})]", premise);
                        let attr = ast_from_text::<ast::Attr>(&attr_text).clone_for_update();
                        let item_mut_clone = item_mut.clone();
                        teds.push(Box::new(move || {
                            item_mut_clone.add_attr(attr);
                        }));
                    });
                } else {
                    panic!("Expected Decls region for conditional seed decls");
                }
            }
        }
        teds
    }
}

pub fn extract_hayroll_macro_invs_from_seeds(
    hayroll_seeds: &Vec<HayrollSeed>,
) -> Vec<HayrollMacroInv> {
    // A region whose isArg is false is a macro; match args to their macro
    let hayroll_macro_invs: Vec<HayrollMacroInv> = hayroll_seeds
        .iter()
        .filter(|seed| seed.is_invocation())
        .fold(Vec::new(), |mut acc, region| {
            if region.is_arg() == false {
                // Pre-populate all expected argument names with empty vectors
                let preset_args: Vec<(String, Vec<HayrollSeed>)> = region
                    .arg_names()
                    .into_iter()
                    .map(|name| (name, Vec::new()))
                    .collect();
                acc.push(HayrollMacroInv {
                    seed: region.clone(),
                    args: preset_args,
                });
            } else {
                let mut found = false;
                for mac in acc.iter_mut().rev() {
                    if mac.loc_begin() == region.loc_ref_begin() {
                        assert!(mac.args.iter().any(|(name, _)| name == &region.name()));
                        let arg = mac
                            .args
                            .iter_mut()
                            .find(|(name, _)| name == &region.name())
                            .unwrap();
                        arg.1.push(region.clone());
                        found = true;
                        break;
                    }
                }
                if !found {
                    panic!("No matching macro found for arg: {:?}", region.loc_begin());
                }
            }
            acc
        });
    hayroll_macro_invs
}

// Returns a list of HayrollSeed and unmatched HayrollTag
pub fn extract_hayroll_seeds_from_syntax_roots_impl(
    syntax_roots: &HashMap<FileId, SourceFile>,
) -> (Vec<HayrollSeed>, Vec<HayrollTag>) {
    let hayroll_tags: Vec<HayrollTag> = syntax_roots
        .iter()
        .flat_map(|(file_id, root)| {
            root.syntax()
                .descendants_with_tokens()
                // Attach a file_id to each node
                .map(move |element| (element, file_id))
        })
        .filter_map(|(element, file_id)| {
            if let Some(token) = element.clone().into_token() {
                if let Some(byte_str) = ast::ByteString::cast(token) {
                    // Try to parse into serde_json::Value, if it fails, it's not a JSON string
                    let content = match byte_str.value() {
                        Ok(cow) => String::from_utf8_lossy(&cow).to_string(),
                        Err(_) => return None,
                    };
                    // Delete the last \0 byte
                    let content = content.trim_end_matches(char::from(0));
                    let tag_res = serde_json::from_str::<serde_json::Value>(&content);
                    trace!(byte_string = %content, tag = ?tag_res, "Byte String parsed");
                    if let Ok(tag) = tag_res {
                        if tag["hayroll"] == true {
                            let tag = HayrollTag {
                                literal: ast::Literal::cast(element.parent()?)?,
                                tag,
                                file_id: file_id.clone(),
                            };
                            return Some(tag);
                        }
                    }
                }
            }
            None
        })
        .collect();

    // Pair up stmt hayroll_literals that are in the same scope and share the locInv in info
    let hayroll_seeds: Vec<HayrollSeed> = hayroll_tags.iter().fold(Vec::new(), |mut acc, tag| {
        if tag.is_expr() {
            assert!(tag.begin());
            acc.push(HayrollSeed::Expr(tag.clone()));
        } else if (tag.is_stmt() || tag.is_stmts()) && tag.begin() == true {
            acc.push(HayrollSeed::Stmts(tag.clone(), tag.clone())); // For now seedBegin == seedEnd
        } else if tag.is_decl() || tag.is_decls() {
            assert!(tag.begin());
            acc.push(HayrollSeed::Decls(tag.clone()));
        } else if !tag.begin() {
            // Search through the acc to find the begin stmt with the same locInv
            let mut found = false;
            for seed in acc.iter_mut().rev() {
                match seed {
                    HayrollSeed::Stmts(tag_begin, ref mut tag_end) => {
                        if tag_begin.loc_begin() == tag.loc_begin()
                            && tag_begin.seed_type() == tag.seed_type()
                            && tag.begin() == false
                        {
                            *tag_end = tag.clone();
                            found = true;
                            break;
                        }
                    }
                    _ => {}
                }
            }
            if !found {
                panic!(
                    "No matching begin stmt found for end stmt {}",
                    tag.loc_begin()
                );
            }
        } else {
            panic!("Unknown tag");
        }
        acc
    });

    // Collect unmatched begin stmt tags
    let unmatched_begin_tags: Vec<HayrollTag> = hayroll_seeds
        .iter()
        .filter_map(|seed| {
            if let HayrollSeed::Stmts(tag_begin, tag_end) = seed {
                if tag_begin.begin() && !tag_end.begin() {
                    None
                } else if tag_begin.begin() && tag_end.begin() {
                    Some(tag_begin.clone())
                } else {
                    None
                }
            } else {
                None
            }
        })
        .collect();

    (hayroll_seeds, unmatched_begin_tags)
}

pub fn extract_hayroll_seeds_from_syntax_roots(
    syntax_roots: &HashMap<FileId, SourceFile>,
) -> Vec<HayrollSeed> {
    let (seeds, unmatched) = extract_hayroll_seeds_from_syntax_roots_impl(syntax_roots);
    if !unmatched.is_empty() {
        for tag in unmatched {
            error!("Unmatched begin tag: {}", tag.loc_begin());
        }
        panic!("Unmatched begin tags found");
    }
    seeds
}

pub fn extract_unmatched_hayroll_tags_from_syntax_roots(
    syntax_roots: &HashMap<FileId, SourceFile>,
) -> Vec<HayrollTag> {
    let (_seeds, unmatched) = extract_hayroll_seeds_from_syntax_roots_impl(syntax_roots);
    unmatched
}
