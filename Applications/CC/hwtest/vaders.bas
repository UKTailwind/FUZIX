' Pico-Vaders for the Pico Computer 3.
'
' Martin Herhaus's Game*Mite version, with the Game*Mite support library
' removed: no menu to return to, no switches on GP8-GP15 (this machine
' cannot claim those pins at all - the I/O header is GP0-GP7, GP26,
' GP32 and GP34-GP46), and no driver dispatched by name at run time.
'
' The controls are the keyboard, through KEYDOWN - which reports up to
' six keys HELD at once, so moving and firing together works.  The key
' codes are unchanged from the original: its own keyboard driver used
' 128-131 for the arrows and so does this machine.
'
'   left / right   move        space   fire
'   s              start       q or e  quit
'
' (c) 1978 Tomohiro Nishikado of Taito; (c) 2022-2023 Martin Herhaus;
' support library (c) 2020-2023 Thomas Hugo Williams, MIT.
Option Base 0
Option Default None
Option Explicit On

' The Game*Mite has a 320x240 LCD and no MODE statement, because that
' is its only screen.  Here that is MODE 2 - 320x240, sixteen colours -
' and it has to be asked for.
Mode 2

' And the console has to be kept off the picture.  In a graphics mode
' the console renders as pixels, so anything printed to it - including
' the SHELL's prompt, if the game was started in the background - is
' drawn over the game.  This holds the mirror off for as long as the
' program runs and the kernel gives it back when the program ends.
Option Console Serial

' FRAME PACING.  The game caps itself at Timer + 15, a 15ms frame, and
' on the Game*Mite that cap never fires: interpreted MMBasic cannot get
' round the loop in 15ms, so the real pace there is however fast the
' interpreter manages.  Compiled, this machine makes the deadline
' easily and runs AT the cap - which is much faster than the game was
' ever played.  So the period is a constant here, to be set to whatever
' the original actually achieved.
'
'   ./vaders -t     report the measured frame-body time on exit
Const FRAME_MS = 33
Dim frame_ms_total!, frame_count%, frame_start!
Dim timing% = (Instr(MM.CmdLine$, "-t") > 0)

' The controller bitmask, unchanged - the game switches on these.
Const ctrl.R      = &h01
Const ctrl.START  = &h02
Const ctrl.HOME   = &h04
Const ctrl.SELECT = &h08
Const ctrl.L      = &h10
Const ctrl.DOWN   = &h20
Const ctrl.RIGHT  = &h40
Const ctrl.UP     = &h80
Const ctrl.LEFT   = &h100
Const ctrl.ZR     = &h200
Const ctrl.X      = &h400
Const ctrl.A      = &h800
Const ctrl.Y      = &h1000
Const ctrl.B      = &h2000
Const ctrl.ZL     = &h4000
Const ctrl.OPEN  = -1
Const ctrl.CLOSE = -2
Const ctrl.SOFT_CLOSE = -3

' keys_cursor_ext from the original, over KEYDOWN instead of the key map
' the interrupt used to fill.  Same protocol: a negative argument is an
' open/close request and there is nothing to open, a non-negative one is
' answered with the bitmask.
Sub read_ctrl(x%)
  Local i%, n%, k%
  If x% < 0 Then Exit Sub
  x% = 0
  n% = KeyDown(0)
  For i% = 1 To n%
    k% = KeyDown(i%)
    If k% = 32 Then Inc x%, ctrl.A
    If k% = 98 Then Inc x%, ctrl.B
    If k% = 101 Or k% = 113 Then Inc x%, ctrl.SELECT
    If k% = 115 Then Inc x%, ctrl.START
    If k% = 128 Then Inc x%, ctrl.UP
    If k% = 129 Then Inc x%, ctrl.DOWN
    If k% = 130 Then Inc x%, ctrl.LEFT
    If k% = 131 Then Inc x%, ctrl.RIGHT
  Next i%
End Sub

' Wait until nothing is held, so one press is one action.
Sub wait_idle()
  Local k%
  Do
    k% = 0
    read_ctrl(k%)
    If Not k% Then Exit Do
    Pause 5
  Loop
End Sub

' The library beeped through the sound module; a tone is the same idea.
Sub beep_ok()
  Play Tone 1500, 1500, 30
