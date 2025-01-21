use std::{collections::HashMap, env, ops::RangeInclusive, path::Path, time::Instant};
use ide::{Edition, RootDatabase, SourceChange};
use ide_db::{base_db::{SourceDatabase, SourceDatabaseFileInputExt, SourceRootDatabase}, source_change::{SourceChangeBuilder, TreeMutator}, symbol_index::SymbolsDatabase, EditionedFileId};
use project_model::CargoConfig;
use load_cargo;
use anyhow::Result;
use serde_json::{self, value};
use syntax::{ast::{self, SourceFile}, syntax_editor::Element, ted::{self, Position}, AstNode, AstToken, SyntaxNode, SyntaxToken, SyntaxElement, T};
use vfs::FileId;
use std::fs;
use hir;

fn ast_from_text<N: AstNode>(text: &str) -> N {
    let parse = SourceFile::parse(text, Edition::CURRENT);
    let node = match parse.tree().syntax().descendants().find_map(N::cast) {
        Some(it) => it,
        None => {
            let node = std::any::type_name::<N>();
            panic!("Failed to make ast node `{node}` from text {text}")
        }
    };
    let node = node.clone_subtree();
    assert_eq!(node.syntax().text_range().start(), 0.into());
    node
}

fn get_dollar_token_mut() -> SyntaxToken {
    let macro_rules = ast_from_text::<ast::MacroRules>("macro_rules! M {($x:expr) => {};}");
    // find a dollar token in the parsed macro_rules
    let dollar_token = macro_rules.syntax().descendants_with_tokens().find(|element| element.kind() == T![$]).unwrap().into_token().unwrap();
    // println!("DollarToken: {:?}", dollar_token);
    let dollar_parent_node = dollar_token.parent().unwrap().clone_for_update();
    // find the dollar token in the dollar_parent_node that is now mutable
    let dollar_token_mut = dollar_parent_node.descendants_with_tokens().find(|element| element.kind() == T![$]).unwrap().into_token().unwrap();
    dollar_token_mut
}

#[derive(Clone)]
struct HayrollSeed {
    literal: ast::Literal,
    tag: serde_json::Value,
    file_id: FileId,
}

#[derive(Clone)]
enum HayrollRegion {
    Expr(HayrollSeed),
    Stmt(HayrollSeed, HayrollSeed),
}

impl HayrollRegion {
    fn file_id(&self) -> FileId {
        match self {
            HayrollRegion::Expr(seed) => seed.file_id,
            HayrollRegion::Stmt(seed_begin, _) => seed_begin.file_id,
        }
    }

    fn is_arg(&self) -> bool {
        match self {
            HayrollRegion::Expr(seed) => seed.tag["isArg"] == true,
            HayrollRegion::Stmt(seed_begin, _) => seed_begin.tag["isArg"] == true,
        }
    }

    fn name(&self) -> String {
        match self {
            HayrollRegion::Expr(seed) => seed.tag["name"].as_str().unwrap().to_string(),
            HayrollRegion::Stmt(seed_begin, _) => seed_begin.tag["name"].as_str().unwrap().to_string(),
        }
    }

    fn arg_names(&self) -> Vec<String> {
        if !self.is_arg() {
            return Vec::new();
        }
        let seed = match self {
            HayrollRegion::Expr(seed) => seed,
            HayrollRegion::Stmt(seed_begin, _) => seed_begin,
        };
        let arg_names = seed.tag["argNames"].as_array().unwrap();
        arg_names.iter().map(|arg_name| arg_name.as_str().unwrap().to_string()).collect()
    }
    
    fn loc_inv(&self) -> String {
        match self {
            HayrollRegion::Expr(seed) => seed.tag["locInv"].as_str().unwrap().to_string(),
            HayrollRegion::Stmt(seed_begin, _) => seed_begin.tag["locInv"].as_str().unwrap().to_string(),
        }
    }

    fn loc_decl(&self) -> String {
        match self {
            HayrollRegion::Expr(seed) => seed.tag["locDecl"].as_str().unwrap().to_string(),
            HayrollRegion::Stmt(seed_begin, _) => seed_begin.tag["locDecl"].as_str().unwrap().to_string(),
        }
    }

    fn is_lvalue(&self) -> bool {
        match self {
            HayrollRegion::Expr(seed) => seed.tag["isLvalue"] == true,
            HayrollRegion::Stmt(_, _) => false,
        }
    }

