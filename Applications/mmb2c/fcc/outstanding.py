"""Every MMBasic command and function mmbc does NOT translate.

MMBasic's token names carry the trailing '(' ("Abs(") and its command
table carries PIO assembler pseudo-ops and two comment artefacts, so
both sides need normalising before they can be compared at all.
"""
import os
import re

MMB = "/mnt/d/Dropbox/PicoMite/PicoMite/AllCommands.h"
MMB2C = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                     os.pardir, "mmb2c.py")

src = open(MMB, encoding="utf-8", errors="replace").read().split("\n")


def region(a, b):
    out, on = [], False
    for line in src:
        if a in line:
            on = True
            continue
        if on and b in line:
            break
        if on:
            out.append(line)
    return out


def names(lines):
    out = []
    for line in lines:
        s = line.lstrip()
        if s.startswith("//") or s.startswith("/*"):
            continue
        m = re.search(r'\(unsigned char \*\)"([^"]*)"', line)
        if m and m.group(1):
            out.append(m.group(1))
    return out


def norm(n):
    return n.rstrip("(").strip().upper()


# Language syntax, not functions: the parser handles these, they are
# never rows in a BUILTINS table.
SYNTAX = {
    "AND", "OR", "NOT", "XOR", "MOD", "INV", "AS", "ELSE", "FOR", "GOTO",
    "GOSUB", "THEN", "STEP", "TO", "WHEN", "UNTIL", "WHILE", "NEXT",
    "ELSEIF", "ENDIF", "END IF", "SHL", "SHR", "IS", "OUTPUT", "INPUT",
}

cmds = names(region("#ifdef INCLUDE_COMMAND_TABLE",
                    "#endif /* INCLUDE_COMMAND_TABLE"))
toks = names(region("#ifdef INCLUDE_TOKEN_TABLE",
                    "#endif /* INCLUDE_TOKEN_TABLE"))

text = open(MMB2C).read()
body = text[text.index("def statement_inner"):]
body = body[:body.index("\n    def ", 10)]
stmts = set()
for m in re.finditer(r"up == '([A-Z0-9$?.]+)'", body):
    stmts.add(m.group(1))
for m in re.finditer(r"up in \(([^)]*)\)", body):
    for w in re.findall(r"'([A-Z0-9$?.]+)'", m.group(1)):
        stmts.add(w)
stmts.add("TYPE")

tbl = text[text.index("BUILTINS = {"):]
tbl = tbl[:tbl.index("\n}")]
funcs = set(re.findall(r"'([A-Z0-9$?.]+)'\s*:", tbl))


def supported(name, have):
    up = norm(name)
    if not up:
        return True
    if up in have:
        return True
    return up.split()[0] in have


def keep_cmd(n):
    u = norm(n)
    if not u or not u[0].isalpha():        # */ and /* artefacts
        return False
    if u.startswith("_"):                  # PIO assembler pseudo-ops
        return False
    return True


def keep_tok(n):
    u = norm(n)
    if not u or not u[0].isalpha():        # operators
        return False
    return u not in SYNTAX


miss_c = sorted({norm(c) for c in cmds if keep_cmd(c) and not supported(c, stmts)})
miss_f = sorted({norm(t) for t in toks if keep_tok(t) and not supported(t, funcs)})
tot_c = len({norm(c) for c in cmds if keep_cmd(c)})
tot_f = len({norm(t) for t in toks if keep_tok(t)})

print("COMMANDS   %d real, %d translated, %d outstanding" %
      (tot_c, tot_c - len(miss_c), len(miss_c)))
print("FUNCTIONS  %d real, %d translated, %d outstanding" %
      (tot_f, tot_f - len(miss_f), len(miss_f)))
print()
print("=== COMMANDS OUTSTANDING ===")
print("  " + "  ".join(miss_c))
print()
print("=== FUNCTIONS OUTSTANDING ===")
print("  " + "  ".join(miss_f))
