# The Hayroll Reaper: From Seeded Rust to Reconstructed Macros/Functions

## Purpose

Reaper consumes the Rust code produced by C2Rust after Seeder has inserted seeds
to macro invocation and macro argument positions. It groups related seeds,
checks invocation and argument compatibility, and then reconstructs each macro
expansion either as a Rust `macro_rules!` or as a regular Rust `fn` with a
calling convention that preserves lvalue/rvalue semantics. The output aims to be
readable Rust while keeping the original C behaviour.

## Inputs & Outputs

**Input**: A Rust file translated by C2Rust from a seeded C compilation unit
(`*.seeded.rs`). The file contains byte-string literals that encode Hayroll tag
JSON (inserted by Seeder), and C2Rust adds `#[c2rust::src_loc = "line:col"]`
attributes to top-level items.

**Output**: An edited Rust file where each seeded macro expansion is either (1)
replaced by a call to a newly synthesized Rust function, or (2) replaced by an
invocation of a synthesized `macro_rules!` macro, or (3) left as-is.

**Design intent**: Never change observable program behavior. Reconstruction only
reshapes already-expanded code into macro/function syntax; side effects and
value categories (lvalue/rvalue) are preserved.

## Core Data Structures

This section names the logical data structures and shows how they connect
bottom-up using fields carried in tags.

### `HayrollTag`

A `HayrollTag` is the parsed JSON object embedded in a `b"..."` byte‑string
literal in the Rust AST generated from a seed. The tag fields encode the
information that the Reaper needs to match seeded macro regions back to macro
invocations. See the Tag section in `README-seeder.md` for the full field list
and semantics.

Construction process:

Reaper scans all byte‑string literals in the input file and identifies as
`HayrollTag` those who parse into JSON objects and has a `hayroll` field.

### `HayrollSeed`

A `HayrollSeed` refers to the complete instrumentation structure inserted into
the source code. There are three kinds of `HayrollSeed`s by `astKind`: `Expr`,
`Stmt(s)`, and `Decl(s)`. A Seed designates one of those kinds of AST nodes that
was either expanded from a macro or a macro argument.

`HayrollSeed`s are constructed from `HayrollTag`s. For `Expr` and `Decl(s)`
seeds, each seed corresponds to only one `HayrollTag`. A `Stmt(s)` seeds
corresponds to a pair of `HayrollTag`s, one before the statement(s) and one
after, which are paired by matching their `locBegin` fields. The metadata that
the tags say about the macro invocation or macro argument is considered the
properties of the seed.

### `HayrollMacroInv`

A `HayrollMacroInv` represents one macro invocation and all its related
arguments as a composite of seeds: the single **invocation body seed** plus all
**argument seeds** grouped by formal parameter name.

Data structure:

- `body_seed: HayrollSeed`: the seed that covers the entire expansion body of
  the invocation.
- `arg_seeds: Vec<(arg_name: String, Vec<HayrollSeed>)>`: for each formal
  parameter name, the list of argument occurrence seeds inside the invocation
  body (multiple occurrences of the same argument can appear).

Construction process:

1. Identify invocation seeds (`isArg == false`).
2. For every argument seed (`isArg == true`), match it to the invocation whose
   body seed has `locBegin == arg.locRefBegin` (`locRefBegin` on an argument
   seed always stores the `locBegin` of its parent invocation seed, see
   `README-seeder.md`).
3. Insert the argument seed into that invocation’s `arg_seeds` entry keyed by
   its `name` (which must be one of the body seed’s `argNames` in order).

### `HayrollMacroInvCluster`

A `HayrollMacroInvCluster` groups all `HayrollMacroInv` objects that originate
from the **same macro definition site**. It is the unit on which Reaper decides
"function vs. macro vs. skip".

Data structure:

- `def_anchor: String`: the macro definition anchor (`locRefBegin`) shared by
  every invocation’s body seed in the cluster (stable per original macro
  definition).
- `invocations: Vec<HayrollMacroInv>`: all invocation composites whose body
  seed’s `locRefBegin == def_anchor`.

Construction process:

1. Start from the full list of `HayrollMacroInv`.
2. Partition them by `inv.seed.locRefBegin` (the body seed’s definition
   anchor). Each partition becomes one cluster; that anchor becomes
   `def_anchor`.

## Reconstruction Categories based on Compatibility Levels

Reconstruction involves folding multiple macro invocations that sourced from the
same macro definition site, either into a Rust function or a macro, before which
Reaper needs to check the **structural** and **type** compatibility across
invocations.

### Structural Compatibility

- Definition: Same `astKind` for the invocation seeds across invocations; for
  each formal parameter, all of its uses across an invocation share the same
  `astKind`. (For example, an `x` that is always used as `Expr` vs. sometimes as
  `Stmt` would fail.)
- What it enables: If **all** invocations in a cluster are structurally
  compatible, they can be reconstructed as a `macro_rules!` macro (token-level
  substitution with `:expr` / `:stmt` matchers).

### Type Compatibility (Stronger)

- Definition: Structural compatibility **and**, for every parameter occurrence
  that is an `Expr`, the **types** match across uses and across invocations;
  also the invocation bodies themselves are type-compatible. (Type is deduced
  from the expression context already present in Rust.)
- What it enables: If **all** invocations in a cluster are type-compatible
  **and** each invocation is individually eligible (`canBeFn=true`, an analysis
  result from Maki, marking that the macro is free from side effects), the
  cluster can be reconstructed as a **Rust `fn`**.

### Decision Lattice

- **Cluster -> `fn`** when: (i) cluster is type-compatible; (ii) every
  invocation has property `canBeFn=true`.
- **Else, Cluster -> `macro_rules!`** when: (i) cluster is structurally compatible.
- **Else**: leave the expanded code as-is (emit a warning and skip).

## Lvalue/Rvalue-Aware Calling Convention (for `fn` Reconstruction)

For the function case, Reaper picks a single, stable API per cluster that works
across all call sites while preserving lvalue/rvalue semantics:

- For each parameter position `i`, scan all invocations and all uses of that
  parameter within each invocation.
  - If **every** occurrence is supplied an **lvalue** (`isLvalue=true`),
    parameter `i` is an **lvalue parameter** and the synthesized function takes
    `*mut T` (unsafe pointer). At call sites Reaper passes `&mut ARG_EXPR`
    (address-of), so the callee can mutate it.
  - If **any** occurrence is supplied an **rvalue**, parameter `i` becomes an
    **rvalue parameter** and the function takes plain `T`. Call sites pass the
    expression value directly.
- The reconstructed function’s **return** is modeled as an **rvalue** value `R`
  unless the invocation body itself was tagged as an lvalue expression in
  **any** invocation; in that case, Reaper wraps the call in a leading `*` at
  the call site to preserve the lvalue use context. (The synthesized `fn` still
  returns an rvalue; the lvalue context is restored at the call site.)

This rule yields a single function type usable at all call sites without losing
mutability or requiring extra mutability.
