use anyhow::Result;
use hayroll::{merger_core, util};
use std::{env, path::Path};
use tracing::error;

fn main() -> Result<()> {
    let args: Vec<String> = env::args().collect();
    let mut keep_src_loc = false;
    let mut base_arg: Option<String> = None;
    let mut patch_arg: Option<String> = None;

    for arg in args.iter().skip(1) {
        if arg == "--keep-src-loc" {
            keep_src_loc = true;
        } else if base_arg.is_none() {
            base_arg = Some(arg.clone());
        } else if patch_arg.is_none() {
            patch_arg = Some(arg.clone());
        } else {
            error!(usage = %format!("Usage: {} <base-workspace-path> <patch-workspace-path> [--keep-src-loc]", args[0]));
            std::process::exit(1);
        }
    }

    if base_arg.is_none() || patch_arg.is_none() {
        error!(usage = %format!("Usage: {} <base-workspace-path> <patch-workspace-path> [--keep-src-loc]", args[0]));
        std::process::exit(1);
    }

    util::init_logging();

    let base_workspace_path = Path::new(base_arg.as_ref().unwrap());
    let patch_workspace_path = Path::new(patch_arg.as_ref().unwrap());
    merger_core::run(base_workspace_path, patch_workspace_path, keep_src_loc)
}
