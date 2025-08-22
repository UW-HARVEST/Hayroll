# The Hayroll Seeder: AST Region Tagging for Macro Reconstruction

## Role in the Hayroll Pipeline and Design Intent

Hayroll ultimately needs to reconstruct C macro invocations and reason about which regions of original C code correspond to what Rust constructs.  Before Rust‐side reconstruction (**Reaper**) can happen, the **Seeder** stage walks a single compilation unit (CU) (the result of copying all `#include`s into one file) and inserts **tags** or **seeds** into the C text. These tags:

- Delimit precise AST regions (expressions, statement spans, declaration clusters) relevant to later Rust macro/function extraction.
- Carry semantic hints: AST category, whether a region is an lvalue, whether it can be reconstructed into a Rust function or a Rust macro.
- Enable the downstream Reaper (Rust side) to unambiguously locate and classify regions for macro reconstruction.
- Do not change observable C program behavior. All inserted content is wrapped in syntactic forms that are semantic-preserving (e.g., string literals that have no side-effects, statement no‑ops, etc.)

The seeded CU file is then translated into Rust by **C2Rust** before the **Reaper** stage processes it.

## Inputs and Outputs

**Inputs**

- **Input CU**: A single C source string representing one compilation unit, with `#include`s expanded and line markers removed.
- **Invocation summaries**: A list of invocation and argument summaries from Maki. Each entry specifies source coordinates for the invocation/argument and its AST category, and for expressions, whether it is an lvalue.

**Output**: A modified C source string containing embedded tags.

## Tag Structure

Each tag is a JSON object that gets serialized into a C string literal.  The tag fields encode the information that the Reaper needs to match regions back to macro invocations.  

A `Stmt`/`Stmts` region needs a pair of tags (begin/end); An `Expr`/`Decl`/`Decls` region needs only one tag.  Unknown or unsupported AST categories are skipped.

| Field name | Purpose |
|-----------|-----------------|
| `hayroll` | A constant boolean marker to quickly filter tags.  Always `true`. |
| `astKind` | Coarse AST category: one of `Expr`, `Stmt`, `Stmts`, `Decl`, `Decls`. |
| `isLvalue` | Whether an `Expr` region yields an lvalue.  This controls how the wrapper is generated (see below). Always `false` for non-`Expr` regions. |
| `begin` | `Stmt`/`Stmts` regions have paired begin/end tags; `true` for opening tag; `false` for closing tag.  Always `true` for `Expr`/`Decl`/`Decls`. |
| `isArg` | `true` if this region is an argument of a macro invocation; `false` for the invocation itself. |
| `name` | Macro name (for invocation tags) or argument name (for argument tags). |
| `argNames` | List of macro parameter identifiers for the surrounding invocation.  Used only on invocation tags. |
| `locBegin`/`locEnd` | `file:line:col` positions delimiting the region in the original source file. |
| `cuLnColBegin`/`cuLnColEnd` | `line:col` positions in the CU file.  Reaper uses these to locate `Decl`/`Decls` tags in the Rust AST. |
| `locRefBegin` | For invocation regions, this refers to the location of the macro definition in the original source file; for argument regions, it refers to the `locBegin` of the invocation region it belongs to. |
| `canFn` | Whether the invocation body could be lifted into a Rust function.  Only true when Maki reports the expansion as hygienic and free of various kinds of side effects.  Invocations that could not be lifted into Rust functions will be turned into Rust macros.  Always false for argument tags. |

## Tagging Strategies by Region Category

Seeder treats expressions, statement spans and declaration clusters differently in order to preserve both behaviour and syntactic meaning.  The code snippets below illustrate the shape of the instrumentations.

### Expression regions (`Expr`)

For macro invocations that are expressions, Seeder replaces it with a conditional (ternary) expression, with whose condition clause being the non-empty tag string (dereferenced to avoid compiler optimizations) (and thus always evaluates to non-zero), true branch being the original expression (address taken in lvalue case), and false branch being a null pointer of the same type (dereferenced in rvalue case). The conditional expression preserves the original semantics, marks the expression region precisely, and attaches the tag.

Two variants exist, depending on whether the expression produces an **lvalue** or an **rvalue**. In C, an lvalue is an expression that stands for a memory location, and thus can be assigned to and taken the address of. Typical examples include variables and dereferenced pointers. Any expression that is not an lvalue is considered an rvalue.

#### Rvalue expression

When the expansion yields an rvalue, the wrapper looks like this (simplified pseudocode, where `TAG` is the JSON literal and `EXPR` is the original expression):

```c
(
    (*TAG)
    ?
    (EXPR)
    :
    (*(__typeof__(EXPR)*)(0))
)
```

In this case, the true branch evaluates `EXPR` and returns its value; the false branch converts `0` to a pointer of the correct type, dereferences it (in an unevaluated context) and thus yields a dummy value of that type.  Both branches have the same type, so the ternary expression preserves the original type of `EXPR`.  Because the tag literal always evaluates to non-zero, the false branch is unreachable, but including it ensures that parsing succeeds in all contexts.