End Sub

' (no menu to return to)
Dim CONTROLLERS$(1) = ("keys_cursor_ext", "ctrl.gamemite")
Const VERSION_STRING$ = "PICO-VADERS ON THE PC3"
' (the keyboard needs no opening)
FrameBuffer Create
Font 1
Const HIGH_SCORE_FILENAME$ = "/root/.high-scores/pico-vaders.csv"
Const X_MAX% = 204
Dim ctrl$
Dim alien$(3, 2)
Dim aliens%(55, 4)
Dim ply$(3)
Dim bnk%(4, 2, 8)
Dim a_bomb%(10, 4)
Dim noise%(200)
Dim uxpl%(3)
Dim snd%(4) = (100, 90, 85, 80, 70)
Dim udir%
Dim ux%
Dim ua%
Dim uscr%
Dim UfoSndMin% = 800, UfoSndMax% = 1100, UfoSnd% = 800, Ustp% = 100
Dim anr% = 55
Dim myst% = 0
Dim score%
Dim high_score%
Dim mvsnd% = 0
Dim adir% = 1
Dim ba%, bx%, by%
Dim plx%
Dim a_ground%
Dim num_aliens%
Dim trn%
Dim plhit%
Dim bombs_out%
Dim next_frame%
Dim game_over%
Dim y_pos%
Dim xpl$
Dim uf1$, uf2$
Dim level%
Dim lives%
Dim anim%
Dim tick%
Dim bn%
Dim bmax%
Dim dummy%
init_gfx()
init_sound()
read_high_score()
Cls
new_game_label:
intro()
read_ctrl(ctrl.OPEN)
wait_idle()
plx% = 103 : y_pos% = 48
anim% = 1 : tick% = 1
level% = 1 : lives% = 3 : score% = 0 : game_over% = 0
next_level_label:
num_aliens% = 55
bn% = 1
bmax% = Min(2 + Int(level% / 2), 10)
ua% = 0
setup_aliens()
draw_screen()
next_life_label:
plhit% = 0
clear_bombs()
Box 72, 232, 40, 8, 1, 0, 0
If lives% > 1 Then Gui Bitmap 72, 232, ply$(1), 16, 8, 1, Rgb(Green), 0
If lives% > 2 Then Gui Bitmap 88, 232, ply$(1), 16, 8, 1, Rgb(Green), 0
Do
 next_frame% = Timer + FRAME_MS : frame_start! = Timer
 move_single()
 draw_player()
 move_player()
 draw_bullet()
 draw_bomb()
 If Not(tick% Mod 16) Then drop_bomb()
 If Not(tick% Mod 4) Then draw_ufo()
 Inc tick%
 start_ufo()
 If num_aliens% = 0 Then Exit Do
 If plhit% Then Exit Do
 If a_ground% Then explode_player() : game_over% = 1 : Exit Do
 Inc bn% : If bn% > bmax% Then bn% = 1
 If timing% Then Inc frame_ms_total!, Timer - frame_start! : Inc frame_count%, 1
 Do While Timer < next_frame% : Loop
Loop
If plhit% Then
 explode_player()
 Inc lives%, -1
 If lives% = 0 Then game_over% = 1 : Goto game_over_label
 dummy% = twait%(2000)
 Goto next_life_label
EndIf
If num_aliens% = 0 Then
 Inc level%
 If level% < 6 Then Inc y_pos%, 8
 dummy% = twait%(2000)
 Goto next_level_label
EndIf
game_over_label:
If game_over% Then
 write_high_score()
 show_game_over()
 Goto new_game_label
EndIf

Sub init_gfx()
 Local a%, al%, i%, n%
 Restore sr1
 For al% = 1 To 3
  For i% = 1 To 2
   alien$(al%, i%) = ""
   For n% = 1 To 16
    Read a%
    Cat alien$(al%, i%), Chr$(a%)
   Next
  Next
 Next
 For i% = 1 To 3
  ply$(i%) = ""
  For n% = 1 To 16 : Read a% : Cat ply$(i%), Chr$(a%) : Next
 Next
 Restore xpld
 xpl$ = ""
 For n% = 1 To 16 : Read a% : Cat xpl$, Chr$(a%) : Next
 Restore ufo
 uf1$ = ""
 For n% = 1 To 16 : Read a% : Cat uf1$, Chr$(a%) : Next
 uf2$ = ""
 For n% = 1 To 16 : Read a% : Cat uf2$, Chr$(a%) : Next
