"""Builds zzostoom_cheat_sheet.dll with the portable llvm-mingw toolchain.

    py build.py            # incremental build -> build/zzostoom_cheat_sheet.dll
    py build.py --install  # build and copy the DLL into the ME3 eldenring-mods folder
    py build.py --clean    # remove build/
"""
import concurrent.futures
import glob
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
TOOLCHAIN = os.environ.get("ZZ_LLVM_MINGW") or next(
    iter(sorted(glob.glob(os.path.join(ROOT, "..", "tools", "llvm-mingw-*", "bin")))), "")
BUILD = os.path.join(ROOT, "build")
OBJ = os.path.join(BUILD, "obj")
OUT = os.path.join(BUILD, "zzostoom_cheat_sheet.dll")
ME3_MODS = os.path.expandvars(r"%LOCALAPPDATA%\garyttierney\me3\config\profiles\eldenring-mods")

IMGUI = os.path.join(ROOT, "third_party", "imgui")
MINHOOK = os.path.join(ROOT, "third_party", "minhook")

OWN_SOURCES = sorted(glob.glob(os.path.join(ROOT, "src", "*.cpp")) +
                     glob.glob(os.path.join(ROOT, "src", "generated", "*.cpp")))
THIRD_PARTY = [os.path.join(IMGUI, f) for f in
               ("imgui.cpp", "imgui_draw.cpp", "imgui_tables.cpp", "imgui_widgets.cpp",
                "backends/imgui_impl_dx12.cpp", "backends/imgui_impl_win32.cpp")] + \
              [os.path.join(MINHOOK, "src", f) for f in
               ("buffer.c", "hook.c", "trampoline.c", "hde/hde64.c")]

DEFINES = ["-DUNICODE", "-D_UNICODE", "-DNOMINMAX", "-DNDEBUG", "-D_WIN32_WINNT=0x0A00",
           # MSVC ABI for COM methods that return structs (GetCPUDescriptorHandleForHeapStart...)
           "-DWIDL_EXPLICIT_AGGREGATE_RETURNS",
           # we feed the controller to ImGui ourselves (the backend would read it
           # through our own XInput hook)
           "-DIMGUI_IMPL_WIN32_DISABLE_GAMEPAD"]
INCLUDES = ["-I" + p for p in (os.path.join(ROOT, "src"), IMGUI, os.path.join(IMGUI, "backends"),
                                os.path.join(MINHOOK, "include"))]
CXXFLAGS = ["-std=c++20", "-O2", "-Wall", "-Wno-unknown-pragmas", "-Wno-missing-braces"]
CFLAGS = ["-std=c11", "-O2", "-w"]
LIBS = ["-ld3d12", "-ldxgi", "-ld3dcompiler", "-ldinput8", "-ldxguid", "-luser32", "-lgdi32",
        "-lshell32", "-lole32", "-luuid", "-limm32", "-ldwmapi", "-lversion"]


def tool(name):
    path = os.path.join(TOOLCHAIN, name + ".exe")
    if not os.path.exists(path):
        sys.exit(f"Toolchain not found ({path}). Set ZZ_LLVM_MINGW to llvm-mingw's bin folder.")
    return path


def obj_path(src):
    rel = os.path.relpath(src, ROOT).replace(os.sep, "_").replace("/", "_")
    return os.path.join(OBJ, os.path.splitext(rel)[0] + ".o")


def headers_mtime():
    return max(os.path.getmtime(h) for h in glob.glob(os.path.join(ROOT, "src", "*.h")))


def compile_one(src, newest_header):
    obj = obj_path(src)
    own = src in OWN_SOURCES
    if os.path.exists(obj):
        obj_time = os.path.getmtime(obj)
        if obj_time > os.path.getmtime(src) and (not own or obj_time > newest_header):
            return src, True, ""
    is_c = src.endswith(".c")
    cmd = [tool("clang" if is_c else "clang++"), "-c", src, "-o", obj] + DEFINES + INCLUDES + \
          (CFLAGS if is_c else CXXFLAGS)
    p = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    return src, p.returncode == 0, (p.stdout + p.stderr).strip()


def main():
    if "--clean" in sys.argv:
        shutil.rmtree(BUILD, ignore_errors=True)
        print("cleaned")
        return
    os.makedirs(OBJ, exist_ok=True)
    newest_header = headers_mtime()
    sources = THIRD_PARTY + OWN_SOURCES
    ok = True
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        for src, success, output in pool.map(lambda s: compile_one(s, newest_header), sources):
            if output:
                print(f"--- {os.path.relpath(src, ROOT)}\n{output}")
            ok &= success
    if not ok:
        sys.exit("Build failed.")
    cmd = [tool("clang++"), "-shared", "-o", OUT] + [obj_path(s) for s in sources] + \
          ["-static", "-s", "-Wl,--gc-sections"] + LIBS
    p = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if p.returncode != 0:
        print(p.stdout + p.stderr)
        sys.exit("Link failed.")
    print(f"OK -> {OUT} ({os.path.getsize(OUT) // 1024} KB)")
    if "--install" in sys.argv:
        dest = os.path.join(ME3_MODS, os.path.basename(OUT))
        shutil.copy2(OUT, dest)
        print(f"Installed -> {dest}")


if __name__ == "__main__":
    main()
