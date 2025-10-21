use std::{collections::HashMap, fs, path::Path};

use anyhow::Result;
use ide_db::base_db::SourceDatabase;
use load_cargo;
use project_model::CargoConfig;
use syntax::{ast::SourceFile, AstNode};
use tracing::{debug, info};
use vfs::FileId;

use crate::hayroll_ds::{
    extract_hayroll_seeds_from_syntax_roots, CodeRegion, HayrollMeta, HayrollSeed,
};
use crate::util::{apply_source_change, collect_syntax_roots_from_db, SourceChangeBuilderSet};

pub fn run(workspace_path: &Path) -> Result<()> {
    let cargo_config = CargoConfig::default();
    let load_cargo_config = load_cargo::LoadCargoConfig {
        load_out_dirs_from_check: false,
        with_proc_macro_server: load_cargo::ProcMacroServerChoice::None,
        prefill_caches: false,
    };

    let (mut db, vfs, _proc_macro) =
        load_cargo::load_workspace_at(workspace_path, &cargo_config, &load_cargo_config, &|_| {})?;

    // Pass 1: remove expression seeds by peeling their guard wrappers.
    {
        let syntax_roots: HashMap<FileId, SourceFile> = collect_syntax_roots_from_db(&db);
        let mut builder_set = SourceChangeBuilderSet::from_syntax_roots(&syntax_roots);
        let hayroll_seeds = extract_hayroll_seeds_from_syntax_roots(&syntax_roots);
        let seeds_by_file =
            group_and_order_seeds(hayroll_seeds, |seed| matches!(seed, HayrollSeed::Expr(_)));
        let total_expr: usize = seeds_by_file.values().map(|seeds| seeds.len()).sum();
        info!(count = total_expr, "Removing expression hayroll seeds");

        apply_expr_seed_edits(&mut builder_set, &syntax_roots, seeds_by_file);

        let source_change = builder_set.finish();
        apply_source_change(&mut db, &source_change);
    }

    // Pass 2: remove statement seeds by deleting the begin/end tag statements precisely.
    let syntax_roots: HashMap<FileId, SourceFile> = collect_syntax_roots_from_db(&db);
    let mut builder_set = SourceChangeBuilderSet::from_syntax_roots(&syntax_roots);
    let hayroll_seeds = extract_hayroll_seeds_from_syntax_roots(&syntax_roots);

    for seed in hayroll_seeds.into_iter() {
        let HayrollSeed::Stmts(..) = seed else {
            continue;
        };
        let code_region = seed.get_raw_code_region(true);
        let CodeRegion::Stmts { parent, range } = code_region else {
            continue;
        };

        let begin_stmt = parent.statements().nth(*range.start()).unwrap();
        let end_stmt = parent.statements().nth(*range.end()).unwrap();

        let file_id = seed.file_id();
        let mut editor = builder_set.make_editor(parent.syntax());
        editor.delete(begin_stmt.syntax());
        editor.delete(end_stmt.syntax());
        builder_set.add_file_edits(file_id, editor);
    }

    let source_change = builder_set.finish();
    apply_source_change(&mut db, &source_change);

    for file_id in syntax_roots.keys() {
        let file_path = vfs.file_path(*file_id);
        debug!(file = %file_path, "Writing cleaned file to disk");
        let code = db.file_text(*file_id).to_string();
        let code = if code.ends_with('\n') {
            code
        } else {
            code + "\n"
        };
        let path = file_path.as_path().unwrap();
        fs::write(path, code)?;
    }

    Ok(())
}

fn group_and_order_seeds<F>(
    seeds: Vec<HayrollSeed>,
    predicate: F,
) -> HashMap<FileId, Vec<HayrollSeed>>
where
    F: Fn(&HayrollSeed) -> bool,
{
    let mut grouped: HashMap<FileId, Vec<HayrollSeed>> = HashMap::new();
    for seed in seeds.into_iter() {
        if predicate(&seed) {
            let file_id = seed.file_id();
            grouped.entry(file_id).or_default().push(seed);
        }
    }

    for seeds in grouped.values_mut() {
        seeds.sort_by_key(|seed| seed.first_tag().literal.syntax().text_range().start());
        seeds.reverse();
    }

    grouped
}

fn apply_expr_seed_edits(
    builder_set: &mut SourceChangeBuilderSet,
    syntax_roots: &HashMap<FileId, SourceFile>,
    seeds_by_file: HashMap<FileId, Vec<HayrollSeed>>,
) {
    for (file_id, seeds) in seeds_by_file.into_iter() {
        if seeds.is_empty() {
            continue;
        }
        let Some(root) = syntax_roots.get(&file_id) else {
            continue;
        };
        let mut editor = builder_set.make_editor(root.syntax());
        let mut touched = false;
        for seed in seeds {
            let code_region = seed.get_raw_code_region(true);
            let replacement_region = code_region.peel_tag().clone_for_update();
            let range = code_region.syntax_element_range();
            let replacement_elements = replacement_region.syntax_element_vec();
            editor.replace_all(range, replacement_elements);
            touched = true;
        }
        if touched {
            builder_set.add_file_edits(file_id, editor);
        }
    }
}