End Sub

Sub init_sound()
 Local i%
 For i% = 1 To 200 : noise%(i%) = Int(Rnd * 1000) : Next
End Sub

Sub read_high_score()
 If Mm.Info(Exists File HIGH_SCORE_FILENAME$) Then
  Local s$
  Open HIGH_SCORE_FILENAME$ For Input As #1
  Line Input #1, s$
  high_score% = Val(Field$(s$, 2, ","))
  Close #1
 EndIf
End Sub

Sub write_high_score()
 If Not Mm.Info(Exists Dir "/root/.high-scores") Then
' (one filesystem)
' (one filesystem)
  MkDir "/root/.high-scores"
' (one filesystem)
 EndIf
 Open HIGH_SCORE_FILENAME$ For Output As #1
 Print #1, "PLAYER 1, " Str$(high_score%)
 Close #1
End Sub

Sub clear_bombs()
 Local i%
 For i% = 1 To 10
  If a_bomb%(i%, 3) Then
   Line 50 + a_bomb%(i%, 1), a_bomb%(i%, 2), 50 + a_bomb%(i%, 1), a_bomb%(i%, 2) + 4, , Rgb(Black)
  EndIf
  a_bomb%(i%, 3) = 0
 Next
 bombs_out% = 0
 If ba% Then Line 50 + bx%, by%, 50 + bx%, by% + 4, , Rgb(Black)
 ba% = 0
End Sub

Sub intro()
 Local key%, y%
 If ctrl$ <> "" Then wait_idle()
 Box 0, 30, 320, 210, , 0, 0
 inc_score(0, 1)
 Const txt$ = "Press START to play"
 Text 160, 216, txt$, CT, , , Rgb(Green)
 Box 50, 229, 220, 1, , , Rgb(Green)
 y% = 30
 Text 144, y%, "PLA"
 Text 176, y% + Mm.Info(FontHeight) - 2 , "Y", I : Inc y%, 18
 If Not key% Then key% = poll_ctrl%(600)
 Text 160, y%, "PICOVADERS", CT : Inc y%, 25
 If Not key% Then key% = poll_ctrl%(600)
 Text 160, y%, "*SCORE ADVANCE TABLE*", CT: Inc y%, 20
 If Not key% Then key% = poll_ctrl%(600)
 Gui Bitmap 104, y%, uf1$, 16, 8, 1, Rgb(Red), 0
 Text 130, y%, "= ? MYSTERY" : Inc y%, 18
 If Not key% Then key% = poll_ctrl%(600)
 Gui Bitmap 104, y%, alien$(1, 1), 16, 8, 1, Rgb(White), 0
 Text 130, y%, "=30 POINTS" : Inc y%, 20
 If Not key% Then key% = poll_ctrl%(600)
 Gui Bitmap 104, y%, alien$(2, 1), 16, 8, 1, Rgb(White), 0
 Text 130, y%, "=20 POINTS" : Inc y%, 20
 If Not key% Then key% = poll_ctrl%(600)
 Gui Bitmap 104, y%, alien$(3, 1), 16, 8, 1, Rgb(White), 0
 Text 130, y%, "=10 POINTS" : Inc y%, 2 * Mm.Info(FontHeight)
 If Not key% Then key% = poll_ctrl%(600)
 If InStr(Mm.Device$, "PicoMite") Then Font 7
 Text 160, y%, "(C) 1978 BY TAITO", CT
 Inc y%, Mm.Info(FontHeight) + 1
 Text 160, y%, UCase$(VERSION_STRING$), CT
 Inc y%, Mm.Info(FontHeight) + 1
 Text 160, y%, "2022-2023 BY MARTIN HERHAUS", CT
 Font 1
 If Not key% Then key% = poll_ctrl%(2000)
 Local x% = 271
 Do While (x% > 177) And (Not key%)
  key% = intro_alien%(x%, -1)
 Loop
 Do While (x% < 278) And (Not key%)
  Text x% + 1, 40, "Y", I
  key% = intro_alien%(x%, 1)
 Loop
 Do While (x% > 176) And (Not key%)
  Text x% - 7, 30, "Y"
  key% = intro_alien%(x%, -1)
 Loop
 Do While (x% < 270) And (Not key%)
  key% = intro_alien%(x%, 1)
 Loop
 Text 168, 30, "Y"
 Box 174, 30, 320 - 174, 10, , 0, 0
 If key% Then
  beep_ok()
  Pause 1000
 Else
  key% = poll_ctrl%()
  beep_ok()
 EndIf
