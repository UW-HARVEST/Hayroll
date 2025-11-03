use std::{collections::HashMap, fs, path::Path};

use anyhow::Result;
use ide::RootDatabase;
use ide_db::base_db::{SourceDatabase, SourceDatabaseFileInputExt};
use load_cargo;
use project_model::CargoConfig;
use syntax::{
    ast::{self, ElseBranch, HasModuleItem, Item, SourceFile, UseTree},
    syntax_editor::{Element, Position},
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

    let (mut base_db, base_vfs, _proc_macro) = load_cargo::load_workspace_at(
        base_workspace_path,
        &cargo_config,
        &load_cargo_config,
        &|_| {},
    )?;
    let base_syntax_roots: HashMap<FileId, SourceFile> = collect_syntax_roots_from_db(&base_db);
    let mut base_builder_set = SourceChangeBuilderSet::from_syntax_roots(&base_syntax_roots);
    info!(
        found_files = base_syntax_roots.len(),
        "Found Rust files in the base workspace"
    );
    for (file_id, _root) in &base_syntax_roots {
        debug!(file = %base_vfs.file_path(*file_id), "base workspace file");
    }
    let base_hayroll_seeds = extract_hayroll_seeds_from_syntax_roots(&base_syntax_roots);
    let base_hayroll_conditional_macros: Vec<HayrollConditionalMacro> = base_hayroll_seeds
        .iter()
        .filter(|seed| seed.is_conditional())
        .map(|seed| HayrollConditionalMacro { seed: seed.clone() })
        .collect();

    let (patch_db, patch_vfs, _proc_macro) = load_cargo::load_workspace_at(
        patch_workspace_path,
        &cargo_config,
        &load_cargo_config,
        &|_| {},
    )?;
    let patch_syntax_roots: HashMap<FileId, SourceFile> = collect_syntax_roots_from_db(&patch_db);
    let mut patch_builder_set = SourceChangeBuilderSet::from_syntax_roots(&patch_syntax_roots);
    info!(
        found_files = patch_syntax_roots.len(),
        "Found Rust files in the patch workspace"
    );
    for (file_id, _root) in &patch_syntax_roots {
        debug!(file = %patch_vfs.file_path(*file_id), "patch workspace file");
    }
    let patch_hayroll_seeds = extract_hayroll_seeds_from_syntax_roots(&patch_syntax_roots);
    let patch_hayroll_conditional_macros: Vec<HayrollConditionalMacro> = patch_hayroll_seeds
        .iter()
        .filter(|seed| seed.is_conditional())
        .map(|seed| HayrollConditionalMacro { seed: seed.clone() })
        .collect();

    // In base_hayroll_conditional_macros, some may share the same loc_ref_begin()
    // Create a list that only keeps one of them (the first one encountered)
    let base_hayroll_conditional_macros_unique_ref: Vec<HayrollConditionalMacro> =
        base_hayroll_conditional_macros
            .iter()
            .fold(Vec::new(), |mut acc, macro_| {
                if !acc
                    .iter()
                    .any(|m| m.seed.loc_ref_begin() == macro_.seed.loc_ref_begin())
                {
                    acc.push(macro_.clone());
                }
                acc
            });

    // Pair the elements in base_hayroll_conditional_macros with patch_hayroll_conditional_macros by loc_ref_begin()
    // Note that not every element in either list will have a match in the other list
    // We only keep the ones that have a match in both lists
    let paired_conditional_macros: Vec<(&HayrollConditionalMacro, &HayrollConditionalMacro)> =
        base_hayroll_conditional_macros_unique_ref
            .iter()
            .filter_map(|base_macro| {
                patch_hayroll_conditional_macros
                    .iter()
                    .find(|patch_macro| {
                        patch_macro.seed.loc_ref_begin() == base_macro.seed.loc_ref_begin()
                    })
                    .map(|patch_macro| (base_macro, patch_macro))
            })
            .collect();

    for (base_macro, patch_macro) in paired_conditional_macros.iter() {
        print!(
            "Processing conditonal macro pair {} and {}\n",
            base_macro.seed.loc_begin(),
            patch_macro.seed.loc_begin()
        );
        let decl_root = base_syntax_roots.get(&base_macro.seed.file_id()).unwrap();
        let mut base_editor = base_builder_set.make_editor(decl_root.syntax());
        match (base_macro.is_placeholder(), patch_macro.is_placeholder()) {
            (false, true) => {
                // Base has concrete code, patch is placeholder, no edit needed
                info!("Base has concrete code, patch is placeholder, no edit needed");
            }
            (true, false) => {
                info!(
                    "Base is placeholder, patch has concrete code, need to replace base with patch"
                );
                // Replace the tags themselves altogether
                let base_code_region = base_macro.seed.get_raw_code_region(true);
                let patch_code_region = patch_macro.seed.get_raw_code_region(true);
                let patch_code_region_mut =
                    patch_code_region.make_mut_with_builder_set(&mut patch_builder_set);
                match (&base_code_region, &patch_code_region_mut) {
                    (CodeRegion::Expr(_), CodeRegion::Expr(_))
                    | (CodeRegion::Stmts { .. }, CodeRegion::Stmts { .. }) => {
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
                        info!(
                            "Mismatched types between base and patch code regions, cannot replace"
                        );
                    }
                }
            }
            (false, false) => {
                // Check if the base seed already has the patch variant in mergedVariants
                if base_macro
                    .seed
                    .merged_variants()
                    .contains(&patch_macro.loc_begin())
                {
                    info!("Base macro already has the patch variant in mergedVariants, skipping merge");
                } else {
                    // Both have concrete code, need to merge
                    info!("Both have concrete code, need to merge");
                    let base_code_region = base_macro.seed.get_raw_code_region_inside_tag();
                    let patch_code_region = patch_macro.seed.get_raw_code_region_inside_tag();
                    let patch_code_region_mut =
                        patch_code_region.make_mut_with_builder_set(&mut patch_builder_set);
                    match (&base_code_region, &patch_code_region_mut) {
                        (CodeRegion::Expr(base_expr), CodeRegion::Expr(patch_expr)) => {
                            // base: if cfg!(xx) { val1 } [else if cfg!(yy) { val2 } ...] else { 0 }
                            // patch: if cfg!(zz) { val3 } else { 0 }
                            // merged: if cfg!(xx) { val1 } [else if cfg!(yy) { val2 } ...] else if cfg!(zz) { val3 } else { 0 }
                            let base_block =
                                ast::BlockExpr::cast(base_expr.syntax().clone()).unwrap();
                            let base_if =
                                ast::IfExpr::cast(base_block.tail_expr().unwrap().syntax().clone())
                                    .unwrap();
                            let patch_block =
                                ast::BlockExpr::cast(patch_expr.syntax().clone()).unwrap();
                            let patch_if = ast::IfExpr::cast(
                                patch_block.tail_expr().unwrap().syntax().clone(),
                            )
                            .unwrap();

                            let mut else_branch = base_if.else_branch().unwrap();
                            while let ElseBranch::IfExpr(else_if) = else_branch {
                                // There is no if without else branch in cfg expr, so unwrap is safe
                                let next_else = else_if.else_branch().unwrap();
                                else_branch = next_else;
                            }
                            let last_block = match else_branch {
                                ElseBranch::Block(block) => block,
                                ElseBranch::IfExpr(_) => unreachable!(), // because of the while let above
                            };
                            base_editor.replace(last_block.syntax(), patch_if.syntax());
                        }
                        (CodeRegion::Stmts { .. }, CodeRegion::Stmts { .. }) => {
                            let mut patch_stmts_nodes = patch_code_region_mut.syntax_element_vec();
                            // Put an empty line before the inserted stmts to make it look better
                            patch_stmts_nodes.insert(0, get_empty_line_element_mut());
                            base_editor
                                .insert_all(base_code_region.position_after(), patch_stmts_nodes);
                        }
                        (CodeRegion::Decls(_), CodeRegion::Decls(_)) => {
                            // We will merge all top-level declarations later anyways
                            // So no need to do anything here
                        }
                        _ => {
                            // Mismatched types, cannot merge
                            info!("Mismatched types between base and patch code regions, cannot merge");
                        }
                    }
                    // Update the HayrollTag in the replaced code to append the merged variant
                    let new_variant = patch_macro.loc_begin();
                    let new_literal = base_macro
                        .with_appended_merged_variants(&new_variant)
                        .clone_for_update();
                    let old_literal = base_macro.seed.first_tag().literal.clone();
                    base_editor.replace(old_literal.syntax(), new_literal.syntax());
                }
            }
            (true, true) => {
                // Both are placeholders, no edit needed
                info!("Both are placeholders, no edit needed");
            }
        }
        base_builder_set.add_file_edits(base_macro.seed.file_id(), base_editor);
    }

    // Merge top-level items from patch into corresponding base files
    // Strategy:
    // - For each patch file that also exists in base (matched by file path),
    //   compute signatures of base items (name + attr set ignoring order).
    // - For each patch item, if not a HAYROLL_TAG_FOR* and its signature isn't in base,
    //   insert it: macros at file top (after top-level attrs), others at file bottom.
    // - Only consider items with a name (or use ...); unnamed items are skipped to avoid accidental duplication.

    // Build a relpath->FileId index for base files (strip workspace root prefix)
    let mut base_path_to_id: HashMap<String, FileId> = HashMap::new();
    for (fid, _) in &base_syntax_roots {
        let vfs_path = base_vfs.file_path(*fid);
        let abs_ra = vfs_path.as_path().unwrap();
        let abs_std: &std::path::Path = abs_ra.as_ref();
        let rel = abs_std.strip_prefix(base_workspace_path)?;
        let rel_str = rel.to_string_lossy().to_string();
        base_path_to_id.insert(rel_str, *fid);
    }

    // Helper to get ExternItem under extern "C" blocks
    let extern_c_items = |root: &SourceFile| -> Vec<ast::ExternItem> {
        root.items()
            .into_iter()
            .filter_map(|item| {
                if let Item::ExternBlock(ext_block) = item {
                    Some(ext_block)
                } else {
                    None
                }
            })
            .filter(|ext_block| {
                if let Some(abi) = ext_block.abi() {
                    if let Some(abi_str) = abi.abi_string() {
                        if let Ok(value) = abi_str.value() {
                            return value == "C";
                        }
                    }
                }
                false
            })
            .flat_map(|ext_block| {
                ext_block
                    .extern_item_list()
                    .unwrap()
                    .extern_items()
                    .into_iter()
            })
            .collect()
    };
    // Helper to get an item's simple name (direct child Name) or UseTree (for merging use) or ABI (for extern items)
    let item_name = |item: &dyn ast::AstNode| -> Option<String> {
        item.syntax().children().find_map(|child| {
            ast::Name::cast(child.clone())
                .map(|name| name.to_string())
                .or_else(|| UseTree::cast(child.clone()).map(|tree| tree.to_string()))
                .or_else(|| ast::Abi::cast(child).map(|abi| abi.to_string()))
        })
    };
    // Helper to collect the set of attribute spellings directly on the item (ignore order)
    let item_attr_set = |item: &dyn ast::HasAttrs| -> std::collections::BTreeSet<String> {
        item.attrs().map(|a| a.to_string()).collect()
    };
    // Helper to decide if an item is a macro definition
    let is_macro_def = |item: &ast::Item| -> bool {
        ast::MacroRules::cast(item.syntax().clone()).is_some()
            || ast::MacroDef::cast(item.syntax().clone()).is_some()
    };

    for (patch_fid, patch_root) in &patch_syntax_roots {
        let vfs_path = patch_vfs.file_path(*patch_fid);
        let Some(abs_ra) = vfs_path.as_path() else {
            continue;
        };
        let abs_std: &std::path::Path = abs_ra.as_ref();
        let Ok(rel) = abs_std.strip_prefix(patch_workspace_path) else {
            continue;
        };
        let rel_str = rel.to_string_lossy().to_string();
        let Some(base_fid) = base_path_to_id.get(&rel_str).copied() else {
            continue;
        };
        let base_root = base_syntax_roots.get(&base_fid).unwrap();

        let base_sigs: std::collections::BTreeSet<(String, std::collections::BTreeSet<String>)> =
            base_root
                .items()
                .into_iter()
                .filter_map(|item| {
                    item_name(&item).map(|name| {
                        let attrs = item_attr_set(&item);
                        (name, attrs)
                    })
                })
                .collect();

        // Collect items to insert, categorized by placement
        let mut to_top: Vec<syntax::SyntaxElement> = Vec::new();
        let mut to_bot: Vec<syntax::SyntaxElement> = Vec::new();

        for p_item in patch_root.items() {
            let Some(name) = item_name(&p_item) else {
                continue;
            };
            let attrs = item_attr_set(&p_item);
            if base_sigs.contains(&(name.clone(), attrs.clone())) {
                continue;
            }

            let elem = p_item.syntax().clone_for_update().syntax_element();
            if is_macro_def(&p_item) {
                // insert macro at top (after file attrs), keep an empty line after
                to_top.push(elem);
                to_top.push(get_empty_line_element_mut());
            } else {
                // insert others at bottom, with an empty line before
                to_bot.push(get_empty_line_element_mut());
                to_bot.push(elem);
            }
        }

        if !to_top.is_empty() || !to_bot.is_empty() {
            let mut editor = base_builder_set.make_editor(base_root.syntax());
            if !to_top.is_empty() {
                let top = top_pos(base_root);
                editor.insert_all(top, to_top);
            }
            if !to_bot.is_empty() {
                let bot = bot_pos(base_root);
                editor.insert_all(bot, to_bot);
            }
            base_builder_set.add_file_edits(base_fid, editor);
        }

        let Some(base_first_extern_c) = base_root.items().into_iter().find_map(|item| match item {
            Item::ExternBlock(ext_block) => Some(ext_block),
            _ => None,
        }) else {
            // No extern "C" block in base file, any extern "C" items in patch
            // should have already be handled when merging top-level items above
            continue;
        };

        // Collect items under an extern "C" block in base file
        let base_extern_c_sigs: std::collections::BTreeSet<(
            String,
            std::collections::BTreeSet<String>,
        )> = extern_c_items(base_root)
            .into_iter()
            .filter_map(|item| {
                item_name(&item).map(|name| {
                    let attrs = item_attr_set(&item);
                    (name, attrs)
                })
            })
            .collect();

        // Print debug info
        for p_item in extern_c_items(patch_root) {
            let Some(name) = item_name(&p_item) else {
                continue;
            };
            let attrs = item_attr_set(&p_item);
            if base_extern_c_sigs.contains(&(name.clone(), attrs.clone())) {
                continue;
            }

            let elem = p_item.syntax().clone_for_update().syntax_element();
            let mut editor = base_builder_set.make_editor(base_root.syntax());
            // Insert at the end of the first extern "C" block found in base file
            let insert_pos = Position::before(
                base_first_extern_c
                    .extern_item_list()
                    .unwrap()
                    .r_curly_token()
                    .unwrap(),
            );
            editor.insert_all(insert_pos, vec![elem, get_empty_line_element_mut()]);
            base_builder_set.add_file_edits(base_fid, editor);
        }
    }

    // Finalize edits from the single global builder
    let source_change = base_builder_set.finish();
    // Apply edits to the in-memory DB via file_text inputs
    apply_source_change(&mut base_db, &source_change);

    // Write back all modified files to disk
    for file_id in base_syntax_roots.keys() {
        let file_path = base_vfs.file_path(*file_id);
        let code = base_db.file_text(*file_id).to_string();
        let code = if code.ends_with("\n") {
            code
        } else {
            code + "\n"
        };
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