#### Lvalue expression

If the expression is an lvalue, Seeder first takes the address of the original expression, then wraps it similarly to the rvalue case, and finally dereferences the result so it remains an lvalue.

```c
(
    *(
        (*TAG)
        ?
        (&(EXPR))
        :
        ((__typeof__(EXPR)*)(0))
    )
)
```

The conditional `(*TAG)? ... : ...` evaluates the tag string literal and yields non-zero on the true branch, so it selects the `&(EXPR)` branch. On the false branch, `( (__typeof__(EXPR)*)(0) )` yields a null pointer of the same type but is never dereferenced. The outer `*` dereferences whichever pointer the conditional chose, producing an lvalue of the correct type.  Consequently, using this wrapper in assignment or `&` contexts behaves exactly like the original expression.

#### Expression Tagging Summary

The difference between lvalue and rvalue macro arguments is an important subtlety.  In C, some macros expand to expressions that can appear on the left hand side of an assignment (lvalue), while others cannot (rvalue).  If Seeder were to wrap both the same way, it might accidentally loose the assignability of an lvalue, or try to take the address of an rvalue.  The lvalue wrapper uses `&EXPR` to obtain a pointer and dereferences it at the outermost level; this ensures the wrapper is itself an lvalue of the same type, so operations like `++` or taking an address still work.  The rvalue wrapper omits the address-of and instead dereferences a null pointer on the unused branch, preserving rvalue semantics.  The `isLvalue` field in the tag tells Reaper which wrapper was used and thus whether the underlying expression is assignable.

These expression tagging strategies guarantee:

- **Preserved value category**: The result is still an lvalue or an rvalue as appropriate.
- **No changed side effects**: Only the selected branch is evaluated; the unused branch has no side effects.
- **Type correctness**: The use of `__typeof__` ensures the wrapper has the same type as the original expression on both branches.

### Statement spans (`Stmt` and `Stmts`)

For one or more statements, Seeder sandwiches the original statement list between a begin and end tag string literal (both prefaced with `*` to avoid compiler optimizations).  A begin tag string literal is placed as its own expression statement, followed by the original statement list, followed by an end tag string literal statement, all wrapped in a compound statement:

```c
*TAG_BEGIN;
ORIGINAL_STATEMENTS;
*TAG_END;
```

Control flow semantics (loops, conditionals, etc.) remain unchanged because the tags do not alter scopes or variable lifetimes.

### Declaration clusters (`Decl` and `Decls`)

Declarations present a special challenge: top-level declarations may be reordered by C2Rust.  The `c2rust` translator groups items by kind (functions, statics, type definitions, etc.) and reorders them to satisfy Rust’s requirements.  After C2Rust translation, sandwich tags for top-level declarations are not guaranteed to still sandwich the original declarations. Instead, we make use of an undocumented C2Rust feature that decorates each top-level item with a `#[c2rust::src_loc = "ln:col"]` attribute, which contains the line and column of the original C declaration.  This allows us to match Rust top-level items back to their original C declarations.  Seeder attaches a sidecar constant pointer variable containing the tag string literal after the original declaration (which contains the begin and end locations in the C compilation unit file):

```c
ORIGINAL_DECL const char * HAYROLL_TAG_FOR_<MACRO_NAME> = TAG;
```

## Example Walk-through

Consider the following simple C file with three macro invocations:

```c
#define EXPR_MACRO_ADD(x, y) ((x) + (y))
#define STMT_MACRO_INCR(x) do { (x)++; } while (0)
#define DECL_MACRO_INT int a;

DECL_MACRO_INT

int main() {
    STMT_MACRO_INCR(a);
    return EXPR_MACRO_ADD(a, 5);
}
```

Running Seeder on its compilation unit injects tags around each invocation.  In the seeded C output the declaration is followed by a tag variable, the statement macro call is sandwiched by begin/end tags, and both arguments of the expression macro call are wrapped separately (the first as an lvalue, the second as an rvalue).  A snippet of the seeded CU looks like this (whitespace and comments added for readability):

```c
DECL_MACRO_INT const char * HAYROLL_TAG_FOR_DECL_MACRO_INT = "{ … \"astKind\":\"Decl\", … , \"cuLnColBegin\":\"7:1\", … }";

int main()
{
    *"{ … \"astKind\":\"Stmt\", \"begin\":true, … }";
    STMT_MACRO_INCR
    (
        (*
            ((*"{ … \"isArg\":true, \"astKind\":\"Expr\", \"isLvalue\":true, … }"?
            (&(a)):
            ((__typeof__(a)*)(0)))
        )
    );
    *"{ … \"astKind\":\"Stmt\", \"begin\":false, … }";
    return 
    (
        *"{ … \"astKind\":\"Expr\", \"begin\":true, … }"?
        EXPR_MACRO_ADD
        (   // Arg 1
            (*
                ((*"{ … \"isArg\":true, \"astKind\":\"Expr\", \"isLvalue\":true, … }"?
                (&(a)):
                ((__typeof__(a)*)(0))))
            ),
            // Arg 2
            (
                *"{ … \"isArg\":true, \"astKind\":\"Expr\", \"isLvalue\":false, … }"?
                (5)
                :
                (*(__typeof__(5)*)(0))
            )
        ):
        (*(__typeof__(EXPR_MACRO_ADD(a,5))*)(0))
    );
}
```