End Sub

Function intro_alien%(x%, dir%)
 Inc x%, dir%
 Select Case x%
  Case < 270
   Gui Bitmap x%, 30, alien$(1, 1 + (x% Mod 2)), 16, 8, 1, Rgb(White), 0
  Case 270
   Gui Bitmap x%, 30, alien$(1, 1 + (x% Mod 2)), 16, 8, 1, Rgb(Black), 0
 End Select
 intro_alien% = poll_ctrl%(30)
End Function

Function poll_ctrl%(duration%)
 ' Wait up to `duration` MILLISECONDS for a button, or for ever when it
 ' is zero.  Milliseconds because that is what the library used - it
 ' compared against Timer, which is in milliseconds - and the intro
 ' spaces its lines 600 apart expecting 0.6s, not 6s.
 Local t% = Timer + duration%
 Do
  poll_ctrl% = 0
  read_ctrl(poll_ctrl%)
  poll_ctrl% = poll_ctrl% And (ctrl.A Or ctrl.START Or ctrl.SELECT)
  If poll_ctrl% = ctrl.SELECT Then
   on_quit()
   poll_ctrl% = 0
   t% = Timer + duration%
  ElseIf poll_ctrl% Then
   Exit Do
  EndIf
  Pause 5
 Loop While duration% = 0 Or Timer < t%
End Function

Function twait%(duration%, mask%)
 Local t% = Timer + duration%
 Do While Timer < t%
  read_ctrl(twait%)
  If twait% = ctrl.SELECT Then on_quit()
  twait% = twait% And mask%
  If twait% Then Exit Do
  Pause 5
 Loop
End Function

Sub on_quit()
 ' The library drew a modal message box over a saved copy of the
 ' screen.  Same idea, without the widget set: the picture is kept in
 ' the framebuffer and put back if the answer is no.
 Local k%
 beep_ok()
 wait_idle()
 FrameBuffer Copy N, F
 Box 60, 100, 200, 40, 2, Rgb(Green), Rgb(Black)
 Text 160, 108, "QUIT GAME?", CT, 1, 1, Rgb(White), Rgb(Black)
 Text 160, 122, "Y = YES   N = NO", CT, 1, 1, Rgb(White), Rgb(Black)
 Do
  k% = KeyDown(1)
  If k% = 121 Or k% = 89 Then end_program(0)
  If k% = 110 Or k% = 78 Then Exit Do
  Pause 10
 Loop
 wait_idle()
 FrameBuffer Copy F, N
End Sub

Sub break_cb()
 end_program(1)
End Sub

Sub end_program(break%)
 ' No menu to hand back to: put the screen and the sound down and stop.
 Play Stop
 FrameBuffer Write N
 FrameBuffer Close
 Colour Rgb(White), Rgb(Black)
 Cls
 If timing% And frame_count% > 0 Then
   Print "frames        "; frame_count%
   Print "body avg ms   "; frame_ms_total! / frame_count%
   Print "cap is        "; FRAME_MS; " ms"
 EndIf
 End
End Sub

Sub start_ufo()
 ufo_x()
 If myst% > 20 And ua% = 0 Then
  ua% = 1
  myst% = 0
  udir% = Choice(Int(Rnd * 2) = 1, -2, 2)
  ux% = Choice(udir% = 2, 0, X_MAX%)
  Select Case Int(Rnd * 10)
   Case 7 To 8 : uscr% = 100
   Case 9 :      uscr% = 150
   Case Else:    uscr% = 50
  End Select
 EndIf
End Sub

