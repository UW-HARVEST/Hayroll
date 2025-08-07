# Hayroll dependencies

Hayroll has several major dependencies. Please install them before starting to install Hayroll.
You can either use our automated script `prerequisites.bash`, of follow the following steps manually. 
After that, the directory structure should look like this:

```
installation_folder/
├── Hayroll/
├── Maki/
├── libmcs/
├── tree-sitter/
└── tree-sitter-c_preproc/
```

## C2Rust

Tested version: 0.19.0

[C2Rust](https://github.com/immunant/c2rust) is a static-analysis-based C-to-Rust translation tool.

Please follow the steps on their [README](https://github.com/immunant/c2rust/blob/master/README.md#installation) to install it.  You may need to use the "Installing from Git" instructions rather than the "Installing from crates.io" instructions.  Hayroll does not need to see C2Rust's installation folder, but the `c2rust` command must be on your PATH.

## Maki (Hayroll-modified Version):

Tested version: tag 0.1.3

[Maki](https://github.com/UW-HARVEST/Maki) is a C macro analysis tool. Hayroll uses a modified version.

Please follow the "Local Setup (Required by Hayroll)" section in Maki's README. Do not use the docker version. 

## Z3

Tested version: 4.13.4

[Z3](https://github.com/Z3Prover/z3) is an automated theorem prover and a SAT solver.

Please follow the common CMake installation workflow, or read `README-CMake.md` which can be found at Z3's root folder. Do not forget to run `sudo make install` as the last step. Hayroll does not need to see Z3's folder, but looks for required libraries in system paths.

## Tree-sitter

Tested version: 0.25.3

[tree-sitter](https://github.com/tree-sitter/tree-sitter) is a lightweight parser generator.

```
git clone https://github.com/tree-sitter/tree-sitter.git
make -C tree-sitter
```

## Hayroll Tree-sitter-c_preproc

Tested version: tag 0.1.3

[tree-sitter-c_preproc](https://github.com/UW-HARVEST/tree-sitter-c_preproc) is a parser for C macros.

```
git clone https://github.com/UW-HARVEST/tree-sitter-c_preproc.git
make -C tree-sitter-c_preproc
```

## Libmcs

Tested version: 1.2.0

[Libmcs](https://gitlab.com/gtd-gmbh/libmcs) is a math library. Hayroll's test suite uses it.

It is recommended to turn off complex number support when running `./configure`, because C2Rust does not fully support complex number functionalities. `./configure` opens an interactive menu that leads you through such options. 

```
git clone https://gitlab.com/gtd-gmbh/libmcs.git
cd libmcs
./configure
make
```

# Hayroll

```
git clone https://github.com/UW-HARVEST/Hayroll
cd Hayroll
./build.bash
```

After that, you can optionally run tests, which should take less than a minute to finish:

```
cd ./build && ctest
```
