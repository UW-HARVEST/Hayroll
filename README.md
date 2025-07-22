<img src="images/hayroll-200x200.png" align="right" width="200px"/>

# The HARVEST Hayroll C Macro to Rust Translation System

<br clear="left"/>

Hayroll is a system that wraps the [c2rust](https://github.com/immunant/c2rust) tool for converting code written in C to the Rust programming language.  Hayroll does a better job of translating C preprocessor macros and conditional compilation.  Hayroll is a drop-in replacement for c2rust.  "Hayroll" stands for "*H*ARVEST *A*nnotator for *Y*ielding *R*egions *O*f *L*exical *L*ogic".




## Installation

### Dependencies

Hayroll has several major dependencies. Please install them before starting to install Hayroll.
You can either use our automated script `prerequisites.bash`, of follow the following steps manually. 
After that, the directory structure should look like this:

```
installation_folder/
├── Hayroll/
├── Maki/
├── tree-sitter/
└── tree-sitter-c_preproc/
```

#### C2Rust

Tested version: 0.19.0

[C2Rust](https://github.com/immunant/c2rust) is a static-analysis-based C-to-Rust translation tool.

Please follow the steps on their [README](https://github.com/immunant/c2rust/blob/master/README.md#installation) to install it.  You may need to use the "Installing from Git" instructions rather than the "Installing from crates.io" instructions.  Hayroll does not need to see C2Rust's installation folder, but the `c2rust` command must be on your PATH.

#### Maki (Hayroll-modified Version):

Tested version: tag 0.1.1

[Maki](https://github.com/UW-HARVEST/Maki) is a C macro analysis tool. Hayroll uses a modified version.

Please follow the "Local Setup (Required by Hayroll)" section in Maki's README. Do not use the docker version. 

#### Z3

Tested version: 4.13.4

[Z3](https://github.com/Z3Prover/z3) is an automated theorem prover and a SAT solver.

Please follow the common CMake installation workflow, or read `README-CMake.md` which can be found at Z3's root folder. Do not forget to run `sudo make install` as the last step. Hayroll does not need to see Z3's folder, but looks for required libraries in system paths.

#### Tree-sitter

Tested version: 0.25.3

[tree-sitter](https://github.com/tree-sitter/tree-sitter) is a lightweight parser generator.

```
git clone https://github.com/tree-sitter/tree-sitter.git
make -C tree-sitter
```

#### Hayroll Tree-sitter-c_preproc

Tested version: tag 0.1.1

[tree-sitter-c_preproc](https://github.com/UW-HARVEST/tree-sitter-c_preproc) is a parser for C macros.

```
git clone https://github.com/UW-HARVEST/tree-sitter-c_preproc.git
make -C tree-sitter-c_preproc
```

#### Libmcs

Tested version: 1.2.0

[Libmcs](https://gitlab.com/gtd-gmbh/libmcs) is a math library. Hayroll's test suite uses it.

It is recommended to turn off complex number support when running `./configure`, because C2Rust does not fully support complex number functionalities. `./configure` opens an interactive menu that leads you through such options. 

```
git clone https://gitlab.com/gtd-gmbh/libmcs.git
cd libmcs
./configure
make
```

### Hayroll

https://github.com/UW-HARVEST/Hayroll

Tested version: tag 0.1.1

Hayroll's core functionalities. Please install it after installing all the above dependencies, and some minor dependencies: `sudo apt install libspdlog-dev libboost-stacktrace-dev`

Then run `./build.bash`. After that, you can optionally run tests with `cd ./build; ctest`, which should take less than a minuite to finish.

## Usage

The `./pipeline` executable (a soft link to `./build/pipeline`) offers a turn-key solution from C source files to Rust files, with macros (partially) preserving their structures.

` ./pipeline <path_to_compile_commands.json> <output_directory>`

### `compile_commands.json`

To build a project, a C build system typically makes multiple calls to the compiler with a long list of arguments. A `compile_commands.json` records these commands and arguments for the convenience of downstream analysis.

There are multiple ways to generate a `compile_commands.json`, and `bear` (`sudo apt install bear`) is one easy way. For example, to generate for Libmcs, simply run `make clean; bear -- make`. Then you will see a `compile_commands.json` like:

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
