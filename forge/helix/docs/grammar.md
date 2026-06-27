# Helix Language Grammar & Syntax Specification

Version: 0.2.0

Helix is a native programming language designed for writing command-line applications. It features a JavaScript-like syntax and compiles to native x86-64 Win64 assembly via the Forge compiler.

---

## 1. Program Structure & Entry Point

A Helix program (typically saved with a `.hlx` extension) consists of module imports, external declarations, function definitions, and statements.

### Top-level Execution
Helix supports top-level execution. If statements exist outside any explicit function declaration, they are implicitly wrapped by the compiler into a `main()` function.

```hlx
import console { print }

print("Hello, Helix!");
```

If a `main()` function is explicitly defined, the top-level statements outside functions will be prepend-inserted at the beginning of the `main()` function.

---

## 2. Lexical Elements

### Keywords
The following words are reserved and cannot be used as identifiers:
`function`, `global`, `const`, `var`, `let`, `if`, `else`, `switch`, `case`, `for`, `while`, `until`, `do`, `pass`, `break`, `return`, `import`, `extern`, `int`, `str`, `bool`, `float`, `long`.

### Identifiers
Identifiers name variables, functions, and modules. 
* Variables, constants, and function names **must be written in CamelCase** (e.g., `myVariable`, `calculateTotal`).
* They must start with a letter (`a-z`, `A-Z`) or an underscore `_`, followed by any number of alphanumeric characters or underscores.

### Literals & Comments
* **Integer Literals**: Decimal (`42`) or Hexadecimal (`0xFF`).
* **String Literals**: Enclosed in double quotes (`"Hello"`).
* **Single-line Comments**: Start with `//`.
* **Multi-line Comments**: Start with `/*` and end with `*/`.

---

## 3. Data Types

Helix supports the following data types:

* **`int`**: Standard integer for arithmetic and indexing.
* **`str`**: Character or string type.
* **`bool`**: Boolean type (`True` or `False`).
* **`float`**: Floating-point number for decimal operations.
* **`long`**: Large integer type for extended capacity.

---

## 4. Variables & Scopes

Helix provides strict control over variable mutability and scope. Variables and constants must use **CamelCase** naming.

### Declarations
* **`global <type>`**: File-global scope. Variables declared as global are exported and available when the file is imported by another module.
* **`const`**: A constant value that cannot be changed after initialization.
* **`var`**: File-global scope with dynamic typing.
* **`let`**: Block-scoped variable with dynamic typing.

### Examples
```hlx
global int globalCount = 0;
const maxLimit = 100;
var myDynamicGlobal = "Hello";
let myLocalVar = 10;
```

---

## 5. Functions

### Declaration
Functions are declared using the `function` keyword. Function names must be in **CamelCase**.
```hlx
function calculateSum(a, b) {
    // codes
    return a + b;
}
```

### Return Values
Functions can return values using the `return` statement. If a function is meant to return `void`, you can use an empty `return;` statement.

```hlx
function doSomething() {
    return; // Automatically evaluated as void
}
```

> [!IMPORTANT]
> **Parameter / Argument Limit (Max 4)**
> Due to calling convention constraints in the current Forge x86-64 assembly generator, **functions are strictly limited to a maximum of 4 parameters**.

---

## 6. Control Flow

Helix provides comprehensive control flow structures. 
*Note: Curly braces `{}` are mandatory for all control flow blocks.*

### If / Else If / Else
```hlx
if (condition) {
    // codes
} else if (anotherCondition) {
    // codes
} else {
    // codes
}
```

### Switch Statement
A `switch` block evaluates cases. An empty `case` acts as the default fallback if no `True` condition is met.
```hlx
switch {
    case (True) {
        // codes
    }
    case (False) {
        // codes
    }
    case {
        // Fallback (default) if no other case is True
    }
}
```

### Loops (For, While, Until, Do)

* **For Loop**: Includes variable declaration, break condition, and increment expression.
```hlx
for(int i = 0; i = 5; i++) {
    // codes
}
```

* **While Loop**: Executes as long as the condition evaluates to `True`. Evaluates condition *before* execution.
```hlx
while(True) {
    // codes
}
```

* **Until Loop**: Executes as long as the condition evaluates to `False`. Evaluates condition *before* execution.
```hlx
until(False) {
    // codes
}
```

* **Do-While Loop**: Executes the block first, then evaluates the condition.
```hlx
do {
    // codes
} while(True)
```

