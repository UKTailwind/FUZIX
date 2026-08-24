Option Base 0
' MATH(CROSSING ...) and MATH WINDOW.
'
' Both were run on a real MMBasic 6.03.02 BEFORE either was written,
' and both had something in them that reading MATHS.c would have got
' wrong:
'
'   - MATH WINDOW into an INTEGER array TRUNCATES.  The reference casts
'     with (long long int), toward zero, where everything else in
'     MMBasic that lands a float in an integer rounds.  1..5 windowed
'     onto 0..10 gives 0 2 5 7 10, not 0 3 5 8 10.
'   - MATH(CROSSING) returns an OFFSET from the first element, not a
'     subscript, so under OPTION BASE 1 a program wants a(found + 1).
'   - a crossing too near the end to confirm ENDS the search rather
'     than being skipped: the reference breaks there, so a late spike
'     hides a real crossing after it.  "cross cf4" is that case.
'
' Every line here is blessed against the interpreter with
' devtools/ab.py - the values and the five refusals alike.

Dim Float a(9)
a(0)=-3 : a(1)=-1 : a(2)=1 : a(3)=4 : a(4)=2
a(5)=-2 : a(6)=-5 : a(7)=0 : a(8)=3 : a(9)=6
Print "cross dflt"; Math(CROSSING a())
Print "cross lvl2"; Math(CROSSING a(), 2)
Print "cross down"; Math(CROSSING a(), 0, -1)
Print "cross cf2 "; Math(CROSSING a(), 0, 1, 2)
Print "cross cf4 "; Math(CROSSING a(), 0, 1, 4)
Print "cross none"; Math(CROSSING a(), 99)
Dim Integer b(5)
b(0)=-2 : b(1)=-1 : b(2)=0 : b(3)=1 : b(4)=2 : b(5)=3
Print "cross int "; Math(CROSSING b(), 0)

Dim Float w(4), o(4)
w(0)=0 : w(1)=5 : w(2)=10 : w(3)=15 : w(4)=20
Math Window w(), 0, 1, o()
Print "win ff    "; o(0); o(1); o(2); o(3); o(4)
Dim Integer oi(4)
Math Window w(), 0, 100, oi()
Print "win fi    "; oi(0); oi(1); oi(2); oi(3); oi(4)
Dim Integer wi(4)
wi(0)=1 : wi(1)=2 : wi(2)=3 : wi(3)=4 : wi(4)=5
Math Window wi(), 0, 1, o()
Print "win if    "; o(0); o(1); o(2); o(3); o(4)
Math Window wi(), 0, 10, oi()
Print "win ii    "; oi(0); oi(1); oi(2); oi(3); oi(4)
Dim Float lo, hi
Math Window w(), -1, 1, o(), lo, hi
Print "win range "; lo; hi; o(0); o(4)
Dim Integer ilo, ihi
Math Window w(), -1, 1, o(), ilo, ihi
Print "win irange"; ilo; ihi

On Error Skip 1
Print Math(CROSSING a(), 0, 0)
Print "e dir0   :"; MM.ErrMsg$
On Error Skip 1
Print Math(CROSSING a(), 0, 5)
Print "e dir5   :"; MM.ErrMsg$
On Error Skip 1
Print Math(CROSSING a(), 0, 1, 0)
Print "e cf0    :"; MM.ErrMsg$
On Error Skip 1
Print Math(CROSSING a(), 0, 1, 99)
Print "e cf99   :"; MM.ErrMsg$
Dim Float short(3)
On Error Skip 1
Math Window w(), 0, 1, short()
Print "e winsize:"; MM.ErrMsg$
' Two more refusals are NOT here, because they are not run-time ones:
' a string where MATH WINDOW wants a range variable, and a 2-D array
' where MATH(CROSSING) wants a one-dimensional one.  MMBasic raises
' "Invalid variable" and "Argument 1 must be a 1D numerical array" when
' the statement runs; the types are known before the program runs here,
' so both are refused at translation, naming the line.  A test cannot
' hold both behaviours, and refusing earlier is the better one.