    fn is_expr(&self) -> bool {
        match self {
            HayrollRegion::Expr(_) => true,
            HayrollRegion::Stmt(_, _) => false,
        }
    }

    // Find the code region, which is the node(s) that contains the whole region
    // For lvalues, the code region includes the star PrefixExpr
    // Returns immutable node(s)
    fn get_code_region(&self) -> CodeRegion {
        match self {
            HayrollRegion::Expr(seed) => {
                let if_expr = parent_until_kind::<ast::IfExpr>(&seed.literal).unwrap();
                if self.is_lvalue() {
                    let star_expr = parent_until_kind_and_cond::<ast::PrefixExpr>(
                        &if_expr, 
                        |prefix_expr| prefix_expr.op_kind().unwrap() == ast::UnaryOp::Deref
                    ).unwrap();
                    CodeRegion::Expr(star_expr.into())
                } else {
                    CodeRegion::Expr(if_expr.into())
                }
            }
            HayrollRegion::Stmt(seed_begin, seed_end) => {
                let stmt_begin = parent_until_kind::<ast::Stmt>(&seed_begin.literal).unwrap();
                let stmt_end = parent_until_kind::<ast::Stmt>(&seed_end.literal).unwrap();
                let stmt_list = parent_until_kind::<ast::StmtList>(&stmt_begin).unwrap();
                let elements = stmt_begin..=stmt_end;
                CodeRegion::Span{parent: stmt_list, elements}
            }
        }
    }

    // Peel the tag from the region, and return the body of the region
    // For lvalues, the body includes the star PrefixExpr
    // Returns imutable node(s) for rvalus, mutable node(s) for lvalues
    fn peel_tag(&self) -> CodeRegion {
        match self {
            HayrollRegion::Expr(seed) => {
                let if_expr = parent_until_kind::<ast::IfExpr>(&seed.literal).unwrap();
                let then_branch = if_expr.then_branch().unwrap();
                // If this is lvalue, then there should be an address-of operator (RefExpr) in then_branch
                // and also a star PrefixExpr in the parent of the if expression
                // In that case, we should build a new Expr with the RefExpr's expr as the body
                if self.is_lvalue() {
                    let star_expr = parent_until_kind::<ast::PrefixExpr>(&if_expr).unwrap();
                    let mutator = TreeMutator::new(&star_expr.syntax().clone());
                    let star_expr = mutator.make_mut(&star_expr);
                    // println!("StarExpr1: {:}", star_expr);
                    let if_expr = mutator.make_mut(&if_expr);
                    let then_branch = mutator.make_mut(&then_branch);
                    ted::replace(if_expr.syntax(), then_branch.syntax());
                    // println!("StarExpr2: {:}", star_expr);
                    CodeRegion::Expr(star_expr.into())
                } else {
                    // println!("ThenBranch: {:}", then_branch);
                    CodeRegion::Expr(then_branch.into())
                }
            }
            HayrollRegion::Stmt(seed_begin, seed_end) => {
                let stmt_begin = parent_until_kind::<ast::Stmt>(&seed_begin.literal).unwrap();
                let stmt_begin_next: ast::Stmt = ast::Stmt::cast(stmt_begin.syntax().next_sibling().unwrap()).unwrap();
                let stmt_end = parent_until_kind::<ast::Stmt>(&seed_end.literal).unwrap();
                let stmt_end_prev = ast::Stmt::cast(stmt_end.syntax().prev_sibling().unwrap()).unwrap();
                let stmt_list = parent_until_kind::<ast::StmtList>(&stmt_begin).unwrap();
                CodeRegion::Span{parent: stmt_list, elements: stmt_begin_next..=stmt_end_prev}
            }
        }
    }
}

enum CodeRegion {
    Expr(ast::Expr),
    // Span{parent: ast::StmtList, elements: RangeInclusive<SyntaxElement>},
    Span { parent: ast::StmtList, elements: RangeInclusive<ast::Stmt> },
}

impl CodeRegion {
    // LUB: Least Upper Bound
    fn lub(&self) -> SyntaxNode {
        match self {
            CodeRegion::Expr(expr) => expr.syntax().clone(),
            CodeRegion::Span { parent, elements: _ } => {
                parent.syntax().clone()
            }
        }
    }

