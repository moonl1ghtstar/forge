# Tools

Helper tools live here. Core compiler stays in `forge/`.

Current tools:

- `token-dump`
- `ast-dump`
- `asm-preview`
- `smoke-test`

Suggested split:

- `tools/cpp/` - C++ helper utilities
- `tools/scripts/` - automation scripts
- `tools/assets/` - shared non-code files

Rule:

- no compiler core code here
- no C++ runtime dependency in `forge/`
