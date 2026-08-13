#!/usr/bin/env python3
"""APX-284 — no-static-variables guard for the foundation module and its plugins.

Two checks, both hard failures (exit code 1 on any violation):

1. **No file-scope mutable `static` variables.** The foundation refactor
   (`docs/foundation-refactor-design.md`) forbids mutable file-scope statics:
   they are process-wide state, and because every plugin statically links its
   own copy of `sk-foundation`, each shared library would get a *separate*
   copy of that state (the pre-refactor host/plugin split-logger bug). Only
   the following are allowed in `foundation/` and `plugins/`:
     - `static const` data,
     - `static` functions (definitions and prototypes),
     - file-scope statics listed in `no-statics-allowlist.txt` (documented
       exceptions: OS-mandated crash/DbgHelp process state, TLS
       `platform_err`, process-wide filesystem state, plugin instance state,
       SK_TESTS-only test scaffolding).

2. **No deleted accessor reintroduction.** `sk_app_api(`, `sk_logger_api(`,
   and `sk_repository_api(` free-function accessors were deleted by the
   refactor (APX-278 / PR plan §7). Any code occurrence (declaration or call
   site) in `foundation/` or `plugins/` is a reintroduction and fails.

The check is comment-aware: occurrences inside `/* */` / `//` comments do not
count (the docs and header prose legitimately name the removed APIs).

Usage:
    python3 scripts/check-no-statics.py            # run both checks
    python3 scripts/check-no-statics.py --dump     # print detected path:symbol
                                                   # pairs for allowlist editing
    python3 scripts/check-no-statics.py --allowlist FILE   # custom allowlist

Wired into the build as a CTest test (tests/CMakeLists.txt,
`sk-no-statics-guard`), so CI runs it on every platform and build type.
"""

import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCAN_DIRS = ("foundation", "plugins")
ALLOWLIST_DEFAULT = os.path.join(REPO_ROOT, "no-statics-allowlist.txt")
ACCESSOR_RE = re.compile(r"\bsk_(?:app|logger|repository)_api\s*\(")
STATIC_RE = re.compile(r"static\b")
IDENT_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


def strip_comments(text):
    """Replace comment contents with spaces, preserving newlines, so line
    numbers in reports stay aligned with the source file."""
    out = []
    i = 0
    n = len(text)
    in_block = False
    while i < n:
        if not in_block:
            if text[i] == "/" and i + 1 < n and text[i + 1] == "/":
                j = text.find("\n", i)
                if j == -1:
                    j = n
                out.append(" " * (j - i))
                i = j
            elif text[i] == "/" and i + 1 < n and text[i + 1] == "*":
                in_block = True
                out.append("  ")
                i += 2
            else:
                out.append(text[i])
                i += 1
        else:
            if text[i] == "*" and i + 1 < n and text[i + 1] == "/":
                in_block = False
                out.append("  ")
                i += 2
            else:
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
    return "".join(out)


def skip_string(text, i):
    """Advance past a double-quoted string starting at text[i] == '\"'."""
    n = len(text)
    i += 1
    while i < n:
        if text[i] == "\\":
            i += 2
        elif text[i] == '"':
            return i + 1
        else:
            i += 1
    return n


def skip_char(text, i):
    """Advance past a char literal starting at text[i] == \"'\"."""
    n = len(text)
    i += 1
    while i < n:
        if text[i] == "\\":
            i += 2
        elif text[i] == "'":
            return i + 1
        else:
            i += 1
    return n


def find_first_paren(text, start):
    """Index of the first '(' outside string/char literals, or -1."""
    i = start
    n = len(text)
    while i < n:
        c = text[i]
        if c == '"':
            i = skip_string(text, i)
        elif c == "'":
            i = skip_char(text, i)
        elif c == "(":
            return i
        else:
            i += 1
    return -1


