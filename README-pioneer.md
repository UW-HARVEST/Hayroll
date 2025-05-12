# The Hayroll Pioneer Symbolic Executor for C Preprocessor Directives

## Background

When compiling C code, a certain set of macros are defined. These depend on the OS, architecture, and `-D<MACRO_NAME>=<MACRO_VALUE>` command-line arguments. Macros can selectively enable program features (including further macro definitions), with disabled features removed by conditional compilation directives during preprocessing, incurring no run-time overhead.

The existing `c2rust` translation framework operates on preprocessed C code, meaning only the code enabled by user-specified `-D` flags is translated. The remaining code paths and configurability are lost in the resulting Rust code.

The condition for inclusion or exclusion of each line can be expressed as a boolean/arithmetic expression. Pioneer symbolically executes C preprocessor directives. Hayroll runs the preprocessor multiple times with different macro definitions, runs `c2rust` on each, and then merges all the resulting Rust programs into a single Rust implementation that uses Rust's `#cfg` macros.

TODO: In the previous paragraph, what is the relationship of the last sentence to the first two?  The sentence about Hayroll makes no mention of Pioneer, so why is that sentence in a document about Pioneer?

## Input and Output

- **Input:** A single C compilation unit.
- **Output:**
  - For each line of code: the premise (symbolic boolean/arithmetic expression) under which the line survives preprocessing.
  - For each macro expansion site:
    - For each possible macro definition that may expand at this site: the premise under which this definition is expanded.

TODO: Does "macro expansion site" include those within `#define` definitions, or only within "pure C code" lines of a file?

TODO: Is "macro expansion site" a symbol, or might it also include following parentheses that enclose arguments?


Note: The actual output of the program is structured as a hierarchical premise tree rather than a direct mapping from each line to its premise. This hierarchical structure serves as an optimization, but this document describes only the fundamental logical relationships.

## Core Concepts

### Symbolic Value and Expression

TODO: What is the point of "mathematically" in the text below?  What other type of correspondence are you contrasting with?

TODO: What does "user-defined macros" mean? Is it the macro name, or the macro body, or both, or something else?

TODO: Do symbolic values not correspond to macros defined using `#define`?  If so, why does the text specicially call out `-D`?

TODO: It's confusing that the two sentences with "correspond" give the items in different orders.  Are there two correspondences or one?

TODO: What is the distinction between "symbolic values" and "symbolic representations"?  If there is a difference, define both.  If there is no difference, use just one term.

Symbolic values mathematically correspond to user-defined macros (`-D`). Each macro corresponds to two symbolic representations:

- Boolean value (`defMACRO_NAME`) indicating whether the macro is defined.
- Integer value (`valMACRO_NAME`) representing its numeric value.

These representations are distinct because the C macro system allows testing a macro's definedness independently from its value.

A _symbolic expression_ is constructed from symbolic values or constants through boolean logic (`&&`, `||`, `!`) and integer arithmetic operations (`+`, `-`, `*`, `/`, `:?`, and comparisons). All premises are symbolic expressions.

### Program Point

The input to the C preprocessor is an if-then "program" without loops, made up of sequential execution and conditional branches. 
Preprocessor directives, such as `#define` and `#if`, are the bones of this program, while non-preprocessor C code sections do not affect its control flow.

TODO: Above, can "C code sections" be replaced by "lines"?  If not, why not?

TODO: The following sentence feels like an implementation detail that is worth documenting, but not in a high-level document (or not until later in the document).

Pioneer represents this program by an Abstract Syntax Tree (AST).
An AST node either stands for a preprocessor directive, or is a `c_tokens` node, meaning a contiguous set of non-preprocessor lines, represented as an unstructured list of C tokens.
Pioneer contains a `tree-sitter-c_preproc` grammar to parse this AST.
For simplicity, this document ignores `#include` directives, assuming they have been inlined.

TODO: Is a simpler definition of "Program point":  "the beginning or end of a preprocessor line"?

A **Program Point** refers to a position between two adjacent AST nodes.
Program Points are represented using AST nodes, meaning the position right before the stored AST node.
This AST node could either stand for a preprocessor directive or be a `c_tokens` node.
Technically speaking, `c_token` nodes cannot be executed like preprocessor directives, but including them in the AST/ProgramPoint representations would make it easier to collect information about macro expansions in our algorithm.

