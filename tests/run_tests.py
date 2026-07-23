from pathlib import Path
import sys
import os
import shutil
import multiprocessing

TEST_ROOT = Path(__file__).parent
if str(TEST_ROOT) not in sys.path:
    sys.path.insert(0, str(TEST_ROOT))

import utils

from utils import cleanup_test_files, assert_output

def test_1_basic_compile(forge_bin):
    hello_hlx = os.path.join(TEST_ROOT, "cases", "hello.hlx")
    hello_exe = os.path.join(TEST_ROOT, "cases", "hello.exe")
    math_hlx = os.path.join(TEST_ROOT, "cases", "math.hlx")
    math_exe = os.path.join(TEST_ROOT, "cases", "math.exe")
    
    cleanup_test_files([hello_exe, math_exe])
    
    utils.run_command([forge_bin, hello_hlx], expected_code=0)
    utils.assert_exists(hello_exe)
    stdout, _, _ = utils.run_command([hello_exe], expected_code=0)
    assert_output(stdout, "Hello World!")
    
    utils.run_command([forge_bin, math_hlx], expected_code=0)
    utils.assert_exists(math_exe)
    stdout, _, _ = utils.run_command([math_exe], expected_code=0)
    assert_output(stdout, "120\n80\n2000")
    
    cleanup_test_files([hello_exe, math_exe])

def test_2_custom_output(forge_bin):
    hello_hlx = os.path.join(TEST_ROOT, "cases", "hello.hlx")
    custom_exe = os.path.join(TEST_ROOT, "..", "custom_output.exe")
    
    cleanup_test_files([custom_exe])
    utils.run_command([forge_bin, hello_hlx, "-o", custom_exe], expected_code=0)
    utils.assert_exists(custom_exe)
    stdout, _, _ = utils.run_command([custom_exe], expected_code=0)
    assert_output(stdout, "Hello World!")
    cleanup_test_files([custom_exe])

def test_3_object_generation(forge_bin):
    hello_hlx = os.path.join(TEST_ROOT, "cases", "hello.hlx")
    hello_obj = os.path.join(TEST_ROOT, "..", "hello.obj")
    
    cleanup_test_files([hello_obj])
    utils.run_command([forge_bin, hello_hlx, "-obj", "-o", hello_obj], expected_code=0)
    utils.assert_exists(hello_obj)

def test_4_manual_linking(forge_bin):
    hello_obj = os.path.join(TEST_ROOT, "..", "hello.obj")
    linked_exe = os.path.join(TEST_ROOT, "..", "linked.exe")
    
    cleanup_test_files([linked_exe])
    utils.assert_exists(hello_obj)
    utils.run_command([forge_bin, "-link", hello_obj, "-o", linked_exe], expected_code=0)
    utils.assert_exists(linked_exe)
    stdout, _, _ = utils.run_command([linked_exe], expected_code=0)
    assert_output(stdout, "Hello World!")
    cleanup_test_files([hello_obj, linked_exe])

def test_5_space_path_handling(forge_bin):
    temp_space_dir = os.path.join(TEST_ROOT, "temp space")
    cleanup_test_files([temp_space_dir])
    os.makedirs(temp_space_dir, exist_ok=True)
    
    src_hello = os.path.join(TEST_ROOT, "cases", "hello.hlx")
    dest_hello = os.path.join(temp_space_dir, "hello.hlx")
    shutil.copy2(src_hello, dest_hello)
    dest_exe = os.path.join(temp_space_dir, "hello.exe")
    
    utils.run_command([forge_bin, dest_hello], expected_code=0)
    utils.assert_exists(dest_exe)
    stdout, _, _ = utils.run_command([dest_exe], expected_code=0)
    assert_output(stdout, "Hello World!")
    cleanup_test_files([temp_space_dir])

