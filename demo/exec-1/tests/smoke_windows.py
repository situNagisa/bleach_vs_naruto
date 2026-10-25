"""Exercise the real executable and its asynchronous frame loop on Windows.

Usage: python tests/smoke_windows.py out/build/windows-clang-cl-debug/exec-0.exe
The test opens its own window, resizes/minimizes/restores it, then requests close.
"""

import ctypes
from ctypes import wintypes
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def main():
    if sys.platform != "win32":
        raise SystemExit("This smoke test requires Windows.")
    executable = Path(sys.argv[1]).resolve(strict=True)
    user = ctypes.WinDLL("user32", use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user.MoveWindow.argtypes = [wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.BOOL]
    user.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]

    with tempfile.TemporaryFile() as output:
        process = subprocess.Popen([str(executable)], cwd=executable.parent, stdout=output, stderr=output)
        try:
            def alive_for(seconds):
                until = time.monotonic() + seconds
                while time.monotonic() < until:
                    if process.poll() is not None:
                        raise RuntimeError(f"premature exit: {process.returncode:#x}")
                    time.sleep(0.05)

            found = []

            @callback_type
            def locate(hwnd, unused):
                owner = wintypes.DWORD()
                user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
                if owner.value == process.pid:
                    title = ctypes.create_unicode_buffer(256)
                    user.GetWindowTextW(hwnd, title, len(title))
                    if title.value == "BVN - Main Menu":
                        found.append(hwnd)
                return True

            deadline = time.monotonic() + 15
            while not found and time.monotonic() < deadline:
                user.EnumWindows(locate, 0)
                alive_for(0.1)
            if not found:
                raise RuntimeError("menu window was not created")
            hwnd = found[0]
            alive_for(3)
            for width, height in [(800, 600), (1280, 720), (960, 540)]:
                if not user.MoveWindow(hwnd, 100, 100, width, height, True):
                    raise ctypes.WinError(ctypes.get_last_error())
                alive_for(1)
            user.ShowWindow(hwnd, 6)  # SW_MINIMIZE
            alive_for(1)
            user.ShowWindow(hwnd, 9)  # SW_RESTORE
            alive_for(2)
            # Address the GLFW window, not Process.CloseMainWindow's guessed window.
            if not user.PostMessageW(hwnd, 0x0010, 0, 0):  # WM_CLOSE
                raise ctypes.WinError(ctypes.get_last_error())
            if process.wait(timeout=10) != 0:
                raise RuntimeError(f"exit failed: {process.returncode:#x}")
            output.seek(0)
            diagnostics = output.read().decode("utf-8", errors="replace")
            if diagnostics.strip():
                raise RuntimeError(f"unexpected runtime diagnostics:\n{diagnostics}")
            print("PASS: real frame loop, resize, minimize/restore, clean close; no runtime diagnostics")
        except BaseException:
            if process.poll() is None:
                process.kill()
                process.wait()
            output.seek(0)
            print(output.read().decode("utf-8", errors="replace"), file=sys.stderr)
            raise


if __name__ == "__main__":
    main()
