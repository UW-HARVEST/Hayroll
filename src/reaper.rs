use anyhow::Result;
use hayroll::{reaper_core, util};
use std::{env, path::Path};
use tracing::error;

fn main() -> Result<()> {
    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        error!(usage = %format!("Usage: {} <workspace-path>", args[0]));
        std::process::exit(1);
    }

    util::init_logging();

    let workspace_path = Path::new(&args[1]);
    reaper_core::run(workspace_path)
}
