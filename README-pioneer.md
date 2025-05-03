# The Hayroll Pioneer Symbolic Executor for C Preprocessor Directives

## Background

When compiling C code, a certain set of macros are defined.  These depend on the OS, architecture, and `-D<MACRO_NAME>=<MACRO_VALUE>` command-line arguments. Macros can selectively enable program features (including further macro definitions), with disabled features removed by conditional compilation directives during preprocessing, incurring no runtime overhead.

The existing `c2rust` translation framework operates on preprocessed C code, meaning only the code enabled by user-specified `-D` flags is translated, causing the remaining code paths and configurability to be lost in the resulting Rust code.

The conditions for inclusion or exclusion of each line can be expressed as boolean logic and arithmetic operations. Pioneer symbolically executes C preprocessor directives, automatically determining the conditions leading to all possible outcomes. Hayroll runs the preprocessor multiple times with different macro definitions, runs `c2rust` on each, and then merges all the resulting Rust programs into a single Rust implementation that uses Rust's `#cfg` macros.

## Input and Output

- **Input:** A single C compilation unit.
- **Output:**
  - For each line of code: the premise (symbolic boolean/arithmetic expression) under which the line survives preprocessing.
  - For each macro expansion: the premise under which a specific macro definition is expanded.

TODO: Is the macro expansion a span of characters in the source code?  Does the information include what the macro expands to, or just the condition under which it is expanded?  There might be different premises for different expansions; is that also an output?

Note: The actual output of the program is structured as a hierarchical premise tree rather than a direct mapping from each line to its premise. This hierarchical structure serves as an optimization, but this document describes only the fundamental logical relationships.

## Core Concepts

### Symbolic Value and Expression

Symbolic values mathematically correspond to user-defined macros (`-D`). Each macro corresponds to two symbolic representations:

- Boolean value (`defMACRO_NAME`) indicating whether the macro is defined.
- Integer value (`valMACRO_NAME`) representing its numeric value.

These representations are distinct because the C macro system allows testing a macro's definedness independently from its value.

A _symbolic expression_ is constructed from symbolic values or constants through boolean logic (`&&`, `||`, `!`) and integer arithmetic operations (`+`, `-`, `*`, `/`, `:?`, and comparisons). All premises are symbolic expressions.

### Program Point

The input to the C preprocessor is an if-then "program" without loops, made up of sequential execution and conditional branches. Pioneer represents this program by an Abstract Syntax Tree (AST).  One variety of AST node is `c_tokens`, for a contiguous set of non-preprocessor lines, represented as an unstructured list of C tokens.

A **Program Point** refers to the position between AST nodes. Program Points are represented using AST nodes indicating the upcoming directive to be executed.

TODO: Explain the Program Point AST nodes.  How are they related to `c_tokens` AST nodes?  Is the set of Program Points the same as the set of locations that a `c_tokens` could possibly appear?  What is the span of a Program point withing the source code?

Pioneer contains a `tree-sitter-c_preproc` grammar to parse this AST.
For simplicity, this document ignores `#include` directive, assuming they have been inlined.

TODO: To further clarify the relationship among `c_tokens` and Program Points and how they fit into the AST, please show an example here.


### Symbol Table

Similar to the symbol table used during C compilation to record identifiers and their types, the preprocessor maintains a symbol table mapping macro names to their definitions (token sequences).
The expansion of macros depends on this symbol table, effectively substituting macro names with their bodies if defined, or leaving the macro name intact if undefined.

Fromally, a symbol table can be represented as:
$$
\sigma : String \rightarrow ProgramPoint
$$
indicating the mapping from macro names to their definition nodes.

What is a definition node?  I presume it is a subset of a `#define` line.  Is it a node of type `c_tokens`?  If so, the characterization of `c_tokens` as only between preprocessor lines is incorrect.

### State

A **State** is defined as:

$$
s: State = (n: ProgramPoint, \sigma: SymbolTable, p: Premise)
$$

It represents execution conditions $p$ and symbol definitions $\sigma$ when reaching program point $n$. This means that when the macro definitions satisfy $p$, a concrete preprocessor execution will pass by this $n$, at which time $\sigma$ is defined. Symbolic execution progresses states according to the directives encountered, with `#if` conditions splitting states to explore multiple execution paths.


