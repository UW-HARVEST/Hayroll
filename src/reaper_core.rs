use std::{collections::HashMap, fs, path::Path};

use anyhow::Result;
use ide::RootDatabase;
use ide_db::{
    base_db::{SourceDatabase, SourceDatabaseFileInputExt},
    source_change::SourceChangeBuilder,
};
use load_cargo;
use project_model::CargoConfig;
use serde_json;
use syntax::{
    ast::{self, SourceFile},
    syntax_editor::Element,
    AstNode, AstToken,
};
use tracing::{debug, info, trace, warn};
use vfs::FileId;

use crate::hayroll_ds::{
    CodeRegion, HayrollConditionalMacro, HayrollMacroDB, HayrollMacroInv, HayrollMeta, HayrollSeed, HayrollTag,
};
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
    // for conditional_macro in hayroll_conditional_macros.iter() {
    let teds = hayroll_conditional_macros.iter()
        .flat_map(|conditional_macro| {

        if conditional_macro.is_placeholder() {
            return Vec::new();
        }

        // let file_id = conditional_macro.file_id();
        // let syntax_root = syntax_roots.get(&file_id).unwrap();

        // Original region to replace (without deref for exprs)
        let code_region = conditional_macro.seed.get_raw_code_region_inside_tag();
        // New region with cfg attached
        if code_region.is_empty() {
            return Vec::new();
        }

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

fn extract_hayroll_macro_invs_from_seeds(hayroll_seeds: &Vec<HayrollSeed>) -> Vec<HayrollMacroInv> {
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
    hayroll_macro_invs
}

fn extract_hayroll_seeds_from_syntax_roots(syntax_roots: &HashMap<FileId, SourceFile>) -> Vec<HayrollSeed> {
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

    // Check that all Stmts seeds have both begin and end tags
    for seed in &hayroll_seeds {
        if let HayrollSeed::Stmts(tag_begin, tag_end) = seed {
            if !tag_begin.begin()
                || tag_end.begin()
                || tag_begin.loc_begin() != tag_end.loc_begin()
                || tag_begin.seed_type() != tag_end.seed_type() {
                panic!("Unmatched begin/end tags for Stmts seed: {:?} {:?}", tag_begin, tag_end);
            }
        }
    }

    hayroll_seeds
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
