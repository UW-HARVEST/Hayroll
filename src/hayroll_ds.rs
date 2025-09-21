use std::ops::RangeInclusive;

use serde_json;
use syntax::{
    ast::{self},
    ted, AstNode, SyntaxElement, SyntaxNode,
};
use syntax::syntax_editor::Element; // for syntax().syntax_element()
use tracing::warn;
use vfs::FileId;

use crate::util::{
    ast_from_text, expr_from_text, find_items_in_range, get_dollar_token_mut, get_source_file,
    parent_until_kind, parent_until_kind_and_cond, LnCol,
};

// HayrollTag literal in the source code
#[derive(Clone)]
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
}

impl<T: HasHayrollTag> HayrollMeta for T {
    fn is_arg(&self) -> bool {
        self.hayroll_tag().tag["isArg"] == true
    }
    fn name(&self) -> String {
        self.hayroll_tag().tag["name"].as_str().unwrap().to_string()
    }
    fn arg_names(&self) -> Vec<String> {
        self.hayroll_tag().tag["argNames"].as_array().unwrap().iter().map(|a| a.as_str().unwrap().to_string()).collect()
    }
    fn loc_begin(&self) -> String {
        self.hayroll_tag().tag["locBegin"].as_str().unwrap().to_string()
    }
    fn loc_end(&self) -> String {
        self.hayroll_tag().tag["locEnd"].as_str().unwrap().to_string()
    }
    fn cu_ln_col_begin(&self) -> String {
        self.hayroll_tag().tag["cuLnColBegin"].as_str().unwrap().to_string()
    }
    fn cu_ln_col_end(&self) -> String {
        self.hayroll_tag().tag["cuLnColEnd"].as_str().unwrap().to_string()
    }
    fn loc_ref_begin(&self) -> String {
        self.hayroll_tag().tag["locRefBegin"].as_str().unwrap().to_string()
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
        self.hayroll_tag().tag["astKind"].as_str().unwrap().to_string()
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
                    let star_expr = parent_until_kind_and_cond::<ast::PrefixExpr>(&if_expr, |prefix_expr| {
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
                let elements = stmt_begin..=stmt_end;
                let region = CodeRegion::Stmts { parent: stmt_list, elements };
                region
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

    pub fn get_raw_decls_tag_item(&self) -> ast::Item {
        match self {
            HayrollSeed::Expr(_) => panic!("get_raw_decls_tag_item() is not applicable to Expr"),
            HayrollSeed::Stmts(_, _) => panic!("get_raw_decls_tag_item() is not applicable to Stmts"),
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
                    .unwrap();
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
    Stmts { parent: ast::StmtList, elements: RangeInclusive<ast::Stmt> },
    // A list of possibly scattered Fn/Static/Struct/Union/Const/TypeAlias/...
    Decls(Vec<ast::Item>),
}

impl CodeRegion {
    #[allow(dead_code)]
    pub fn clone_subtree(&self) -> CodeRegion {
        match self {
            CodeRegion::Expr(expr) => CodeRegion::Expr(expr.clone_subtree()),
            CodeRegion::Stmts { parent, elements } => {
                let begin = elements.start();
                let end = elements.end();
                let pos_begin = parent.statements().position(|stmt| stmt == *begin).unwrap();
                let pos_end = parent.statements().position(|stmt| stmt == *end).unwrap();
                let parent = parent.clone_subtree();
                let begin = parent.statements().nth(pos_begin).unwrap();
                let end = parent.statements().nth(pos_end).unwrap();
                CodeRegion::Stmts { parent, elements: begin..=end }
            }
            CodeRegion::Decls(decls) => CodeRegion::Decls(decls.iter().map(|d| d.clone_subtree()).collect()),
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
    pub fn make_mut_with_mutator(&self, mutator: &ide_db::source_change::TreeMutator) -> CodeRegion {
        match self {
            CodeRegion::Expr(expr) => CodeRegion::Expr(mutator.make_mut(expr)),
            CodeRegion::Stmts { parent, elements } => {
                let start_mut = mutator.make_mut(elements.start());
                let end_mut = mutator.make_mut(elements.end());
                CodeRegion::Stmts { parent: mutator.make_mut(parent), elements: start_mut..=end_mut }
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

    pub fn make_mut_with_builder(&self, builder: &mut ide_db::source_change::SourceChangeBuilder) -> CodeRegion {
        match self {
            CodeRegion::Expr(expr) => CodeRegion::Expr(builder.make_mut(expr.clone())),
            CodeRegion::Stmts { parent, elements } => {
                let start_mut = builder.make_mut(elements.start().clone());
                let end_mut = builder.make_mut(elements.end().clone());
                CodeRegion::Stmts { parent: builder.make_mut(parent.clone()), elements: start_mut..=end_mut }
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
                    while let Some(paren_expr) = ast::ParenExpr::cast(if_or_paren_expr.syntax().clone()) {
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
            CodeRegion::Stmts { parent, elements } => {
                let stmt_begin = elements.start();
                let stmt_end = elements.end();
                let stmt_begin_next: ast::Stmt = ast::Stmt::cast(stmt_begin.syntax().next_sibling().unwrap()).unwrap();
                let stmt_end_prev: ast::Stmt = ast::Stmt::cast(stmt_end.syntax().prev_sibling().unwrap()).unwrap();
                let parent_mut = mutator.make_mut(parent);
                CodeRegion::Stmts { parent: parent_mut, elements: stmt_begin_next..=stmt_end_prev }
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
                        if let Some(_) = parent_until_kind_and_cond::<ast::ExternBlock>(item, |ext| {
                            ext.abi().map_or(false, |abi| abi.abi_string().map_or(false, |s| s.value().unwrap() == "C"))
                        }) {
                            // Wrap the item in a new extern "C" block of its own
                            let new_extern_c_str = format!("extern \"C\" {{\n    {}\n}}", item.to_string());
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

    // Returns a range of syntax elements that represent the code region.
    // NOTE: Only valid for Expr and Stmts variants; Decls is not well-defined for syntax element ranges.
    pub fn syntax_element_range(&self) -> RangeInclusive<SyntaxElement> {
        match self {
            CodeRegion::Expr(expr) => expr.syntax().syntax_element().clone()..=expr.syntax().syntax_element().clone(),
            CodeRegion::Stmts { elements, .. } => {
                let start = elements.start();
                let end = elements.end();
                start.syntax().syntax_element().clone()..=end.syntax().syntax_element().clone()
            }
            CodeRegion::Decls(_) => {
                panic!("syntax_element_range is not well-defined for Decls variant - use syntax_element_vec instead");
            }
        }
    }

    // Returns a vector of syntax elements that represent the code region.
    #[allow(dead_code)]
    pub fn syntax_element_vec(&self) -> Vec<SyntaxElement> {
        match self {
            CodeRegion::Expr(expr) => vec![expr.syntax().syntax_element().clone()],
            CodeRegion::Stmts { parent, elements } => {
                let start = elements.start();
                let end = elements.end();
                let mut seen_begin = false;
                let mut seen_end = false;
                parent
                    .statements()
                    .filter_map(|stmt| {
                        let mut ret: Option<ast::Stmt> = None;
                        if &stmt == start {
                            seen_begin = true;
                            ret = Some(stmt.clone())
                        }
                        if &stmt == end {
                            seen_end = true;
                            ret = Some(stmt.clone())
                        }
                        if seen_begin && !seen_end {
                            ret = Some(stmt.clone())
                        }
                        ret
                    })
                    .map(|stmt| stmt.syntax().clone().into())
                    .collect::<Vec<SyntaxElement>>()
            }
            CodeRegion::Decls(decls) => decls.iter().map(|d| d.syntax().syntax_element().clone()).collect(),
        }
    }
}

impl std::fmt::Display for CodeRegion {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            CodeRegion::Expr(expr) => write!(f, "{}", expr),
            CodeRegion::Stmts { parent, elements } => {
                let start = elements.start();
                let end = elements.end();
                write!(f, "{}", {
                    let mut seen_begin = false;
                    let mut seen_end = false;
                    parent
                        .statements()
                        .filter_map(|stmt| {
                            let mut ret: Option<String> = None;
                            if &stmt == start {
                                seen_begin = true;
                                ret = Some(stmt.to_string())
                            }
                            if &stmt == end {
                                seen_end = true;
                                ret = Some(stmt.to_string())
                            }
                            if seen_begin && !seen_end {
                                ret = Some(stmt.to_string())
                            }
                            ret
                        })
                        .collect::<Vec<String>>()
                        .join("\n")
                })
            }
            CodeRegion::Decls(decls) => {
                write!(f, "{}", decls.iter().map(|d| d.to_string()).collect::<Vec<String>>().join("\n"))
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
        for ((_, arg_regions), requires_lvalue) in self.args.iter().zip(args_require_lvalue.iter()) {
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
        let macro_name = self.name();
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
                    vec![syntax::NodeOrToken::Token(dollar_token_mut), syntax::NodeOrToken::Node(name_node)]
                },
            ),
            HayrollSeed::Decls(_) => self.seed.get_raw_code_region(true).individualize_decls(),
        };
        let macro_def = format!(
            "macro_rules! {}\n{{\n    ({}) => {{\n    {}\n    }}\n}}",
            macro_name, macro_args, macro_body
        );
        let macro_rules_node = ast_from_text::<ast::MacroRules>(&macro_def);
        macro_rules_node.clone_for_update()
    }

    pub fn macro_call(&self) -> ast::MacroCall {
        let macro_name = self.name();
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
                    warn!(macro_name = %self.name(), arg = %arg_name, "argument is never used in macro");
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
        let fn_body = self.replace_arg_regions_into(true, false, args_require_lvalue, |arg_region| {
            let name = arg_region.name();
            let name_token = ast::make::tokens::ident(&name);
            let name_node = name_token.parent().unwrap().clone_for_update();
            vec![syntax::NodeOrToken::Node(name_node)]
        });
        let fn_ = format!("unsafe fn {}({}){} {{\n    {}\n}}", self.seed.name(), arg_with_types, return_type, fn_body);
        ast_from_text::<ast::Fn>(&fn_).clone_for_update()
    }

    pub fn call_expr(&self, args_require_lvalue: &Vec<bool>) -> ast::Expr {
        let fn_name = self.name();
        let args_spelling: String = self
            .args
            .iter()
            .zip(args_require_lvalue.iter())
            .filter_map(|((_, arg_regions), requires_lvalue)| {
                if arg_regions.is_empty() {
                    warn!(macro_name = %self.name(), "an argument is never used in macro");
                    None
                } else {
                    let arg_code_region = arg_regions[0].get_raw_code_region(!requires_lvalue).peel_tag();
                    Some(arg_code_region.to_string())
                }
            })
            .collect::<Vec<String>>()
            .join(", ");
        let call_expr = format!("{}({})", fn_name, args_spelling);
        let call_expr = if self.is_lvalue() { format!("*{}", call_expr) } else { call_expr };
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
            seeds.is_empty() || seeds.iter().all(|seed| seed.is_structurally_compatible_with(&seeds[0]))
        })
    }

    pub fn args_internally_type_compatible(&self) -> bool {
        self.args.iter().all(|(_, seeds)| {
            seeds.is_empty() || seeds.iter().all(|seed| seed.is_type_compatible_with(&seeds[0]))
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
        self.invocations.iter().all(|inv| inv.is_structurally_compatible_with(&first))
    }

    pub fn invs_internally_type_compatible(&self) -> bool {
        assert!(!self.invocations.is_empty());
        let first = &self.invocations[0];
        self.invocations.iter().all(|inv| inv.is_type_compatible_with(&first))
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
            .fold(vec![true; self.invocations[0].args.len()], |acc, arg_reqs| {
                acc.iter().zip(arg_reqs.iter()).map(|(a, b)| *a && *b).collect()
            })
    }
}

// HayrollMacroDB is a database of HayrollMacroInv collected from the source code
pub struct HayrollMacroDB {
    pub map: std::collections::HashMap<String, HayrollMacroCluster>, // definition location -> invocations
}

impl HayrollMacroDB {
    pub fn new() -> Self {
        HayrollMacroDB { map: std::collections::HashMap::new() }
    }

    pub fn from_hayroll_macro_invs(hayroll_macros: &Vec<HayrollMacroInv>) -> Self {
        let mut db = HayrollMacroDB::new();
        for mac in hayroll_macros.iter() {
            let loc_decl = mac.loc_ref_begin();
            if !db.map.contains_key(&loc_decl) {
                db.map.insert(loc_decl.clone(), HayrollMacroCluster { invocations: Vec::new() });
            }
            db.map.get_mut(&loc_decl).unwrap().invocations.push(mac.clone());
        }
        db
    }
}
