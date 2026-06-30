# Forge IR

Forge now lowers both Helix and C ASTs into a shared IR before codegen.

Pipeline:

`source -> lexer -> parser -> AST -> semantic analysis -> IR builder -> optimizer -> x86-64 codegen`

IR shape:

- `IRProgram` holds functions and extern declarations.
- `IRFunction` holds locals, basic blocks, and SSA temp count.
- `IRBasicBlock` holds linear instruction lists and CFG edges through jumps/branches.
- `IRInstruction` is three-address code with one optional result temp.
- `IRValue` carries constants, temps, locals, and string literals.

Supported instructions:

- `const`
- `load`
- `store`
- `add`
- `sub`
- `mul`
- `div`
- `mod`
- `neg`
- `cmp`
- `jump`
- `branch`
- `call`
- `return`

Current passes:

- constant folding
- dead instruction elimination

Debug dump:

`forge hello.hlx -dump-ir`