### Loop Controls
* **`break();`**: Stops and exits the current loop.
* **`pass();`**: Skips the rest of the current iteration and moves to the next (equivalent to `continue`).

```hlx
while(True) {
    if(a == 5) {
        break();
    }
}

for(int i = 0; i = 5; i++) {
    if(i == 3) {
        pass();
    }
}
```

---

## 7. Modules & Imports

Helix uses a straightforward import system without trailing semicolons.

### Importing Modules
* **Import All (Forge Root)**: `import <module>`
* **Import All (Project Root)**: `import "<file>.hlx"`
* **Selective Import (Forge Root)**: `import <module> { <func1>, <func2> }`
* **Selective Import (Project Root)**: `import "<file>.hlx" { <func1>, <func2> }`

### Namespacing & Calling Conventions
When importing modules, you can access functions by prefixing them with the module name.
```hlx
import console

console.print("Hello");
```

---

## 8. Built-in Functions & 9. External Function Interface (FFI)

The built-in `console` functions (externally implemented) include:
* `print(value)`: Prints an integer or a string literal to stdout.
* `input()`: Reads an integer from stdin.
* `clear()`: Clears the console.
* `color(hex_value)`: Sets the console text color using a 24-bit RGB hex value.

Extern functions interface with C functions:
```hlx
extern {
    function puts(text: string) -> int;
}
```

---

## 10. Operator Precedence

| Level | Operators | Description | Associativity |
| :--- | :--- | :--- | :--- |
| 1 | `()` | Parentheses | Left-to-right |
| 2 | `-`, `!`, `++` | Unary Minus, Logical NOT, Increment | Right-to-left |
| 3 | `*`, `/` | Multiplication, Division | Left-to-right |
| 4 | `+`, `-` | Addition, Subtraction | Left-to-right |
| 5 | `<`, `>`, `<=`, `>=` | Relational Comparisons | Left-to-right |
| 6 | `==`, `!=` | Equality Comparisons | Left-to-right |

---

## 11. Formal Grammar (EBNF)

Below is the formal EBNF grammar defining the Helix language:

```ebnf
program        = { import_stmt | extern_block | function | statement } ;

import_stmt    = "import" ( IDENT | STRING ) [ "{" IDENT { "," IDENT } "}" ] ;

extern_block   = "extern" "{" { extern_decl } "}" ;

extern_decl    = "function" IDENT "(" [ extern_params ] ")" [ "->" IDENT ] ";" ;

extern_params  = extern_param { "," extern_param } ;

extern_param   = IDENT [ ":" IDENT ] ;

function       = "function" IDENT "(" [ params ] ")" block ;

params         = IDENT { "," IDENT } ;

block          = "{" { statement } "}" ;

statement      = var_decl
               | assign_stmt
               | if_stmt
               | switch_stmt
               | for_stmt
               | while_stmt
               | until_stmt
               | do_stmt
               | loop_ctrl_stmt
               | return_stmt
               | expr_stmt ;

var_decl       = ( "global" IDENT | "const" | "var" | "let" ) IDENT "=" expr ";" ;

assign_stmt    = IDENT "=" expr ";" ;

if_stmt        = "if" "(" expr ")" block { "else" "if" "(" expr ")" block } [ "else" block ] ;

switch_stmt    = "switch" "{" { "case" "(" expr ")" block } [ "case" block ] "}" ;

for_stmt       = "for" "(" ( IDENT IDENT | "var" IDENT | "let" IDENT ) "=" expr ";" expr ";" expr ")" block ;

while_stmt     = "while" "(" expr ")" block ;

until_stmt     = "until" "(" expr ")" block ;

do_stmt        = "do" block "while" "(" expr ")" ;

loop_ctrl_stmt = ( "break" | "pass" ) "(" ")" ";" ;

return_stmt    = "return" [ expr ] ";" ;

expr_stmt      = expr ";" ;

expr           = compare { ( "==" | "!=" ) compare } ;

compare        = add_sub { ( "<" | ">" | "<=" | ">=" ) add_sub } ;

add_sub        = mul_div { ( "+" | "-" ) mul_div } ;

mul_div        = unary { ( "*" | "/" ) unary } ;

unary          = ( "-" | "!" ) unary
               | primary ;

primary        = NUMBER
               | STRING
               | "True"
               | "False"
               | call
               | IDENT
               | "(" expr ")" ;

call           = IDENT [ "." IDENT ] "(" [ args ] ")" ;

args           = expr { "," expr } ;
```
