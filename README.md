<img src="images/hayroll-200x200.png" align="right" width="200px"/>

# The HARVEST Hayroll C macro to Rust translator

Hayroll converts C macros into Rust code.  Hayroll wraps the [c2rust](https://github.com/immunant/c2rust) tool for converting code written in C to the Rust programming language, improving c2rust's translation of C preprocessor macros and conditional compilation.  The `hayroll` command is a drop-in replacement for c2rust.  "Hayroll" stands for "**H**ARVEST **A**nnotator for **Y**ielding **R**egions **O**f **L**exical **L**ogic".


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

The `./hayroll` executable offers a turn-key solution from C source files to Rust files, with macros (partially) preserving their structures.

` ./hayroll <path_to_compile_commands.json> <output_directory>`

### `compile_commands.json`

To build a project, a C build system typically makes multiple calls to the compiler with a long list of arguments. A `compile_commands.json` records these commands and arguments for the convenience of downstream analysis.

An easy way to generate a `compile_commands.json` file is to run
```
make clean && bear -- make
```


### Skipping some C files

You should manually delete any source files that you do not want to translate.

### Output

`./hayroll` overwrites the output directory. You will see several intermediate files for each original C file.

- `xxx.c`: The C source file.
- `xxx.cu.c`: The C compilation unit source file. This is `xxx.c` with all necessary `#include`s copy-pasted into a single file, which we call the compilation unit file. A compilation unit file is standalone compilable.
- `xxx.cpp2c`: Maki's macro analysis result on `xxx.cu.c`.
- `xxx.seeded.cu.c`: `xxx.cu.c` with Hayroll's macro info tags (seeds) inserted (seeded).
- `xxx.seeded.rs`: `xxx.seeded.cu.c` translated to Rust by C2Rust, where C macros were expanded and translated as-is, together with Hayroll's seeds.
- `xxx.rs`: The final output, Rust code with previously expanded C macro sections reverted into Rust functions or Rust macros.


### Example

This section shows how to run Hayroll on version 1.2.0 of the [LibmCS mathematical library](https://gitlab.com/gtd-gmbh/libmcs).

#### Clone and build LibmCS

```
git clone --branch 1.2.0 https://gitlab.com/gtd-gmbh/libmcs.git
pushd libmcs >/dev/null
  if [[ ! -f lib/libmcs.a && ! -f build/libmcs.a ]]; then
    echo "[*] Configuring LibmCS (non-interactive)…"
    # Passing an explicit empty string to --cross-compile prevents the script
    # from prompting for a tool-chain path; all other options are disabled to
    # match Hayroll’s requirements.
    ./configure \
        --cross-compile="" \
        --compilation-flags="" \
        --disable-denormal-handling \
        --disable-long-double-procedures \
        --disable-complex-procedures \
        --little-endian
    make -j"$(nproc)"
  fi
popd >/dev/null
```

####


, which uses complex numbers.
Since c2rust does not have full support for complex numbers, delete source files under `libm/complexf/` before running `./hayroll`.




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

<!--  LocalWords:  img src px hayroll ARVEST nnotator ielding egions exical ogic cd ctest md json sudo LibmCS c99 Wextra frounding fno DLIBMCS FPU DAZ Ilibm linux libm complexf cpp2c Maki C2Rust
 -->
