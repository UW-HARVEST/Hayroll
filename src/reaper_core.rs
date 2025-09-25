use std::{collections::HashMap, fs, path::Path, time::Instant};

use anyhow::Result;
use ide::{RootDatabase, SourceChange};
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
    ted, AstNode, AstToken,
};
use tracing::{debug, info, trace, warn};
use vfs::FileId;

use crate::hayroll_ds::{
    CodeRegion, HayrollConditionalMacro, HayrollMacroDB, HayrollMacroInv, HayrollMeta, HayrollSeed, HayrollTag
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

    let mut syntax_roots: HashMap<FileId, (SourceFile, Option<SourceChangeBuilder>)> = vfs
        .iter()
        .filter_map(|(file_id, path)| {
            // Include only Rust source files, not toml files
            if !path.to_string().ends_with(".rs") {
                return None;
            }
            let builder = SourceChangeBuilder::new(file_id);
            // Using the sema-based parser allows querying semantic info i.e. type of an expression
            // But it's much slower than the db-based parser
            // let root = sema.parse_guess_edition(file_id);
            let root = db.parse(EditionedFileId::current_edition(file_id)).tree();
            Some((file_id, (root, Some(builder))))
        })
        .collect();
    info!(found_files = syntax_roots.len(), "Found Rust files in the workspace");
    for (file_id, (_root, _)) in &syntax_roots {
        debug!(file = %vfs.file_path(*file_id), "workspace file");
    }

    let hayroll_tags: Vec<HayrollTag> = syntax_roots
        .iter()
        .flat_map(|(file_id, (root, _))| {
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
    
    let mut delayed_tasks: Vec<Box<dyn FnOnce()>> = Vec::new();

    // For each macro db entry, generate a new macro/func definition and add that to the top/bottom of the file
    // For each macro invocation, replace the invocation with a macro/func call
    for (_loc_decl, cluster) in hayroll_macro_db.map.iter() {
        let (syntax_root, builder) = syntax_roots.get_mut(&cluster.file_id()).unwrap();
        let builder = builder.as_mut().unwrap();
        let syntax_root_mut = builder.make_mut(syntax_root.clone());

        if cluster.can_be_fn() {
            // Add the function definition to the bottom of the file
            let fn_ = cluster.fn_();
            let fn_elem = fn_.syntax().syntax_element().clone();
            delayed_tasks.push(Box::new(move || {
                ted::insert_all(bot_pos(&syntax_root_mut), vec![get_empty_line_element_mut(), fn_elem]);
            }));

            // Call convention, which args must stay lvalue (ptr convention)
            let arg_requires_lvalue = cluster.args_require_lvalue();

            // Replace the macro expansions with the function calls
            for inv in cluster.invocations.iter() {
                let code_region = inv.seed.get_raw_code_region(false); // A C function always returns an rvalue
                let (_, builder) = syntax_roots.get_mut(&inv.file_id()).unwrap();
                let builder = builder.as_mut().unwrap();
                let region_mut = code_region.make_mut_with_builder(builder);
                let region_mut_element_range = region_mut.syntax_element_range();
                let fn_call_node = inv.call_expr_or_stmt_mut(&arg_requires_lvalue).syntax_element();
                // We can call syntax_element_range() here because decls macro cannot be wrapped into a function
                delayed_tasks.push(Box::new(move || {
                    ted::replace_all(region_mut_element_range, vec![fn_call_node]);
                }));
            }
        } else if cluster.invs_internally_structurally_compatible() {
            // Not type-compatible, but can still be reconstructed as a Rust macro
            let macro_rules = cluster.macro_rules();
            let macro_rules_elem = macro_rules.syntax().syntax_element();
            let top = top_pos(&syntax_root_mut);
            delayed_tasks.push(Box::new(move || {
                ted::insert_all(top, vec![macro_rules_elem, get_empty_line_element_mut()])
            }));

            // Replace the macro invocations with the macro calls
            for inv in cluster.invocations.iter() {
                let code_region = inv.seed.get_raw_code_region(true); // macro invocation may be lvalue or rvalue
                let (_, builder) = syntax_roots.get_mut(&inv.file_id()).unwrap();
                let builder = builder.as_mut().unwrap();
                let region_mut = code_region.make_mut_with_builder(builder);
                let macro_call_node = inv.macro_call().syntax().syntax_element();

                match code_region {
                    CodeRegion::Expr(_) | CodeRegion::Stmts { .. } => {
                        let region_mut_element_range = region_mut.syntax_element_range();
                        delayed_tasks.push(Box::new(move || {
                            ted::replace_all(region_mut_element_range, vec![macro_call_node]);
                        }));
                    }
                    CodeRegion::Decls(_) => {
                        let mut items = region_mut.syntax_element_vec();
                        let seed_item = inv.seed.get_raw_decls_tag_item();
                        let seed_item_mut = builder.make_mut(seed_item);
                        items.push(seed_item_mut.syntax().syntax_element().clone());
                        let bot = bot_pos(&syntax_root_mut);
                        delayed_tasks.push(Box::new(move || {
                            for item in items {
                                ted::remove(item);
                            }
                            ted::insert_all(bot, vec![get_empty_line_element_mut(), macro_call_node]);
                        }));
                    }
                }
            }
        } else {
            warn!(loc = %cluster.invocations[0].loc_begin(), "Hayroll macro cannot be converted: incompatible argument usage; skipping");
        }
    }

    // Apply conditional macros: attach cfg attributes or wrap expressions
    for conditonal_macro in hayroll_conditional_macros.iter() {
        let file_id = conditonal_macro.file_id();
        let (_, builder) = syntax_roots.get_mut(&file_id).unwrap();
        let builder = builder.as_mut().unwrap();

        // Original region to replace (without deref for exprs)
        let code_region = conditonal_macro.seed.get_raw_code_region_inside_tag();
        // New region with cfg attached
        if code_region.is_empty() {
            continue;
        }
        let new_region = conditonal_macro.attach_cfg_mut();
        if new_region.is_empty() {
            continue;
        }

        match (&code_region, &new_region) {
            (CodeRegion::Expr(_), CodeRegion::Expr(new_expr)) => {
                let region_mut = code_region.make_mut_with_builder(builder);
                let range = region_mut.syntax_element_range();
                let new_elem = new_expr.syntax().syntax_element().clone();
                delayed_tasks.push(Box::new(move || {
                    ted::replace_all(range, vec![new_elem]);
                }));
            }
            (CodeRegion::Stmts { .. }, CodeRegion::Stmts { .. }) => {
                let region_mut = code_region.make_mut_with_builder(builder);
                let range = region_mut.syntax_element_range();
                let new_elems = new_region.syntax_element_vec();
                delayed_tasks.push(Box::new(move || {
                    ted::replace_all(range, new_elems);
                }));
            }
            (CodeRegion::Decls(_), CodeRegion::Decls(new_items)) => {
                let region_mut = code_region.make_mut_with_builder(builder);
                if let CodeRegion::Decls(old_items) = region_mut {
                    assert_eq!(old_items.len(), new_items.len(), "decl item count should not change after cfg attach");
                    for (old_item, new_item) in old_items.iter().zip(new_items.iter()) {
                        // Replace each item in place
                        let old_item_mut = builder.make_mut(old_item.clone());
                        let new_item_node = new_item.syntax().clone();
                        delayed_tasks.push(Box::new(move || {
                            ted::replace(old_item_mut.syntax(), &new_item_node);
                        }));
                    }
                } else {
                    panic!("code_region should be Decls");
                }
            }
            _ => {
                // Should not happen if attach_cfg preserves the kind
                warn!("conditional macro region kind mismatch after attach_cfg");
            }
        }
    }

    // Print consumed time, tag "replace"
    let duration = start.elapsed();
    info!(phase = "replace", ?duration, "Time elapsed in replace");

    for task in delayed_tasks {
        task();
    }

    // Print consumed time, tag "analysis"
    let duration = start.elapsed();
    info!(phase = "analysis", ?duration, "Time elapsed in analysis");

    let mut source_change = SourceChange::default();
    for (_, (_, builder)) in syntax_roots.iter_mut() {
        let builder = builder.take().unwrap();
        source_change = source_change.merge(builder.finish());
    }

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
