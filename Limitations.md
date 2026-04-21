# Hayroll Limitations

This document summarizes the macro patterns and program structures that the current Hayroll prototype does not reconstruct as Rust abstractions.

## Macro patterns not reconstructed

**AST node kinds unsupported by Seeder.** Seeder only annotates code regions whose `astKind` is one of `Expr`, `Stmt`, `Stmts`, `Decl`, or `Decls`. Invocations whose expansion falls outside this set are not seeded and therefore cannot be reconstructed. In practice this includes:

- Type nodes (macros that expand to a type in a declarator, cast, or generic context).
- Integer-constant-expression-only contexts (e.g., `case` labels, bit-field widths, array sizes) when the macro itself is what plays that role.
- Token pasting (`##`) and stringification (`#`) inside macro bodies.
- Macros whose expansion is a function pointer.
- Identifier-only arguments. A parameter that is used purely as an identifier (e.g., `#define DECLARE_COUNTER(name) int name = 0;`) corresponds to an AST kind that Seeder does not currently annotate. The macro is expanded but not rebuilt into a `macro_rules!`.

**Nested macro definitions.** A macro whose body invokes another macro whose expansion Maki cannot reliably analyze is not reconstructed. Hayroll conservatively leaves such expansions expanded.

**Macros whose definitions vary across `#ifdef` branches.** A definition that changes between configurations is not handled by the current macro layer. Extending this would require the macro layer to consume premise information from the conditional-compilation layer so that each invocation is tied to the configuration under which its definition applies.

**Non-syntactical macros.** Non-syntactical macros (those that are not a well-formed `Expr`/`Stmt`/`Decl` region) are deliberately not reconstructed. They remain as C2Rust-expanded code and are still functionally correct. Pioneer treats them as opaque tokens, Maki still observes their expansions, and Seeder simply emits no seed for them. If a non-syntactical macro interacts with conditional compilation, correctness may depend on the specific pattern; the only such pattern we observed in benchmarks was C++ compatibility wrappers (`extern "C"` fragments) which we conservatively ignored.

## Pioneer scaling

Pioneer's symbolic execution does not scale to exhaustively explore the full configuration space of larger codebases. For zlib, whitelist mode (explicitly selected configurations) works; exhaustive exploration triggers state explosion. Codebases with a comparably large `#ifdef` fan-out should expect to use whitelist mode rather than full enumeration.

## Scope of the current prototype

The prototype is implemented against C2Rust specifically. The design is translator-agnostic, but other translators (especially LLM-based or heavily optimizing ones) may not preserve the syntactic footprint that Seeder relies on, in which case seeds can fail to align with the translated Rust. A source-correspondence interface supplied by the underlying translator would remove the dependency on Seeder's current strategy.
