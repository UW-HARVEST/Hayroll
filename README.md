# The HARVEST Hayroll C Macro to Rust Translation System

Hayroll: HARVEST Annotator for Yielding Regions of Lexical Logic

## Installation

Hayroll has several major dependencies. Please install them before starting to install Hayroll. 
The directory structure should look like this:

```
installation_folder/
├── Hayroll/
├── Maki/
├── tree-sitter/
└── tree-sitter-c_preproc/
```

If you change the relative position of these directories, you should update that in Hayroll's `CMakeLists.txt`.

### C2Rust

https://github.com/immunant/c2rust

Recommended version: 0.19.0

C2Rust is a static-analysis-based C to Rust translations tool. 

Please follow the steps on their README to install it. Hayroll does not need to see C2Rust's installation folder, but requires the `c2rust` command to be reachable via PATH. 

### Maki (Hayroll-modified Version): 

https://github.com/UW-HARVEST/Maki

Recommended version: tag 0.1.0

The original [Maki](https://dl.acm.org/doi/abs/10.1145/3597503.3623323) is a C macro analysis tool. Hayroll modifies it and depends on that modified version. 

Do not install the docker version according to its original README. Instead, please install it locally by following the normal CMake installaton workflow. You can find a `CMakeLists.txt` at its root folder. 

Maki requires the Clang toolchain: `sudo apt install clang-14 llvm-14 libclang-14-dev`.

### Tree-sitter

https://github.com/tree-sitter/tree-sitter

Recommended version: 0.25.3

Tree-sitter is a lightweight parser generator. 

Please simply run `make` after cloning the repo. 

### Hayroll Tree-sitter-c_preproc

https://github.com/UW-HARVEST/tree-sitter-c_preproc

Recommended version: tag 0.1.0

Tree-sitter-c_preproc is a Tree-sitter syntax written by Hayroll for parsing C macros. 

Please simply run `make` after cloning the repo. 

### Libmcs

https://gitlab.com/gtd-gmbh/libmcs

Recommended version: 1.2.0

Libmcs is a math library. Hayroll's test suite relies on it. 

Please run `./configure` and `make` according to its README. 
It is recommended to turn off complex number support when running `./configure`, because C2Rust does not fully support complex number functionalities. 

### Hayroll

https://github.com/UW-HARVEST/Hayroll

Recommended version: tag 0.1.0

Hayroll's core functionalities. Please install it after clearing all the above dependencies, and some minor dependencies: 

- The Rust toolchain: https://www.rust-lang.org/tools/install
- `sudo apt install clang-17 llvm-17 libclang-17-dev libz3-dev libspdlog-dev libboost-stacktrace-dev`

Then run `./build.bash`. After that, you can optionally run tests with `cd ./build; ctest`, which should take less than a minuite to finish. 

## Usage

The `./pipeline` executable (a soft link to `./build/pipeline`) offers a turn-key solution from C source files to Rust files, with macros (partially) preserving their structures.

` ./pipeline <path_to_compile_commands.json> <output_directory>`

### `compile_commands.json`

To build a project, a C build system typically makes multiple calls to the compiler with a long list of arguments. A `compile_commands.json` records these commands and arguments for the convenience of downstream analysis. 

There are multiple ways to generate a `compile_commands.json`, and `bear` (`sudo apt install bear`) is one easy way. For example, to generate for Libmcs, simply run `bear -- make` instead of `make`. Then you will see a `compile_commands.json` like:

```
[
  {
    "arguments": [
      "/usr/bin/gcc",
      "-c",
      "-Wall",
      "-std=c99",
      "-pedantic",
      "-Wextra",
      "-frounding-math",
      "-g",
      "-fno-builtin",
      "-DLIBMCS_FPU_DAZ",
      "-DLIBMCS_WANT_COMPLEX",
      "-Ilibm/include",
      "-Ilibm/common",
      "-Ilibm/mathd/internal",
      "-Ilibm/mathf/internal",
      "-o",
      "build-x86_64-linux-gnu/obj/libm/mathf/sinhf.o",
      "libm/mathf/sinhf.c"
    ],
    "directory": "/home/<username>/libmcs",
    "file": "/home/<username>/libmcs/libm/mathf/sinhf.c",
    "output": "/home/<username>/libmcs/build-x86_64-linux-gnu/obj/libm/mathf/sinhf.o"
  },
  ...
]
```

It's recommended to manually keep only the source files that you want to translate before sending the `compile_commands.json` to Hayroll `./pipeline`. In the Libmcs case, since Hayroll (C2Rust underneath) does not have full support for complex numbers, it's recommended to delete entires for any source files under `libm/complexf/`. 

### Output

`./pipeline` overwrites the output directory. You will see several intermediate files for each translation task. 

- `xxx.c`: The C source file. 
- `xxx.cu.c`: The C compilation unit source file. This is `xxx.c` with all necessary `#include`s copy-pasted into a single file, which we call the compilation unit file. A compilation unit file is standalone compilable. 
- `xxx.cpp2c`: Maki's macro analysis result on `xxx.cu.c`.
- `xxx.seeded.cu.c`: `xxx.cu.c` with Hayroll's macro info tags (seeds) inserted (seeded). 
- `xxx.seeded.rs`: `xxx.seeded.cu.c` translated to Rust by C2Rust, where C macros were expanded and translated as-is, together with Hayroll's seeds. 
- `xxx.rs`: The final output, Rust code with previously expanded C macro sections reverted into Rust functions or Rust macros.