def has_top_level_eq(text, start, end):
    """True if there is an '=' at paren/brace depth 0 in text[start:end]."""
    depth = 0
    i = start
    while i < end:
        c = text[i]
        if c == '"':
            i = skip_string(text, i)
        elif c == "'":
            i = skip_char(text, i)
        elif c in "([{":
            depth += 1
            i += 1
        elif c in ")]}":
            depth -= 1
            i += 1
        elif c == "=" and depth == 0:
            return True
        else:
            i += 1
    return False


def extract_symbol(joined):
    """Extract the variable name from a joined static declaration.

    Handles plain declarators, array declarators, `= { ... }` initializers,
    and anonymous `struct { ... } name;` / `union { ... } name;`.
    """
    text = joined.rstrip()
    # Cut the initializer at the first top-level '=' (e.g. `= {`, `= NULL`).
    if has_top_level_eq(text, 0, len(text)):
        depth = 0
        i = 0
        while i < len(text):
            c = text[i]
            if c == '"':
                i = skip_string(text, i)
            elif c == "'":
                i = skip_char(text, i)
            elif c in "([{":
                depth += 1
                i += 1
            elif c in ")]}":
                depth -= 1
                i += 1
            elif c == "=" and depth == 0:
                text = text[:i]
                break
            else:
                i += 1

    # Anonymous struct/union: the name follows the closing '}'.
    if "{" in text and "}" in text:
        text = text[text.rfind("}") + 1 :]
    # Function-pointer declarator: `static void (*cb)(int);` -> name after '('.
    m = re.search(r"\(\s*\*", text)
    if m:
        name = IDENT_RE.search(text, m.end())
        return name.group(0) if name else None
    # Strip an array declarator (`name[N]`) so the name is the last identifier.
    depth = 0
    for i, ch in enumerate(text):
        if ch == "[" and depth == 0:
            text = text[:i]
            break
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
    ids = IDENT_RE.findall(text)
    if not ids:
        return None
    return ids[-1]


def decl_extent(text, start):
    """Return the end index (exclusive) of the declaration starting at start.

    Handles prototypes (`;`), variable declarations with initializers
    (`;` after `{...}` / `(...)`), anonymous `struct/union { ... } name;`,
    and function definitions (up to the matching '}' of the body).
    """
    n = len(text)
    depth = 0
    i = start
    while i < n:
        c = text[i]
        if c == '"':
            i = skip_string(text, i)
            continue
        if c == "'":
            i = skip_char(text, i)
            continue
        if c in "([{":
            if c == "{" and depth == 0:
                # '{' at depth 0: function body if the previous token is ')';
                # otherwise (anonymous type / initializer) keep scanning.
                prev = i - 1
                while prev >= 0 and text[prev] in " \t\n\r":
                    prev -= 1
                if prev >= 0 and text[prev] == ")":
                    # Function body: consume to the matching '}'.
                    i += 1
                    bd = 1
                    while i < n and bd > 0:
                        ch = text[i]
                        if ch == '"':
                            i = skip_string(text, i)
                            continue
                        if ch == "'":
                            i = skip_char(text, i)
                            continue
                        if ch == "{":
                            bd += 1
                        elif ch == "}":
                            bd -= 1
                        i += 1
                    return i
            depth += 1
            i += 1
        elif c in ")]}":
            depth -= 1
            i += 1
        elif c == ";" and depth == 0:
            return i + 1
        else:
            i += 1
    return n


def scan_static_decl(text, start):
    """Classify a file-scope `static` declaration starting at text[start].

    Returns (kind, end) where kind is 'func' or 'var', and end is the index
    just past the whole declaration (function bodies included). Returns
    (None, None) for `static const` (always allowed).
    """
    n = len(text)
    i = start + 6  # past "static"
    while i < n and text[i] in " \t\r":
        i += 1
    if text.startswith("const", i) and (i + 5 >= n or not text[i + 5].isalnum()):
        return (None, None)

    end = decl_extent(text, start)
    decl = text[start:end]

    # First '(' inside the declaration decides function vs variable. A
    # function's parameter list is preceded directly by its name; an
    # initializer call (`static int x = foo();`) has a top-level '=' before.
    paren = find_first_paren(decl, 0)
    if paren != -1:
        prev = paren - 1
        while prev >= 0 and decl[prev] in " \t\n\r":
            prev -= 1
        if prev >= 0 and (decl[prev].isalnum() or decl[prev] == "_"):
            if not has_top_level_eq(decl, 0, paren):
                return ("func", end)
    return ("var", end)


