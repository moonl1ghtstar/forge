"""End-to-end NASM compatibility test.

For every .asm file produced by the Anvil compiler codegen, assemble it
with both NASM (-f win64) and the built-in Anvil assembler, then compare:
  - .text section bytes
  - relocation records (offset, type, symbol)
  - non-section symbols (name, section, value, storage class)

COFF metadata that may legally differ (timestamps, section symbols,
symbol order, string table layout) is excluded from comparison.

Usage: python tests/test_asm_e2e.py <asm-dir> [--keep]
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

TEST_ROOT = Path(__file__).parent
PROJECT_ROOT = TEST_ROOT.parent
if str(TEST_ROOT) not in sys.path:
    sys.path.insert(0, str(TEST_ROOT))

import utils

NASM = shutil.which("nasm")
GCC = shutil.which("gcc")


def ensure_ascii_root():
    mapped = Path("Q:/")
    created = False
    if not mapped.exists():
        subprocess.run([
            "powershell", "-NoProfile", "-Command",
            f"subst Q: '{PROJECT_ROOT}'",
        ], check=True)
        created = True
    return mapped, created


def build_helper(workspace: Path) -> Path:
    root, _ = ensure_ascii_root()
    helper_c = workspace / "asm_helper.c"
    helper_exe = workspace / "asm_helper.exe"
    helper_c.write_text(
        """#include <stdio.h>
