use std::{collections::HashMap, fs, path::Path};

use anyhow::Result;
use hir::{Semantics, db::ExpandDatabase, prettify_macro_expansion};
use ide_db::base_db::SourceDatabase;
use load_cargo;
use project_model::CargoConfig;
use syntax::{ast::{self, SourceFile}, AstNode};
use tracing::{debug, info};
use vfs::FileId;

use crate::util::*;

pub fn run(workspace_path: &Path) -> Result<()> {
    let cargo_config = CargoConfig::default();
    let load_cargo_config = load_cargo::LoadCargoConfig {
        load_out_dirs_from_check: false,
        with_proc_macro_server: load_cargo::ProcMacroServerChoice::None,
        prefill_caches: false,
    };

    let (mut db, vfs, _proc_macro) =
        load_cargo::load_workspace_at(workspace_path, &cargo_config, &load_cargo_config, &|_| {})?;

    let sema = Semantics::new(&db);
    let syntax_roots: HashMap<FileId, SourceFile> = collect_syntax_roots_from_sema(&sema);
    let mut builder_set = SourceChangeBuilderSet::from_syntax_roots(&syntax_roots);

    let mut inlined_count: usize = 0;

    info!(
        found_files = syntax_roots.len(),
        "Found Rust files in the workspace (Inliner)"
    );
    for (file_id, _root) in &syntax_roots {
        debug!(file = %vfs.file_path(*file_id), "Inliner workspace file");
    }

    for (file_id, root) in &syntax_roots {
        for macro_call in root
            .syntax()
            .descendants()
            .filter_map(ast::MacroCall::cast)
        {
            let Some(macro_def) = sema.to_def(&macro_call) else {
                continue;
            };
            let span_map = sema.db.expansion_span_map(macro_def.as_macro_file());
            let expanded = sema.parse_or_expand(macro_def.as_file());
            let prettified = prettify_macro_expansion(
                &db,
                expanded,
                &span_map,
                sema.file_to_module_def(*file_id).unwrap().krate().into()
            );

            inlined_count += 1;
            let mut editor = builder_set.make_editor(macro_call.syntax());
            editor.replace(macro_call.syntax(), prettified);
            builder_set.add_file_edits(*file_id, editor);
        }
    }

    info!(inlined_macros = inlined_count, "Applied inline macro transformations");

    let source_change = builder_set.finish();
    apply_source_change(&mut db, &source_change);

    for file_id in syntax_roots.keys() {
        let file_path = vfs.file_path(*file_id);
        let path = file_path.as_path().unwrap();
        let code = db.file_text(*file_id).to_string();
        let code = if code.ends_with("\n") {
            code
        } else {
            code + "\n"
        };
        fs::write(path, code)?;
    }

    Ok(())
}