    // Takes a TreeMutator or a SourceChangeBuilder and returns a mutable CodeRegion
    fn make_mut_with_mutator(&self, mutator: &TreeMutator) -> CodeRegion {
        match self {
            CodeRegion::Expr(expr) => CodeRegion::Expr(mutator.make_mut(expr)),
            CodeRegion::Span { parent, elements } => {
                let start_mut = mutator.make_mut(elements.start());
                let end_mut = mutator.make_mut(elements.end());
                CodeRegion::Span { parent: mutator.make_mut(parent), elements: start_mut..=end_mut }
            }
        }
    }

    fn make_mut_with_builder(&self, builder: &mut SourceChangeBuilder) -> CodeRegion {
        match self {
            CodeRegion::Expr(expr) => CodeRegion::Expr(builder.make_mut(expr.clone())),
            CodeRegion::Span { parent, elements } => {
                let start_mut = builder.make_mut(elements.start().clone());
                let end_mut = builder.make_mut(elements.end().clone());
                CodeRegion::Span { parent: builder.make_mut(parent.clone()), elements: start_mut..=end_mut }
            }
        }
    }

    fn syntax_element_range(&self) -> RangeInclusive<SyntaxElement> {
        match self {
            CodeRegion::Expr(expr) => expr.syntax().syntax_element().clone()..=expr.syntax().syntax_element().clone(),
            CodeRegion::Span { parent: _, elements } => {
                let start = elements.start();
                let end = elements.end();
                start.syntax().syntax_element().clone()..=end.syntax().syntax_element().clone()
            }
        }
    }
}

impl std::fmt::Display for CodeRegion {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            CodeRegion::Expr(expr) => write!(f, "{}", expr),
            CodeRegion::Span { parent, elements } => {
                let start = elements.start();
                let end = elements.end();
                write!(f, "{}", {
                    let mut  seen_begin = false;
                    let mut seen_end = false;
                    parent.statements().filter_map(|stmt| {
                        if &stmt == start {
                            seen_begin = true;
                            Some(stmt.to_string())
                        } else if &stmt == end {
                            seen_end = true;
                            Some(stmt.to_string())
                        } else if seen_begin && !seen_end {
                            Some(stmt.to_string())
                        } else {
                            None
                        }
                    }).map(|stmt| stmt.to_string()).collect::<Vec<String>>().join("\n")
                })
            }
        }
    }
}

#[derive(Clone)]
struct HayrollMacroInv {
    region: HayrollRegion,
    args: Vec<HayrollRegion>,
}

impl HayrollMacroInv {
    // Replace the args tagged code regions into $argName, for generating macro definition
    fn replace_arg_regions_into_names(&self) -> CodeRegion {
        let original_region = self.region.get_code_region();
        let mutator = TreeMutator::new(&original_region.lub());
        let mutated_region = original_region.make_mut_with_mutator(&mutator);
        let mut delayed_tasks: Vec<Box<dyn FnOnce()>> = Vec::new();
        for arg in self.args.iter() {
            let name = arg.name();
            let name_token = ast::make::tokens::ident(&name);
            let name_node = name_token.parent().unwrap().clone_for_update();
            let dollar_token_mut = get_dollar_token_mut();
            let new_tokens = vec![syntax::NodeOrToken::Token(dollar_token_mut), syntax::NodeOrToken::Node(name_node)];
            // ted::replace_all(arg.get_code_region().make_mut_with_mutator(&mutator).syntax_element_range(), new_tokens);
            let arg_region = arg.get_code_region().make_mut_with_mutator(&mutator);
            let arg_region = arg_region.syntax_element_range();
            delayed_tasks.push(Box::new(move || {
                ted::replace_all(arg_region, new_tokens);
            }));
        }
        for task in delayed_tasks {
            task();
        }
        mutated_region
    }

    // Returns immutable node
    fn macro_call(&self) -> ast::MacroCall {
        let macro_name = self.region.name();
        let args_spelling: String = self.args.iter()
            .map(|arg| {
                arg.peel_tag().to_string()
            })
            .collect::<Vec<String>>()
            .join(", ");        
        let macro_call = if self.region.is_expr() {
            format!("{}!({})", macro_name, args_spelling)
        } else {
            format!("{}!({});", macro_name, args_spelling)
        };
        ast_from_text::<ast::MacroCall>(&macro_call)
    }
}

