use std::{collections::HashMap, env, path::Path, time::Instant};
use ide::{RootDatabase, SourceChange};
use ide_db::{base_db::{SourceDatabase, SourceDatabaseFileInputExt, SourceRootDatabase}, source_change::SourceChangeBuilder, symbol_index::SymbolsDatabase, EditionedFileId};
use project_model::CargoConfig;
use load_cargo;
use anyhow::Result;
use serde_json::{self, value};
use syntax::{ast::{self, SourceFile}, ted, AstNode, AstToken, SyntaxNode};
use vfs::FileId;
use std::fs;
use hir;

#[derive(Clone)]
struct HayrollSeed {
    literal: ast::Literal,
    tag: serde_json::Value,
    file_id: FileId,
}

enum HayrollRegion {
    Expr(HayrollSeed),
    Stmt(HayrollSeed, HayrollSeed),
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

    // Print consumed time, tag "hayroll_literals"
    let duration = start.elapsed();
    println!("Time elapsed in hayroll_literals is: {:?}", duration);
        
    // Find out all Expr typed nodes, and replace them with their nested value
    // if (*"str") { A } else { B } -> A
    // *if (*"str") { &A } else { B } -> *&A

    let mut replace_tasks: Vec<(SyntaxNode, Option<SyntaxNode>)> = Vec::new();

    for region in hayroll_regions.iter() {
        match region {
            HayrollRegion::Expr(seed) => {
                let (literal, tag, file_id) = (&seed.literal, &seed.tag, &seed.file_id);
                let (_, builder) = syntax_roots.get_mut(&file_id).unwrap();
                let builder = builder.as_mut().unwrap();
                let mut node: SyntaxNode = literal.syntax().clone();
                while !ast::IfExpr::can_cast(node.kind()) {
                    node = node.parent().unwrap();
                }
                let if_expr = ast::IfExpr::cast(node).unwrap();
                // let if_expr = builder.make_mut(if_expr);
                let if_expr_as_expr = ast::Expr::cast(if_expr.syntax().clone()).unwrap();
                let if_true = if_expr.then_branch().unwrap();
                let if_true_expr = ast::Expr::cast(if_true.syntax().clone()).unwrap();
                
                println!("IfExpr: {:?}", if_expr);
                println!("IfTrue: {:?}", if_true_expr);
                
                // let if_true_expr_new = if_true_expr.clone();
                // let if_true_expr_new = if_true_expr.clone_for_update();
                // Find the node to replace in the LATEST syntax tree
                let if_true_expr_new = builder.make_mut(if_true_expr.clone());

                // Find the node to replace in the LATEST syntax tree
                let if_expr_as_expr_mut = builder.make_mut(if_expr_as_expr.clone());
                // let if_expr_as_expr_mut = if_expr_as_expr.clone();

                // ted::replace(if_expr_as_expr_mut.syntax(), if_true_expr_new.syntax());
                replace_tasks.push((if_expr_as_expr_mut.syntax().clone(), Some(if_true_expr_new.syntax().clone())));

                println!("IfExpr: {:?}", if_expr);
                println!("IfTrue: {:?}", if_true_expr);

                // // Print the type of the if_true_expr
                // let ty = sema.type_of_expr(&if_true_expr);
                // println!("Type: {:?}", ty);
            }
            HayrollRegion::Stmt(seed_begin, seed_end) => {
                let (literal_begin, tag, file_id) = (&seed_begin.literal, &seed_begin.tag, &seed_begin.file_id);
                let (literal_end, _, _) = (&seed_end.literal, &seed_end.tag, &seed_end.file_id);
                let (_, builder) = syntax_roots.get_mut(&file_id).unwrap();
                let builder = builder.as_mut().unwrap();
                // Simply remove the seeds
                let node_begin: SyntaxNode = literal_begin.syntax().parent().unwrap().parent().unwrap().parent().unwrap().parent().unwrap().parent().unwrap();
                let node_end: SyntaxNode = literal_end.syntax().parent().unwrap().parent().unwrap().parent().unwrap().parent().unwrap().parent().unwrap();
                let node_begin = builder.make_syntax_mut(node_begin);
                let node_end = builder.make_syntax_mut(node_end);
                replace_tasks.push((node_begin, None));
                replace_tasks.push((node_end, None));
            }
        }
    }

    // Print consumed time, tag "replace"
    let duration = start.elapsed();
    println!("Time elapsed in replace is: {:?}", duration);

    // insert an empty function to every syntax_root
    for (_, (root, builder)) in syntax_roots.iter_mut() {
        // Modify only rust source files, not toml files
        let builder = builder.as_mut().unwrap();
        let root_mut = builder.make_syntax_mut(root.syntax().clone());
        let func_new_text = r#"
fn hayroll() {
    println!("Hello, Hayroll!");
}"#;
        let func_new = SourceFile::parse(func_new_text, syntax::Edition::DEFAULT).syntax_node();
        ted::append_child(&root_mut, func_new.clone_for_update());
    }

    // Reverse iterate to avoid invalidating the node
    // replace_tasks.reverse();
    // Seems unnecessary
    for (old_node, new_node) in replace_tasks.iter() {
        // Replace or delete
        if let Some(new_node) = new_node {
            ted::replace(old_node, new_node);
        } else {
            ted::remove(old_node);
        }
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
