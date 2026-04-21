# Extending Hayroll with a New Macro Pattern

This is a sketch of where to edit if you want Hayroll to reconstruct a macro pattern it currently leaves expanded (for example, one of the cases listed in [Limitations.md](Limitations.md)). It names the modules and files involved.

A macro flows through three stages that each have to agree on the new pattern: **detection** (Maki), **annotation** (Seeder), and **reconstruction** (Reaper).

## Maki: teach the analyzer to recognize the pattern

Maki is the Clang plugin that classifies each macro expansion. It lives under `dependencies/Maki/src/`, or you can find the repository from [prerequisites.bash](prerequisites.bash). Note that Hayroll uses a modified version of Maki. Do not use the original Maki authors' repository.

The files most relevant to classification are:

- `MacroExpansionNode.{hh,cc}`: the per-expansion record and its properties.
- `MacroForest.{hh,cc}`: builds the tree of expansions.
- `Cpp2CASTConsumer.{hh,cc}`: drives the AST traversal and emits the `.cpp2c` summary consumed downstream.
- `ASTUtils.{hh,cc}`: AST-kind classification helpers.

You need to ensure that the expansion is detected, its AST kind is emitted with a label the rest of the pipeline can key off, and any analysis that gates reconstruction (hygiene, side-effect freedom, lvalue-ness) is extended to your new case. Maki already emits a free-form `ASTKind` string per invocation, so a new label here is the typical entry point.

## Seeder: invent a tag variant and a region-preserving seed

Seeder is at `src/Seeder.hpp`. It reads Maki's `.cpp2c` output and injects seeds into the C compilation unit so that the Rust side can find each macro region after C2Rust translation.

Three things likely need to change:

1. **Tag schema.** Tags are JSON objects embedded as C string literals. The shared field list (including `astKind`, `isLvalue`, `canBeFn`, `argNames`, locations) is documented in [README-seeder.md](README-seeder.md). If your new pattern needs extra semantic metadata (e.g., the identity of an identifier-kind parameter), add a field here.
2. **Seed structure for the new `astKind`.** The three existing families are `Expr` (conditional-expression seeds), `Stmt`/`Stmts` (begin/end sandwich seeds), and `Decl`/`Decls` (sidecar tag variable after the declaration). A new pattern either reuses one of these families or introduces a fourth with its own semantic-preserving C scaffolding. The dispatch lives in `genInstrumentationTasks` in `Seeder.hpp`.
3. **Validation.** Seeder skips invocations whose expansion or definition lies in a non-project header, and drops AST kinds outside its supported set. The filtering checks early in `Seeder.hpp` need to admit the new kind.

The seeded C program must preserve the original observable behavior and type structure so that C2Rust still produces correct Rust.

## C2Rust: normally nothing to do

C2Rust is consumed as-is. The only thing to verify is that whatever C scaffolding Seeder injects survives translation intact (string literals, ternary expressions, sidecar `static`s all do). Aggressive C2Rust options that change lowering would need re-validation; the default configuration used by Hayroll is stable.

## Reaper: reconstruct from the new tag

Reaper is the Rust-side stage. The code is in `src/reaper_core.rs` and the shared data types in `src/hayroll_ds.rs`; the logic is documented in [README-reaper.md](README-reaper.md).

Key extension points:

- `HayrollTag` (in `hayroll_ds.rs`): if you added a tag field, parse it here.
- `HayrollSeed`: currently an enum over `Expr`, `Stmts`, `Decls`. A new `astKind` is either a new variant with its own AST-locator logic (mirroring the existing `parent_until_kind` chains) or an extension of an existing variant.
- `HayrollMacroInv` / `HayrollMacroInvCluster`: the grouping layer is largely pattern-agnostic; usually no change is needed unless your pattern changes how invocations share a definition.
- Compatibility checks (structural, type) and the `fn` vs. `macro_rules!` decision: extend if the new pattern changes what "structurally compatible" means, or if its reconstruction target is neither a Rust function nor `macro_rules!`.
- The synthesis that emits the final Rust `fn` or `macro_rules!`: add a branch for the new `astKind` so it produces the appropriate Rust form.

## 5. Merger and Cleaner: typically pass-through

Merger (`src/merger_core.rs`) folds per-split outputs across configurations. Cleaner (`src/cleaner_core.rs`) strips residual scaffolding. Neither is pattern-aware in the current design, so a new macro pattern normally does not require changes here: but if your seed scaffolding leaks past Reaper, Cleaner is where you would add the matching strip rule.

## 6. Tests

Hayroll's tests live under `tests/`. At minimum, add:

- A small C fixture exercising the new pattern.
- An expected `.rs` output (or golden-file comparison) showing it reconstructs.
- A negative case confirming that structurally similar but unsupported inputs still fall back gracefully to expanded-code form.
