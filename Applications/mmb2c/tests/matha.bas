' The first batch of MATH additions: SHIFT, POWER, V_NORMALISE,
' V_CROSS, V_PRINT, M_PRINT, and the functions MAGNITUDE and DOTPRODUCT.
'
' Every line here - the values AND the error wordings - is blessed
' against a real MMBasic 6.03.02: devtools/ab.py runs this file on the
' interpreter and diffs it against matha.expected.  Re-run that before
' changing any expected line.
'
' The cases that are here because they caught something:
'
'   shr    the fourth argument is a BARE U.  A real MMBasic silently
'          ignores "U" and shifts arithmetically instead; we refuse the
'          quoted form rather than copy the silence.
'   pow*   POWER into an INTEGER array rounds the exponent to a whole
'          number first, so 2.7 cubes.  Into a float array it does not.
'   pow1   an exponent of exactly 1 is a copy, not pow(x, 1).
'   mag2   MAGNITUDE takes any rank and reads the array flat.
'   self   V_NORMALISE and V_CROSS may write back over a source, which
'   alias  works only because both read everything before writing.

Option Base 0

' --- MATH SHIFT -------------------------------------------------------
Dim Integer s(3), t(3)
s(0) = 1
s(1) = -8
s(2) = 255
s(3) = -1
Math Shift s(), 2, t()
Print "shl  "; t(0); t(1); t(2); t(3)
Math Shift s(), -2, t()
Print "sar  "; t(0); t(1); t(2); t(3)
Math Shift s(), -2, t(), U
Print "shr  "; t(0); t(1); t(2); t(3)

' --- MATH POWER -------------------------------------------------------
Dim Float pf(3), qf(3)
pf(0) = 2 : pf(1) = 3 : pf(2) = 0.5 : pf(3) = -2
Math Power pf(), 2, qf()
Print "powf "; qf(0); qf(1); qf(2); qf(3)
Math Power pf(), 1, qf()
Print "pow1 "; qf(0); qf(1); qf(2); qf(3)
Dim Integer ai(3), bi(3)
ai(0) = 2 : ai(1) = 3 : ai(2) = 4 : ai(3) = 5
Math Power ai(), 3, bi()
Print "powi "; bi(0); bi(1); bi(2); bi(3)
Math Power ai(), 2.7, bi()
Print "pow* "; bi(0); bi(1); bi(2); bi(3)

' --- MATH(MAGNITUDE / DOTPRODUCT --------------------------------------
Dim Float v(2), w(2)
v(0) = 3 : v(1) = 4 : v(2) = 12
w(0) = 1 : w(1) = 2 : w(2) = 3
Print "mag  "; Math(MAGNITUDE v())
Print "dot  "; Math(DOTPRODUCT v(), w())
Dim Float two(1,1)
two(0,0) = 1 : two(1,0) = 2 : two(0,1) = 2 : two(1,1) = 4
Print "mag2 "; Math(MAGNITUDE two())

' --- MATH V_NORMALISE -------------------------------------------------
Dim Float n(2)
Math V_Normalise v(), n()
Print "norm "; n(0); n(1); n(2)
Math V_Normalise v(), v()
Print "self "; v(0); v(1); v(2)

' --- MATH V_CROSS -----------------------------------------------------
Dim Float x(2), y(2), z(2)
x(0) = 1 : x(1) = 0 : x(2) = 0
y(0) = 0 : y(1) = 1 : y(2) = 0
Math V_Cross x(), y(), z()
Print "cross"; z(0); z(1); z(2)
Math V_Cross x(), y(), x()
Print "alias"; x(0); x(1); x(2)

' --- MATH V_PRINT / M_PRINT -------------------------------------------
Dim Float fp(2)
fp(0) = 1.5 : fp(1) = -2.25 : fp(2) = 3
Math V_Print fp()
Dim Integer ip(2)
ip(0) = 10 : ip(1) = 255 : ip(2) = -3
Math V_Print ip()
Math V_Print ip(), HEX
Dim Float m(1,2)
m(0,0) = 1 : m(1,0) = 2
m(0,1) = 3 : m(1,1) = 4
m(0,2) = 5 : m(1,2) = 6
Math M_Print m()
Dim Integer mi(1,1)
mi(0,0) = 7 : mi(1,0) = 8
mi(0,1) = 9 : mi(1,1) = 10
Math M_Print mi()

' --- the run-time refusals, in MMBasic's own words --------------------
Dim Integer e1(3), e2(4)
On Error Skip 1
Math Shift e1(), 99, e1()
Print "err shrange :"; MM.ErrMsg$
On Error Skip 1
Math Shift e1(), 1, e2()
Print "err shsize  :"; MM.ErrMsg$
On Error Skip 1
Math Power e1(), 2, e2()
Print "err powsize :"; MM.ErrMsg$
Dim Float f1(3), f2(4), f3(2)
On Error Skip 1
Math V_Normalise f1(), f2()
Print "err normsize:"; MM.ErrMsg$
On Error Skip 1
Math V_Cross f1(), f3(), f3()
Print "err cross1  :"; MM.ErrMsg$
On Error Skip 1
Math V_Cross f3(), f1(), f3()
Print "err cross2  :"; MM.ErrMsg$
On Error Skip 1
Math V_Cross f3(), f3(), f1()
Print "err cross3  :"; MM.ErrMsg$
On Error Skip 1
Print Math(DOTPRODUCT f1(), f2())
Print "err dotsize :"; MM.ErrMsg$
