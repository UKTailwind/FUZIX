"""Every MMBasic command and function, with mmb2c's status, as TSV.

The names come from MMBasic's own AllCommands.h and the status from
mmb2c.py's dispatch, by the same reader mmedit/genkw.py uses - so this
cannot flatter us. Category assignment is a separate hand-kept table
(catmap.py), because which of the five a gap belongs in is a judgement
and not something a scan can decide.

    python3 statuslist.py > /tmp/status.tsv
"""
import re
import sys

MMB = "/mnt/d/Dropbox/PicoMite/PicoMiteV6.03.00/AllCommands.h"
MMB2C = "/home/peter/src/mmb2c/mmb2c.py"

src = open(MMB, encoding="utf-8", errors="replace").read().split("\n")


def region(start_marker, end_marker):
    out, on = [], False
    for line in src:
        if start_marker in line:
            on = True
            continue
        if on and end_marker in line:
            break
        if on:
            out.append(line)
    return out


def names(lines):
    out = []
    for line in lines:
        t = line.lstrip()
        if t.startswith("//") or t.startswith("/*"):
            continue
        m = re.search(r'\(unsigned char \*\)"([^"]*)"', line)
        if m and m.group(1):
            out.append(m.group(1))
    return out


cmds = names(region("#ifdef INCLUDE_COMMAND_TABLE",
                    "#endif /* INCLUDE_COMMAND_TABLE"))
toks = names(region("#ifdef INCLUDE_TOKEN_TABLE",
                    "#endif /* INCLUDE_TOKEN_TABLE"))

text = open(MMB2C).read()
body = text[text.index("def statement_inner"):]
body = body[:body.index("\n    def ", 10)]
supported = set()
for m in re.finditer(r"up == '([A-Z0-9$?]+)'", body):
    supported.add(m.group(1))
for m in re.finditer(r"up in \(([^)]*)\)", body):
    for w in re.findall(r"'([A-Z0-9$?]+)'", m.group(1)):
        supported.add(w)
tbl = text[text.index("BUILTINS = {"):]
tbl = tbl[:tbl.index("\n}")]
for name, lo, hi in re.findall(r"'([A-Z0-9.$]+)': \((\d+), (\d+)\)", tbl):
    supported.add(name)
for m in re.finditer(r"MATHFUNCS = \{([^}]*)\}", text):
    supported.update(re.findall(r"'([A-Z0-9]+)'", m.group(1)))
supported.update(["AND", "OR", "NOT", "XOR", "MOD", "INV", "THEN", "TO",
                  "STEP", "AS", "ELSE", "ELSEIF", "END IF", "END SUB",
                  "END FUNCTION", "END SELECT", "CASE ELSE", "SELECT CASE",
                  "EXIT DO", "EXIT FOR", "EXIT SUB", "EXIT FUNCTION",
                  "LOOP", "UNTIL", "WHILE", "WEND", "NEXT",
                  "TYPE", "END TYPE", "DEFINEFONT", "END DEFINEFONT",
                  "BASE", "EXPLICIT", "DEFAULT", "SELECT",
                  "INTEGER", "FLOAT", "STRING",
                  "OUTPUT", "APPEND", "RANDOM",
                  "TEMPR START", "LINE INPUT", "ARRAY SET", "ARRAY ADD",
                  "ELSE IF"])


def is_supported(nm):
    u = nm.upper()
    if u.endswith("("):
        u = u[:-1]
    return u in supported


for kind, lst in (("cmd", cmds), ("fn", toks)):
    for nm in lst:
        print("%s\t%s\t%s" % (kind, nm,
                              "yes" if is_supported(nm) else "no"))