When this seeded CU is passed through C2Rust, the tags survive translation.  For example, the tag for the declaration appears as a `static` pointer following the translated variable `a`, both annotated with `#[c2rust::src_loc]` attributes so that Reaper can match them.  Statement tags become dereferences of the tag literal on entry and exit of the macro expansion, and expression wrappers become nested conditional expressions in Rust.  The presence of tags does not alter the logic of the program, but it gives the Rust side the positional information needed to reconstruct the original macro.

```rust
#[c2rust::src_loc = "7:1"]
pub static mut a: libc::c_int = 0;
#[c2rust::src_loc = "7:29"]
pub static mut HAYROLL_TAG_FOR_DECL_MACRO_INT: *const libc::c_char =
    b"{ … \"astKind\":\"Decl\", … , \"cuLnColBegin\":\"7:1\", … }\0" as *const u8
        as *const libc::c_char;

#[c2rust::src_loc = "9:1"]
unsafe fn main_0() -> libc::c_int {
    *(b"{ … \"astKind\":\"Stmt\", \"begin\":true, … }\0" as *const u8 as *const libc::c_char);

    let ref mut fresh0 = *if *(b"{ … \"isArg\":true, \"astKind\":\"Expr\", \"isLvalue\":true, … }\0" as *const u8 as *const libc::c_char) as libc::c_int != 0
    {
        &mut a
    } else {
        0 as *mut libc::c_int
    };

    *fresh0 += 1;
    *fresh0;

    *(b"{ … \"astKind\":\"Stmt\", \"begin\":false, … }\0" as *const u8 as *const libc::c_char);

    return if *(b"{ … \"astKind\":\"Expr\", \"begin\":true, … }\0" as *const u8 as *const libc::c_char) as libc::c_int != 0
    {
        *(if *(b"{ … \"isArg\":true, \"astKind\":\"Expr\", \"isLvalue\":true, … }\0" as *const u8 as *const libc::c_char) as libc::c_int != 0
        {
            &mut a
        } else {
            0 as *mut libc::c_int
        })
        +
        (if *(b"{ … \"isArg\":true, \"astKind\":\"Expr\", \"isLvalue\":false, … }\0" as *const u8 as *const libc::c_char) as libc::c_int != 0
        {
            5 as libc::c_int
        } else {
            *(0 as *mut libc::c_int)
        })
    } else {
        *(0 as *mut libc::c_int)
    };
}

pub fn main() {
    unsafe { ::std::process::exit(main_0() as i32) }
}
```

## Implementation

Seeder executes the following high-level algorithm:

1. **Parse invocation summaries**: For each line of the `.cpp2c` summary produced by Maki, extract the JSON for macro invocations and arguments.  Discard entries lacking a name, definition or invocation location.  Only keep invocations whose AST category is one of `Expr`, `Stmt`, `Stmts`, `Decl` or `Decls`.
2. **Extract spellings**: Using a lightweight text editor abstraction, extract the exact token spelling of each invocation and argument from the CU source.  This is stored in the invocation/argument summary so that type information can be derived via `__typeof__` when building wrappers.
3. **Generate instrumentation tasks**: For each argument (`isArg = true`) and each invocation (`isArg = false`), call `genInstrumentationTasks` to compute begin/end wrapper fragments based on `astKind`, `isLvalue`, `name` and `canFn`.  This function returns a list of edits (each specifying line, column and insertion string) tailored to the region category.
4. **Insert tasks into the CU**: Apply the instrumentation tasks to the CU via a mutable text editor.  Tasks are ordered so that later insertions do not affect earlier offsets.
5. **Return instrumented source**: Serialize the edits back into a single string.  The result compiles exactly like the input C code but contains tags that the Rust side can parse.

## Limitations

Seeder deliberately skips instrumentation when it cannot confidently map a region back to user code:

- If either the expansion or the definition of the macro lies in a non-project header (usually system headers) that Hayroll Pioneer executes concretely, Seeder does not insert tags (see the early return in the implementation).  This avoids polluting standard library macros.
- AST categories outside the supported set (`Expr`, `Stmt`, `Stmts`, `Decl`, `Decls`) are ignored.
- Statement macros that are expanded right after an `if` or `else` keyword are not handled correctly.