def scan_file(relpath):
    """Return (statics, accessors): file-scope static decls and accessor hits.

    statics: list of (line_no, symbol, decl_text)
    accessors: list of (line_no, match_text)
    """
    full = os.path.join(REPO_ROOT, relpath)
    with open(full, encoding="utf-8", errors="replace") as f:
        raw = f.read()
    text = strip_comments(raw)
    n = len(text)

    statics = []
    i = 0
    while i < n:
        if text[i] == "s" and text.startswith("static", i):
            # Column 0 only (file scope): previous char is a newline or start.
            at_col0 = i == 0 or text[i - 1] == "\n"
            after = i + 6
            if at_col0 and (after >= n or text[after] in " \t\r"):
                kind, end = scan_static_decl(text, i)
                if kind == "var":
                    line_no = text.count("\n", 0, i) + 1
                    symbol = extract_symbol(text[i:end])
                    statics.append((line_no, symbol, text[i:end].strip()))
                i = end if end is not None else after
                continue
        i += 1

    accessors = []
    for m in ACCESSOR_RE.finditer(text):
        line_no = text.count("\n", 0, m.start()) + 1
        accessors.append((line_no, m.group(0)))
    return statics, accessors


def load_allowlist(path):
    allowed = set()
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            allowed.add(line)
    return allowed


def main():
    args = sys.argv[1:]
    dump = "--dump" in args
    allowlist_path = ALLOWLIST_DEFAULT
    if "--allowlist" in args:
        allowlist_path = args[args.index("--allowlist") + 1]

    allowed = load_allowlist(allowlist_path) if not dump else set()

    violations = []
    accessor_violations = []
    detected = set()
    for dirname in SCAN_DIRS:
        base = os.path.join(REPO_ROOT, dirname)
        for root, _dirs, files in os.walk(base):
            for fn in sorted(files):
                if not fn.endswith((".c", ".h")):
                    continue
                rel = os.path.relpath(os.path.join(root, fn), REPO_ROOT).replace(os.sep, "/")
                statics, accessors = scan_file(rel)
                for line_no, symbol, decl in statics:
                    key = f"{rel}:{symbol}"
                    detected.add(key)
                    if symbol is None or key not in allowed:
                        violations.append((rel, line_no, decl, key))
                for line_no, mtext in accessors:
                    accessor_violations.append((rel, line_no, mtext))

    if dump:
        for key in sorted(detected):
            print(key)
        return 0

    failed = False
    if violations:
        failed = True
        print("ERROR: file-scope mutable static variables found (not allowed):", file=sys.stderr)
        for rel, line_no, decl, key in sorted(violations):
            print(f"  {rel}:{line_no}: {decl}", file=sys.stderr)
            print(f"    -> only allowed via no-statics-allowlist.txt entry: {key}", file=sys.stderr)
    if accessor_violations:
        failed = True
        print("ERROR: deleted accessor reintroduced (sk_app_api / sk_logger_api / sk_repository_api):", file=sys.stderr)
        for rel, line_no, mtext in sorted(accessor_violations):
            print(f"  {rel}:{line_no}: {mtext}(", file=sys.stderr)

    unused = sorted(allowed - detected)
    if unused:
        print(f"NOTE: {len(unused)} allowlist entr(y/ies) no longer match any static (can be removed):", file=sys.stderr)
        for key in unused:
            print(f"  {key}", file=sys.stderr)

    if failed:
        print("check-no-statics: FAILED", file=sys.stderr)
        return 1
    print(f"check-no-statics: OK ({len(detected)} file-scope statics allowlisted, "
          f"{len(accessor_violations)} accessor hits)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