#include \"anv-asm.h\"
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    return anv_assemble_file(argv[1], argv[2]);
}
""",
        encoding="ascii",
    )
    env = os.environ.copy()
    env["TEMP"] = r"C:\temp"
    env["TMP"] = r"C:\temp"
    Path(env["TEMP"]).mkdir(parents=True, exist_ok=True)
    cmd = [
        GCC, "-std=c11", "-O2",
        "-I" + str(root / "anvil" / "assembler" / "src" / "anv-asm"),
        "-I" + str(root / "anvil" / "assembler" / "src" / "parser"),
        "-I" + str(root / "anvil" / "assembler" / "src" / "lexer"),
        "-I" + str(root / "anvil" / "assembler" / "src" / "x86-encode"),
        "-I" + str(root / "anvil" / "assembler" / "src" / "coff-writer"),
        "-I" + str(root / "anvil" / "helix" / "src" / "errors"),
        str(helper_c),
        str(root / "anvil" / "assembler" / "src" / "anv-asm" / "anv-asm.c"),
        str(root / "anvil" / "assembler" / "src" / "parser" / "asm-parser.c"),
        str(root / "anvil" / "assembler" / "src" / "lexer" / "asm-lexer.c"),
        str(root / "anvil" / "assembler" / "src" / "x86-encode" / "x86-encode.c"),
        str(root / "anvil" / "assembler" / "src" / "coff-writer" / "coff-writer.c"),
        str(root / "anvil" / "helix" / "src" / "errors" / "helix-errors.c"),
        "-o", str(helper_exe),
    ]
    utils.run_command(cmd, expected_code=0, timeout=120, env=env)
    return helper_exe


def parse_coff(data: bytes):
    """Extract sections, relocations, symbols, string table from a COFF obj."""
    num_sections = int.from_bytes(data[2:4], "little")
    sym_ptr = int.from_bytes(data[8:12], "little")
    sym_count = int.from_bytes(data[12:16], "little")

    sections = {}
    for i in range(num_sections):
        h = 20 + i * 40
        name = data[h:h + 8].split(b"\x00", 1)[0].decode("ascii", "replace")
        size = int.from_bytes(data[h + 16:h + 20], "little")
        ptr = int.from_bytes(data[h + 20:h + 24], "little")
        reloc_ptr = int.from_bytes(data[h + 24:h + 28], "little")
        reloc_count = int.from_bytes(data[h + 32:h + 34], "little")
        sections[name] = {
            "size": size,
            "data": data[ptr:ptr + size],
            "relocs": [],
        }
        if reloc_count:
            for r in range(reloc_count):
                p = reloc_ptr + r * 10
                sections[name]["relocs"].append({
                    "va": int.from_bytes(data[p:p + 4], "little"),
                    "sym_idx": int.from_bytes(data[p + 4:p + 8], "little"),
                    "type": int.from_bytes(data[p + 8:p + 10], "little"),
                })

    if sym_ptr == 0:
        return sections, [], ""

    sym_end = sym_ptr + sym_count * 18
    # anv pads the symbol table to a 4-byte boundary before the string
    # table; NASM 3.02rc9 does not. Try both positions and keep the one
    # that yields a plausible (in-range) string-table size.
    str_off = sym_end
    str_table_size = 0
    for cand in sorted({sym_end, sym_ptr + ((sym_count * 18 + 3) & ~3)}):
        if cand + 4 > len(data):
            continue
        size = int.from_bytes(data[cand:cand + 4], "little")
        if 4 <= size <= len(data) - cand:
            str_off = cand
            str_table_size = size
            break
    string_table = data[str_off:str_off + str_table_size]

    def sym_name(off):
        p = sym_ptr + off * 18
        if data[p:p + 4] == b"\x00\x00\x00\x00":
            s_off = int.from_bytes(data[p + 4:p + 8], "little")
            end = string_table.index(b"\x00", s_off)
            return string_table[s_off:end].decode("ascii", "replace")
        return data[p:p + 8].split(b"\x00", 1)[0].decode("ascii", "replace")

    symbols = []
    for s in range(sym_count):
        p = sym_ptr + s * 18
        value = int.from_bytes(data[p + 8:p + 12], "little", signed=True)
        section = int.from_bytes(data[p + 12:p + 14], "little", signed=True)
        storage = data[p + 16]
        aux = data[p + 17]
        symbols.append({
            "name": sym_name(s),
            "value": value,
            "section": section,
            "storage": storage,
            "aux": aux,
        })
    return sections, symbols, string_table


def section_name(sec_num, sections):
    if sec_num <= 0:
        return f"UNDEF[{sec_num}]"
    names = list(sections.keys())
    if sec_num - 1 < len(names):
        return names[sec_num - 1]
    return f"SEC[{sec_num}]"


def extract_text(data: bytes) -> bytes:
    sections, _, _ = parse_coff(data)
    return sections.get(".text", {}).get("data", b"")


def normalize_relocs(sections, symbols):
    out = []
    for name in sorted(sections.keys()):
        for r in sections[name]["relocs"]:
            sym = symbols[r["sym_idx"]]["name"] if r["sym_idx"] < len(symbols) else f"<{r['sym_idx']}>"
            out.append((name, r["va"], r["type"], sym))
    return sorted(out)


def normalize_symbols(sections, symbols):
    out = []
    for s in symbols:
        # Only external symbols (storage class 2) are compared. Section
        # symbols and local labels may legitimately differ in name between
        # assemblers (NASM mangles local labels, e.g. main_entry.Lcp1_sloop).
        if s["storage"] != 2:
            continue
        out.append((s["name"], section_name(s["section"], sections), s["value"], s["storage"]))
    return sorted(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("asm_dir", type=Path)
    ap.add_argument("--keep", action="store_true")
    args = ap.parse_args()

    if not NASM:
        raise AssertionError("nasm not found")
    if not GCC:
        raise AssertionError("gcc not found")

    root, created = ensure_ascii_root()
    Path(r"C:\temp").mkdir(parents=True, exist_ok=True)
    ws = Path(tempfile.mkdtemp(prefix="anv-e2e-", dir=r"C:\temp"))
    failures = 0
    total = 0
    try:
        helper = build_helper(ws)
        for asm_path in sorted(args.asm_dir.glob("*.asm")):
            name = asm_path.stem
            ref_obj = ws / f"{name}.nasm.obj"
            got_obj = ws / f"{name}.anv.obj"
            try:
                utils.run_command([NASM, "-f", "win64", "-o", str(ref_obj), str(asm_path)], expected_code=0)
            except AssertionError:
                print(f"[SKIP ] {name} (nasm rejects input)")
                continue
            rc = subprocess.run([str(helper), str(asm_path), str(got_obj)], capture_output=True, text=True)
            if rc.returncode != 0:
                print(f"[FAIL ] {name}: anv assembler rc={rc.returncode}")
                print(rc.stderr.strip() or "(no stderr)")
                failures += 1
                total += 1
                continue

            total += 1
            ref = parse_coff(ref_obj.read_bytes())
            got = parse_coff(got_obj.read_bytes())
            problems = []

            ref_text = ref[0].get(".text", {}).get("data", b"")
            got_text = got[0].get(".text", {}).get("data", b"")
            if ref_text != got_text:
                diff = next((i for i in range(min(len(ref_text), len(got_text)))
                             if ref_text[i] != got_text[i]), min(len(ref_text), len(got_text)))
                problems.append(
                    f".text differs at {diff}: nasm={ref_text[diff:diff+8].hex()} "
                    f"anv={got_text[diff:diff+8].hex()} "
                    f"(len nasm={len(ref_text)} anv={len(got_text)})"
                )

            ref_rel = normalize_relocs(ref[0], ref[1])
            got_rel = normalize_relocs(got[0], got[1])
            if ref_rel != got_rel:
                problems.append(f"relocations differ:\n  nasm={ref_rel}\n  anv ={got_rel}")

            ref_sym = normalize_symbols(ref[0], ref[1])
            got_sym = normalize_symbols(got[0], got[1])
            if ref_sym != got_sym:
                problems.append(f"symbols differ:\n  nasm={ref_sym}\n  anv ={got_sym}")

            if problems:
                print(f"[FAIL ] {name}")
                for p in problems:
                    print("   " + p.replace("\n", "\n   "))
                failures += 1
            else:
                print(f"[PASS ] {name} ({len(ref_text)} text bytes, {len(ref_rel)} relocs)")

        print(f"\nPassed: {total - failures}/{total}")
        if failures:
            return 1
        return 0
    finally:
        if not args.keep:
            shutil.rmtree(ws, ignore_errors=True)
        if created:
            subprocess.run([
                "powershell", "-NoProfile", "-Command", "subst Q: /d",
            ], check=False)


if __name__ == "__main__":
    sys.exit(main())
