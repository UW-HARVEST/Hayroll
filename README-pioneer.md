# The Hayroll Pioneer Symbolic Executor for C Preprocessor Directives

## Background

When compiling C code, a certain set of macros are defined. These depend on the
OS, architecture, and `-D<MACRO_NAME>=<MACRO_VALUE>` command-line
arguments. Macros can selectively enable program features (including further
macro definitions). Disabled features are removed by conditional compilation
directives during preprocessing, incurring no run-time overhead.

The existing `c2rust` translation framework operates on preprocessed C code,
meaning it translates only the code enabled by one setting of macro definitions
(e.g., user-specified `-D` flags). The remaining code paths and
configurability are lost in the resulting Rust code.

The condition for inclusion of each line can be expressed as a
boolean/arithmetic expression, based on the kinds of computation that is allowed
in preprocessing. Pioneer symbolically executes C preprocessor directives to
compute such inclusion conditions for each line of code (represented with an AST
node in Pioneer's output).

Pioneer is part of the Hayroll pipeline. According to Pioneer's output, Hayroll
runs the preprocessor multiple times with different macro definitions, runs
`c2rust` on each, and then merges all the resulting Rust programs into a single
Rust implementation that uses Rust's `#cfg` macros. The algorithm that decides
what macro definition conbinations to run with and how many total runs are
needed is still work-in-progress.

## Input and Output

Pioneer is provided as a C++ library. The input is a path to the source file, 
and the outputs are in-memory data structures that will be described later. 

- **Input:** A single C compilation unit.
- **Output:**
  - For each line of code (represented with an AST node): the premise (symbolic
    boolean/arithmetic expression) under which the line survives preprocessing.
  - For each macro expansion site within pure C code lines (represented with the
    line:col coordinate of the starting byte of the expansion):
    - For each possible macro definition that may expand at this site: the
      premise under which this definition is expanded.

Note: The actual output of the program is structured as a hierarchical premise tree rather than a direct mapping from each line to its premise.
## Core Concepts

### Symbolic Value and Expression

Symbolic values express the definedness and value of user-defined macros (`-D`). We assume such macros are defined to integers, which is the common case. Pioneer does not explore potential executions from non-integer `-D` values. Each macro is associated with two symbolic values:

- Boolean value (`defMACRO_NAME`) indicating whether the macro is defined.
- Integer value (`valMACRO_NAME`) representing its numeric value.

These two values are distinct because the C macro system allows testing a macro's definedness independently from its value.

A macro that is defined by a `#define` directive in program text or a system
header is not represented with symbolic values, as the user cannot customize its
value with `-D` input. While the user input may affect whether this directive is
reached via conditional macros, that is handled by the State data structure
introduced later.

A _symbolic expression_ is constructed from symbolic values or constants through
boolean logic (`&&`, `||`, `!`) and integer arithmetic operations (`+`, `-`,
`*`, `/`, `()?():()`, and comparisons). All premises are symbolic expressions.

### Program Point

The input to the C preprocessor is an if-then "program" without loops, made up
of sequential execution and conditional branches.  Preprocessor directives, such
as `#define` and `#if`, are the bones of this program, while non-preprocessor C
lines do not affect its control flow.

Pioneer represents this program by an Abstract Syntax Tree (AST).  An AST node
either stands for a preprocessor directive, or is a `c_tokens` node, meaning a
contiguous set of non-preprocessor lines, represented as an unstructured list of
C tokens. Pioneer contains a `tree-sitter-c_preproc` grammar to parse this AST.

For simplicity, this document ignores `#include` directives, assuming they have
been inlined -- an entire compilation unit is a single AST. The underlying
implementation maintains a data structure that acts as a portal between
`#include` and root nodes in different ASTs, in order to achieve that single-AST
abstraction.

A **Program Point** is the beginning of a preprocessor line.  Program Points are
represented using AST nodes, meaning the line start of that AST node.  This AST
node could either stand for a preprocessor directive or be a `c_tokens` node.
Technically speaking, `c_token` nodes cannot be executed like preprocessor
directives, but including them in the AST/ProgramPoint representations would
make it easier to collect information about macro expansions in our algorithm.

For example, the following C program:

```
#ifndef HEADER_GUARD
#define HEADER_GUARD
int f();
#endif
```

is parsed into this AST:

```
preproc_ifndef "#ifndef HEADER_GUARD [then] #endif"
  then:
    preproc_define "#define HEADER_GUARD"
    c_tokens "int f();"
```

### Symbol Table

Pioneer maintains a symbol table mapping program-defined macro names to their
definitions (token sequences). Macros defined in both the target project and
system headers are included. The expansion of macros depends on this symbol
table, effectively substituting macro names with their bodies if defined, or
leaving the macro name intact if undefined.

Formally, a symbol table can be represented as a partial function:

$$
\sigma : String \rightarrow ProgramPoint
$$

indicating the mapping from macro names to their definition nodes (the `#define` line). 

### State

Pioneer finds all possible outputs of a C preprocessor execution by exploring
all its execution states. In Pioneer, a **State** is defined as:

$$
s: State = (n: ProgramPoint, \sigma: SymbolTable, p: Premise)
$$

Pioneer keeps a set of States during execution. A certain State being in the
set means that, when the user-provided macro definitions satisfy its $p$ (a symbolic expression), a
concrete preprocessor execution will pass by this $n$, at which time macros in
$\sigma$ are defined. Pioneer explores the reachable state space according to
its algorithm.

## Symbolic Executor Algorithm

### Notations

- $F : ProgramPoint \rightarrow (SymbolTable, Premise) \rightarrow List(State)$ turns $n : ProgramPoint$ into the transition
  function associated with the AST node immediately after $n$.

- $next : ProgramPoint \rightarrow ProgramPoint$ identifies the next
  ProgramPoint according to control flow sequence. 
  It only accepts non-conditional preprocessor directives. 
  The last ProgramPoint is
  repesented with `EOF`.

- $nextThen : ProgramPoint \rightarrow ProgramPoint$ and $nextElse :
  ProgramPoint \rightarrow ProgramPoint$ refer to the initial ProgramPoints of
  the respective branches following an `#if` directive.
  They only accept conditional preprocessor directives. 

- Given a symbol table $\sigma$, the function $eval_\sigma : Tokens \rightarrow
  Premise$ evaluates tokens into a symbolic expression within the context of
  $\sigma$.

### Algorithm Pseudocode

```
Input: astRoot: ProgramPoint

worklist = {(astRoot, emptySymbolTable, true)} : List(State)
lineToPremise = [] : ProgramPoint -> Premise, initially false
expansionToPremise = [] : (ExpansionSite x MacroDef) -> Premise, initially false

While worklist not empty:
  s: State = (n: ProgramPoint, σ: SymbolTable, p: Premise) taken from worklist
  lineToPremise[n] ||= p

  if n is c_tokens:
    for each e: ExpansionSite in n:
      expansionToPremise[e][σ(e.name)] ||= p

  newStates: List(State) = F(n)(σ, p)
  worklist += newStates

Output: lineToPremise, expansionToPremise
```

### Invariant of Outputs

For any given $e : ExpansionSite$, all possible different definitions 
$d_i : MacroDef, 0 \le i < k$ according to which it expands,
and $n: ProgramPoint$ which contains $e$,
it is guaranteed that

$$
\forall i,j.\quad i\neq j \rightarrow \lnot (expansionToPremise[e][d_i] \land expansionToPremise[e][d_j])
$$

and

$$
lineToPremise[n] = \bigvee_{i=0}^{k-1}{expansionToPremise[e][d_i]}
$$

This essentially says that $d_i$ is a _logical partition_ of
$lineToPremise[n]$, similar to _set partition_. For $lineToPremise$, lines
in different branches of the same `#if` directive follow a similar invariant.

### Macro Expansion and Evaluation

The condition (`cond`) of an `#if` directive requires symbol table context for
proper expansion and evaluation. During token expansion, identifiers that are
defined in the symbol table are simply replaced. Identifiers that do not appear
in the symbol table are possibly defined by user `-D` flags, and thus they are
symbolized as follows:

- `defined M` -> $defM : bool$
- `M` (outside `defined`) -> $(defM ? valM : 0) : int$

Tokens after expansion might fail to parse into valid expressions, resulting in
preprocessing errors during a concrete execution. Symbolic execution handles
these by discarding related states, marking them unreachable, and stopping
further exploration from such points.

### State Transitions

#### `#define name body` and `#undef name`

$$
n: ProgramPoint = (name: String)
\newline
F(n: ProgramPoint)(\sigma: SymbolTable, p: Premise) = \{(next(n), \sigma [name \mapsto n], p)\}
$$

#### `#if cond`

$$
n: ProgramPoint = (cond: Tokens)
\newline
F(n: ProgramPoint)(\sigma: SymbolTable, p: Premise) = \{(nextThen(n), \sigma, p \wedge eval_\sigma(cond)), (nextElse(n), \sigma, p \wedge \neg eval_\sigma(cond))\}
$$

The same applies for `#elif`.

#### `#ifdef name`

$$
n: ProgramPoint = (name: String)
\newline
F(n: ProgramPoint)(\sigma: SymbolTable, p: Premise) = \{(nextThen(n), \sigma, p \wedge eval_\sigma(defined(name))), (nextElse(n), \sigma, p \wedge \neg eval_\sigma(defined(name)))\}
$$

The same applies for `#ifdef`, `#ifndef`, `#elifdef`, `#elifndef`.

#### `c_tokens`

$$
n: ProgramPoint = (name: String, body: Tokens)
\newline
F(n: ProgramPoint)(\sigma: SymbolTable, p: Premise) = \{(next(n), \sigma, p)\}
$$

#### `EOF`

$$
F(EOF: ProgramPoint)(\sigma: SymbolTable, p: Premise) = \{\}
$$

## Optimizations

### Symbol Segment

Symbol tables are implemented as chained hash maps sharing common underlying
hash map instances, or **SymbolSegment**s. Consecutive `#define` directives aggregate into a single
hash table (SymbolSegment) to prevent exponential growth in symbol table storage.

### Warp

States at identical program points execute collectively in lock-step groups
(**Warp**), inspired by CUDA warps. This grouping batches premise processing on
entering `#if` and helps state merging on exiting `#if`.

### State Merging

When exiting a conditional directive (`#if`), states with identical symbol
tables merge by logically disjoining their premises, reducing redundancy.

### Premise Tree

Premises are represented via a hierarchical structure reflecting the nested
conditional structure to reduce fragmentation. The complete premise for a line
of code is derived by conjoining premises along the path from the root to the
corresponding leaf node.

### Concrete Execution of System Headers

Much control flow complexity hide in the system headers that the target project includes, but the user usually does not care about that. Pioneer concretely executes such system headers (by calling `cc -dM -E` and adding all resulting macro definitions to the symbol table) to avoid state explosion and to simplify output. The user can tell Pioneer what to consider as target project headers, and the rest are treated as system headers and concretely executed. 