Sub drop_bomb()
 If bombs_out% >= bmax% Then Exit Sub
 Local aln%
 For aln% = 55 To 1 Step -1
  If Not aliens%(aln%, 4) Then Continue For
  For bn% = 1 To 10
   If a_bomb%(bn%, 3) Then
    If a_bomb%(bn%, 4) = aln% Then bn% = 0 : Exit For
   EndIf
  Next
  If Not bn% Then Continue For
  Select Case aliens%(aln%, 1)
   Case < plx% - 8, > plx% + 8
    If Int(Rnd * 25) Then Continue For
  End Select
  If aln% > 44 Then Exit For
  If Not aliens%(aln% + 11, 4) Then Exit For
 Next
 If Not aln% Then Exit Sub
 For bn% = 1 To 10
  If Not a_bomb%(bn%, 3) Then
   a_bomb%(bn%, 1) = aliens%(aln%, 1) + 8
   a_bomb%(bn%, 2) = aliens%(aln%, 2) + 6
   a_bomb%(bn%, 3) = 1
   a_bomb%(bn%, 4) = aln%
   Inc bombs_out%
   Exit For
  EndIf
 Next
End Sub

Sub draw_bomb()
 Local i%
 For i% = 1 To 10
  If a_bomb%(i%, 3) = 1 Then
   Line 50 + a_bomb%(i%, 1), a_bomb%(i%, 2), 50 + a_bomb%(i%, 1), a_bomb%(i%, 2) + 4, , 0
   Inc a_bomb%(i%, 2), 1
   If hit_bunker%(a_bomb%(i%, 1), a_bomb%(i%, 2) + 4) Then
    a_bomb%(i%, 3) = 0
    Inc bombs_out%, -1
    Exit Sub
   EndIf
   If a_bomb%(i%, 2) > 224 Then
    a_bomb%(i%, 3) = 0
    Inc bombs_out%, -1
    Exit Sub
   EndIf
   Line 50 + a_bomb%(i%, 1), a_bomb%(i%, 2), 50 + a_bomb%(i%, 1), a_bomb%(i%, 2) + 4, , Rgb(Yellow)
   If ba% Then
    Select Case a_bomb%(i%, 1)
     Case bx% - 2 To bx% + 2
      Select Case a_bomb%(i%, 2)
       Case by% - 4 To by%
        ba% = 0 : a_bomb%(i%, 3) = 0
        Inc bombs_out%, -1
        explode(42 + a_bomb%(i%, 1), a_bomb%(i%, 2), 0)
        Exit Sub
      End Select
    End Select
   EndIf
   If a_bomb%(i%, 2) > 210 Then
    If a_bomb%(i%, 1) >= plx% And a_bomb%(i%, 1) < plx% + 16 Then plHit% = 1
   EndIf
  EndIf
 Next
End Sub

Sub draw_ufo()
 If ua% = 0 Then Exit Sub
 Play Tone UfoSnd%, UfoSnd%, 150
 Inc UfoSnd%, Ustp%
 If UfoSnD% = UfoSndMin% Or UfoSnd% > UfoSndMax% Then Ustp% = -Ustp%
 Box 50 + ux%, 32, 16, 10, , 0, 0
 Inc ux%, udir%
 If ux% > X_MAX% Or ux% < 0 Then ua% = 0 : Exit Sub
 Gui Bitmap 50 + ux%, 32, uf1$, 16, 8, 1, Rgb(Red), 0
End Sub

Sub ufo_x()
 If Not Uxpl%(1) Then Exit Sub
 Inc Uxpl%(3)
 Play Tone 900 + 15 * Uxpl%(3), 900 + 15 * Uxpl%(3), 100
 Select Case uxpl%(3)
  Case 40
   Text 58 + uxpl%(2), 30, " " + Str$(uscr%) + " ", C, , , Rgb(Red)
  Case 70
   Text 58 + uxpl%(2), 30, " " + Str$(uscr%) + " ", C, , , Rgb(Black)
   Uxpl%(1) = 0
   Uxpl%(3) = 0
   inc_score(uscr%)
 End Select
End Sub

