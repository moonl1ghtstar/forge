import os
import sys
import shutil
import subprocess
import tempfile
from pathlib import Path

def locate_anv() -> str:
    script_dir = Path(__file__).parent.resolve()
    project_root = script_dir.parent
    
    candidates = [
        project_root / "anvil" / "bin" / "bin" / "anv.exe",
        project_root / "anv.exe",
        project_root.parent / "anv.exe",
    ]
    
    # CWD checks
    cwd = Path.cwd()
    candidates.extend([
        cwd / "anvil" / "bin" / "bin" / "anv.exe",
        cwd / "anv.exe",
        cwd.parent / "anv.exe",
    ])
    
    for c in candidates:
        abs_c = c.resolve().absolute()
        if abs_c.exists() and abs_c.is_file():
            if sys.platform == "win32" and abs_c.suffix.lower() != ".exe":
                continue
            return str(abs_c)
            
    # PATH lookup
    anv_name = "anv.exe" if sys.platform == "win32" else "anvil"
    path_lookup = shutil.which(anv_name)
    if path_lookup:
        abs_lookup = Path(path_lookup).resolve().absolute()
        if abs_lookup.exists() and abs_lookup.is_file():
            return str(abs_lookup)
        
    print("Anvil error: anv executable not found", file=sys.stderr)
    sys.exit(1)

def run_command(cmd, expected_code=None, cwd=None, timeout=30):
    try:
        res = subprocess.run(
            cmd, 
            stdout=subprocess.PIPE, 
            stderr=subprocess.PIPE, 
            cwd=cwd, 
            timeout=timeout
        )
        stdout = res.stdout.decode(errors='backslashreplace')
        stderr = res.stderr.decode(errors='backslashreplace')
        code = res.returncode
    except subprocess.TimeoutExpired as e:
        stdout = e.stdout.decode(errors='backslashreplace') if e.stdout else ""
        stderr = e.stderr.decode(errors='backslashreplace') if e.stderr else ""
        raise AssertionError(f"Command timed out after {timeout}s: {' '.join(cmd)}\nStdout: {stdout}\nStderr: {stderr}")
    except OSError as e:
        raise AssertionError(f"Command execution error: {e}")

    if expected_code is not None and code != expected_code:
        raise AssertionError(
            f"Command failed: {' '.join(cmd)}\n"
            f"Expected exit code: {expected_code}, got: {code}\n"
            f"Stdout: {stdout}\n"
            f"Stderr: {stderr}"
        )
    return stdout, stderr, code

def create_temp_workspace() -> str:
    return tempfile.mkdtemp(prefix="anv-test-")

def cleanup_workspace(path: str):
    if not path:
        return
    abs_path = os.path.abspath(path)
    real_path = os.path.realpath(abs_path)
    temp_dir = os.path.realpath(tempfile.gettempdir())
    
    # Safety check: must reside directly under system temp dir and start with anv-test-
    rel = os.path.relpath(real_path, temp_dir)
    if rel.startswith("..") or os.path.isabs(rel) or not os.path.basename(real_path).startswith("anv-test-"):
        raise AssertionError(f"Safety violation: attempt to delete non-temporary workspace directory: {path} (resolved: {real_path})")
        
    if os.path.exists(real_path):
        shutil.rmtree(real_path, ignore_errors=True)

def assert_exists(path: str):
    if not os.path.exists(path):
        raise AssertionError(f"Expected path does not exist: {path}")

def assert_not_exists(path: str):
    if os.path.exists(path):
        raise AssertionError(f"Expected path should not exist: {path}")

def assert_output_contains(output: str, substring: str):
    norm_output = output.replace("\r\n", "\n")
    norm_sub = substring.replace("\r\n", "\n")
    if norm_sub not in norm_output:
        raise AssertionError(f"Expected substring {repr(norm_sub)} not found in output: {repr(norm_output)}")

def assert_return_code(actual: int, expected: int):
    if actual != expected:
        raise AssertionError(f"Return code mismatch: expected {expected}, got {actual}")

def cleanup_test_files(paths):
    for p in paths:
        if os.path.exists(p):
            try:
                if os.path.isdir(p):
                    shutil.rmtree(p)
                else:
                    os.remove(p)
            except OSError:
                pass

def assert_output(actual: str, expected: str):
    actual_norm = actual.replace("\r\n", "\n").strip()
    expected_norm = expected.replace("\r\n", "\n").strip()
    if actual_norm != expected_norm:
        raise AssertionError(
            f"Output mismatch.\n"
            f"Expected: {repr(expected_norm)}\n"
            f"Actual:   {repr(actual_norm)}"
        )
