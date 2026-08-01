import sys
import os
import shutil
from pathlib import Path

TEST_ROOT = Path(__file__).parent
if str(TEST_ROOT) not in sys.path:
    sys.path.insert(0, str(TEST_ROOT))

import utils

def assert_no_leaks(ws_path, stdout, stderr):
    import re
    import tempfile
    combined = stdout + "\n" + stderr
    
    # Check for temporary PID-based anv-work directories
    match = re.search(r'anv-work[/\\](\d+)', combined)
    if match:
        pid = match.group(1)
        sys_temp = tempfile.gettempdir()
        temp_dir_path = os.path.join(sys_temp, "anv-work", pid)
        if os.path.exists(temp_dir_path):
            raise AssertionError(f"Leaked temporary directory found: {temp_dir_path}")
            
    # Check for anv-build leaks in workspace or near it
    leaks = ["anv-build.asm", "anv-build.o", "anv-build.exe"]
    search_dirs = [ws_path, os.path.dirname(ws_path)]
    for d in search_dirs:
        if not d:
            continue
        for leak in leaks:
            leak_path = os.path.join(d, leak)
            if os.path.exists(leak_path):
                raise AssertionError(f"Leaked file found: {leak_path}")

def assert_expected_failure(anv_bin, args, ws_path):
    stdout, stderr, code = utils.run_command([anv_bin] + args, expected_code=None, timeout=15)
    combined = stdout + "\n" + stderr
    
    if code == 0:
        raise AssertionError(f"Expected non-zero exit code for args {args}, got 0")
        
    if "Anvil error:" not in combined:
        raise AssertionError(f"Expected 'Anvil error:' in output. Output:\n{combined}")
        
    assert_no_leaks(ws_path, stdout, stderr)

def test_1_path_containing_spaces(anv_bin):
    ws = utils.create_temp_workspace()
    try:
        space_dir = os.path.join(ws, "sub dir with spaces")
        os.makedirs(space_dir, exist_ok=True)
        
        src = os.path.join(space_dir, "hello.hlx")
        with open(src, "w", encoding="utf-8") as f:
            f.write('import console\nconsole.print("Hello World!");\n')
            
        exe = os.path.join(space_dir, "hello.exe")
        stdout, stderr, code = utils.run_command([anv_bin, src, "-o", exe, "-verbose"], expected_code=0)
        
        utils.assert_exists(exe)
        run_out, run_err, run_code = utils.run_command([exe], expected_code=0)
        utils.assert_output(run_out, "Hello World!")
        
        assert_no_leaks(ws, stdout, stderr)
    finally:
        utils.cleanup_workspace(ws)

def test_2_korean_unicode_path(anv_bin):
    ws = utils.create_temp_workspace()
    try:
        unicode_dir = os.path.join(ws, "한국어_테스트_경로")
        os.makedirs(unicode_dir, exist_ok=True)
        
        src = os.path.join(unicode_dir, "hello.hlx")
        with open(src, "w", encoding="utf-8") as f:
            f.write('import console\nconsole.print("Hello World!");\n')
            
        exe = os.path.join(unicode_dir, "hello.exe")
        stdout, stderr, code = utils.run_command([anv_bin, src, "-o", exe, "-verbose"], expected_code=0)
        
        utils.assert_exists(exe)
        run_out, run_err, run_code = utils.run_command([exe], expected_code=0)
        utils.assert_output(run_out, "Hello World!")
        
        assert_no_leaks(ws, stdout, stderr)
    finally:
        utils.cleanup_workspace(ws)

def test_3_overwrite_existing_executable(anv_bin):
    ws = utils.create_temp_workspace()
    try:
        src = os.path.join(ws, "hello.hlx")
        with open(src, "w", encoding="utf-8") as f:
            f.write('import console\nconsole.print("Hello World!");\n')
            
        exe = os.path.join(ws, "hello.exe")
        with open(exe, "w") as f:
            f.write("DUMMY EXISTING EXECUTABLE CONTENT")
            
        stdout, stderr, code = utils.run_command([anv_bin, src, "-o", exe, "-verbose"], expected_code=0)
        
        utils.assert_exists(exe)
        run_out, run_err, run_code = utils.run_command([exe], expected_code=0)
        utils.assert_output(run_out, "Hello World!")
        
        assert_no_leaks(ws, stdout, stderr)
    finally:
        utils.cleanup_workspace(ws)

