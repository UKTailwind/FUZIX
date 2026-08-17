' switches.bas - check a Game*Mite-style button array on GP34-GP41.
'
' Stays in TEXT mode deliberately: the console is mirrored to both the
' display and the serial port there, so whoever is at the keyboard and
' whoever is on the serial line see the same thing.
'
' Wiring this expects - each switch from its pin to GND, no resistors,
' the pull-ups are internal and the buttons read ACTIVE LOW:
'
'     GP34 bit 0 Down     GP38 bit 4 Select
'     GP35 bit 1 Left     GP39 bit 5 Start
'     GP36 bit 2 Up       GP40 bit 6 B
'     GP37 bit 3 Right    GP41 bit 7 A
'
' Press q on the keyboard to stop.

Option Explicit
Option Base 0

Const BASE = 34, NBUT = 8
Dim name$(7) = ("Down", "Left", "Up", "Right", "Select", "Start", "B", "A")
Dim i%, raw%, mask%, last% = -1, seen%, k$
Dim bits$, names$, one%, agree%

Print "Buttons on GP" + Str$(BASE) + "-GP" + Str$(BASE + NBUT - 1)
Print "each switch to GND, internal pull-ups, active low"
Print

For i% = 0 To NBUT - 1
  SetPin BASE + i%, DIN, PULLUP
Next i%

' Nothing pressed must read as all ones.  If it does not, that pin is
' shorted to ground or wired to the wrong side of the switch, and every
' reading after this would be nonsense.
raw% = Port(GP34, NBUT)
Print "idle reading  &h" + Hex$(raw%, 2); "  (want &hFF)"
If raw% <> &hFF Then
  Print "*** not all pins are idle-high - check these:"
  For i% = 0 To NBUT - 1
    If (raw% And (1 << i%)) = 0 Then
      Print "      GP"; BASE + i%; " (" + name$(i%) + ") reads low with "
      Print "      nothing pressed"
    EndIf
  Next i%
EndIf
Print
Print "Press each button.  q quits."
Print

Do
  raw% = Port(GP34, NBUT)
  mask% = raw% Xor &hFF          ' active low, so invert: 1 = pressed

  If mask% <> last% Then
    last% = mask%
    bits$ = "" : names$ = "" : agree% = 1
    For i% = NBUT - 1 To 0 Step -1
      If mask% And (1 << i%) Then bits$ = bits$ + "1" Else bits$ = bits$ + "0"
    Next i%
    For i% = 0 To NBUT - 1
      ' Read the pin on its own as well: PORT and PIN disagreeing would
      ' mean the group read, not the wiring, is wrong.
      one% = (Pin(BASE + i%) = 0)
      If one% <> ((mask% And (1 << i%)) <> 0) Then agree% = 0
      If one% Then names$ = names$ + name$(i%) + " "
    Next i%
    seen% = seen% Or mask%
    Print "port &h" + Hex$(raw%, 2); "  bits " + bits$;
    Print "  down: " + Choice(names$ = "", "-", names$);
    If Not agree% Then Print "  *** PORT and PIN disagree";
    Print
  EndIf

  k$ = Inkey$
  If k$ = "q" Or k$ = "Q" Then Exit Do
  Pause 20
Loop

Print
Print "buttons seen this run:"
For i% = 0 To NBUT - 1
  Print "  GP"; BASE + i%; " bit"; i%; " " + name$(i%) + "  ";
  If seen% And (1 << i%) Then Print "ok" Else Print "NEVER PRESSED"
Next i%

For i% = 0 To NBUT - 1
  SetPin BASE + i%, OFF
Next i%
Print "done"
