use std::{collections::HashMap, fs, path::Path};

use anyhow::Result;
use ide::RootDatabase;
use ide_db::base_db::{SourceDatabase, SourceDatabaseFileInputExt};
use load_cargo;
use project_model::CargoConfig;
use syntax::{
    ast::SourceFile,
    AstNode,
};
use tracing::{debug, info};
use vfs::FileId;

use crate::hayroll_ds::*;
use crate::util::*;

pub fn run(base_workspace_path: &Path, patch_workspace_path: &Path) -> Result<()> {
    let cargo_config = CargoConfig::default();
    let load_cargo_config = load_cargo::LoadCargoConfig {
        load_out_dirs_from_check: false,
        with_proc_macro_server: load_cargo::ProcMacroServerChoice::None,
        prefill_caches: false,
    };

    let (mut base_db, base_vfs, _proc_macro) =
        load_cargo::load_workspace_at(base_workspace_path, &cargo_config, &load_cargo_config, &|_| {})?;
    let base_syntax_roots: HashMap<FileId, SourceFile> = collect_syntax_roots_from_db(&base_db);
    let mut base_builder_set = SourceChangeBuilderSet::from_syntax_roots(&base_syntax_roots);
    info!(found_files = base_syntax_roots.len(), "Found Rust files in the base workspace");
    for (file_id, _root) in &base_syntax_roots {
        debug!(file = %base_vfs.file_path(*file_id), "base workspace file");
    }
    let base_hayroll_seeds = extract_hayroll_seeds_from_syntax_roots(&base_syntax_roots);
    let base_hayroll_conditional_macros: Vec<HayrollConditionalMacro> = base_hayroll_seeds.iter()
        .filter(|seed| seed.is_conditional())
        .map(|seed| HayrollConditionalMacro { seed: seed.clone() })
        .collect();

    let (mut patch_db, patch_vfs, _proc_macro) =
        load_cargo::load_workspace_at(patch_workspace_path, &cargo_config, &load_cargo_config, &|_| {})?;
    let patch_syntax_roots: HashMap<FileId, SourceFile> = collect_syntax_roots_from_db(&patch_db);
    let mut patch_builder_set = SourceChangeBuilderSet::from_syntax_roots(&patch_syntax_roots);
    info!(found_files = patch_syntax_roots.len(), "Found Rust files in the patch workspace");
    for (file_id, _root) in &patch_syntax_roots {
        debug!(file = %patch_vfs.file_path(*file_id), "patch workspace file");
    }
    let patch_hayroll_seeds = extract_hayroll_seeds_from_syntax_roots(&patch_syntax_roots);
    let patch_hayroll_conditional_macros: Vec<HayrollConditionalMacro> = patch_hayroll_seeds.iter()
        .filter(|seed| seed.is_conditional())
        .map(|seed| HayrollConditionalMacro { seed: seed.clone() })
        .collect();

    // In base_hayroll_conditional_macros, some may share the same loc_ref_begin()
    // Create a list that only keeps one of them (the first one encountered)
    let base_hayroll_conditional_macros_unique_ref: Vec<HayrollConditionalMacro> = base_hayroll_conditional_macros.iter()
        .fold(Vec::new(), |mut acc, macro_| {
            if !acc.iter().any(|m| m.seed.loc_ref_begin() == macro_.seed.loc_ref_begin()) {
                acc.push(macro_.clone());
            }
            acc
        });

    // Pair the elements in base_hayroll_conditional_macros with patch_hayroll_conditional_macros by loc_ref_begin()
    // Note that not every element in either list will have a match in the other list
    // We only keep the ones that have a match in both lists
    let paired_conditional_macros: Vec<(&HayrollConditionalMacro, &HayrollConditionalMacro)> = base_hayroll_conditional_macros_unique_ref.iter()
        .filter_map(|base_macro| {
            patch_hayroll_conditional_macros.iter()
                .find(|patch_macro| patch_macro.seed.loc_ref_begin() == base_macro.seed.loc_ref_begin())
                .map(|patch_macro| (base_macro, patch_macro))
        })
        .collect();

    for (base_macro, patch_macro) in paired_conditional_macros.iter() {
        print!("Processing conditonal macro pair {} and {}\n", base_macro.seed.loc_begin(), patch_macro.seed.loc_begin());
        let decl_root = base_syntax_roots.get(&base_macro.seed.file_id()).unwrap();
        let mut base_editor = base_builder_set.make_editor(decl_root.syntax());
        match (base_macro.is_placeholder(), patch_macro.is_placeholder()) {
            (false, true) => {
                // Base has concrete code, patch is placeholder, no edit needed
                info!("Base has concrete code, patch is placeholder, no edit needed");
            }
            (true, false) => {
                // Base is placeholder, patch has concrete code, need to replace base with patch
                info!("Base is placeholder, patch has concrete code, need to replace base with patch");
                // TODO: implement replacement logic
                let base_code_region = base_macro.seed.get_raw_code_region(true);
                let patch_code_region = patch_macro.seed.get_raw_code_region(true);
                let patch_code_region_mut = patch_code_region.make_mut_with_builder_set(&mut patch_builder_set);
                match (&base_code_region, &patch_code_region) {
                    (CodeRegion::Expr(_), CodeRegion::Expr(_)) | (CodeRegion::Stmts { .. }, CodeRegion::Stmts { .. }) => {
                        let base_region_element_range = base_code_region.syntax_element_range();
                        let patch_stmts_nodes = patch_code_region_mut.syntax_element_vec();
                        base_editor.replace_all(base_region_element_range, patch_stmts_nodes);
                    }
                    (CodeRegion::Decls(_), CodeRegion::Decls(_)) => {
                        // We will merge all top-level declarations later anyways
                        // So no need to do anything here
                    }
                    _ => {
                        // Mismatched types, cannot replace
                        info!("Mismatched types between base and patch code regions, cannot replace");
                    }
                }
            }
            (false, false) => {
                // Both have concrete code, need to merge
                info!("Both have concrete code, need to merge");
                // TODO: implement merging logic
            }
            (true, true) => {
                // Both are placeholders, no edit needed
                info!("Both are placeholders, no edit needed");
            }
        }
        base_builder_set.add_file_edits(base_macro.seed.file_id(), base_editor);       
    }

    // Finalize edits from the single global builder
    let source_change = base_builder_set.finish();
    // Apply edits to the in-memory DB via file_text inputs
    apply_source_change(&mut base_db, &source_change);

    // Write back all modified files to disk
    for file_id in base_syntax_roots.keys() {
        let file_path = base_vfs.file_path(*file_id);
        let code = base_db.file_text(*file_id).to_string();
        let code = if code.ends_with("\n") { code } else { code + "\n" };
        let path = file_path.as_path().unwrap();
        fs::write(path, code)?;
    }

    Ok(())
}

// Apply the source change to the RootDatabase
fn apply_source_change(db: &mut RootDatabase, source_change: &ide::SourceChange) {
    // Best-effort transactional behavior: cancel outstanding queries first.
    db.request_cancellation();

    // Apply per-file text edits directly to DB inputs.
    for (file_id, (text_edit, snippet)) in source_change.source_file_edits.iter() {
        let mut code = db.file_text(*file_id).to_string();
        text_edit.apply(&mut code);
        if let Some(snippet) = snippet {
            snippet.apply(&mut code);
        }
        db.set_file_text(*file_id, &code);
    }
}