Sub draw_bunkers()
 Local i%, j%
 For i% = 0 To 3
  draw_bunker(80 + i% * 45, 184)
  For j% = 1 To 8
   bnk%(i% + 1, 1, j%) = 1
   bnk%(i% + 1, 2, j%) = 1
  Next
 Next
End Sub

Sub draw_bunker(bx%, by%)
 Box bx%, by% + 4, 22, 12, , Rgb(Green), Rgb(Green)
 Box bx% + 1, by% + 3, 20, 1, , Rgb(Green), Rgb(Green)
 Box bx% + 2, by% + 2, 18, 1, , Rgb(Green), Rgb(Green)
 Box bx% + 3, by% + 1, 16, 1, , Rgb(Green), Rgb(Green)
 Box bx% + 4, by%, 14, 1, , Rgb(Green), Rgb(Green)
 Box bx% + 5, by% + 14, 12, 2, , 0, 0
 Box bx% + 6, by% + 13, 10, 1, , 0, 0
 Box bx% + 7, by% + 12, 8, 1, , 0, 0
End Sub

Function hit_bunker%(TsX%, Tsy%)
 Local bhx%, bhy%
 Select Case TsY%
  Case 184 To 200
   Select Case TsX%
    Case 30 To 51
     bhy% = Int((Tsy% - 184) / 8)
     bhx% = 1 + Int((TsX% - 30) / 3)
     If Bnk%(1, bhy%, bhx%) = 1 Then
      Bnk%(1, bhy%, bhx%) = 0
      hit_bunker% = 1
      Line 50 + TsX%, Tsy%, 50 + TsX%, TsY% + 4, , 0
      debunk 50 + TsX%, tsy%
     EndIf
    Case 75 To 96
     bhy% = Int((TsY% - 184) / 8)
     bhx% = 1 + Int((TsX% - 75) / 3)
     If Bnk%(2, bhy%, bhx%) = 1 Then
      Bnk%(2, bhy%, bhx%) = 0
      hit_bunker% = 2
      Line 50 + TsX%, Tsy%, 50 + TsX%, TsY% + 4, , 0
      debunk 50 + TsX%, TsY%
     EndIf
    Case 120 To 141
     bhy% = Int((TsY% - 184) / 8)
     bhx% = 1 + Int((TsX% - 120) / 3)
     If Bnk%(3, bhy%, bhx%) = 1 Then
      Bnk%(3, bhy%, bhx%) = 0
      hit_bunker% = 3
      Line 50 + TsX%, Tsy%, 50 + TsX%, TsY% + 4, , 0
      debunk 50 + TsX%, TsY%
     EndIf
    Case 165 To 186
     bhy% = Int((TsY% - 184) / 8)
     bhx% = 1 + Int((TsX% - 165) / 3)
     If Bnk%(4, bhy%, bhx%) = 1 Then
      Bnk%(4, bhy%, bhx%) = 0
      hit_bunker% = 4
      Line 50 + TsX%, Tsy%, 50 + TsX%, TsY% + 4, , 0
      debunk 50 + TsX%, TsY%
     EndIf
   End Select
 End Select
End Function

Sub debunk(x%, y%)
 Local i%
 For i% = 1 To 40 : Pixel x% - 3 + Rnd * 8, y% - 5 + Rnd * 8, 0 : Next
End Sub

Sub draw_bullet()
 If Not ba% Then Exit Sub
 Line 50 + bx%, by%, 50 + bx%, by% + 4, , Rgb(Black)
 Inc by%, -2
 If by% <= 32 Then ba% = 0 : Exit Sub
 Line 50 + bx%, by%, 50 + bx%, by% + 4, , Rgb(White)
 If by% Mod 8 Then Exit Sub :
 If collision%(bx%, by%) Then
  Line 50 + bx%, by%, 50 + bx%, by% + 4, , 0
  ba% = 0
 EndIf
 If hit_bunker%(bx%, by%) Then ba% = 0 : Exit Sub
 If ua% Then
  Select Case by%
   Case 32 To 40
    Select Case bx%
     Case ux% To ux% + 15
      uxpl%(1) = 1 : uxpl%(2) = ux% : uxpl%(3) = 0
      Gui Bitmap 50 + ux%, 32, uf2$, 16, 8, 1, Rgb(Red), 0
      ua% = 0
    End Select
  End Select
 EndIf
