# Forge C Frontend

This frontend handles a small C subset and lowers it into Forge's shared AST
IR under `forge/helix/src/ast/helix-ast.h`.

Supported subset:

- `int` functions
- local variables: `int x = expr;`
- assignments
- `if` / `else`
- `while`
- `for`
- `return`
- function calls
- `break`
- `continue`

Pipeline:

`C source -> C lexer -> C parser -> shared AST IR -> semantic analysis -> x86-64 asm -> nasm/ld -> exe`