def test_6_temporary_file_cleanup(forge_bin):
    hello_hlx = os.path.join(TEST_ROOT, "cases", "hello.hlx")
    temp_exe = os.path.join(TEST_ROOT, "..", "temp_cleanup_check.exe")
    
    cleanup_test_files([temp_exe])
    _, stderr, _ = utils.run_command([forge_bin, hello_hlx, "-o", temp_exe, "-verbose"], expected_code=0)
    utils.assert_exists(temp_exe)
    cleanup_test_files([temp_exe])
    
    import re
    import tempfile
    match = re.search(r'forge-work[/\\](\d+)', stderr)
    if not match:
        raise AssertionError("Could not identify temporary forge-work sub-folder PID in compiler verbose output.")
        
    pid = match.group(1)
    sys_temp = tempfile.gettempdir()
    temp_dir_path = os.path.join(sys_temp, "forge-work", pid)
    
    if os.path.exists(temp_dir_path):
        raise AssertionError(f"Leaked temporary directory found: {temp_dir_path}")
        
    leaks = ["forge-build.asm", "forge-build.o", "forge-build.exe"]
    search_dirs = [os.path.join(TEST_ROOT, ".."), TEST_ROOT, os.path.join(TEST_ROOT, "cases")]
    for d in search_dirs:
        for leak in leaks:
            leak_path = os.path.join(d, leak)
            if os.path.exists(leak_path):
                raise AssertionError(f"Leaked file found in repository: {leak_path}")

def parallel_worker(args):
    forge_bin, src, dst = args
    try:
        utils.run_command([forge_bin, src, "-o", dst], expected_code=0)
        return True, ""
    except Exception as e:
        return False, str(e)

def test_7_parallel_compilation(forge_bin):
    hello_hlx = os.path.join(TEST_ROOT, "cases", "hello.hlx")
    tasks = []
    out_files = []
    for i in range(1, 11):
        out_exe = os.path.join(TEST_ROOT, "..", f"parallel_{i}.exe")
        out_files.append(out_exe)
        cleanup_test_files([out_exe])
        tasks.append((forge_bin, hello_hlx, out_exe))
        
    try:
        pool = multiprocessing.Pool(processes=10)
        results = pool.map(parallel_worker, tasks)
        pool.close()
        pool.join()
    except Exception as e:
        cleanup_test_files(out_files)
        raise AssertionError(f"Parallel multiprocessing execution error: {e}")
        
    errors = []
    for i, (success, err_msg) in enumerate(results):
        out_exe = out_files[i]
        if not success:
            errors.append(f"Build {i+1} failed: {err_msg}")
            continue
        if not os.path.exists(out_exe):
            errors.append(f"Build {i+1} succeeded but output file '{out_exe}' is missing.")
            continue
            
        try:
            stdout, _, _ = utils.run_command([out_exe], expected_code=0)
            assert_output(stdout, "Hello World!")
        except Exception as e:
            errors.append(f"Executable {i+1} failed to run correctly: {e}")
            
    cleanup_test_files(out_files)
    if errors:
        raise AssertionError("; ".join(errors))

def test_8_error_handling(forge_bin):
    nonexistent = os.path.join(TEST_ROOT, "cases", "nonexistent.hlx")
    cleanup_test_files([nonexistent])
    stdout, stderr, code = utils.run_command([forge_bin, nonexistent], expected_code=None)
    if code == 0:
        raise AssertionError("Expected compiler to fail with non-zero exit code on nonexistent file.")
    if "Forge error:" not in stderr and "Forge error:" not in stdout:
        raise AssertionError(f"Expected 'Forge error:' in output. Stdout: {stdout}\nStderr: {stderr}")

def main():
    forge_bin = utils.locate_forge()
    tests = [
        ("Basic compile", test_1_basic_compile),
        ("Custom output", test_2_custom_output),
        ("Object generation", test_3_object_generation),
        ("Manual linking", test_4_manual_linking),
        ("Space path", test_5_space_path_handling),
        ("Cleanup check", test_6_temporary_file_cleanup),
        ("Parallel build", test_7_parallel_compilation),
        ("Error handling", test_8_error_handling),
    ]
    
    passed_count = 0
    failed_count = 0
    total_tests = len(tests)
    
    for name, test_func in tests:
        print(f"[ RUN ] {name}")
        try:
            test_func(forge_bin)
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
