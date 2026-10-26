#!/usr/bin/env python3
"""Builds a minimal, static, reduced-operator ONNX Runtime for Second Opinion (ADR 0009, Q20).

Output prefix layout, consumed by cmake/OnnxRuntime.cmake with
  -DSO_ORT_MINIMAL=ON -DSO_ORT_MINIMAL_ROOT=<prefix>:

  <prefix>/include/onnxruntime_*.h            public C and C++ API headers
  <prefix>/lib/libonnxruntime_minimal.a       one merged static archive (macOS: universal, lipo'd)
  <prefix>/lib/onnxruntime_minimal.lib        (Windows instead of the .a)
  <prefix>/model/nmp.ort                      Basic Pitch converted to ORT format
  <prefix>/LICENSE, ThirdPartyNotices.txt     ORT licence files (shipped in the bundle)

Needs: Python 3.10+, git, CMake 3.28+, a C++20 compiler, and `pip install onnxruntime==1.21.0 onnx`
for the model conversion. macOS: Xcode command line tools. Windows: run from an
"x64 Native Tools Command Prompt for VS 2022" (lib.exe on PATH).

Status: recipe written from the ORT 1.21 build documentation; not yet run end to end (the Linux
dev VM has 2 shared cores, see ADR 0009). Expect a few hours of compute per architecture on such
a machine and roughly 15 to 30 minutes per architecture on a current laptop.

Usage:
  python cmake/ort_minimal/build_ort_minimal.py --prefix build/ort-minimal [--work build/ort-src]
"""

from __future__ import annotations

import argparse
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

ORT_VERSION = "1.21.0"
ORT_REPO = "https://github.com/microsoft/onnxruntime.git"
HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
OPS_CONFIG = HERE / "nmp.required_operators_and_types.config"
MODEL = REPO / "models" / "basic_pitch" / "nmp.onnx"

# Static archives in the ORT build tree that the runtime never needs (tools, tests, full protobuf).
EXCLUDE_SUBSTRINGS = ("gtest", "gmock", "protoc", "benchmark", "test", "mocked", "custom_op",
                      "onnxruntime_providers_shared")
EXCLUDE_EXACT = {"libprotobuf.a", "libprotobufd.a", "libprotobuf.lib", "libprotobufd.lib"}