struct HayrollMacroDB{
    map: HashMap<String, Vec<HayrollMacroInv>>,
}

impl HayrollMacroDB {
    fn new() -> Self {
        HayrollMacroDB {
            map: HashMap::new(),
        }
    }

    fn from_hayroll_macro_invs(hayroll_macros: &Vec<HayrollMacroInv>) -> Self {
        // Collect macros by locDecl
        let mut db = HayrollMacroDB::new();
        for mac in hayroll_macros.iter() {
            let loc_decl = mac.region.loc_decl();
            if !db.map.contains_key(&loc_decl) {
                db.map.insert(loc_decl.clone(), Vec::new());
            }
            db.map.get_mut(&loc_decl).unwrap().push(mac.clone());
        }
        db
    }
}

// Takes a node and returns the parent node until the parent node satisfies the condition
fn parent_until(node: SyntaxNode, condition: fn(SyntaxNode) -> bool) -> Option<SyntaxNode> {
    let mut node = node;
    while !condition(node.clone()) {
        node = node.parent()?;
    }
    Some(node)
}

// Takes a node and returns the parent node until the parent node is of the given kind i.e. IfExpr
// Also takes a predicate to check if the parent node satisfies the extra condition
fn parent_until_kind_and_cond<T>(node: &impl ast::AstNode, condition: fn(&T) -> bool) -> Option<T>
where
    T: ast::AstNode,
{
    let mut node = node.syntax().clone();
    while !(T::can_cast(node.kind()) && condition(&T::cast(node.clone()).unwrap())) {
        node = node.parent()?;
    }
    Some(T::cast(node)?)
}

// Takes a node and returns the parent node until the parent node is of the given kind i.e. IfExpr
fn parent_until_kind<T>(node: &impl ast::AstNode) -> Option<T>
where
    T: ast::AstNode,
{
    parent_until_kind_and_cond(node, |_| true)
}