End Sub

Function collision%(x%, y%)
 Local ax%, ay%, i%
 Select Case y%
  Case y_pos% + 16 To 214
   For i% = 1 To 55
    If aliens%(i%, 4) Then
     ax% = aliens%(i%, 1) : ay% = aliens%(i%, 2)
     Select Case x%
      Case ax% + 1 To ax% + 13
       Select Case y%
        Case ay% To ay% + 7
         collision% = 1
         explode(ax% + 50, ay%, 1)
         aliens%(i%, 4) = 0
         Inc num_aliens%, -1
         inc_score(40 - (10 * aliens%(i%, 3)))
         Exit Function
       End Select
     End Select
    EndIf
   Next i%
 End Select
End Function

Sub inc_score(delta%, full%)
 Inc score%, delta%
 high_score% = Max(score%, high_score%)
 print_score_at(74, 16, score%)
 If (score% = high_score%) Or full% Then print_score_at(214, 16, high_score%)
 If full% Then Text 58, 0, "SCORE<1>" : Text 198, 0, "HI-SCORE"
End Sub

Sub print_score_at(x%, y%, score%)
 Local s$ = Str$(score%)
 If Len(s$) < 4 Then s$ = String$(4 - Len(s$), "0") + s$
 Text x%, y%, s$
End Sub

Sub explode(x%, y%, snd%)
 Local i%
 Gui Bitmap x%, y%, xpl$, 16, 8, 1, Rgb(Yellow), 0
 draw_ufo()
 If snd% = 1 Then
  For i% = 1 To 75
   Play Tone noise%(i%), noise%(i%), 2
   Pause 1
  Next
  Play Stop
 Else
  Pause 20
 EndIf
 draw_ufo()
 Box x%, y%, 16, 10, , 0, 0
End Sub

Sub move_player()
 Local i%, key%
 read_ctrl(key%)
 Select Case key%
  Case 0
   Exit Sub
  Case ctrl.LEFT
   If plx% > 16 Then Inc plx%, -1
  Case ctrl.RIGHT
   If plx% < 188 Then Inc plx%, 1
  Case ctrl.A
   If ba% Then Exit Sub
   If Not ua% Then Inc myst%, Int(Rnd * 3)
   ba% = 1 : bx% = plx% + 7 : by% = 210
   For i% = 1000 To 1 Step -50 : Play Tone 1000 + i%, 1000 + i%, 5 : Pause 2 : Next
  Case ctrl.SELECT, ctrl.START
   on_quit()
 End Select
End Sub

Sub draw_player()
 Gui Bitmap 50 + plx%, 214, ply$(1), 16, 8, 1, Rgb(Green), 0
End Sub

Sub explode_player()
 Local i%, nse%
 For i% = 1 To 3
  Gui Bitmap 50 + plx%, 214, ply$(2), 16, 8, 1, Rgb(Green), 0
  For nse% = 1 To 100 : Play Tone noise%(nse%), noise%(nse%), 2 : Pause 1 : Next
  Gui Bitmap 50 + plx%, 214, ply$(3), 16, 8, 1, Rgb(Green), 0
  For nse% = 100 To 200 : Play Tone noise%(nse%), noise%(nse%), 2 : Pause 1 : Next
 Next
 For nse% = 1 To 200 : Play Tone noise%(nse%), noise%(nse%), 2 : Pause 1 : Next
 Play Stop
 dummy% = twait%(500)
 clear_bombs()
 Box 50 + plx%, 214, 16, 10, , 0, 0
End Sub

Sub setup_aliens()
 Local at%, num% = 1, r%, n%
 a_ground% = 0
 For r% = 1 To 5
  Select Case r%
   Case 1:    at% = 1
   Case 2, 3: at% = 2
   Case Else: at% = 3
  End Select
  For n% = 1 To 11
   aliens%(num%, 1) = n% * 16
   aliens%(num%, 2) = y_pos% + r% * 16
   aliens%(num%, 3) = at%
   aliens%(num%, 4) = 1
   Inc num%
  Next
 Next
 trn% = 0
End Sub

