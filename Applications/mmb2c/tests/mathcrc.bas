' MATH(CRC8|CRC12|CRC16|CRC32 ...) over the check string every CRC
' specification uses, "123456789", then each optional argument in turn.
Option Explicit

Dim String s$ = "123456789"
Dim integer ai(8), k
Dim float af(8)

For k = 0 To 8
  ai(k) = Asc(Mid$(s$, k + 1, 1))
  af(k) = ai(k)
Next k

' the defaults: poly 7 / &H80D / &H1021 / &H4C11DB7, masks 0, no reversal
Print Hex$(Math(CRC8 s$))
Print Hex$(Math(CRC12 s$))
Print Hex$(Math(CRC16 s$))
Print Hex$(Math(CRC32 s$))

' the same bytes as an integer array and as a float array
Print Hex$(Math(CRC16 ai()))
Print Hex$(Math(CRC16 af()))

' CRC-16/CCITT-FALSE is the default polynomial with a start mask
Print Hex$(Math(CRC16 s$, 9, &H1021, &HFFFF))

' XMODEM is the same polynomial with no masks at all
Print Hex$(Math(CRC16 s$, 0, &H1021))

' an EMPTY slot keeps that argument's default, and MMBasic allows it
Print Hex$(Math(CRC16 s$, , , &HFFFF))

' a length shorter than the source
Print Hex$(Math(CRC16 s$, 4))
Print Hex$(Math(CRC16 ai(), 4))

' reverseIn and reverseOut, the two that select the reflected forms:
' CRC-32/ISO-HDLC (the zip/PNG one) is the default polynomial with
' both reversals, a start mask of all ones and an end mask of all ones
Print Hex$(Math(CRC32 s$, 0, &H4C11DB7, &HFFFFFFFF, &HFFFFFFFF, 1, 1))

' ... and CRC-16/ARC, reflected with the reversed-notation polynomial
Print Hex$(Math(CRC16 s$, 0, &H8005, 0, 0, 1, 1))

' CRC-8 with an end mask, which is where the reverse/XOR ORDER shows.
' An all-ones or all-zero mask cannot show it - reversing commutes with
' XOR against a symmetric mask - so the third line is the one that
' does: &H20 here, where 6.03.00's XOR-then-reverse gives &HDF.
Print Hex$(Math(CRC8 s$, 0, &H07, 0, &HFF))
Print Hex$(Math(CRC8 s$, 0, &H07, 0, &HFF, 0, 1))
Print Hex$(Math(CRC8 s$, 0, &H07, 0, &H0F, 0, 1))

' the empty string is the start mask straight through
Print Hex$(Math(CRC16 "", 0, &H1021, &HABCD))
