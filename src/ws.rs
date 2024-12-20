use std::{env, path::Path};
use camino;
use ide::{RootDatabase, SourceChange};
use ide_db::{base_db::{SourceDatabase, SourceDatabaseFileInputExt, SourceRootDatabase}, source_change::SourceChangeBuilder};
use project_model::CargoConfig;
use load_cargo;
use anyhow::Result;
use serde_json::{self, value};
use syntax::{ast, AstNode, AstToken, SyntaxNode};
use vfs::{AbsPath, FileId};
use std::fs;
use hir;

fn main() -> Result<()> {
    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        eprintln!("Usage: {} <workspace-path>", args[0]);
        std::process::exit(1);
    }

    let workspace_path = Path::new(&args[1]);
    let workspace_path_buf = fs::canonicalize(workspace_path)?;
    let workspace_utf8_path = camino::Utf8Path::from_path(workspace_path_buf.as_path()).unwrap();
    let workspace_abs_path = AbsPath::assert(workspace_utf8_path);
    let cargo_config = CargoConfig::default();
    let load_cargo_config = load_cargo::LoadCargoConfig {
        load_out_dirs_from_check: false,
        with_proc_macro_server: load_cargo::ProcMacroServerChoice::None,
        prefill_caches: false,
    };
    let (mut db, vfs, _proc_macro) = load_cargo::load_workspace_at(workspace_path, &cargo_config, &load_cargo_config, &|_| {})?;
    let sema = &hir::Semantics::new(&db);

    let file_ids: Vec<vfs::FileId> = vfs
        .iter()
        .filter_map(|(file_id, path)| {
            // Take only files that exist under the workspace
            if path.as_path().is_some_and(|p| p.starts_with(workspace_abs_path)) {
                Some(file_id)
            } else {
                None
            }
        })
        .collect();

    let syntax_roots: Vec<(SyntaxNode, FileId)> = file_ids.iter().map(|file_id| (sema.parse_guess_edition(*file_id).syntax().clone(), file_id.clone())).collect();

    let hayroll_literals: Vec<(ast::Literal, value::Value, FileId)> = syntax_roots
        .iter()
        .flat_map(|(node, file_id)| {
            node.descendants_with_tokens()
            // Attatch a file_id to each node
            .filter_map(move |element| Some((element, file_id)))
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
                            return Some((ast::Literal::cast(element.parent()?)?, tag, file_id.clone()));
                        }
                    }
                }
            }
            None
        }).collect();
        
    // Find out all Expr typed nodes, and replace them with their nested value
    // if (*"str") { A } else { B } -> A
    // *if (*"str") { &A } else { B } -> *&A

    let peel_expr_tags = |is_arg: bool, mut source_change: SourceChange| -> SourceChange {
        for (literal, tag, file_id) in hayroll_literals.iter() {
            let mut builder = SourceChangeBuilder::new(file_id.clone());
            
            if tag["astKind"] == "Expr" && tag["isArg"] == is_arg {
                let mut if_expr: SyntaxNode = literal.syntax().clone();
                while !ast::IfExpr::can_cast(if_expr.kind()) {
                    if_expr = if_expr.parent().unwrap();
                }
                let if_expr = ast::IfExpr::cast(if_expr).unwrap();
                let if_expr_as_expr = ast::Expr::cast(if_expr.syntax().clone()).unwrap();
                let if_true = if_expr.then_branch().unwrap();
                let if_true_expr = ast::Expr::cast(if_true.syntax().clone()).unwrap();
                
                // Deep copy the if_true_expr
                let if_true_expr_new = if_true_expr.clone_subtree().clone().clone_for_update();
                
                println!("IfExpr: {:?}", if_expr);
                println!("IfTrue: {:?}", if_true_expr);

                builder.replace_ast(if_expr_as_expr, if_true_expr_new);
            }
            source_change = source_change.merge(builder.finish());
        }
        source_change
    };

    // First pass: args
    let mut source_change = SourceChange::default();
    source_change = peel_expr_tags(true, source_change);
    apply_source_change(&mut db, &source_change);

    // // Second pass: not args
    // let mut source_change = SourceChange::default();
    // source_change = peel_expr_tags(false, source_change);
    // apply_source_change(&mut db, &source_change);

    for file_id in file_ids.iter() {
        let file_path = vfs.file_path(*file_id);
        let code = db.file_text(*file_id).to_string();
        let path = file_path.as_path().unwrap();
        fs::write(path, code)?;
    }

    Ok(())
}

fn apply_source_change(db: &mut RootDatabase, source_change: &ide::SourceChange){
    // Fs edits (NOT TESTED!!!)
    for file_system_edit in source_change.file_system_edits.iter() {
        let (dst, contents) = match file_system_edit {
            ide::FileSystemEdit::CreateFile { dst, initial_contents } => (dst, initial_contents.clone()),
            ide::FileSystemEdit::MoveFile { src, dst } => {
                (dst, db.file_text(*src).to_string())
            }
            ide::FileSystemEdit::MoveDir { src, src_id, dst } => {
                // temporary placeholder for MoveDir since we are not using MoveDir in ide assists yet.
                (dst, format!("{src_id:?}\n{src:?}"))
            }
        };
        let sr = db.file_source_root(dst.anchor);
        let sr = db.source_root(sr);
        let mut base = sr.path_for_file(&dst.anchor).unwrap().clone();
        base.pop();
        let created_file_path = base.join(&dst.path).unwrap();
        fs::write(created_file_path.as_path().unwrap(), contents).unwrap();
    }

    // Source file edits
    for (file, (text_edit, snippet)) in source_change.source_file_edits.iter() {
        let mut code = db.file_text(*file).to_string();
        text_edit.apply(&mut code);
        // Snippet (NOT TESTED!!!)
        if let Some(snippet) = snippet {
            snippet.apply(&mut code);
        }
        db.set_file_text(*file, &code);
    }
}
