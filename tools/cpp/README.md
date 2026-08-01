# C++ Tools

Use this folder for helper programs only.

Suggested layout:

- `src/` - tool source files
- `include/` - local headers
- `tests/` - tool-specific tests
- `build/` - generated artifacts, ignored by git

Build:

```bash
cmake -S tools/cpp -B tools/cpp/build
cmake --build tools/cpp/build
```

Commands:

- `anvil-tools token-dump <file>`
- `anvil-tools ast-dump <file>`
- `anvil-tools asm-preview <file>`
- `anvil-tools smoke-test`