def test_4_missing_source_file(anv_bin):
    ws = utils.create_temp_workspace()
    try:
        src = os.path.join(ws, "nonexistent.hlx")
        exe = os.path.join(ws, "hello.exe")
        assert_expected_failure(anv_bin, [src, "-o", exe, "-verbose"], ws)
    finally:
        utils.cleanup_workspace(ws)

def test_5_empty_source_file(anv_bin):
    ws = utils.create_temp_workspace()
    try:
        src = os.path.join(ws, "empty.hlx")
        with open(src, "w") as f:
            pass
            
        exe = os.path.join(ws, "empty.exe")
        stdout, stderr, code = utils.run_command([anv_bin, src, "-o", exe, "-verbose"], expected_code=0)
        
        utils.assert_exists(exe)
        assert_no_leaks(ws, stdout, stderr)
    finally:
        utils.cleanup_workspace(ws)

def test_6_output_path_points_to_existing_directory(anv_bin):
    ws = utils.create_temp_workspace()
    try:
        src = os.path.join(ws, "hello.hlx")
        with open(src, "w", encoding="utf-8") as f:
            f.write('import console\nconsole.print("Hello World!");\n')
            
        existing_dir = os.path.join(ws, "existing_dir")
        os.makedirs(existing_dir, exist_ok=True)
        
        assert_expected_failure(anv_bin, [src, "-o", existing_dir, "-verbose"], ws)
    finally:
        utils.cleanup_workspace(ws)

def test_7_output_parent_directory_does_not_exist(anv_bin):
    ws = utils.create_temp_workspace()
    try:
        src = os.path.join(ws, "hello.hlx")
        with open(src, "w", encoding="utf-8") as f:
            f.write('import console\nconsole.print("Hello World!");\n')
            
        nonexistent_parent_exe = os.path.join(ws, "no_such_parent", "hello.exe")
        assert_expected_failure(anv_bin, [src, "-o", nonexistent_parent_exe, "-verbose"], ws)
    finally:
        utils.cleanup_workspace(ws)

def test_8_long_nested_path(anv_bin):
    ws = utils.create_temp_workspace()
    try:
        # Target total path length around ~180-220 characters
        ws_len = len(ws)
        sub_len = 200 - ws_len - len("hello.hlx") - 2
        if sub_len < 10:
            sub_len = 10
            
        nested_dir_name = "a" * sub_len
        nested_dir = os.path.join(ws, nested_dir_name)
        os.makedirs(nested_dir, exist_ok=True)
        
        src = os.path.join(nested_dir, "hello.hlx")
        with open(src, "w", encoding="utf-8") as f:
            f.write('import console\nconsole.print("Hello World!");\n')
            
        exe = os.path.join(nested_dir, "hello.exe")
        stdout, stderr, code = utils.run_command([anv_bin, src, "-o", exe, "-verbose"], expected_code=0)
        
        utils.assert_exists(exe)
        run_out, run_err, run_code = utils.run_command([exe], expected_code=0)
        utils.assert_output(run_out, "Hello World!")
        
        assert_no_leaks(ws, stdout, stderr)
    finally:
        utils.cleanup_workspace(ws)

def main():
    anv_bin = utils.locate_anv()
    
    tests = [
        ("Compile source from path containing spaces", test_1_path_containing_spaces),
        ("Compile source from Korean/Unicode path", test_2_korean_unicode_path),
        ("Overwrite existing executable", test_3_overwrite_existing_executable),
        ("Missing source file", test_4_missing_source_file),
        ("Empty source file", test_5_empty_source_file),
        ("Output path points to existing directory", test_6_output_path_points_to_existing_directory),
        ("Output parent directory does not exist", test_7_output_parent_directory_does_not_exist),
        ("Long nested path (~180-220 chars)", test_8_long_nested_path),
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
