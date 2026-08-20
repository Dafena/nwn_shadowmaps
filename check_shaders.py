#!/usr/bin/env python3
"""Compile-check the injector's runtime-built GLSL without launching the game.

The receiver shader is assembled from C++ string literals and only ever compiled
inside a live GL context, so a syntax error in it is invisible until the game
runs -- and it is the pass that draws every shadow, so "invisible until it runs"
means "invisible until everything is black".

This extracts each shader from the source exactly as the C++ concatenates it and
runs glslangValidator over it. Not a substitute for testing in game (it cannot
check uniforms actually get bound, or that the maths is right), but it catches
the class of mistake that editing these strings by hand actually produces.

    python3 check_shaders.py

Requires glslangValidator (package `glslang`). Exit code is non-zero if any
shader fails to compile.
"""
import re
import subprocess
import sys
import shutil

# Files to scan. Every `static const char* <name> = "#version ...` in them is
# found and compiled, so adding a shader needs no edit here.
SOURCES = ["shadow_fullscreen_receiver.inc"]


def find_shaders(path):
    """Yield (label, first-literal-line-index) for each shader initialiser."""
    lines = open(path, encoding="utf-8", errors="replace").read().split("\n")
    decl = re.compile(r'static\s+const\s+char\s*\*\s*(\w+)\s*=\s*$')
    for i, line in enumerate(lines):
        m = decl.search(line.strip())
        if not m:
            continue
        j = i + 1
        while j < len(lines) and (lines[j].strip().startswith("//") or not lines[j].strip()):
            j += 1
        if j < len(lines) and '"#version' in lines[j]:
            yield m.group(1), j
    return


def grab(lines, idx):
    """Concatenate the string literals of one C++ initialiser, skipping the
    comments interleaved between them. Stops at the first line that is neither a
    comment, blank, nor a string literal -- which is where the statement ends."""
    out = []
    i = idx
    while i < len(lines):
        stripped = lines[i].strip()
        if stripped.startswith("//") or stripped == "":
            i += 1
            continue
        lits = re.findall(r'"((?:[^"\\]|\\.)*)"', lines[i])
        if not lits:
            break
        for piece in lits:
            out.append(piece.encode().decode("unicode_escape"))
        i += 1
    return "".join(out)


def named_string(path, name):
    """Extract a named C++ string-literal initializer that need not start
    with #version. Used to assemble the foliage fragment injected at runtime."""
    lines = open(path, encoding="utf-8", errors="replace").read().split("\n")
    decl = re.compile(rf'static\s+const\s+char\s*\*\s*{re.escape(name)}\s*=')
    for i, line in enumerate(lines):
        if not decl.search(line.strip()):
            continue
        j = i
        while j < len(lines) and '"' not in lines[j]:
            j += 1
        return grab(lines, j)
    raise RuntimeError(f"missing C++ string initializer {name} in {path}")


def injected_foliage_shader():
    path = "shadow_shader_interposition.inc"
    outputs = named_string(path, "kOutputs")
    helpers = named_string(path, "kA2cShadowHelpers")
    body = named_string(path, "kBody")
    return ("#version 330 compatibility\n"
            "#define gl_FragColor compat_glFragColor\n" + outputs + "\n" +
            helpers + "\nvoid main(){\n"
            "  gl_FragColor=vec4(0.6,0.7,0.8,0.5);\n" + body + "\n}\n")


def main():
    if not shutil.which("glslangValidator"):
        print("glslangValidator not found (install `glslang`)", file=sys.stderr)
        return 2
    failures = 0
    found = [(p, n, i) for p in SOURCES for n, i in find_shaders(p)]
    if not found:
        print("no shaders found -- has the source layout changed?", file=sys.stderr)
        return 2
    for path, label, idx in found:
        lines = open(path, encoding="utf-8", errors="replace").read().split("\n")
        src = grab(lines, idx)
        if not src.startswith("#version"):
            print(f"FAIL  {label:22s} extraction did not start at #version "
                  f"({path}:{idx + 1}) -- has the source moved?")
            failures += 1
            continue
        ext = ".vert" if src.find("gl_Position") >= 0 and "void main" in src and "FragColor" not in src else ".frag"
        tmp = f"/tmp/nwn_shadow_check{ext}"
        open(tmp, "w").write(src)
        res = subprocess.run(["glslangValidator", tmp],
                             capture_output=True, text=True)
        if res.returncode == 0:
            print(f"ok    {label:22s} {len(src):6d} bytes")
        else:
            print(f"FAIL  {label:22s} {path}:{idx + 1}")
            print(res.stdout.strip() or res.stderr.strip())
            failures += 1
    try:
        src = injected_foliage_shader()
        tmp = "/tmp/nwn_shadow_check_foliage.frag"
        open(tmp, "w").write(src)
        res = subprocess.run(["glslangValidator", tmp],
                             capture_output=True, text=True)
        if res.returncode == 0:
            print(f"ok    {'a2c foliage injection':22s} {len(src):6d} bytes")
        else:
            print("FAIL  a2c foliage injection shadow_shader_interposition.inc")
            print(res.stdout.strip() or res.stderr.strip())
            failures += 1
    except Exception as exc:
        print(f"FAIL  a2c foliage injection extraction: {exc}")
        failures += 1
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
