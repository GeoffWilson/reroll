#!/usr/bin/env python3
"""Argument-count audit: compares our C with the original's own stack use.

    tools/argcheck.py build/params.txt src/main.c src/game/*.c

build/params.txt comes from `make param-sizes` (the decompiler's prototype for
every original function: stack argument bytes and register arguments).

1. Calls from C into the original (a cast of a numeric address, directly or
   through a macro): the stack bytes we pass should cover what the callee
   reads. Fewer means the callee reads leftover stack, as setup_s.c's call of
   0x449d00 did. Register arguments (ecx/edx) are listed for checking by hand.
2. Hooked replacements (main.c's hook tables): our function should not read
   more stack arguments than the original does (parameters only discarded
   with `(void)x;` don't count, nor fastcall's register arguments).

Exit status 1 if any call passes too few bytes or any replacement reads too
many.
"""
import json
import re
import subprocess
import sys

CFLAGS = ["--target=i686-linux-gnu", "-fsyntax-only", "-DREROLL_NATIVE", "-fms-extensions", "-Isrc", "-Ibuild/render",
          "-Xclang", "-ast-dump=json"]


def load_params(path):
    params = {}
    for line in open(path):
        parts = line.split()
        if len(parts) >= 3 and parts[1] != "?":
            params[int(parts[0], 16)] = ((int(parts[1]) + 3) & ~3, parts[2])  # whole stack slots
    return params


def strip(node):
    while node.get("kind") in ("ImplicitCastExpr", "ParenExpr"):
        node = node["inner"][0]
    return node


def literal_address(node):
    """The address if `node` is a cast of an integer literal to a function pointer."""
    node = strip(node)
    if node.get("kind") != "CStyleCastExpr":
        return None
    inner = strip(node["inner"][0])
    if inner.get("kind") == "IntegerLiteral":
        return int(inner["value"])
    return None


def arg_bytes(arg):
    qual = arg.get("type", {}).get("qualType", "")
    return 8 if re.search(r"\b(u?int64_t|long long|double)\b", qual) else 4


def walk(node, visit):
    visit(node)
    for child in node.get("inner", ()) or ():
        if isinstance(child, dict):
            walk(child, visit)


def read_params(function):
    """Ids of the parameters the body reads (a bare `(void)x;` is not a read)."""
    ids = set()

    def visit(node, discarded=False):
        if node.get("kind") == "CStyleCastExpr" and node.get("type", {}).get("qualType") == "void":
            discarded = strip(node["inner"][0]).get("kind") == "DeclRefExpr"
        if node.get("kind") == "DeclRefExpr" and not discarded:
            ids.add(node.get("referencedDecl", {}).get("id"))
        for child in node.get("inner", ()) or ():
            if isinstance(child, dict):
                visit(child, discarded)

    for child in function["inner"]:
        if child.get("kind") == "CompoundStmt":
            visit(child)
    return ids


def stack_bytes_read(function):
    """Stack argument bytes up to the last parameter the body reads; fastcall
    functions take their first two (small) arguments in ecx and edx."""
    plist = [c for c in function["inner"] if c.get("kind") == "ParmVarDecl"]
    if "fastcall" in function.get("type", {}).get("qualType", ""):
        plist = plist[2:]
    used = read_params(function)
    end = 0
    offset = 0
    for p in plist:
        offset += arg_bytes(p)
        if p.get("id") in used:
            end = offset
    return end


def line_of(node, current):
    loc = node.get("range", {}).get("begin", {})
    loc = loc.get("expansionLoc", loc)
    return loc.get("line", current[0])


def main():
    params = load_params(sys.argv[1])
    files = sys.argv[2:]
    problems = 0
    calls_checked = 0
    register_calls = []
    over = []
    definitions = {}  # C function name -> (file, stack argument bytes it reads)
    for path in files:
        ast = json.loads(subprocess.run(["clang", *CFLAGS, path], capture_output=True, text=True,
                                        check=True).stdout)
        current = [0]

        def visit(node):
            nonlocal problems, calls_checked
            loc = node.get("loc", {})
            if "line" in loc:
                current[0] = loc["line"]
            kind = node.get("kind")
            if kind == "FunctionDecl" and node.get("inner") and any(
                    c.get("kind") == "CompoundStmt" for c in node["inner"]):
                if loc.get("file", path) == path or "file" not in loc:
                    definitions[node["name"]] = (path, stack_bytes_read(node))
            if kind != "CallExpr":
                return
            address = literal_address(node["inner"][0])
            if address is None or address < 0x400000 or address >= 0x4b0000:
                return
            calls_checked += 1
            ours = sum(arg_bytes(a) for a in node["inner"][1:])
            line = line_of(node, current)
            if address not in params:
                print(f"{path}:{line}: call 0x{address:x}: not a function start in params.txt")
                problems += 1
                return
            needed, registers = params[address]
            if registers != "-":
                register_calls.append(f"{path}:{line}: call 0x{address:x} passes {ours} stack bytes; "
                                      f"original reads {needed} stack bytes and registers {registers}")
            if ours < needed:
                print(f"{path}:{line}: call 0x{address:x} passes {ours} bytes, the original reads {needed}")
                problems += 1
            elif ours > needed:
                over.append(f"{path}:{line}: call 0x{address:x} passes {ours} bytes, the original reads {needed}")

        walk(ast, visit)

    hooks = re.findall(r'\{"([^"]+)", 0x([0-9a-f]+), (\w+),', open("src/main.c").read())
    hooks_checked = 0
    for name, address, symbol in hooks:
        address = int(address, 16)
        if symbol not in definitions or address not in params:
            continue
        hooks_checked += 1
        path, ours = definitions[symbol]
        needed, registers = params[address]
        if ours > needed:
            print(f"hook {name} 0x{address:x}: {symbol} ({path}) takes {ours} bytes, the original reads {needed}"
                  + (f" and registers {registers}" if registers != "-" else ""))
            problems += 1
        elif registers != "-":
            register_calls.append(f"hook {name} 0x{address:x}: {symbol} ({path}); original reads registers {registers}")

    print(f"\n{calls_checked} calls into the original and {hooks_checked} hooked replacements checked; "
          f"{problems} problem(s)")
    if register_calls:
        print("\nRegister arguments (check by hand):")
        print("\n".join(register_calls))
    if over:
        print(f"\nCalls passing more than the original reads ({len(over)}; harmless under cdecl, "
              f"but worth a look):")
        print("\n".join(over))
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
