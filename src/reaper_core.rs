use std::{collections::HashMap, fs, path::Path, time::Instant};

use anyhow::Result;
use ide::RootDatabase;
use ide_db::{
    base_db::{SourceDatabase, SourceDatabaseFileInputExt},
    source_change::SourceChangeBuilder,
    EditionedFileId,
};
use load_cargo;
use project_model::CargoConfig;
use serde_json;
use syntax::{
    ast::{self, SourceFile},
    syntax_editor::Element,
    AstNode, AstToken,
};
use tracing::{debug, error, info, trace, warn};
use vfs::FileId;

use crate::hayroll_ds::{
    CodeRegion, HayrollConditionalMacro, HayrollMacroDB, HayrollMacroInv, HayrollMeta, HayrollSeed, HayrollTag,
};
use crate::util::{bot_pos, get_empty_line_element_mut, top_pos};

pub fn run(workspace_path: &Path) -> Result<()> {
    // Record the start time
    let start = Instant::now();

    let cargo_config = CargoConfig::default();
    let load_cargo_config = load_cargo::LoadCargoConfig {
        load_out_dirs_from_check: false,
        with_proc_macro_server: load_cargo::ProcMacroServerChoice::None,
        prefill_caches: false,
    };

    // Print consumed time, tag "load_cargo"
    let duration = start.elapsed();
    info!(phase = "load_cargo", ?duration, "Time elapsed in load_cargo");

    let (mut db, vfs, _proc_macro) =
        load_cargo::load_workspace_at(workspace_path, &cargo_config, &load_cargo_config, &|_| {})?;

    // Print consumed time, tag "db"
    let duration = start.elapsed();
    info!(phase = "db", ?duration, "Time elapsed in db");

    // Collect all Rust source file roots; we will use a single global builder and switch files via edit_file
    let syntax_roots: HashMap<FileId, SourceFile> = vfs
        .iter()
        .filter_map(|(file_id, path)| {
            // Include only Rust source files, not toml files
            if !path.to_string().ends_with(".rs") {
                return None;
            }
            let root = db.parse(EditionedFileId::current_edition(file_id)).tree();
            Some((file_id, root))
        })
        .collect();
    info!(found_files = syntax_roots.len(), "Found Rust files in the workspace");
    for (file_id, _root) in &syntax_roots {
        debug!(file = %vfs.file_path(*file_id), "workspace file");
    }

    // Create a single global builder; pick any file id (must exist since we found Rust files)
    let first_file_id = *syntax_roots.keys().next().expect("No Rust files found in workspace");
    let mut builder = SourceChangeBuilder::new(first_file_id);

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
                        if tag_begin.loc_begin() == tag.loc_begin() && tag_begin.seed_type() == tag.seed_type() && tag.begin() == false {
                            *tag_end = tag.clone();
                            found = true;
                            break;
                        }
                    }
                    _ => {}
                }
            }
            if !found {
                panic!("No matching begin stmt found for end stmt");
            }
        } else {
            panic!("Unknown tag");
        }
        acc
    });

    // A region whose isArg is false is a macro; match args to their macro
    let hayroll_macro_invs: Vec<HayrollMacroInv> = hayroll_seeds.iter()
        .filter(|seed| seed.is_invocation())
        .fold(Vec::new(), |mut acc, region| {
            if region.is_arg() == false {
                // Pre-populate all expected argument names with empty vectors
                let preset_args: Vec<(String, Vec<HayrollSeed>)> = region
                    .arg_names()
                    .into_iter()
                    .map(|name| (name, Vec::new()))
                    .collect();
                acc.push(HayrollMacroInv { seed: region.clone(), args: preset_args });
            } else {
                let mut found = false;
                for mac in acc.iter_mut().rev() {
                    if mac.loc_begin() == region.loc_ref_begin() {
                        assert!(mac.args.iter().any(|(name, _)| name == &region.name()));
                        let arg = mac.args.iter_mut().find(|(name, _)| name == &region.name()).unwrap();
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
        }
    );

    let hayroll_conditional_macros: Vec<HayrollConditionalMacro> = hayroll_seeds.iter()
        .filter(|seed| seed.is_conditional())
        .map(|seed| HayrollConditionalMacro { seed: seed.clone() })
        .collect();

    let hayroll_macro_db = HayrollMacroDB::from_hayroll_macro_invs(&hayroll_macro_invs);

    // Print consumed time, tag "hayroll_literals"
    let duration = start.elapsed();
    info!(phase = "hayroll_literals", ?duration, "Time elapsed in hayroll_literals");

    // For each macro db entry, generate a new macro/func definition and add that to the top/bottom of the file
    // For each macro invocation, replace the invocation with a macro/func call
    for (_loc_decl, cluster) in hayroll_macro_db.map.iter() {
        // Work in the declaration file for inserts
        let decl_file_id = cluster.file_id();
        let decl_root = syntax_roots.get(&decl_file_id).unwrap();
        let mut editor = builder.make_editor(decl_root.syntax());

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
        builder.add_file_edits(decl_file_id, editor);
    }

    // Apply conditional macros: attach cfg attributes or wrap expressions
    for conditional_macro in hayroll_conditional_macros.iter() {
        if conditional_macro.is_placeholder() {
            continue;
        }

        let file_id = conditional_macro.file_id();
        let syntax_root = syntax_roots.get(&file_id).unwrap();
        let mut editor = builder.make_editor(syntax_root.syntax());

        // Original region to replace (without deref for exprs)
        let code_region = conditional_macro.seed.get_raw_code_region_inside_tag();
        // New region with cfg attached
        if code_region.is_empty() {
            continue;
        }
        let new_region = conditional_macro.attach_cfg_mut();
        if new_region.is_empty() {
            continue;
        }

        match (&code_region, &new_region) {
            (CodeRegion::Expr(_), CodeRegion::Expr(_)) | (CodeRegion::Stmts { .. }, CodeRegion::Stmts { .. }) => {
                let range = code_region.syntax_element_range();
                let new_elems = new_region.syntax_element_vec();
                editor.replace_all(range, new_elems);
            }
            (CodeRegion::Decls(old_items), CodeRegion::Decls(new_items)) => {
                assert_eq!(old_items.len(), new_items.len(),
                    "decl item count should not change after cfg attach");
                for (old_item, new_item) in old_items.iter().zip(new_items.iter()) {
                    // Replace each item in place
                    editor.replace(old_item.syntax(), &new_item.syntax());
                }
            }
            _ => {
                // Should not happen if attach_cfg preserves the kind
                error!("conditional macro region kind mismatch after attach_cfg");
            }
        }

        builder.add_file_edits(file_id, editor);
    }

    // Print consumed time, tag "replace"
    let duration = start.elapsed();
    info!(phase = "replace", ?duration, "Time elapsed in replace");

    // Print consumed time, tag "analysis"
    let duration = start.elapsed();
    info!(phase = "analysis", ?duration, "Time elapsed in analysis");

    // Finalize edits from the single global builder
    let source_change = builder.finish();

    apply_source_change(&mut db, &source_change);

    for file_id in syntax_roots.keys() {
        let file_path = vfs.file_path(*file_id);
        let code = db.file_text(*file_id).to_string();
        let code = if code.ends_with("\n") { code } else { code + "\n" };
        let path = file_path.as_path().unwrap();
        fs::write(path, code)?;
    }

    // Print consumed time, tag "write"
    let duration = start.elapsed();
    info!(phase = "write", ?duration, "Time elapsed in write");

    Ok(())
}

// Apply the source change to the RootDatabase
fn apply_source_change(db: &mut RootDatabase, source_change: &ide::SourceChange) {
    // Source file edits
    for (file_id, (text_edit, snippet)) in source_change.source_file_edits.iter() {
        let mut code = db.file_text(file_id.clone()).to_string();
        text_edit.apply(&mut code);
        if let Some(snippet) = snippet {
            snippet.apply(&mut code);
        }
        db.set_file_text(*file_id, &code);
    }
}
