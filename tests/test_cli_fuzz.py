import sys
import os
from pathlib import Path

TEST_ROOT = Path(__file__).parent
if str(TEST_ROOT) not in sys.path:
    sys.path.insert(0, str(TEST_ROOT))

import utils

HELLO_HLX = os.path.join(TEST_ROOT, "cases", "hello.hlx")

def assert_fuzz_failure(forge_bin, args):
    stdout, stderr, code = utils.run_command([forge_bin] + args, expected_code=None, timeout=10)
    
    if code == 0:
        raise AssertionError(f"Expected non-zero exit code for args {args}, got 0")
    
    combined_output = stdout + "\n" + stderr
    if "Forge error:" not in combined_output:
        raise AssertionError(f"Expected 'Forge error:' in output for args {args}. Output:\n{combined_output}")

def test_1_empty_arguments(forge_bin):
    assert_fuzz_failure(forge_bin, [])

def test_2_invalid_flag(forge_bin):
    assert_fuzz_failure(forge_bin, ["unknown_file.hlx", "-abc"])

def test_3_missing_output_value(forge_bin):
    assert_fuzz_failure(forge_bin, [HELLO_HLX, "-o"])

def test_4_link_mode_without_objects(forge_bin):
    assert_fuzz_failure(forge_bin, ["-link"])

def test_5_link_missing_object(forge_bin):
    assert_fuzz_failure(forge_bin, ["-link", "nonexistent.obj"])

def test_6_conflicting_output_flags(forge_bin):
    assert_fuzz_failure(forge_bin, [HELLO_HLX, "-asm", "-obj"])

def test_7_invalid_run_object_combination(forge_bin):
    assert_fuzz_failure(forge_bin, [HELLO_HLX, "-run", "-obj"])

def test_8_conflicting_pipeline_flags(forge_bin):
    assert_fuzz_failure(forge_bin, [HELLO_HLX, "-dump-ir", "-run"])

def test_9_unknown_long_option(forge_bin):
    assert_fuzz_failure(forge_bin, [HELLO_HLX, "--unknown"])

def test_10_empty_output(forge_bin):
    assert_fuzz_failure(forge_bin, [HELLO_HLX, "-o", ""])

def test_11_empty_input(forge_bin):
    assert_fuzz_failure(forge_bin, [""])

def test_12_invalid_extension(forge_bin):
    assert_fuzz_failure(forge_bin, ["invalid.txt"])

def test_13_invalid_linker_arguments(forge_bin):
    assert_fuzz_failure(forge_bin, ["hello.obj", "-link", "fake.obj", "extra_argument"])

def main():
    forge_bin = utils.locate_forge()
    
    tests = [
        ("Empty arguments", test_1_empty_arguments),
        ("Invalid flag", test_2_invalid_flag),
        ("Missing output value", test_3_missing_output_value),
        ("Link mode without objects", test_4_link_mode_without_objects),
        ("Link missing object", test_5_link_missing_object),
        ("Conflicting output flags", test_6_conflicting_output_flags),
        ("Invalid run/object combination", test_7_invalid_run_object_combination),
        ("Conflicting pipeline flags", test_8_conflicting_pipeline_flags),
        ("Unknown long option", test_9_unknown_long_option),
        ("Empty output", test_10_empty_output),
        ("Empty input", test_11_empty_input),
        ("Invalid extension", test_12_invalid_extension),
        ("Invalid linker arguments", test_13_invalid_linker_arguments),
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
