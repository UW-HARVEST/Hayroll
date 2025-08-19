<img src="images/hayroll-200x200.png" align="right" width="200px"/>

# Hayroll: translate C macros to Rust

Hayroll converts C macros into Rust code.  Hayroll wraps the [c2rust](https://github.com/immunant/c2rust) tool for converting code written in C to the Rust programming language, improving c2rust's translation of C preprocessor macros and conditional compilation.  The `hayroll` command is a drop-in replacement for c2rust.  "Hayroll" stands for "**H**ARVEST **A**nnotator for **Y**ielding **R**egions **O**f **L**exical **L**ogic".


## Example output of c2rust and Hayroll

The `c2rust` program runs the C preprocessor before translating from C to Rust.  This means that macros are expanded (which destroys abstractions that the programmer put in the code) and that conditionally-compiled code is lost.

For example, consider translating this C code (note especially the code comments):

```c
float sinhf(float x) {
#ifdef __LIBMCS_FPU_DAZ               // conditional compilation
    x *= __volatile_onef;             // conditional compilation
#endif                                // conditional compilation
    float t, w, h;
    int32_t ix, jx;
    GET_FLOAT_WORD(jx, x);            // statement macro
    ix = jx & 0x7fffffff;
    if (!FLT_UWORD_IS_FINITE(ix)) {   // expression macro
        return x + x;
    }

    h = 0.5f;
    ...
```

The output of `c2rust` is (note again the code comments):

```rust
pub unsafe extern "C" fn sinhf(mut x: libc::c_float) -> libc::c_float {
                                      // conditionally compiled code is lost
    let mut t: libc::c_float = 0.;
    let mut w: libc::c_float = 0.;
    let mut h: libc::c_float = 0.;
    let mut ix: int32_t = 0;
    let mut jx: int32_t = 0;
    loop {                            // statement macro is expanded
        let mut gf_u = ieee_float_shape_type { value: 0. };
        gf_u.value = x;
        jx = gf_u.word as int32_t;
        if !(0 as libc::c_int == 1 as libc::c_int) {
            break;
        }
    }                                 // ... end of statement macro expansion
    ix = jx & 0x7fffffff as libc::c_int;
    if !((ix as libc::c_long) < 0x7f800000 as libc::c_long) { // expr macro expanded
        return x + x;
    }
    h = 0.5f32;
    ...
```

By contrast, the output of Hayroll is (the code comments highlight improvements):

```rust
pub unsafe extern "C" fn sinhf(mut x: libc::c_float) -> libc::c_float {
    #[cfg(feature = "__LIBMCS_FPU_DAZ")] // (WIP) conditional compilation is retained
    { x *= __volatile_onef; }            // (WIP) conditional compilation is retained
    let mut t: libc::c_float = 0.;
    let mut w: libc::c_float = 0.;
    let mut h: libc::c_float = 0.;
    let mut ix: int32_t = 0;
    let mut jx: int32_t = 0;
    GET_FLOAT_WORD(&mut jx, &mut x);     // statement macro becomes a function
    ix = jx & 0x7fffffff as libc::c_int;
    if FLT_UWORD_IS_FINITE(&mut ix as *mut int32_t) == 0 { // expr macro is function
        return x + x;
    }
    h = 0.5f32;
```


## Installation

Ensure that only one version of LLVM and Clang are installed on your computer,
and that they are the same version and not version 20.
(This is necessary for c2rust.)

Install Rust, if it is not already installed:
```
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
```

Now, install Hayroll:

```
git clone https://github.com/UW-HARVEST/Hayroll
cd Hayroll
./prerequisites.bash
./build.bash
# Optionally, run tests (takes less than one minute):
cd ./build && ctest
```

The `prerequisites.bash` script has been tested on Ubuntu.
For installation on other operating systems, follow the instructions in [prerequisites.md](prerequisites.md), and please contribute back your instructions or a pull request to make `prerequisites.bash` work on more operating systems.  Thanks!

## Usage

### `compile_commands.json`

To build a project, a C build system typically makes multiple calls to the
compiler with a long list of arguments. A `compile_commands.json` records these
commands and arguments for the convenience of downstream analysis.

An easy way to generate a `compile_commands.json` file is to run
```
make clean && bear -- make
```

### Skipping some C files

You should manually delete entires in `compile_commands.json`
that point to any source files that you do not want to translate.

### Calling Hayroll

The `./hayroll` executable offers a turn-key solution from C source
files to Rust files, with macros (partially) preserving their
structure.

```
 <path_to_Hayroll>/hayroll <path_to_compile_commands.json> <output_directory>
```

### Output

`./hayroll` overwrites the output directory. You will see several intermediate
files for each original C file.

- `xxx.c`: The C source file.
- `xxx.cu.c`: The C compilation unit source file. This is `xxx.c` with all necessary `#include`s copy-pasted into a single file, which we call the compilation unit file. A compilation unit file is standalone compilable.
- `xxx.premise_tree.c`: Output by the Hayroll Pioneer symbolic executor from executing `xxx.c`. For each line of code, it shows the compilation flag combination required for the code to survive conditional compilation macros.
- `xxx.cpp2c`: Maki's macro analysis result on `xxx.cu.c`.
- `xxx.seeded.cu.c`: `xxx.cu.c` with Hayroll's macro info tags (seeds) inserted (seeded).
- `xxx.seeded.rs`: `xxx.seeded.cu.c` translated to Rust by C2Rust, where C macros were expanded and translated as-is, together with Hayroll's seeds.
- `xxx.rs`: The final output, Rust code with previously expanded C macro sections reverted into Rust functions or Rust macros.


### Example of running Hayroll

This section shows how to run Hayroll on version 1.2.0 of the [LibmCS mathematical library](https://gitlab.com/gtd-gmbh/libmcs).

#### Clone and configure LibmCS

If you installed Hayroll via `prerequisites.bash`, this step should have already been done automatically.

```
git clone --branch 1.2.0 https://gitlab.com/gtd-gmbh/libmcs.git
cd libmcs
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
```

#### Create `compile_commands.json`

```
make clean && bear -- make
```

This command creates a `compile_commands.json` file of this form:

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

#### Remove some entries from `compile_commands.json`

LibmCS uses complex numbers, but c2rust does not have full support for complex numbers.
If you installed and configured LibmCS via `prerequisites.bash`, it is expected that no entires in `compile_commands.json` should point to source files under `libm/complexf/`.
In case you configured it manually, please remove such entries.

#### Run Hayroll

```
/PATH/TO/hayroll compile_commands.json hayroll-output/
```

In the `hayroll-output/` directory, you will find files such as `xxx.rs`.

#### Compare Hayroll and c2rust output

To see the difference between Hayroll and c2rust output, you can use `diff` to compare the intermediate and final Rust files:

```
diff hayroll-output/xxx.seeded.rs hayroll-output/xxx.rs
```

- `xxx.seeded.rs` is generated by c2rust and contains Rust code with C macros expanded and tagged by Hayroll. The macros are expanded as plain code, but Hayroll inserts special markers (seeds) to indicate macro regions.
- `xxx.rs` is the final output from Hayroll, where macros have been extracted and converted into Rust functions or macros based on the tags. This file is typically much simpler and more readable than the direct c2rust output.
