# Forge Coding Style Guide

This document defines the coding conventions used in the Forge compiler project.

The purpose of this guide is to keep the codebase consistent,
readable, and maintainable as Forge grows.

---

# General Principles

## Keep Code Simple

Prefer readable and explicit code over clever solutions.

Good:

```c
if (token.type == TOKEN_IDENTIFIER) {
    parse_identifier();
}
```

Avoid:

```c
token.type == TOKEN_IDENTIFIER ? parse_identifier() : 0;
```

---

## One Responsibility Per Function

Functions should have a clear purpose.

Good:

```c
static int parse_expression(Parser *parser);

static void free_ast_node(ASTNode *node);
```

Avoid:

```c
static void parse_everything_and_generate_code();
```

Large operations should be split into smaller functions.

---

# File Organization

Forge uses a modular structure:

```
forge/
├── lexer/
├── parser/
├── sema/
├── ir/
├── codegen/
└── errors/
```

Each module should contain:

```
module/
├── include headers
└── implementation files
```

Example:

```
lexer/
├── helix-lexer.h
└── helix-lexer.c
```

---

# Naming Conventions

## Functions

Use `snake_case`.

Good:

```c
parse_expression()
generate_code()
ir_build_program()
```

Bad:

```c
parseExpression()
GenerateCode()
```

---

## Variables

Use descriptive `snake_case` names.

Good:

```c
source_path
token_count
current_function
```

Bad:

```c
sp
cnt
tmp
```

Short names are allowed only for small scopes:

```c
for (int i = 0; i < count; i++)
```

---

## Types

Use `PascalCase`.

Good:

```c
ASTNode
IRProgram
Token
ParserContext
```

---

## Constants

Use uppercase snake case.

Good:

```c
MAX_TOKEN_LENGTH
DEFAULT_STACK_SIZE
```

---

## Static Functions

Internal functions should use `static`.

Example:

```c
static int compile_source_to_asm(
    const char *source_path,
    const char *asm_path
);
```

Functions not exposed outside the module should not be exported.

---

# Header Files

## Header Guards

Use:

```c
#ifndef FORGE_MODULE_NAME_H
#define FORGE_MODULE_NAME_H

#endif
```

Example:

```c
#ifndef FORGE_LEXER_H
#define FORGE_LEXER_H

typedef struct {
    int type;
} Token;

#endif
```

---

## Include Order

Preferred order:

```c
// Standard library
#include <stdio.h>
#include <stdlib.h>

// Platform headers
#include <windows.h>

// Forge headers
#include "lexer/lexer.h"
#include "errors/errors.h"
```

---

# Formatting

## Indentation

Use 4 spaces.

Do not use tabs.

Good:

```c
if (condition) {
    do_work();
}
```

---

## Braces

Always use braces.

Good:

```c
if (value > 0) {
    return value;
}
```

Avoid:

```c
if (value > 0)
    return value;
```

---

## Line Length

Keep lines reasonably short.

Recommended:

```
< 100 characters
```

Long expressions should be split.

---

# Memory Management

Forge is written in C and uses manual memory management.

Every allocation must have a matching free.

Example:

```c
char *buffer = malloc(size);

if (!buffer) {
    return NULL;
}

...

free(buffer);
```

---

## Ownership Rules

Functions should clearly define ownership.

Example:

```c
ASTNode *parse_program();
```

Documentation should specify:

```
Returns:
    Newly allocated ASTNode.
    Caller must free using ast_free().
```

---

## Avoid Memory Leaks

Before returning early:

```c
free(resource);

return error;
```

Check every error path.

---

# Error Handling

Forge uses return codes for errors.

Preferred:

```c
if (parse_failed) {
    return 1;
}
```

Avoid:

```c
exit(1);
```

inside library modules.

Only the CLI layer should terminate the program.

---

# Error Messages

Use:

```
Forge error: <description>
```

Example:

```c
fprintf(
    stderr,
    "Forge error: cannot open file '%s'\n",
    path
);
```

Errors should explain:

- what failed
- where it failed
- possible solution if available

---

# AST / IR Style

## AST Ownership

AST nodes are allocated dynamically.

Every AST allocation requires cleanup:

```c
ast_free(node);
```

---

## IR Ownership

IR objects follow:

```
IRProgram *
    |
    +-- IRInstruction *
```

The owner of `IRProgram` is responsible for freeing all child objects.

---

# Compiler Pipeline Rules

The compiler pipeline should remain separated:

```
Lexer
 ↓
Parser
 ↓
Semantic Analysis
 ↓
IR
 ↓
Optimizer
 ↓
Codegen
```

Do not mix responsibilities.

Bad:

```c
parser_emit_assembly();
```

Good:

```c
parser -> AST -> IR -> Codegen
```

---

# Logging

Use Forge debug logging format.

Example:

```c
fprintf(stderr,
    "[Forge] compiling: %s\n",
    file
);
```

Available levels:

```
[Forge]
[Forge debug]
[Forge warning]
[Forge error]
```

---

# Comments

Comments should explain why,
not what.

Good:

```c
// Windows x64 requires 32 bytes of shadow space before CALL.
stack_size += 32;
```

Bad:

```c
// Add 32
stack_size += 32;
```

---

# Git Changes

Code style changes should not be mixed with feature changes.

Bad:

```
feat(parser): add while parsing and reformat all files
```

Good:

```
style(parser): format parser files

feat(parser): add while parsing
```

---

# Testing

New features should include tests when possible.

Example:

```
helix/test/
├── loops.hlx
├── structs.hlx
└── functions.hlx
```

Tests should verify:

- parsing
- semantic analysis
- generated output
- runtime behavior

---

# Pull Request Checklist

Before submitting:

- [ ] Code follows this style guide
- [ ] No memory leaks introduced
- [ ] Error paths are handled
- [ ] New features include tests
- [ ] Compiler pipeline separation is maintained
- [ ] Build succeeds

---

Consistent code style allows Forge to scale
from a personal compiler project into a maintainable systems project.
```