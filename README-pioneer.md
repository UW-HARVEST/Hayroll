# The Hayroll Pioneer Symbolic Executor for C Preprocessor Directives

## Background

When compiling C code, a certain set of macros are defined. These depend on the OS, architecture, and `-D<MACRO_NAME>=<MACRO_VALUE>` command-line arguments. Macros can selectively enable program features (including further macro definitions), with disabled features removed by conditional compilation directives during preprocessing, incurring no run-time overhead.

The existing `c2rust` translation framework operates on preprocessed C code, meaning it translates only the code enabled by one setting of macro definitions (most notably, user-specified `-D` flags). The remaining code paths and configurability are lost in the resulting Rust code.

The condition for inclusion of each line can be expressed as a boolean/arithmetic expression, based on the kinds of computation that is allowed in preprocessing. Pioneer symbolically executes C preprocessor directives to compute such inclusion conditions for each line of code (represented with an AST node in Pioneer's output).

Pioneer is part of the Hayroll pipeline. According to Pioneer's output, Hayroll runs the preprocessor multiple times with different macro definitions, runs `c2rust` on each, and then merges all the resulting Rust programs into a single Rust implementation that uses Rust's `#cfg` macros.

TODO: "According to Pioneer's output": not here, but elsewhere the documentation should give the algorithm for exactly how many times the preprocessor is run.  What guarantee is provided about the completeness of the set of runs?

## Input and Output

- **Input:** A single C compilation unit.
- **Output:**
  - For each line of code (represented with an AST node): the premise (symbolic boolean/arithmetic expression) under which the line survives preprocessing.
  - For each macro expansion site within pure C code lines (represented with the line:col coordinate of the starting byte of the expansion):
    - For each possible macro definition that may expand at this site: the premise under which this definition is expanded.

Note: The actual output of the program is structured as a hierarchical premise tree rather than a direct mapping from each line to its premise.
## Core Concepts

### Symbolic Value and Expression

Symbolic values express to the definedness and value of user-defined macros (`-D`). We assume such macros are defined to integers, which is the common case. Each macro is associated with two symbolic values:

- Boolean value (`defMACRO_NAME`) indicating whether the macro is defined.
- Integer value (`valMACRO_NAME`) representing its numeric value.

These two values are distinct because the C macro system allows testing a macro's definedness independently from its value.

A macro that is defined by a `#define` directive in program text is not represented with symbolic values, as the user cannot customize its value with `-D` input. While the user input may affect whether this directive is reached via conditional macros, that is handled by the State data structure introduced later.

TODO: "in program text": What about in system header files?  It seems to me that those are like user-defined macros: they are unknown and all possible variation in them should be preserved.  Is this already handled, or will it be handled in the future, or is it out of scope?

A _symbolic expression_ is constructed from symbolic values or constants through boolean logic (`&&`, `||`, `!`) and integer arithmetic operations (`+`, `-`, `*`, `/`, `:?`, and comparisons). All premises are symbolic expressions.

### Program Point

The input to the C preprocessor is an if-then "program" without loops, made up of sequential execution and conditional branches. 
Preprocessor directives, such as `#define` and `#if`, are the bones of this program, while non-preprocessor C lines do not affect its control flow.

Pioneer represents this program by an Abstract Syntax Tree (AST).
An AST node either stands for a preprocessor directive, or is a `c_tokens` node, meaning a contiguous set of non-preprocessor lines, represented as an unstructured list of C tokens. Pioneer contains a `tree-sitter-c_preproc` grammar to parse this AST.

For simplicity, this document ignores `#include` directives, assuming they have been inlined -- an entire compilation unit is a single AST.

TODO: Briefly, what is the behavior of the implementation?  (You don't have to give implementation details here, just the user perspective.)

A **Program Point** is the beginning of a preprocessor line.
Program Points are represented using AST nodes, meaning the line start of that AST node.
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
preproc_ifndef "#ifndef HEADER_GUARD [then] #endif"
  then:
    preproc_define "#define HEADER_GUARD"
    c_tokens "int f();"
```

### Symbol Table

Pioneer maintains a symbol table mapping macro names to their definitions (token sequences).
The expansion of macros depends on this symbol table, effectively substituting macro names with their bodies if defined, or leaving the macro name intact if undefined.

TODO: "mapping macro names":  which macro names?  Some? all?  Not the user-defined ones?  What about those defined in system header files that are outside the code the programmer wrote?

Formally, a symbol table can be represented as:
$$
\sigma : String \rightarrow ASTNode
$$
indicating the mapping from macro names to their definition nodes (the `#define` line). 

TODO: Above, I would use _MacroDef_ rather than _AstNode_, because it is more specific.  (Even if your implementation doesn't have such a notion, clarity in this document is desirable.)

### State

Pioneer explores all possible program states of a concrete C preprocessor in order to explore all its possible outputs. In Pioneer, a **State** is defined as:

TODO: I am confused by "concrete C preprocessor".  Do you mean a C program?  or a preprocessor execution?  The state is used by an abstract interpretation, so what does the word "concrete" indicate there?

TODO: "in order to explore":  Avoid passive voice, and make the actor clear.  What system will do that?  Pioneer, or some other part of the system?

$$
s: State = (n: ProgramPoint, \sigma: SymbolTable, p: Premise)
$$

Pioneer keeps a list of States during execution. A certain State being in the list means that, when the user-provided macro definitions satisfy its $p$, a concrete preprocessor execution will pass by this $n$, at which time macros in $\sigma$ are defined. Pioneer explores the reachable state space according to its algorithm.

## Symbolic Executor Algorithm

### Notations

- $n : (SymbolTable, Premise) \rightarrow List(State)$ is the transition function associated with the first AST node after $n : ProgramPoint$.  TODO: I would replace "the first AST node after" by "the AST node at" or "the AST node immediately after".
- $next : ProgramPoint \rightarrow ProgramPoint$ identifies the next ProgramPoint according to control flow sequence. The last ProgramPoint is repesented with `EOF`.
- $nextThen : ProgramPoint \rightarrow ProgramPoint$ and $nextElse : ProgramPoint \rightarrow ProgramPoint$ refer to the initial ProgramPoints of the respective branches following an `#if` directive.
- Given a symbol table $\sigma$, the function $eval_\sigma : Tokens \rightarrow Premise$ evaluates tokens into a symbolic expression within the context of $\sigma$.

### Algorithm Pseudocode

```
Input: astRoot: ProgramPoint

worklist = {(astRoot, emptySymbolTable, true)} : List(State)
lineToPremise = [] : ProgramPoint -> Premise, initially false
expansionToPremise = [] : ExpansionSite -> ProgramPoint -> Premise, initially false

While worklist not empty:
  s: State = (n: ProgramPoint, σ: SymbolTable, p: Premise) taken from worklist
  lineToPremise[n] ||= p

  if n is c_tokens:
    for each e: ExpansionSite in n:
      expansionToPremise[e][σ(e.name)] ||= p

  newStates: List(State) = n(σ, p)
  worklist += newStates

Output: lineToPremise, expansionToPremise
```

TODO: Why does `expansionToPremise` have type "ExpansionSite -> ProgramPoint -> Premise" rather than "ExpansionSite x ProgramPoint -> Premise"?  If you will depend on currying, the former is needed.  If you won't do currying, then the latter is clearer.  Also, should "ProgramPoint" be "MacroDef" instead?

What invariants does `expansionToPremise` satisfy?  for example, for a given e and distinct s1 and s2, are the premises disjoint for `expansionToPremise[e][s1]` and `expansionToPremise[e][s2]`?



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

The condition (`cond`) of an `#if` directive requires symbol table context for proper expansion and evaluation. During token expansion, identifiers that are defined in the symbol table are simply replaced. Identifiers that do not appear in the symbol table are possibly defined by user `-D` flags, and thus they are symbolized as follows:

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
