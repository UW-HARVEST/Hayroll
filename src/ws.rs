use std::{env, path::Path};
use camino;
use ide::{RootDatabase, SourceChange};
use ide_db::{base_db::{SourceDatabase, SourceDatabaseFileInputExt, SourceRootDatabase}, source_change::SourceChangeBuilder};
use project_model::CargoConfig;
use load_cargo;
use anyhow::Result;
use syntax::{ast::{self, HasName}, AstNode};
use vfs::AbsPath;
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

    let mut source_change = SourceChange::default();

    for file_id in file_ids.iter() {
        // Print filename and path
        let mut builder = SourceChangeBuilder::new(*file_id);

        let file_path = vfs.file_path(*file_id);
        println!("File: {}", file_path);

        let syntax = sema.parse_guess_edition(*file_id).syntax().clone();

        let functions: Vec<ast::Fn> = syntax
            .descendants()
            .filter_map(|node| {
                if let Some(fn_def) = ast::Fn::cast(node) {
                    Some(fn_def)
                } else {
                    None
                }
            }).collect();

        // Collect all name references, we will rename them later
        let name_refs: Vec<ast::NameRef> = syntax
            .descendants()
            .filter_map(|node| {
                if let Some(name_ref) = ast::NameRef::cast(node) {
                    Some(name_ref)
                } else {
                    None
                }
            }).collect();

        let old_fn_names: Vec<String> = functions
            .iter()
            .map(|f| f.name().map(|n| n.to_string()).unwrap_or_else(|| "<anonymous>".to_string()))
            .collect();

        // rename all functions (and calls to them) to "xxx_renamed"
        for function in functions {
            let name = function.name().map(|n| n.to_string()).unwrap_or_else(|| "<anonymous>".to_string());
            println!("Function: {}", name);

            let new_name = format!("{}_renamed", name);
            let new_name_node = ast::make::name(&new_name).clone_for_update();
            builder.replace_ast(function.name().unwrap(), new_name_node);
        }

        for name_ref in name_refs {
            let name = name_ref.text().to_string();
            // The name must be in the list of old function names
            if !old_fn_names.contains(&name) {
                continue;
            }
            let new_name = format!("{}_renamed", name);
            let new_name_node = ast::make::name_ref(&new_name).clone_for_update();
            builder.replace_ast(name_ref, new_name_node);
        }

        source_change = source_change.merge(builder.finish());
    }

    apply_source_change(&mut db, &source_change);

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