fn main() -> Result<()> {
    // start a timer
    let start = Instant::now();

    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        eprintln!("Usage: {} <workspace-path>", args[0]);
        std::process::exit(1);
    }

    let workspace_path = Path::new(&args[1]);
    let cargo_config = CargoConfig::default();
    let load_cargo_config = load_cargo::LoadCargoConfig {
        load_out_dirs_from_check: false,
        with_proc_macro_server: load_cargo::ProcMacroServerChoice::None,
        prefill_caches: false,
    };

    // Print consumed time, tag "load_cargo"
    let duration = start.elapsed();
    println!("Time elapsed in load_cargo is: {:?}", duration);

    let (mut db, vfs, _proc_macro) = load_cargo::load_workspace_at(workspace_path, &cargo_config, &load_cargo_config, &|_| {})?;
    
    // Print consumed time, tag "db"
    let duration = start.elapsed();
    println!("Time elapsed in db is: {:?}", duration);
    
    let sema = &hir::Semantics::new(&db);
    // Print consumed time, tag "sema"
    let duration = start.elapsed();
    println!("Time elapsed in sema is: {:?}", duration);

    let mut syntax_roots: HashMap<FileId, (SourceFile, Option<SourceChangeBuilder>)> = db.local_roots().iter()
        .flat_map(|&srid| db.source_root(srid).iter().collect::<Vec<_>>())
        .filter_map(|file_id| {
            // Include only Rust source files, not toml files
            if !vfs.file_path(file_id).to_string().ends_with(".rs") {
                return None;
            }
            let builder = SourceChangeBuilder::new(file_id.clone());
            // Using the sema-based parser allows querying semantic info i.e. type of an expression
            // But it's much slower than the db-based parser
            // let root = sema.parse_guess_edition(file_id);
            let root = db.parse(EditionedFileId::current_edition(file_id)).tree();
            Some((file_id, (root, Some(builder))))
        })
        .collect();

    // Print consumed time, tag "syntax_roots"
    let duration = start.elapsed();
    println!("Time elapsed in syntax_roots is: {:?}", duration);

    let hayroll_seeds: Vec<HayrollSeed> = syntax_roots
        .iter()
        .flat_map(|(file_id, (root, _))| {
            root.syntax().descendants_with_tokens()
            // Attatch a file_id to each node
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
                    let tag: serde_json::Result<value::Value> = serde_json::from_str(&content);
                    println!("Byte String: {}, Tag: {:?}", content, tag);
                    if let Ok(tag) = tag {
                        if tag["hayroll"] == true {
                            return Some(HayrollSeed {
                                literal: ast::Literal::cast(element.parent()?)?,
                                tag,
                                file_id: file_id.clone(),
                            });
                        }
                    }
                }
            }
            None
        })
        .collect();

    // Pair up stmt hayroll_literals that are in the same scope and share the locInv in info
    let hayroll_regions : Vec<HayrollRegion> = hayroll_seeds.iter()
        .fold(Vec::new(), |mut acc, seed| {
            if seed.tag["astKind"] == "Expr" {
                acc.push(HayrollRegion::Expr(seed.clone()));
            } else if seed.tag["astKind"] == "Stmt" && seed.tag["begin"] == true {
                acc.push(HayrollRegion::Stmt(seed.clone(), seed.clone())); // For now seedBegin == seedEnd
            } else if seed.tag["begin"] == false {
                // Search through the acc to find the begin stmt with the same locInv
                let mut found = false;
                for region in acc.iter_mut().rev() {
                    match region {
                        HayrollRegion::Stmt(seed_begin, seed_end) => {
                            if seed_begin.tag["locInv"] == seed.tag["locInv"] {
                                *seed_end = seed.clone();
                                found = true;
                                break;
                            }
                        }
                        _ => {}
                    }
                }
                // Assert found
                if !found {
                    panic!("No matching begin stmt found for end stmt");
                }
            } else {
                panic!("Unknown tag");
            }
            acc
        }
    );

    // Print out all region's get_body for debugging
    for region in hayroll_regions.iter() {
        println!("Peeled: {}", region.peel_tag());
    }

    // A region whose isArg is false is a macro
    // Other regions are arguments, we should match them with the macro
    let hayroll_macro_invs: Vec<HayrollMacroInv> = hayroll_regions.iter()
        .fold(Vec::new(), |mut acc, region| {
            if region.is_arg() == false {
                acc.push(HayrollMacroInv {
                    region: region.clone(),
                    args: Vec::new(),
                });
            } else {
                let mut found = false;
                for mac in acc.iter_mut().rev() {
                    if mac.region.loc_inv() == region.loc_inv() {
                        mac.args.push(region.clone());
                        found = true;
                        break;
                    }
                }
                // Assert found
                if !found {
                    panic!("{}", format!("No matching macro found for arg: {:?}", region.loc_inv()));
                }
            }
            acc
        }
    );

    // Print all token trees from the macro invs
    for mac in hayroll_macro_invs.iter() {
        let macro_body = mac.replace_arg_regions_into_names();
        println!("Macro Body: {}", macro_body);
    }

    let hayroll_macro_db = HayrollMacroDB::from_hayroll_macro_invs(&hayroll_macro_invs);

    // Print consumed time, tag "hayroll_literals"
    let duration = start.elapsed();
    println!("Time elapsed in hayroll_literals is: {:?}", duration);
        
    // Find out all Expr typed nodes, and replace them with their nested value
    // if (*"str") { A } else { B } -> A
    // *if (*"str") { &A } else { B } -> *&A

    let mut delayed_tasks: Vec<Box<dyn FnOnce()>> = Vec::new();

    // For each macro db entry, generate a new macro definition and add that to the top of the file
    // Ffor each macro invocation, replace the invocation with a macro call
    for (loc_decl, hayroll_macros) in hayroll_macro_db.map.iter() {
        // There is at least one macro invocation for each locDecl
        let hayroll_macro_inv = &hayroll_macros[0];
        let macro_name = hayroll_macro_inv.region.name();
        // arg format: ($x:expr) or ($x:stmt)
        let macro_args = hayroll_macro_inv.args.iter()
            .map(|arg| {
                let arg_name = arg.name();
                let arg_type = match arg {
                    HayrollRegion::Expr(_) => "expr",
                    HayrollRegion::Stmt(_, _) => "stmt",
                };
                format!("${}:{}", arg_name, arg_type)
            })
            .collect::<Vec<String>>()
            .join(", ");
        let macro_body = hayroll_macros[0].replace_arg_regions_into_names();
        let macro_def = format!("macro_rules! {}\n{{\n    ({}) => {{\n    {}\n    }}\n}}", macro_name, macro_args, macro_body);
        // Convert the macro definition into a syntax node
        let macro_rules_node = ast_from_text::<ast::MacroRules>(&macro_def);
        let macro_rules_node = macro_rules_node.clone_for_update();
        // Add the macro definition to the top of the file
        let (syntax_root, builder) = syntax_roots.get_mut(&hayroll_macro_inv.region.file_id()).unwrap();
        let builder = builder.as_mut().unwrap();
        let syntax_root = builder.make_mut(syntax_root.clone());
        // prepend_tasks.push((syntax_root, macro_rules_node.syntax().clone()));
        delayed_tasks.push(Box::new(move || {
            let pos = syntax_root.syntax().children()
                .find(|element| !ast::Attr::can_cast(element.kind())).unwrap().clone();
            let pos = Position::before(&pos);
            let empty_line = ast::make::tokens::whitespace("\n");
            ted::insert_all(pos, vec![macro_rules_node.syntax().syntax_element(), syntax::NodeOrToken::Token(empty_line)])
        }));

        // Replace the macro invocations with the macro calls
        for hayroll_macro_inv in hayroll_macros.iter() {
            let code_region = hayroll_macro_inv.region.get_code_region();
            let (_, builder) = syntax_roots.get_mut(&hayroll_macro_inv.region.file_id()).unwrap();
            let builder = builder.as_mut().unwrap();
            let region_mut = code_region.make_mut_with_builder(builder);
            let macro_call_node = hayroll_macro_inv.macro_call().clone_for_update().syntax().syntax_element();
            delayed_tasks.push(Box::new(move || {
                ted::replace_all(region_mut.syntax_element_range(), vec![macro_call_node]);
            }));
        }
    }

    // Print consumed time, tag "replace"
    let duration = start.elapsed();
    println!("Time elapsed in replace is: {:?}", duration);

    for task in delayed_tasks {
        task();
    }


    // Print consumed time, tag "analysis"
    let duration = start.elapsed();
    println!("Time elapsed in analysis is: {:?}", duration);

    let mut source_change = SourceChange::default();
    for (_, (_, builder)) in syntax_roots.iter_mut() {
        let builder = builder.take().unwrap();
        source_change = source_change.merge(builder.finish());
    }

    apply_source_change(&mut db, &source_change);

    for file_id in syntax_roots.keys() {
        let file_path = vfs.file_path(*file_id);
        let code = db.file_text(*file_id).to_string();
        // Attatch a new line at the end of the file, if it doesn't exist
        let code = if code.ends_with("\n") {
            code
        } else {
            code + "\n"
        };
        let path = file_path.as_path().unwrap();
        fs::write(path, code)?;
    }

    // Print consumed time, tag "write"
    let duration = start.elapsed();
    println!("Time elapsed in write is: {:?}", duration);

    Ok(())
}

