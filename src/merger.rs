use anyhow::Result;
use hayroll::{merger_core, util};
use std::{env, path::Path};
use tracing::error;

fn main() -> Result<()> {
    let args: Vec<String> = env::args().collect();
    if args.len() < 3 {
        error!(usage = %format!("Usage: {} <base-workspace-path> <patch-workspace-path>", args[0]));
        std::process::exit(1);
    }

    util::init_logging();

    let base_workspace_path = Path::new(&args[1]);
    let patch_workspace_path = Path::new(&args[2]);
    merger_core::run(base_workspace_path, patch_workspace_path)
}
