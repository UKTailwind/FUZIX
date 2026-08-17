' varaddr.bas - PEEK(VARADDR v) and POKE.
'
' The address of a variable, and writing memory by address.  On this
' machine there is no MMU, so an address is a machine address and these
' are a load and a store with nothing between them and the bus - which
' is the point, and the risk.
'
' Nothing here pokes an address it did not get from VARADDR, which is
' the only way to write one of these that is safe to run in a gate.

Option Explicit
Option Base 0

Dim n%, f, s$, a%(4), t$(2), i%
Dim addr%, v%

Print "-- a scalar integer, there and back"
n% = &h1234
addr% = Peek(VARADDR n%)
Print "byte 0   &h" + Hex$(Peek(BYTE addr%), 2)
Print "byte 1   &h" + Hex$(Peek(BYTE addr% + 1), 2)
Print "integer  &h" + Hex$(Peek(INTEGER addr%))
Poke BYTE addr%, &h78
Print "after    &h" + Hex$(n%)

Print "-- a float"
f = 1.5
addr% = Peek(VARADDR f)
Print "float    "; Peek(FLOAT addr%)
Poke FLOAT addr%, 2.25
Print "after    "; f

Print "-- a string is its LENGTH BYTE, as in MMBasic"
s$ = "hello"
addr% = Peek(VARADDR s$)
Print "length   "; Peek(BYTE addr%)
Print "first    " + Chr$(Peek(BYTE addr% + 1))
Print "third    " + Chr$(Peek(BYTE addr% + 3))
' Upper-case the first character by hand: 'h' is 104, 'H' is 72.
Poke BYTE addr% + 1, Peek(BYTE addr% + 1) - 32
Print "poked    " + s$
' ... and shorten it by writing the length byte
Poke BYTE addr%, 4
Print "shortened" + s$; " len"; Len(s$)

Print "-- one element of an array"
For i% = 0 To 4 : a%(i%) = i% * 100 : Next i%
addr% = Peek(VARADDR a%(2))
Print "a(2)     "; Peek(INTEGER addr%)
Poke INTEGER addr%, 777
Print "after    "; a%(2)
Print "a(3)     "; a%(3); "  (untouched)"

Print "-- the whole array is element 0"
addr% = Peek(VARADDR a%())
Print "a(0)     "; Peek(INTEGER addr%)
Print "same     "; Peek(VARADDR a%()) = Peek(VARADDR a%(0))
' element 1 is eight bytes further on
Print "a(1)     "; Peek(INTEGER addr% + 8)

Print "-- a string array element"
t$(1) = "world"
addr% = Peek(VARADDR t$(1))
Print "length   "; Peek(BYTE addr%)
Print "first    " + Chr$(Peek(BYTE addr% + 1))

Print "-- SHORT and WORD"
n% = 0
addr% = Peek(VARADDR n%)
Poke WORD addr%, &hDEADBEEF
Print "word     &h" + Hex$(Peek(WORD addr%))
Poke SHORT addr%, -2
Print "short    "; Peek(SHORT addr%)

Print "done"