For example, the following C program:

```
#ifndef HEADER_GUARD
#define HEADER_GUARD
int f();
#endif
```

is parsed into this AST:

```
preproc_ifndef (#ifndef HEADER_GUARD [then] #endif)
  then:
    preproc_define (#define HEADER_GUARD)
    c_tokens (int f();)
  (no else here)
```

where the `then` branch of the `preproc_ifndef` node nests two nodes: `preproc_define` and `c_tokens`. 

TODO: I'm not sure how to interpret the above, since it introduces terms that have not yet been defined.

### Symbol Table

TODO: I don't understand the relevance of this section to the document.  The document is supposed to be about Pioneer, but this section is about an implementation detail of the C preprocessor itself.

The preprocessor maintains a symbol table mapping macro names to their definitions (token sequences).
The expansion of macros depends on this symbol table, effectively substituting macro names with their bodies if defined, or leaving the macro name intact if undefined.

Formally, a symbol table can be represented as:
$$
\sigma : String \rightarrow ProgramPoint
$$
indicating the mapping from macro names to their definition nodes (the `#define` line). 

TODO: The definition and the explanation are in conflict with one another.  A program point is not a node, but rather lies *between* nodes.  The mapping should be to DefineLine or DefineDirective or MacroDefinition or the like.


### State

TODO: The previous section was about the C preprocessor.  Is the term **State** (which is defined here) about the C preprocessor, or is it about Pioneer?

A **State** is defined as:

$$
s: State = (n: ProgramPoint, \sigma: SymbolTable, p: Premise)
$$

TODO: This definition, which associates premises with program points, is in conflict with 
the beginning of this document, where premises are associated with lines of a file.

TODO: What does "when reaching" mean?  What process is reaching a program point?
The paragraph discusses both concrete preprocessor executions and symbolic executions, further confusing the point.

It represents execution conditions $p$ and symbol definitions $\sigma$ when reaching program point $n$. This means that when the user-provided macro definitions satisfy $p$, a concrete preprocessor execution will pass by this $n$, at which time $\sigma$ is defined. Symbolic execution progresses states according to the directives encountered, with `#if` conditions splitting states to explore multiple execution paths.


## Symbolic Executor Algorithm

### Notations

- $n : (SymbolTable, Premise) \rightarrow List(State)$ is the transition function associated with $n : ProgramPoint$.  TODO: A program point lies *between* nodes, so there is no transition function associated with it.  A transition function is associated with a node or operation.
- $next : ProgramPoint \rightarrow ProgramPoint$ identifies the next node in logical order. It returns the next sibling in a sequence of nodes, or, when it reaches the end of the sequence, climbs up its ancestor tree until the ancestor has a next sibling. It returns `EOF` if no next sibling exists for any of its ancestors.  TODO: A program point cannot identify a node, because program points and nodes are distinct.  The term "logical order" is not helpful, because it is not defined.  Why not just say it's the next node in the CFG?  The CFG is a more natural data structure to use for this kind of algorithm than the AST is.
- $nextThen : ProgramPoint \rightarrow ProgramPoint$ and $nextElse : ProgramPoint \rightarrow ProgramPoint$ refer to the initial nodes of the respective branches following an `#if` directive.
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

- `defined M` -> $defM : bool$
- `M` (outside `defined`) -> $(defM ? valM : 0) : int$

Tokens after expansion might fail to parse into valid expressions, resulting in preprocessing errors during a concrete execution. Symbolic execution handles these by discarding related states, marking them unreachable, and stopping further exploration from such points.

## Optimizations

### Symbol Segment

Symbol tables are implemented as chained hash maps sharing common underlying hash map instances. Consecutive `#define` statements aggregate into a single hash table (**SymbolSegment**) to prevent exponential growth in symbol tables.

### Warp

States at identical program points execute collectively in lock-step groups (**Warp**), inspired by CUDA warps. This grouping batches premise processing on entering `#if` and helps state merging on exiting `#if`.

### State Merging

When exiting a conditional directive (`#if`), states with identical symbol tables merge by logically disjoining their premises, reducing redundancy.

### Premise Tree

Premises are represented via a hierarchical structure reflecting the nested conditional structure to reduce fragmentation. The complete premise for a line of code is derived by conjoining premises along the path from the root to the corresponding leaf node.
