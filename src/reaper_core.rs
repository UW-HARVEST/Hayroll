use std::{collections::HashMap, fs, path::Path};

use anyhow::Result;
use ide_db::base_db::SourceDatabase;
use load_cargo;
use project_model::CargoConfig;
use syntax::ast::{Item};
use syntax::{
    ast::SourceFile,
    syntax_editor::Element,
    AstNode,
};
use tracing::{debug, info, warn};
use vfs::FileId;

use crate::hayroll_ds::*;
use crate::util::*;

pub fn run(workspace_path: &Path) -> Result<()> {
    // Record the start time
    let cargo_config = CargoConfig::default();
    let load_cargo_config = load_cargo::LoadCargoConfig {
        load_out_dirs_from_check: false,
        with_proc_macro_server: load_cargo::ProcMacroServerChoice::None,
        prefill_caches: false,
    };

    let (mut db, vfs, _proc_macro) =
        load_cargo::load_workspace_at(workspace_path, &cargo_config, &load_cargo_config, &|_| {})?;

    // ---- First Pass: handle macro invocations that can be converted to functions or macros ----

    // Collect syntax roots from the database (no VFS dependency for discovery)
    let syntax_roots: HashMap<FileId, SourceFile> = collect_syntax_roots_from_db(&db);
    let mut builder_set = SourceChangeBuilderSet::from_syntax_roots(&syntax_roots);
    info!(found_files = syntax_roots.len(), "Found Rust files in the workspace");
    for (file_id, _root) in &syntax_roots {
        debug!(file = %vfs.file_path(*file_id), "workspace file");
    }

    // We are using the SyntaxEditor paradigm, so we need only one builder
    // Create a single global builder; pick any file id
    let hayroll_seeds = extract_hayroll_seeds_from_syntax_roots(&syntax_roots);
    let hayroll_macro_invs = extract_hayroll_macro_invs_from_seeds(&hayroll_seeds);
    let hayroll_macro_db = HayrollMacroDB::from_hayroll_macro_invs(&hayroll_macro_invs);

    // For each macro db entry, generate a new macro/func definition and add that to the top/bottom of the file
    // For each macro invocation, replace the invocation with a macro/func call
    for (_loc_decl, cluster) in hayroll_macro_db.map.iter() {
        // Work in the declaration file for inserts
        let decl_file_id = cluster.file_id();
        let decl_root = syntax_roots.get(&decl_file_id).unwrap();
        let mut editor = builder_set.make_editor(decl_root.syntax());

        if cluster.can_be_fn() {
            // Add the function definition to the bottom of the file
            let fn_ = cluster.fn_();
            let fn_elem = fn_.syntax().syntax_element().clone();
            editor.insert_all(bot_pos(&decl_root), vec![get_empty_line_element_mut(), fn_elem]);

            // Call convention, which args must stay lvalue (ptr convention)
            let arg_requires_lvalue = cluster.args_require_lvalue();

            // Replace the macro expansions with the function calls
            for inv in cluster.invocations.iter() {
                let code_region = inv.seed.get_raw_code_region(false); // A C function always returns an rvalue
                let region_element_range = code_region.syntax_element_range();
                let fn_call_node = inv.call_expr_or_stmt_mut(&arg_requires_lvalue).syntax_element();
                editor.replace_all(region_element_range, vec![fn_call_node]);
            }
        } else if cluster.invs_internally_structurally_compatible() {
            // Not type-compatible, but can still be reconstructed as a Rust macro
            let macro_rules = cluster.macro_rules();
            let macro_rules_elem = macro_rules.syntax().syntax_element();
            let top = top_pos(&decl_root);
            editor.insert_all(top, vec![macro_rules_elem, get_empty_line_element_mut()]);

            // Replace the macro invocations with the macro calls
            for inv in cluster.invocations.iter() {
                let code_region = inv.seed.get_raw_code_region(true); // macro invocation may be lvalue or rvalue
                let macro_call_node = inv.macro_call().syntax().syntax_element();

                match code_region {
                    CodeRegion::Expr(_) | CodeRegion::Stmts { .. } => {
                        let region_element_range = code_region.syntax_element_range();
                        editor.replace_all(region_element_range, vec![macro_call_node]);
                    }
                    CodeRegion::Decls(_) => {
                        let mut items = code_region.syntax_element_vec();
                        let seed_item = inv.seed.get_raw_decls_tag_item();
                        items.push(seed_item.syntax().syntax_element().clone());

                        // Remove items then insert macro call at bottom of the file of invocation
                        let inv_root = syntax_roots.get(&inv.file_id()).unwrap();
                        let bot = bot_pos(&inv_root);
                        for item in items {
                            editor.delete(item);
                        }
                        editor.insert_all(bot, vec![get_empty_line_element_mut(), macro_call_node]);
                    }
                }
            }
        } else {
            warn!(loc = %cluster.invocations[0].loc_begin(), "Hayroll macro cannot be converted: incompatible argument usage; skipping");
        }
        builder_set.add_file_edits(decl_file_id, editor);
    }

    // Finalize edits from the single global builder
    let source_change = builder_set.finish();
    // Apply edits to the in-memory DB via file_text inputs
    apply_source_change(&mut db, &source_change);

    // ---- Second Pass: handle conditional macros ----

    let syntax_roots: HashMap<FileId, SourceFile> = collect_syntax_roots_from_db(&db);
    let mut builder_set = SourceChangeBuilderSet::from_syntax_roots(&syntax_roots);
    let hayroll_seeds: Vec<HayrollSeed> = extract_hayroll_seeds_from_syntax_roots(&syntax_roots);

    // Print number of syntax roots found
    println!("Found {} Rust files in the workspace", syntax_roots.len());
    for (file_id, _root) in &syntax_roots {
        println!("  - {}", vfs.file_path(*file_id));
    }

    let hayroll_conditional_macros: Vec<HayrollConditionalMacro> = hayroll_seeds.iter()
        .filter(|seed| seed.is_conditional())
        .map(|seed| HayrollConditionalMacro { seed: seed.clone() })
        .collect();

    // Apply conditional macros: attach cfg attributes or wrap expressions
    let teds = hayroll_conditional_macros.iter()
        .flat_map(|conditional_macro| {

        let new_teds = conditional_macro.attach_cfg_teds(&mut builder_set);
        new_teds
    }).collect::<Vec<Box<dyn FnOnce()>>>();

    for ted in teds {
        ted();
    }

    // Finalize edits from the single global builder
    let source_change = builder_set.finish();
    // Apply edits to the in-memory DB via file_text inputs
    apply_source_change(&mut db, &source_change);

    // ---- Third Pass: remove any c2rust::src_loc attributes from all items ----
    // Also remove any global items starting with HAYROLL_TAG_FOR

    let syntax_roots: HashMap<FileId, SourceFile> = collect_syntax_roots_from_db(&db);
    let mut builder_set = SourceChangeBuilderSet::from_syntax_roots(&syntax_roots);

    // All items, ignore filtering out HAYROLL_TAG_FOR_* yet
    let items: Vec<Item> = syntax_roots.iter()
        .flat_map(|(_file_id, root)| {
            root.syntax().descendants()
        })
        .filter_map(|node| Item::cast(node))
        .collect();

    for item in items {
        let mut editor = builder_set.make_editor(item.syntax());
        let file_id = builder_set.file_id_of_node(item.syntax()).unwrap();

        // Remove Hayroll tag items (detected by embedded JSON with {"hayroll": true})
        if item_is_hayroll_tag(&item) {
            editor.delete(item.syntax().syntax_element().clone());
            builder_set.add_file_edits(file_id, editor);
            continue; // Skip further processing for this item
        }

        if !item_has_c2rust_src_loc(&item) {
            continue; // No c2rust::src_loc attribute, skip
        }
        
        // Remove c2rust::src_loc attributes
        let item_no_c2rust = peel_c2rust_src_locs_from_item(&item).clone_for_update();
        editor.replace(
            item.syntax().syntax_element().clone(),
            item_no_c2rust.syntax().syntax_element().clone(),
        );
        print!("Removed c2rust::src_loc from item: {} into {}\n", item.syntax().text(), item_no_c2rust.syntax().text());
        builder_set.add_file_edits(file_id, editor);
    }

    // Finalize edits from the single global builder
    let source_change = builder_set.finish();
    // Apply edits to the in-memory DB via file_text inputs
    apply_source_change(&mut db, &source_change);

    // Write back all modified files to disk
    for file_id in syntax_roots.keys() {
        let file_path = vfs.file_path(*file_id);
        let code = db.file_text(*file_id).to_string();
        let code = if code.ends_with("\n") { code } else { code + "\n" };
        let path = file_path.as_path().unwrap();
        fs::write(path, code)?;
    }

    Ok(())
}
