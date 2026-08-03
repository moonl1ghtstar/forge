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

CASES = [
    (
        "movsd_store",
        """section .text
global f
f:
  movsd [rsp+8], xmm0
  ret
""",
    ),
    (
        "movsd_load",
        """section .text
global f
f:
  movsd xmm0, [rsp+8]
  ret
""",
    ),
    (
        "movq_xmm_from_gpr",
        """section .text
global f
f:
  movq xmm0, rax
  ret
""",
    ),
    (
        "movq_gpr_from_xmm",
        """section .text
global f
f:
  movq rax, xmm0
  ret
""",
    ),
    (
        "comisd",
        """section .text
global f
f:
  comisd xmm0, xmm1
  ret
""",
    ),
    (
        "subsd",
        """section .text
global f
f:
  subsd xmm0, xmm1
  ret
""",
    ),
    (
        "mulsd",
        """section .text
global f
f:
  mulsd xmm0, xmm1
  ret
""",
    ),
    (
        "cvtsi2sd",
        """section .text
global f
f:
  cvtsi2sd xmm0, rax
  ret
""",
    ),
    (
        "cvttsd2si",
        """section .text
global f
f:
  cvttsd2si rax, xmm0
  ret
""",
    ),
    (
        "setcc_mem",
        """section .text
global f
f:
  setb byte [rsp+116]
  ret
""",
    ),
    (
        "mov_byte_imm",
        """section .text
global f
f:
  mov byte [rdi], 46
  ret
""",
    ),
    (
        "mov_word_imm",
        """section .text
global f
f:
  mov word [rdi], 0x1234
  ret
""",
    ),
    (
        "mov_dword_imm",
        """section .text
global f
f:
  mov dword [rdi], 0x12345678
  ret
""",
    ),
    (
        "mov_qword_imm",
        """section .text
global f
f:
  mov qword [rdi], 0x12345678
  ret
""",
    ),
]


def extract_text_section(data: bytes) -> bytes:
    """Return the raw .text payload of a COFF object file.

    Only .text bytes are compared: different COFF writers may legally
    differ in timestamp, symbol order, string table offsets, relocation
    order, and section offsets while emitting identical code.
    """
    if len(data) < 20:
        raise AssertionError("COFF object too short for file header")
    num_sections = int.from_bytes(data[2:4], "little")
    for i in range(num_sections):
        header = 20 + i * 40
        if header + 40 > len(data):
            raise AssertionError("COFF section header out of bounds")
        name = data[header:header + 8].split(b"\x00", 1)[0]
        if name != b".text":
            continue
        size = int.from_bytes(data[header + 16:header + 20], "little")
        ptr = int.from_bytes(data[header + 20:header + 24], "little")
        payload = data[ptr:ptr + size]
        if len(payload) != size:
            raise AssertionError("COFF .text payload out of bounds")
        return payload
    raise AssertionError("COFF object has no .text section")


def ensure_ascii_root():
    mapped = Path("Q:/")
    created = False
    if not mapped.exists():
        subprocess.run([
            "powershell",
            "-NoProfile",
            "-Command",
            f"subst Q: '{PROJECT_ROOT}'",
        ], check=True)
        created = True
    return mapped, created


def find_tool(name: str, explicit: str | None = None) -> str:
    if explicit:
        path = Path(explicit)
        if path.exists():
            return str(path)
    tool = shutil.which(name)
    if tool:
        return tool
    raise AssertionError(f"Required tool not found: {name}")


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
        GCC,
        "-std=c11",
        "-O2",
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
        "-o",
        str(helper_exe),
    ]
    utils.run_command(cmd, expected_code=0, timeout=120, env=env)
    return helper_exe


def assemble_and_compare(helper_exe: Path, nasm_exe: str, workspace: Path, name: str, asm_text: str):
    asm_path = workspace / f"{name}.asm"
    ref_obj = workspace / f"{name}.nasm.obj"
    got_obj = workspace / f"{name}.anv.obj"
    asm_path.write_text(asm_text, encoding="ascii")

    utils.run_command([nasm_exe, "-f", "win64", "-o", str(ref_obj), str(asm_path)], expected_code=0)
    utils.run_command([str(helper_exe), str(asm_path), str(got_obj)], expected_code=0)

    ref = extract_text_section(ref_obj.read_bytes())
    got = extract_text_section(got_obj.read_bytes())
    if ref != got:
        limit = min(len(ref), len(got))
        diff = next((i for i in range(limit) if ref[i] != got[i]), limit)
        ref_slice = ref[diff:diff + 8].hex() if diff < len(ref) else ""
        got_slice = got[diff:diff + 8].hex() if diff < len(got) else ""
        note = ""
        if diff < len(ref) and diff < len(got):
            note = (
                f"\nFirst differing byte: nasm[{diff}]=0x{ref[diff]:02x} "
                f"anv[{diff}]=0x{got[diff]:02x}"
            )
        elif len(ref) != len(got):
            note = f"\nLengths differ: nasm={len(ref)} bytes, anv={len(got)} bytes (common prefix matches)"
        raise AssertionError(
            f".text bytes differ for {name} at offset {diff} "
            f"(nasm={ref_slice} anv={got_slice})"
            f"\nnasm .text ({len(ref)} bytes): {ref.hex()}"
            f"\nanv  .text ({len(got)} bytes): {got.hex()}"
            f"{note}"
        )


def main():
    if not NASM:
        raise AssertionError("nasm not found")
    if not GCC:
        raise AssertionError("gcc not found")

    root, created = ensure_ascii_root()
    Path(r"C:\temp").mkdir(parents=True, exist_ok=True)
    ws = Path(tempfile.mkdtemp(prefix="anv-asm-", dir=r"C:\temp"))
    try:
        helper = build_helper(ws)
        for name, asm_text in CASES:
            print(f"[ RUN ] {name}")
            assemble_and_compare(helper, NASM, ws, name, asm_text)
            print(f"[ PASS ] {name}")
        print(f"\nPassed: {len(CASES)}/{len(CASES)}")
        return 0
    finally:
        shutil.rmtree(ws, ignore_errors=True)
        if created:
            subprocess.run([
                "powershell",
                "-NoProfile",
                "-Command",
                "subst Q: /d",
            ], check=False)


if __name__ == "__main__":
    sys.exit(main())
