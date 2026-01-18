use anyhow::Result;
use hayroll::{cleaner_core, util};
use std::{env, path::Path};
use tracing::error;

fn main() -> Result<()> {
    let args: Vec<String> = env::args().collect();
    let mut keep_src_loc = false;
    let mut workspace_arg: Option<String> = None;

    for arg in args.iter().skip(1) {
        if arg == "--keep-src-loc" {
            keep_src_loc = true;
        } else if workspace_arg.is_none() {
            workspace_arg = Some(arg.clone());
        } else {
            error!(usage = %format!("Usage: {} <workspace-path> [--keep-src-loc]", args[0]));
            std::process::exit(1);
        }
    }

    if workspace_arg.is_none() {
        error!(usage = %format!("Usage: {} <workspace-path> [--keep-src-loc]", args[0]));
        std::process::exit(1);
    }

    util::init_logging();

    let workspace_path = Path::new(workspace_arg.as_ref().unwrap());
    cleaner_core::run(workspace_path, keep_src_loc)
}