Sub draw_screen()
 Cls
 Box 50, 229, 220, 1, , , Rgb(Green)
 inc_score(0, 1)
 Text 46, 230, Str$(level%)
 draw_bunkers()
End Sub

Sub draw_aliens()
 Local i%, ax%, ay%, at%
 For i% = 55 To 1 Step -1
  ax% = 50 + aliens%(i%, 1)
  ay% = aliens%(i%, 2)
  at% = aliens%(i%, 3)
  If aliens%(i%, 4) Then
   Box ax%, ay%, 16, 10, , 0, 0
   Gui Bitmap ax%, ay%, alien$(at%, anim%), 16, 8, 1, Rgb(White), 0
  EndIf
 Next
End Sub

Sub move_single()
 Do While Not aliens%(anr%, 4)
  Inc anr%, -1
  If anr% < 1 Then
   anr% = 55
   If trn% = 1 Then adir% = -adir% : down_aliens() : draw_aliens() : trn% = 0
   Inc mvsnd% : mvsnd% = mvsnd% And 3
   If Not ua% Then Play Tone snd%(mvsnd% + 1), snd%(mvsnd% + 1), 80
   anim% = Choice(anim% = 1, 2, 1)
  EndIf
 Loop
 Local ax% = aliens%(anr%, 1), ay% = aliens%(anr%, 2), at% = aliens%(anr%, 3)
 Box ax% + 50, ay%, 16, 10, , 0, 0
 Inc ax%, adir%
 Gui Bitmap ax% + 50, ay%, alien$(at%, anim%), 16, 8, 1, Rgb(White), 0
 aliens%(anr%, 1) = ax%
 If ax% >= X_MAX% Or ax% < 1 Then trn% = 1
 Inc anr%, -1
End Sub

Sub down_aliens()
 Local ax%, ay%, i%
 For i% = 55 To 1 Step -1
  If aliens%(i%, 4) Then
   ax% = aliens%(i%, 1) : ay% = aliens%(i%, 2)
   Box ax% + 50, ay%, 16, 10, , 0, 0
   aliens%(i%, 2) = ay% + 8
   If ay% + 8 >= 202 Then a_ground% = 1
  EndIf
 Next
End Sub

Sub show_game_over()
 wait_idle()
 Box 110, 92, 100, 44, 1, 0, 0
 Text 160, 100, "PLAYER<1>", C
 Do
  Text 160, 116, "GAME OVER", C
  If twait%(600, ctrl.A Or ctrl.START) Then Exit Do
  Text 160, 116, "         ", C
  If twait%(600, ctrl.A Or ctrl.START) Then Exit Do
 Loop
End Sub

sr1:
Data 1, 128, 3, 192, 7, 224, 13, 176, 15, 240, 5, 160, 8, 16, 4, 32
Data 1, 128, 3, 192, 7, 224, 13, 176, 15, 240, 2, 64, 5, 160, 10, 80
sr2:
Data 8, 32, 4, 64, 15, 224, 27, 176, 63, 248, 47, 232, 40, 40, 6, 192
Data 8, 32, 36, 72, 47, 232, 59, 184, 63, 248, 31, 240, 8, 32, 16, 16
sr3:
Data 7, 224, 31, 248, 63, 252, 57, 156, 63, 252, 14, 112, 25, 152, 48, 12
Data 7, 224, 31, 248, 63, 252, 57, 156, 63, 252, 14, 112, 25, 152, 12, 48
plyr:
Data 1, 0, 3, 128, 3, 128, 63, 248, 127, 252, 127, 252, 127, 252, 127, 252
Data 2, 0, 0, 16, 2, 160, 18, 0, 1, 176, 69, 168, 31, 228, 63, 245
Data 16, 4, 130, 25, 16, 192, 2, 2, 75, 97, 33, 196, 31, 224, 55, 228
xpld:
Data 4, 64, 34, 136, 16, 16, 8, 32, 96, 12, 8, 32, 18, 144, 36, 72
ufo:
Data 0, 0, 7, 224, 31, 248, 63, 252, 109, 182, 255, 255, 57, 156, 16, 8
ufo_xpl:
Data 148, 10, 64, 48, 143, 24, 31, 206, 58, 167, 143, 140, 5, 24, 136, 136
                                                                    