## Symbolic Executor Algorithm

### Notations

- $n : (SymbolTable, Premise) \rightarrow List(State)$ is the transition function associated with $n : ProgramPoint$.
- $next : ProgramPoint \rightarrow ProgramPoint$ identifies the next node in logical order. It returns the next sibling in a sequence of nodes, or, when it reaches the end of the sequence, climbs up its ancestor tree until the ancestor has a next sibling. It returns `EOF` if no next sibling exists for any of its ancestors. 
- $nextThen : ProgramPoint \rightarrow ProgramPointt$ and $nextElse : ProgramPoint \rightarrow ProgramPoint$ refer to the initial nodes of the respective branches following an `#if` directive.
- Given a symbol table $\sigma$, the function $eval_\sigma : Tokens \rightarrow Premise$ evaluates tokens into a symbolic expression within the context of $\sigma$.

### Algorithm Pseudocode

```
Input: astRoot: ProgramPoint

tasks = {(astRoot, [], true)} : List(State)
lineToPremise = [] : ProgramPoint -> Premise, initially false
expansionToPremise = [] : ExpansionSite -> ProgramPoint -> Premise, initially false

While tasks not empty:
  s: State = (n: ProgramPoint, σ: SymbolTable, p: Premise) taken from tasks
  lineToPremise[n] ||= p

  if n is c_tokens:
    for each e: ExpansionSite in n:
      expansionToPremise[e][σ(e.name)] ||= p

  newStates: List(State) = n(σ, p)
  tasks += newStates

Output: lineToPremise, expansionToPremise
```

### State Transitions

#### `#define name body`

$$
n: ProgramPoint = (name: String, body: Tokens) \\
n(\sigma: SymbolTable, p: Premise) = \{(next(n), \sigma [name \mapsto body], p)\}
$$

Similar logic applies to empty definitions, function-like definitions, and `#undef` directives. (`#undef` definitions use a special body marking definedness as false, with no value.)

#### `#if cond`

$$
n: ProgramPoint = (cond: Tokens) \\
n(\sigma: SymbolTable, p: Premise) = \{(nextThen(n), \sigma, p \wedge eval_\sigma(cond)), (nextElse(n), \sigma, p \wedge \neg eval_\sigma(cond))\}
$$

The same applies for `#ifdef`, `#ifndef`, `#elif`, `#elifdef`, `#elifndef`.

#### `c_tokens`

$$
n: ProgramPoint = (name: String, body: Tokens) \\
n(\sigma: SymbolTable, p: Premise) = \{(next(n), \sigma, p)\}
$$

#### `EOF`

$$
EOF(\sigma: SymbolTable, p: Premise) = \{\}
$$

### Macro Expansion and Evaluation

The condition (`cond`) of an `#if` directive requires symbol table context for proper expansion and evaluation. During token expansion, identifiers that are found in the symbol table are simply replaced. Identifiers that do not appear in the symbol table are possibly defined by user `-D` flags, and thus they are symbolized as follows:

- `defined M` → $defM : bool$
- `M` (outside `defined`) → $(defM ? valM : 0) : int$

Tokens after expansion might fail to parse into valid expressions, resulting in preprocessing errors during a concrete execution. Symbolic execution handles these by discarding related states, marking them unreachable, and stopping further exploration from such points.

## Optimizations

### Symbol Segment

Symbol tables are implemented as chained hash maps sharing common underlying hash map instances. Consecutive `#define` statements aggregate into a single hash table (**SymbolSegment**) to prevent exponential growth in symbol tables.

### Warp

States at identical program points execute collectively in lock-step groups (**Warp**), inspired by CUDA warps. This grouping batches premise processing on entering `#if` and helps state merging on exiting `#if`.

### State Merging

When exiting a conditional directive (`#if`), states with identical symbol tables merge by logically disjuncting their premises, reducing redundancy.

### Premise Tree

Premises are represented via a hierarchical structure reflecting the nested conditional structure to reduce fragmentation. The complete premise for a line of code is derived by conjuncting premises along the path from the root to the corresponding leaf node.