fn apply_source_change(db: &mut RootDatabase, source_change: &ide::SourceChange){
    // Fs edits (NOT TESTED!!!)
    // for file_system_edit in source_change.file_system_edits.iter() {
    //     let (dst, contents) = match file_system_edit {
    //         ide::FileSystemEdit::CreateFile { dst, initial_contents } => (dst, initial_contents.clone()),
    //         ide::FileSystemEdit::MoveFile { src, dst } => {
    //             (dst, db.file_text(*src).to_string())
    //         }
    //         ide::FileSystemEdit::MoveDir { src, src_id, dst } => {
    //             // temporary placeholder for MoveDir since we are not using MoveDir in ide assists yet.
    //             (dst, format!("{src_id:?}\n{src:?}"))
    //         }
    //     };
    //     let sr = db.file_source_root(dst.anchor);
    //     let sr = db.source_root(sr);
    //     let mut base = sr.path_for_file(&dst.anchor).unwrap().clone();
    //     base.pop();
    //     let created_file_path = base.join(&dst.path).unwrap();
    //     fs::write(created_file_path.as_path().unwrap(), contents).unwrap();
    // }

    // Source file edits
    for (file_id, (text_edit, snippet)) in source_change.source_file_edits.iter() {
        let mut code = db.file_text(file_id.clone()).to_string();
        text_edit.apply(&mut code);
        // Snippet (NOT TESTED!!!)
        if let Some(snippet) = snippet {
            snippet.apply(&mut code);
        }
        db.set_file_text(*file_id, &code);
    }
}
