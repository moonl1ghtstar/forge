# Quick Start

This guide shows how to create and run your first Helix program with Forge.

By the end of this guide, you will:

- Create a `.hlx` source file
- Import a standard module
- Compile a program
- Run the generated executable

---

# 1. Create a Helix File

Create a file named:

```text
main.hlx
```

Add the following code:

```hlx
import console

console.print("Hello from Helix!");
```

This program imports the built-in `console` module and prints a message.

---

# 2. Compile and Run

Open a terminal in the project directory.

Compile the program:

```powershell
forge main.hlx
```

or:

```powershell
4g main.hlx
```

Forge will process the source file through the compiler pipeline:

```text
main.hlx
   ↓
Lexer
   ↓
Parser
   ↓
Semantic Analysis
   ↓
IR Builder
   ↓
IR Optimizer
   ↓
Code Generation
   ↓
x86-64 Assembly
   ↓
COFF Object
   ↓
Executable
```

After successful compilation:

```text
main.exe
```

will be created.

Run it:

```powershell
.\main.exe
```

Output:

```text
Hello from Helix!
```

---

# 3. Compile and Run Automatically

You can use `-run` to compile and execute immediately:

```powershell
forge main.hlx -run
```

Output:

```text
Hello from Helix!
```

---

# 4. Variables

Helix supports multiple variable declarations.

Example:

```hlx
let message = "Hello Forge";

console.print(message);
```

Variables can use:

```text
var
let
const
global
```

---

# 5. Functions

Functions are declared using `function`.

Example:

```hlx
import console

function greet(name) {
    console.print(name);
}

greet("Hello!");
```

Output:

```text
Hello!
```

---

# 6. Using Modules

Modules are imported using:

```hlx
import <module>
```

Example:

```hlx
import console
```

You can also import specific functions:

```hlx
import console { print }
```

Available modules are stored in:

```text
forge/bin/lib/
```

---

# 7. Debugging

Forge provides several inspection options.

## Show tokens

```powershell
forge main.hlx -dump-tokens
```

## Show AST

```powershell
forge main.hlx -dump-ast
```

## Show IR

```powershell
forge main.hlx -dump-ir
```

## Generate assembly

```powershell
forge main.hlx -asm
```

---

# 8. Project Example

A simple project:

```text
HelloWorld/
│
├── main.hlx
└── main.exe
```

main.hlx:

```hlx
import console

console.print("Hello World!");
```

Build:

```powershell
forge main.hlx
```

Run:

```powershell
.\main.exe
```

---

# Next Step

Continue learning:

- `../language/` — Helix language reference
- `../modules/` — Standard module documentation
- `../compiler/` — Forge compiler internals