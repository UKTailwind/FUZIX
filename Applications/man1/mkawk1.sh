#!/bin/sh
#
# Generate awk.1 for man(1) from the upstream one-true-awk page.
#
#   sh mkawk1.sh          -> Applications/man1/awk.1
#
# WHY THIS IS GENERATED.  Applications/awk/PORTING says the point of
# this port is that taking a newer awk is a COPY rather than a merge,
# and two files differ from upstream by three lines between them.  A
# hand-edited man page would be a third, and one that rots quietly.  So
# the upstream page is left alone and converted here instead: rerun this
# after updating awk and the page follows.
#
# WHAT IS CONVERTED, and why each is needed.  man(1) is not nroff:
#
#   .de NAME ... ..   macro definitions.  man(1) has none, so it prints
#                     "**** Unknown formatter command: .de" and then
#                     spills the macro bodies into the page as text.
#                     Dropped; the three macros are handled below.
#   .EX / .EE         upstream's example brackets, defined by those
#                     macros as .nf/.ft CW and .fi/.ft 1.  Become
#                     .nf/.fi, which is the part man(1) can do - and
#                     which is what makes the examples come out as
#                     examples rather than as filled prose.
#   .TF x             a width hint for the list that follows; every use
#                     is immediately followed by a real .TP.  Dropped.
#   .ft / .CT         font selection and a content-type marker.  Neither
#                     means anything here.  Dropped.
#   .ns               no-space mode, used to pin a caption to the example
#                     above it.  man(1) does not know it.  Dropped; the
#                     caption gains a blank line and reads the same.
#   \f(CW             constant-width font.  man(1) reads \f, fails to
#                     match "(", and then prints the letters CW into the
#                     text.  Becomes \fB.
#   \e                nroff's literal backslash.  man(1) eats \ followed
#                     by a letter, so this loses the backslash it was
#                     there to print.  Becomes \.
#
# Then a FUZIX section is appended, because upstream's page describes an
# awk that counts Unicode code points and this one counts bytes.  A page
# that promises what the binary does not do is the thing this whole
# directory exists to avoid.

set -e
D=$(cd "$(dirname "$0")" && pwd)
SRC=$D/../awk/awk.1
OUT=$D/awk.1

[ -r "$SRC" ] || { echo "no $SRC" >&2; exit 1; }

sed -e '/^\.de /,/^\.\.$/d' \
    -e 's/^\.EX$/.nf/' \
    -e 's/^\.EE$/.fi/' \
    -e '/^\.TF/d' \
    -e '/^\.ft /d' \
    -e '/^\.CT /d' \
    -e '/^\.ns$/d' \
    -e 's/\\f(CW/\\fB/g' \
    -e 's/\\e/\\\\/g' \
    "$SRC" > "$OUT"

cat >> "$OUT" <<'TAIL'
.SH ON FUZIX
This is the one true awk as its authors maintain it, with the
differences below.  They are in the build, not in this page: see
Applications/awk/PORTING for why each one is as it is.
.TP
.B Characters are bytes
FUZIX has one locale and no wide characters, so
.BR length ,
.BR substr ,
.BR index ,
.BR match ,
.BR split ,
.B sub
and
.B gsub
count bytes rather than Unicode code points.  UTF-8 passes through
unharmed, but
.B length
of a two-byte character is 2, and
.B substr
can cut one in half.  Everything above about code points should be read
as being about bytes.
.TP
.B Ten open files
A FUZIX process may have ten files open and three are already spent on
standard input, output and error, so seven redirections or pipes at once
is the ceiling.
.TP
.B CSV works
.B \-\-csv
is here and behaves as described above.
.SH SEE ALSO
.BR sed (1),
.BR grep (1),
.BR sort (1)
TAIL

echo "wrote $OUT"
