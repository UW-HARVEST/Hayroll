# Hayroll test cases

Run with `./run-tests.sh`, which expects hayroll to be prebuilt in a `build/` directory under the repository root. Individual tests can be run with `./run-test.sh test-name`.

- simple-defines: expected to work on `main`. demonstrates that even without multiple compile commands, hayroll can generate parameterized rust from uses of `#ifdef` and `#else` that wrap top-level definitions and statements
- simple-define-value: expected to work on `main`. demonstrates that hayroll can handle preprocessor symbols set to integers, if they are `#define`d in C source rather than with the `-D` flag
- required-cli-define: does not yet work on `main`: need to import DefineSet from CLI flags and avoid the required define being minimized out
- multiple-commands: does not yet work on `main`: depends on required-cli-define, then needs support for multiple instances of the same file in a compile_commands manifest
- strings-multiple-commands: depends on all of the above and then needs support for lowering string defines to boolean "equality" defines
- sphincs-minimized: minimized version of the `#include xstr(/path/prefix-CPPSYMBOL)` pattern from SPHINCS. depends on all of the above plus would need a workaround for tree-sitter being unable to parse `#include` lines of this form
