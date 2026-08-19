' mminfo.bas - MM.INFO() sub-keywords, the flat MM.* spellings, GPn pin
' names, KEYDOWN() and the scope of CONST inside a SUB.
'
' Everything printed here has to be the same on the host and on the
' board, so the two answers that cannot be (PATH and CURRENT, which are
' argv[0], and VERSION, which moves every release) are tested by shape
' rather than printed.

Option Explicit

Print "-- what this machine is"
Print "device   [" + MM.Device$ + "]"
Print "platform [" + MM.Info(Platform) + "]"
Print "drive    [" + MM.Info$(Drive) + "]"
' MM.INFO$( is the same function as MM.INFO( - MMBasic overlays both
' spellings onto fun_info and decides the type from the keyword.
Print "same     "; MM.Info(Device) = MM.Info$(Device)
Print "version  "; MM.Info(Version) = MM.Ver

Print "-- OPTION BASE is known when we translate"
Print "base     "; MM.Info(Option Base)

Print "-- pin names"
' MMBasic writes a pin as GPn wherever a pin is expected.  Here a pin is
' its GPIO number, so the name IS the number.
Print "gp0      "; GP0
Print "gp8      "; GP8
Print "gp47     "; GP47
Print "pinno    "; MM.Info(PinNo "GP8")
Print "pinno$   "; MM.Info(PinNo "gp" + Str$(13))
' Unquoted too: MMBasic's PINNO reads the raw text for "GPnn" before it
' evaluates anything, so a bare pin name is legal there as well as a
' string.  Here the bare name is already the number.
Print "pinno bare"; MM.Info(PinNo GP1); MM.Info(PinNo GP47)

Print "-- font metrics follow FONT"
Font 1, 1
Print "1,1      "; MM.FontWidth; " x"; MM.FontHeight
Font 3, 1
Print "3,1      "; MM.Info(FontWidth); " x"; MM.Info(FontHeight)
Font 1, 2
Print "1,2      "; MM.FontWidth; " x"; MM.FontHeight
Font 1, 1

Print "-- the text cursor"
Print "hpos     "; MM.HPos; " "; MM.Info(HPos)
Print "vpos     "; MM.VPos; " "; MM.Info(VPos)

Print "-- files"
Dim f$ = "mminfo.tmp"
Dim ch%
Open f$ For Output As #1
' No trailing newline, so the size does not depend on what the runtime
' writes for one.
Print #1, "12345";
Close #1
Print "file     "; MM.Info(Exists File f$)
Print "size     "; MM.Info(FileSize f$)
Print "dir      "; MM.Info(Exists Dir f$)
MkDir "mminfo.d"
Print "isdir    "; MM.Info(Exists Dir "mminfo.d")
' A directory is -1 to EXISTS FILE, which is how a program tells "wrong
' kind" from "missing" - MMBasic's own answer.
Print "dirasfile"; MM.Info(Exists File "mminfo.d")
Print "missing  "; MM.Info(Exists File "no-such-file")
Print "nosize   "; MM.Info(FileSize "no-such-file")
Kill f$
RmDir "mminfo.d"

Print "-- PATH and CURRENT are argv[0], so test the shape"
Print "path/    "; Right$(MM.Info(Path), 1) = "/"
Print "current  "; Len(MM.Info(Current)) > 0

Print "-- KEYDOWN: nothing is held under the gates"
Print "count    "; KeyDown(0)
Print "first    "; KeyDown(1)
Print "locks    "; KeyDown(8)

Print "-- CONST is local to the SUB it is written in"
one()
two()
three()
four()
Print "outer    " + Banner$
Print "outer2   "; Wide

Const Banner$ = "global banner"
Const Wide = 99

Sub one()
  ' MMBasic: local, because g_LocalIndex is not 0 when this runs.
  Const Widget$ = "a string here"
  Print "one      " + Widget$
End Sub

Sub two()
  Local i%, Widget%
  ' The same name as an INTEGER, in a different routine.  Before the
  ' fix, one()'s string CONST was global and this was
  ' "'widget' is STRING but used as INTEGER".
  For Widget% = 1 To 3
    Inc i%, Widget%
  Next Widget%
  Print "two      "; i%
End Sub

Sub four()
  ' A LOCAL that shadows a GLOBAL CONST.  MMBasic shadows - findvar
  ' looks in the local table first - but a global CONST is emitted as a
  ' #define, and a macro has no scope: with both C names on the same
  ' prefix this declaration was rewritten by the macro and the output
  ' did not compile.
  Local Banner$ = "the local one"
  Print "four     " + Banner$
End Sub

Sub three()
  ' MMBasic allows the same CONST name twice in a routine, because only
  ' one branch runs.  And the expression is evaluated ONCE, where the
  ' statement stands - it is not substituted textually, so a CONST may
  ' call a function.
  Local k% = 2
  If k% > 1 Then
    Const Answer% = Doubled%(k%)
  Else
    Const Answer% = 0
  EndIf
  Print "three    "; Answer%
End Sub

Function Doubled%(n%)
  Doubled% = n% * 2
End Function
