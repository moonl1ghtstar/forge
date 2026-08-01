# Helix

> A modern, native programming language for building fast command-line applications.

Helix is a statically compiled programming language developed for the **Anvil Compiler**. Its syntax is inspired by JavaScript while producing efficient native x86-64 executables through a multi-stage compiler pipeline.

Current Status: **Experimental (v0.2.0)**

---

# Features

- Native x86-64 code generation
- JavaScript-inspired syntax
- Fast recursive-descent parser
- Top-level statements
- Module system
- Struct support
- Simple FFI (`extern`)
- Intermediate Representation (IR)
- Semantic analysis
- Easy-to-read compiler diagnostics

---

# Hello World

```helix
import console { print }

print("Hello, World!");
```

Compile:

```bash
anv hello.hlx
```

Run:

```bash
hello.exe
```

---

# Variables

Helix provides four kinds of variable declarations.

```helix
global int Counter = 0;

const MaxValue = 100;

var Message = "Hello";

let Number = 10;
```

| Keyword | Description |
|----------|-------------|
| `global` | Global variable exported by the module |
| `const` | Immutable constant |
| `var` | Global variable with inferred type |
| `let` | Local block-scoped variable |

---

# Functions

```helix
function add(a, b) {
    return a + b;
}
```

Calling:

```helix
let Result = add(5, 10);
```

Functions currently support **up to four parameters**.

---

# Top-Level Execution

Unlike C, Helix allows executable statements outside functions.

```helix
import console { print }

print("Program started");
```

Anvil automatically wraps top-level statements into `main()`.

If `main()` already exists, top-level statements are inserted before its body.

---

# Modules

Import an entire standard module:

```helix
import console
```

Import only specific functions:

```helix
import console { print, clear }
```

Import a project file:

```helix
import "math.hlx"
```

Selective project import:

```helix
import "math.hlx" { add, sub }
```

Module functions are automatically namespaced.

```helix
console.print("Hello");
```

---

# Structs

Declare a struct:

```helix
struct Point {
    let x;
    let y;
}
```

Initialize using positional values:

```helix
Point p { 5, 10 };
```

Or named fields:

```helix
Point p {
    x = 5;
    y = 10;
};
```

Access fields:

```helix
print(p.x);
print(p.y);
```

---

# Control Flow

## If

```helix
if (score >= 90) {
    print("A");
}
else {
    print("B");
}
```

---

## Switch

```helix
switch {
    case (value == 1) {
        print("One");
    }

    case (value == 2) {
        print("Two");
    }

    case {
        print("Default");
    }
}
```

---

## While

```helix
while (True) {
    break();
}
```

---

## Until

```helix
until (finished) {
    update();
}
```

---

## Do-While

```helix
do {
    update();
}
while (running)
```

---

## For

```helix
for (int i = 0; i < 10; i++) {
    print(i);
}
```

---

## Loop Controls

```helix
break();
pass();
```

- `break()` exits the loop.
- `pass()` skips to the next iteration.

---

# Data Types

Current built-in types:

| Type | Description |
|------|-------------|
| `int` | Integer |
| `long` | 64-bit integer |
| `float` | Floating point |
| `bool` | Boolean |
| `str` | String |

---

# Operators

Arithmetic

```helix
+
-
*
/
```

Comparison

```helix
==
!=
<
>
<=
>=
```

Logical

```helix
!
```

---

# Comments

Single line

```helix
// comment
```

Multi-line

```helix
/*
    comment
*/
```

---

# Standard Library

Current standard modules include:

| Module | Description |
|---------|-------------|
| `console` | Console I/O utilities |

Example:

```helix
import console

console.clear();
console.print("Hello");
```

---

# Foreign Function Interface

Helix can call native C functions.

```helix
extern {
    function puts(text: string) -> int;
}
```

---

# Compiler Architecture

Anvil compiles Helix using several compilation stages.

```
Source (.hlx)
      │
      ▼
Lexer
      │
      ▼
Parser
      │
      ▼
AST
      │
      ▼
Semantic Analysis
      │
      ▼
IR Generation
      │
      ▼
IR Optimization
      │
      ▼
Assembly Generation
      │
      ▼
Executable
```

---

# Project Structure

```
bin/
├── bin/
│   ├── anv.exe
│   ├── fg.exe
│   └── 4g.exe
│
├── lib/
│   ├── package/
│   └── console/
│
└── share/
    ├── examples/
    └── templates/
```

---

# Example

```helix
import console { print }

struct User {
    let age;
    let score;
}

function main() {

    User me {
        age = 17;
        score = 100;
    };

    print(me.age);
    print(me.score);
}
```

---

# Compiler

Helix is compiled by **Anvil**.

Anvil performs:

- Lexical analysis
- Parsing
- Semantic analysis
- IR generation
- IR optimization
- Native code generation

The compiler is written entirely in **C**.

---

# Current Status

Helix is under active development.

Implemented:

- Lexer
- Parser
- AST
- Semantic Analysis
- Module System
- Structs
- Intermediate Representation (IR)
- IR Optimizer
- Native Code Generation

Planned:

- Classes
- Enums
- Generics
- Interfaces
- Pattern Matching
- Package Manager
- Cross-platform Backend
- Language Server (LSP)
- Debug Information

---

# License

Helix and Anvil are open-source projects.

Contributions, bug reports, and feature suggestions are welcome.