def run(cmd: list[str], cwd: Path | None = None) -> None:
    print("+", " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], cwd=cwd, check=True)


def fetch_source(work: Path) -> Path:
    src = work / f"onnxruntime-{ORT_VERSION}"
    if not (src / "tools" / "ci_build" / "build.py").exists():
        work.mkdir(parents=True, exist_ok=True)
        run(["git", "clone", "--depth", "1", "--branch", f"v{ORT_VERSION}", "--recurse-submodules",
             "--shallow-submodules", ORT_REPO, src])
    return src


def convert_model(out_dir: Path) -> Path:
    """nmp.onnx -> nmp.ort (Fixed optimisation style, portable across arm64 and x86_64)."""
    with open(MODEL, "rb") as f:
        if f.read(64).startswith(b"version https://git-lfs"):
            sys.exit(f"{MODEL} is a Git LFS pointer; run `git lfs pull` first")
    tmp = out_dir / "convert"
    shutil.rmtree(tmp, ignore_errors=True)
    tmp.mkdir(parents=True)
    shutil.copy2(MODEL, tmp / "nmp.onnx")
    run([sys.executable, "-m", "onnxruntime.tools.convert_onnx_models_to_ort",
         "--optimization_style", "Fixed", "--enable_type_reduction", "--output_dir", tmp / "out", tmp / "nmp.onnx"])
    generated = (tmp / "out" / "nmp.required_operators_and_types.config").read_text().splitlines()
    committed = OPS_CONFIG.read_text().splitlines()
    strip = lambda lines: [l for l in lines if l and not l.startswith("#")]
    if strip(generated) != strip(committed):
        print("WARNING: the operator config generated from the model differs from the committed "
              f"{OPS_CONFIG.name}; update it and rebuild.", file=sys.stderr)
    return tmp / "out" / "nmp.ort"


def build_one(src: Path, build_dir: Path, arch: str | None) -> Path:
    system = platform.system()
    defines = [
        "onnxruntime_BUILD_UNIT_TESTS=OFF",
        "CMAKE_POSITION_INDEPENDENT_CODE=ON",
    ]
    if system != "Windows":
        # Hide ORT symbols at compile time as well; the plugin link hides the rest
        # (-load_hidden on macOS, --exclude-libs,ALL on Linux).
        defines += ["CMAKE_C_VISIBILITY_PRESET=hidden", "CMAKE_CXX_VISIBILITY_PRESET=hidden",
                    "CMAKE_VISIBILITY_INLINES_HIDDEN=ON"]
    cmd = [sys.executable, src / "tools" / "ci_build" / "build.py",
           "--build_dir", build_dir,
           "--config", "MinSizeRel",
           "--update", "--build", "--parallel", "--skip_tests", "--skip_submodule_sync",
           "--compile_no_warning_as_error",
           "--minimal_build",
           "--include_ops_by_config", OPS_CONFIG,
           "--enable_reduced_operator_type_support",
           "--disable_ml_ops",
           "--disable_rtti",
           # Exceptions stay on: BasicPitchRunner catches Ort::Exception.
           "--cmake_extra_defines", *defines]
    if system == "Darwin":
        # ORT 1.21 itself needs macOS 13.3 (see ADR 0009 and Q9).
        cmd += ["--osx_arch", arch, "--apple_deploy_target", "13.3"]
    elif system == "Windows":
        cmd += ["--cmake_generator", "Visual Studio 17 2022"]   # dynamic CRT (/MD), like JUCE
    run(cmd)
    return build_dir / "MinSizeRel"


def collect_archives(tree: Path) -> list[Path]:
    ext = ".lib" if platform.system() == "Windows" else ".a"
    found = []
    for p in sorted(tree.rglob(f"*{ext}")):
        name = p.name.lower()
        if name in EXCLUDE_EXACT or any(s in name for s in EXCLUDE_SUBSTRINGS):
            continue
        found.append(p)
    if not found:
        sys.exit(f"no static archives found under {tree}")
    return found


def merge(archives: list[Path], out: Path) -> None:
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        out.unlink()
    system = platform.system()
    if system == "Darwin":
        run(["libtool", "-static", "-no_warning_for_no_symbols", "-o", out, *archives])
    elif system == "Windows":
        lib = shutil.which("lib") or sys.exit("lib.exe not found: use a VS 2022 x64 Native Tools prompt")
        run([lib, "/NOLOGO", f"/OUT:{out}", *archives])
    else:
        script = "CREATE {}\n{}SAVE\nEND\n".format(out, "".join(f"ADDLIB {a}\n" for a in archives))
        print("+ ar -M (merging {} archives)".format(len(archives)), flush=True)
        subprocess.run(["ar", "-M"], input=script.encode(), check=True)
        run(["ranlib", out])


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--prefix", required=True, type=Path, help="output prefix (SO_ORT_MINIMAL_ROOT)")
    ap.add_argument("--work", type=Path, default=REPO / "build" / "ort-minimal-work",
                    help="source checkout and build trees")
    ap.add_argument("--arch", action="append",
                    help="macOS only: arm64 and/or x86_64 (default both, lipo'd into one archive)")
    args = ap.parse_args()

    prefix = args.prefix.resolve()
    work = args.work.resolve()
    src = fetch_source(work)
    model = convert_model(work)

    system = platform.system()
    lib_name = "onnxruntime_minimal.lib" if system == "Windows" else "libonnxruntime_minimal.a"
    out_lib = prefix / "lib" / lib_name

    if system == "Darwin":
        per_arch = []
        for arch in args.arch or ["arm64", "x86_64"]:
            tree = build_one(src, work / f"build-{arch}", arch)
            merged = work / f"merged-{arch}" / lib_name
            merge(collect_archives(tree), merged)
            per_arch.append(merged)
        out_lib.parent.mkdir(parents=True, exist_ok=True)
        run(["lipo", "-create", *per_arch, "-output", out_lib])
    else:
        tree = build_one(src, work / "build", None)
        merge(collect_archives(tree), out_lib)

    inc = prefix / "include"
    inc.mkdir(parents=True, exist_ok=True)
    for h in (src / "include" / "onnxruntime" / "core" / "session").glob("*.h"):
        shutil.copy2(h, inc / h.name)
    (prefix / "model").mkdir(parents=True, exist_ok=True)
    shutil.copy2(model, prefix / "model" / "nmp.ort")
    for f in ("LICENSE", "ThirdPartyNotices.txt"):
        shutil.copy2(src / f, prefix / f)

    size_mb = out_lib.stat().st_size / 1e6
    print(f"\nDone: {out_lib} ({size_mb:.1f} MB archive; the linked code is much smaller).")
    print(f"Configure with: -DSO_ORT_MINIMAL=ON -DSO_ORT_MINIMAL_ROOT={prefix}")


if __name__ == "__main__":
    main()
