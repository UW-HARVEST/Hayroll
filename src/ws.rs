use std::{env, path::Path};
use ide::AnalysisHost;
use project_model::CargoConfig;
use load_cargo;
use anyhow::Result;
use syntax::{ast::{self, HasName}, AstNode};

fn main() -> Result<()> {
    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        eprintln!("Usage: {} <workspace-path>", args[0]);
        std::process::exit(1);
    }

    let workspace_path = Path::new(&args[1]);
    let cargo_config = CargoConfig::default();
    let load_cargo_config = load_cargo::LoadCargoConfig {
        load_out_dirs_from_check: false,
        with_proc_macro_server: load_cargo::ProcMacroServerChoice::None,
        prefill_caches: false,
    };
    let (db, vfs, _proc_macro) = load_cargo::load_workspace_at(workspace_path, &cargo_config, &load_cargo_config, &|_| {})?;
    let analysis_host = AnalysisHost::with_database(db);
    let analysis = analysis_host.analysis();

    let file_ids: Vec<vfs::FileId> = vfs
    .iter()
    .filter_map(|(file_id, path)| {
        if let Some(_) = path.as_path() {
            Some(file_id)
        } else {
            None
        }
    })
    .collect();

    for file_id in file_ids {
        // Print filename and path

        let file_path = vfs.file_path(file_id);
        println!("File: {}", file_path);

        let _line_index = analysis.file_line_index(file_id).unwrap();
        let _content = analysis.file_text(file_id).unwrap();
        let syntax = analysis.parse(file_id).unwrap();
        let functions = syntax.syntax()
            .descendants()
            .filter_map(|node| {
                if let Some(fn_def) = ast::Fn::cast(node) {
                    Some(fn_def)
                } else {
                    None
                }
            });

        for function in functions {
            let name = function.name().map(|n| n.to_string()).unwrap_or_else(|| "<anonymous>".to_string());
            println!("Function: {}", name);
        }
    }
    Ok(())
}