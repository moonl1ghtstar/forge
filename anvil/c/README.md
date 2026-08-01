# Anvil C Frontend

This frontend handles a small C subset and lowers it into Anvil's shared AST
and IR layers under `anvil/helix/src/ast/helix-ast.h` and `anvil/helix/src/ir/`.

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

`C source -> C lexer -> C parser -> AST -> semantic analysis -> IR -> optimizer -> x86-64 asm -> nasm/ld -> exe`
