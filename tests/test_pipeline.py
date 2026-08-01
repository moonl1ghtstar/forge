import os
import sys
import shutil
from pathlib import Path

TEST_ROOT = Path(__file__).parent
if str(TEST_ROOT) not in sys.path:
    sys.path.insert(0, str(TEST_ROOT))

import utils

HELLO_HLX = os.path.join(TEST_ROOT, "cases", "hello.hlx")

def test_1_token_dump(anv_bin):
    ws = utils.create_temp_workspace()
    try:
        src = os.path.join(ws, "hello.hlx")
        shutil.copy2(HELLO_HLX, src)
        
        stdout, stderr, code = utils.run_command([anv_bin, src, "-dump-tokens"], expected_code=0)
        
        if not stdout.strip() and not stderr.strip():
            raise AssertionError("Token dump output is empty")
        
        # Check for typical token dumps: print, string, keyword, or identifier identifiers
        combined = (stdout + "\n" + stderr).lower()
        indicators = ["token", "type", "val", "lit", "ident", "print", "hello", "import", "string", "eof", "lex"]
        found = any(ind in combined for ind in indicators)
        if not found:
            raise AssertionError(f"Expected token indicators in output. Output:\n{combined}")
    finally:
        utils.cleanup_workspace(ws)

def test_2_ast_dump(anv_bin):
    ws = utils.create_temp_workspace()
    try:
        src = os.path.join(ws, "hello.hlx")
        shutil.copy2(HELLO_HLX, src)
        
        stdout, stderr, code = utils.run_command([anv_bin, src, "-dump-ast"], expected_code=0)
        
        if not stdout.strip() and not stderr.strip():
            raise AssertionError("AST dump output is empty")
            
        combined = (stdout + "\n" + stderr).lower()
        indicators = ["ast", "tree", "program", "import", "call", "stmt", "decl", "expr", "node", "parse"]
        found = any(ind in combined for ind in indicators)
        if not found:
            raise AssertionError(f"Expected AST structure indicators in output. Output:\n{combined}")
    finally:
        utils.cleanup_workspace(ws)

def test_3_ir_dump(anv_bin):
    ws = utils.create_temp_workspace()
    try:
        src = os.path.join(ws, "hello.hlx")
        shutil.copy2(HELLO_HLX, src)
        
        stdout, stderr, code = utils.run_command([anv_bin, src, "-dump-ir"], expected_code=0)
        
        if not stdout.strip() and not stderr.strip():
            raise AssertionError("IR dump output is empty")
            
        combined = (stdout + "\n" + stderr).lower()
        indicators = ["ir", "function", "block", "instr", "load", "store", "call", "ret", "value", "module", "main"]
        found = any(ind in combined for ind in indicators)
        if not found:
            raise AssertionError(f"Expected IR structure indicators in output. Output:\n{combined}")
    finally:
        utils.cleanup_workspace(ws)

def test_4_asm_generation(anv_bin):
    ws = utils.create_temp_workspace()
    try:
        src = os.path.join(ws, "hello.hlx")
        shutil.copy2(HELLO_HLX, src)
        asm_dst = os.path.join(ws, "hello.asm")
        
        utils.run_command([anv_bin, src, "-asm", "-o", asm_dst], expected_code=0)
        utils.assert_exists(asm_dst)
        
        if os.path.getsize(asm_dst) == 0:
            raise AssertionError("Generated assembly file is empty")
    finally:
        utils.cleanup_workspace(ws)

def test_5_obj_generation(anv_bin):
    ws = utils.create_temp_workspace()
    try:
        src = os.path.join(ws, "hello.hlx")
        shutil.copy2(HELLO_HLX, src)
        obj_dst = os.path.join(ws, "hello.obj")
        
        utils.run_command([anv_bin, src, "-obj", "-o", obj_dst], expected_code=0)
        utils.assert_exists(obj_dst)
        
        if os.path.getsize(obj_dst) == 0:
            raise AssertionError("Generated object file is empty")
    finally:
        utils.cleanup_workspace(ws)

def test_6_final_executable_pipeline(anv_bin):
    ws = utils.create_temp_workspace()
    try:
        src = os.path.join(ws, "hello.hlx")
        shutil.copy2(HELLO_HLX, src)
        exe_dst = os.path.join(ws, "hello.exe")
        
        utils.run_command([anv_bin, src, "-o", exe_dst], expected_code=0)
        utils.assert_exists(exe_dst)
        
        stdout, stderr, code = utils.run_command([exe_dst], expected_code=0)
        utils.assert_return_code(code, 0)
        utils.assert_output(stdout, "Hello World!")
    finally:
        utils.cleanup_workspace(ws)

def main():
    anv_bin = utils.locate_anv()
    
    tests = [
        ("Token dump", test_1_token_dump),
        ("AST dump", test_2_ast_dump),
        ("IR dump", test_3_ir_dump),
        ("Assembly generation", test_4_asm_generation),
        ("Object generation", test_5_obj_generation),
        ("Final executable pipeline", test_6_final_executable_pipeline),
    ]
    
    passed_count = 0
    failed_count = 0
    total_tests = len(tests)
    
    for name, test_func in tests:
        print(f"[ RUN ] {name}")
        try:
            test_func(anv_bin)
            print(f"[ PASS ] {name}")
            passed_count += 1
        except Exception as e:
            print(f"[ FAIL ] {name}")
            print(f"Detail: {e}", file=sys.stderr)
            failed_count += 1
            
    print("\n================================")
    print(f"Passed: {passed_count}/{total_tests}")
    print(f"Failed: {failed_count}/{total_tests}")
    print("================================")
    
    return 1 if failed_count > 0 else 0

if __name__ == "__main__":
    sys.exit(main())
