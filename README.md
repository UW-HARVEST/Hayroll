<img src="images/hayroll-200x200.png" align="right" width="200px"/>

# The HARVEST Hayroll C macro to Rust translation system

Hayroll is a system that wraps the [c2rust](https://github.com/immunant/c2rust) tool for converting code written in C to the Rust programming language.  Hayroll does a better job of translating C preprocessor macros and conditional compilation.  The `hayroll` command is a drop-in replacement for c2rust.  "Hayroll" stands for "*H*ARVEST *A*nnotator for *Y*ielding *R*egions *O*f *L*exical *L*ogic".


## Installation

To install Hayroll:

```
git clone https://github.com/UW-HARVEST/Hayroll
cd Hayroll
./prerequisites.bash
./build.bash
# Optionally, run tests (takes less than one minute):
cd ./build && ctest
```

The `prerequisites.bash` script has been tested on Ubuntu.
For installation on other operating systems, follow the instructions in [prerequisites.md](prerequisites.md), and contribute back your instructions or a pull request to make `prerequisites.bash` work on more operating systems.


## Usage

The `./pipeline` executable offers a turn-key solution from C source files to Rust files, with macros (partially) preserving their structures.

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
