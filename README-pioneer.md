# The Hayroll Pioneer Symbolic Executor for C Preprocessor Directives

## Background

In compiling C code, users can provide macro definitions using the `-D<MACRO_NAME>=<MACRO_VALUE>` flag to influence preprocessor behavior, enabling configurable compilation. For example, macros can selectively enable program features, and the disabled ones are removed by conditional compilation directives during preprocessing, incurring no runtime overhead. Another use case involves defining the same macro name differently under various conditional branches. Depending on the user's `-D` flags, these macros will expand differently during compilation.

The existing `c2rust` translation framework operates on preprocessed C code, meaning only the code enabled by user-specified `-D` flags is translated, causing the remaining code paths and configurability to be lost in the resulting Rust code.

The complexity of the C preprocessor arises from complicated relationships between user-defined flags (`-D`) and the resulting code, involving boolean logic and arithmetic operations. Some combinations of macro definitions may fail to preprocess or compile successfully. Without a unified, structured configuration file, exploring these relationships necessitates symbolic execution. Hayroll addresses this challenge by symbolically executing C preprocessor directives, automatically exploring all possible outcomes from various combinations of user-defined flags. Combined with subsequent steps, Hayroll performs multiple `c2rust` translations of these outcomes and merges them into a configurable Rust implementation using Rust’s `#cfg` macros.

## Input and Output

- **Input:** A single C compilation unit.
- **Output:**
  - For each line of code: the premise (symbolic boolean/arithmetic expression) under which the line survives preprocessing.
  - For each macro expansion: the premise under which a specific macro definition is expanded.

Note: The actual output of the program is structured as a hierarchical premise tree rather than a direct mapping from each line to its premise. This hierarchical structure serves as an optimization, but this document describes only the fundamental logical relationships.

## Core Concepts

### Symbolic Value and Expression

Symbolic values mathematically correspond to user-defined macros (`-D`). Each macro corresponds to two symbolic representations:

- Boolean value (`defMACRO_NAME`) indicating whether the macro is defined.
- Integer value (`valMACRO_NAME`) representing its numeric value.

These representations are separated because the C macro system allows testing a macro's definedness independently from its value.

Symbolic expressions are constructed from symbolic values or constants through boolean logic (`&&`, `||`, `!`) and integer arithmetic operations (`+`, `-`, `*`, `/`, `:?`, and comparisons). All premises are symbolic expressions.

### Program Point

After removing non-preprocessor directive sections (pure C code) from C source code, the remaining preprocessor directives form a structured program without loops, made up of sequential execution and conditional branches. This structured program is represented by an Abstract Syntax Tree (AST), and a **Program Point** refers to the position between AST nodes. Program Points are represented using AST nodes indicating the upcoming directive to be executed.

Hayroll utilizes the tree-sitter parsing library with a specialized `tree-sitter-c_preproc` grammar to parse this AST. While the `#include` directive can introduce multiple ASTs within a single compilation unit, this document simplifies the explanation by assuming they are all inlined into one AST.

For convenience, our grammar parses consecutive non-preprocessor directive sections (pure C code) into an AST node, namely a `c_tokens` node. It is a plain list of C tokens, not containing any internal structures and does not have to be parsable as a C program. 

### Symbol Table

Similar to the symbol table used during C compilation to record identifiers and their types, the preprocessor maintains a symbol table mapping macro names to their definitions (token sequences). Unlike structured compiler values, macro bodies are merely strings of tokens without intrinsic semantic structure, and their semantics depend on context during expansion. The expansion of macros depends on this symbol table, effectively substituting macro names with their bodies if defined, or leaving the macro name intact if undefined.

Fromally, a symbol table can be represented as:
$$
\sigma : String \rightarrow ProgramPoint
$$
indicating the mapping from macro names to their definition nodes.

### State

A **State** is defined as:

$$
s: State = (n: ProgramPoint, \sigma: SymbolTable, p: Premise)
$$

It represents execution conditions $p$ and symbol definitions $\sigma$ when reaching program point $n$. This means that when the user `-D` satisfies $p$, a concrete preprocessor execution will pass by this $n$, at which time $\sigma$ is defined. Symbolic execution progresses states according to the directives encountered, with `#if` conditions splitting states to explore multiple execution paths.


## Symbolic Executor Algorithm

### Notations

- $n(\sigma: SymbolTable, p: Premise) \rightarrow List(State)$ is the transition function associated with $n : ProgramPoint$.
- $next(n: ProgramPoint) : ProgramPoint$ identifies the next node in logical order. It returns the next sibling in a sequence of nodes, or, when it reaches the end of the sequence, climbs up its ancestor tree until the ancestor has a next sibling. It returns `EOF` if no next sibling exists for any of its ancestors. 
- $nextThen(n) : ProgramPoint$ and $nextElse(n) : ProgramPoint$ refer to the initial nodes of the respective branches following an `#if` directive.
- Given a symbol table $\sigma$, the expression $eval_\sigma(t: Tokens) : Premise$ evaluates tokens $t$ into a symbolic expression within the context of $\sigma$.

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
