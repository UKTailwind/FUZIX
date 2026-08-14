

Option explicit
Option default none
'Option console serial
MODE 2
FRAMEBUFFER create
FRAMEBUFFER write f
CLS

'brownian motion demo using sprites with static object collisions
Dim integer x(64),y(64),c(64)
Dim float direction(64)
Dim integer i,j,k, collision=0
Dim string q$

' Create the atom sprites
For i=1 To 64
  direction(i)=Rnd*360 'establish the starting direction for each atom
  c(i)=RGB(Rnd*255,Rnd*255,Rnd*255) 'give each atom a colour
  Circle 10,10,4,1,,RGB(white),c(i) 'draw the atom
  Sprite read i,6,6,9,9 'read it in as a sprite
Next i
CLS

' Load background image
'Load jpg "b:/img320"
CLS RGB(myrtle)

' Draw screen border
Box 0,0,MM.HRES,MM.VRES

' Draw red obstacle boxes and define them as static objects
' Box 1 - top left area
Box 60,40,50,50,3,RGB(red),RGB(red)
Sprite static 1, 60, 40, 50, 50

' Box 2 - top right area
Box 210,40,50,50,3,RGB(red),RGB(red)
Sprite static 2, 210, 40, 50, 50

' Box 3 - center
Box 135,95,50,50,3,RGB(red),RGB(red)
Sprite static 3, 135, 95, 50, 50

' Box 4 - bottom left area
Box 60,150,50,50,3,RGB(red),RGB(red)
Sprite static 4, 60, 150, 50, 50

' Box 5 - bottom right area
Box 210,150,50,50,3,RGB(red),RGB(red)
Sprite static 5, 210, 150, 50, 50

' Place the atoms on screen.
' This is done in three passes.  Positions must be settled for ALL atoms
' before any of them is shown, because SPRITE SHOW saves whatever is under
' the sprite as that sprite's background.  Show one atom on top of another
' and the lower atom becomes part of the upper one's saved background, so
' it gets stamped back onto the screen every time the upper atom moves -
' that is what leaves a permanent trail.

' Pass 1 - claim the grid positions that are clear of the boxes.
' The grid step guarantees grid positions can never overlap each other.
' Positions that hit a box are deferred, marked with x() = -1.
k=1
For i=MM.HRES\9 To MM.HRES\9*8 Step MM.HRES\9
  For j=MM.VRES\9 To MM.VRES\9*8 Step MM.VRES\9
    If Not inside_box(i, j, 9) Then
      x(k)=i
      y(k)=j
    Else
      x(k)=-1
      y(k)=-1
    EndIf
    k=k+1
  Next j
Next i

' Pass 2 - find a home for each deferred atom, avoiding the boxes AND
' every atom already placed.  Deferring the whole pass until the grid is
' known is what stops a random atom landing on a grid cell that has not
' been filled in yet.
For k=1 To 64
  If x(k) = -1 Then
    Do
      x(k) = Rnd*(MM.HRES-9)
      y(k) = Rnd*(MM.VRES-9)
    Loop Until inside_box(x(k), y(k), 9) = 0 And hits_atom(k, x(k), y(k), 9) = 0
  EndIf
Next k

' Pass 3 - nothing overlaps now, so a plain SPRITE SHOW is safe
For k=1 To 64
  Sprite show k,x(k),y(k),1
  vector k,direction(k), 0, x(k), y(k) 'load up the vector move
Next k

' Main animation loop
Do
  For i=1 To 64
    vector i, direction(i), 1, x(i), y(i)
    Sprite show i,x(i),y(i),1
    ' Check for sprite collisions OR background object collisions
    If sprite(S,i)<>-1 Then
      break_collision i
    EndIf
  Next i

  FRAMEBUFFER copy f,n
'  Print @(0,0)Timer;
'  Timer = 0
Loop

' Check if a position is inside any of the static boxes
Function inside_box(px As integer, py As integer, size As integer) As integer
  Local integer b
  For b = 1 To 5
    If sprite(ST, b, A) Then  ' If static object is active
      If px + size > sprite(ST, b, X) And px < sprite(ST, b, X) + sprite(ST, b, W) Then
        If py + size > sprite(ST, b, Y) And py < sprite(ST, b, Y) + sprite(ST, b, H) Then
          inside_box = 1
          Exit Function
        EndIf
      EndIf
    EndIf
  Next b
  inside_box = 0
End Function

' Check whether a 'size' square at px,py would overlap any atom that has
' already been given a position.  Atoms still awaiting one are marked -1
' and are skipped.
Function hits_atom(me As integer, px As integer, py As integer, size As integer) As integer
  Local integer a
  For a = 1 To 64
    If a <> me Then
      If x(a) >= 0 Then
        If px + size > x(a) And px < x(a) + size Then
          If py + size > y(a) And py < y(a) + size Then
            hits_atom = 1
            Exit Function
          EndIf
        EndIf
      EndIf
    EndIf
  Next a
  hits_atom = 0
End Function

' Vector movement subroutine
Sub vector(myobj As integer, angle As float, distance As float, x_new As integer, y_new As integer)
  Static float y_move(64), x_move(64)
  Static float x_last(64), y_last(64)
  Static float last_angle(64)

  If distance=0 Then
    x_last(myobj)=x_new
    y_last(myobj)=y_new
  EndIf
  If angle<>last_angle(myobj) Then
    y_move(myobj)=-Cos(Rad(angle))
    x_move(myobj)=Sin(Rad(angle))
    last_angle(myobj)=angle
  EndIf
  x_last(myobj) = x_last(myobj) + distance * x_move(myobj)
  y_last(myobj) = y_last(myobj) + distance * y_move(myobj)
  x_new=Cint(x_last(myobj))
  y_new=Cint(y_last(myobj))
End Sub

' Handle collisions - break them by bouncing
Sub break_collision(atom As integer)
  Local integer j=1, col, bg_hit, hit
  Local integer bx, by, bw, bh, ax, ay
  Local float current_angle=direction(atom)
  Local float dx, dy

  ' Check what type of collision occurred
  If sprite(e,atom)=1 Then
    ' Collision with left of screen
    current_angle=360-current_angle
  ElseIf sprite(e,atom)=2 Then
    ' Collision with top of screen
    current_angle=((540-current_angle) Mod 360)
  ElseIf sprite(e,atom)=4 Then
    ' Collision with right of screen
    current_angle=360-current_angle
  ElseIf sprite(e,atom)=8 Then
    ' Collision with bottom of screen
    current_angle=((540-current_angle) Mod 360)
  Else
    ' Check for static object collision
    bg_hit = 0
    For col = 1 To sprite(C, atom)
      hit = sprite(C, atom, col)
      If hit >= &H80 And hit < &HF0 Then
        ' Static object collision (codes 0x80-0xBF)
        bg_hit = hit And &H3F  ' Extract object number
        Exit For
      EndIf
    Next col

    If bg_hit > 0 Then
      ' Bounce off static object - determine which side was hit
      bx = sprite(ST, bg_hit, X)
      by = sprite(ST, bg_hit, Y)
      bw = sprite(ST, bg_hit, W)
      bh = sprite(ST, bg_hit, H)
      ax = x(atom) + sprite(W, atom)\2
      ay = y(atom) + sprite(H, atom)\2

      ' Determine if collision is more horizontal or vertical
      dx = Abs(ax - (bx + bw\2))
      dy = Abs(ay - (by + bh\2))

      If dx / bw > dy / bh Then
        ' Horizontal bounce (hit left or right side)
        current_angle = 360 - current_angle
      Else
        ' Vertical bounce (hit top or bottom)
        current_angle = ((540 - current_angle) Mod 360)
      EndIf
    Else
      ' Collision with another sprite or corner
      current_angle = current_angle + 180
    EndIf
  EndIf

  direction(atom) = current_angle
  vector atom, direction(atom), j, x(atom), y(atom) 'break the collision
  Sprite show atom, x(atom), y(atom), 1

  ' If the simple bounce didn't work, try a random bounce.
  ' The <>0 matters: AND is bitwise, so without it an even collision value
  ' (e.g. edges=2, top) ANDed with the 1 from j<10 gives 0 and this whole
  ' recovery loop is silently skipped.
  Do While ((sprite(t,atom) Or sprite(e,atom)) <> 0) And (j < 10)
    Do
      direction(atom) = Rnd*360
      vector atom, direction(atom), j, x(atom), y(atom)
      j = j + 1
    Loop Until x(atom)>=0 And x(atom)<=MM.HRES-sprite(w,atom) And y(atom)>=0 And y(atom)<=MM.VRES-sprite(h,atom)
    Sprite show atom, x(atom), y(atom), 1
  Loop

  ' If that didn't work then place the atom randomly (avoiding boxes)
  Do While (sprite(t,atom) Or sprite(e,atom))
    direction(atom) = Rnd*360
    Do
      x(atom) = Rnd*(MM.HRES-sprite(w,atom))
      y(atom) = Rnd*(MM.VRES-sprite(h,atom))
    Loop Until inside_box(x(atom), y(atom), sprite(w,atom)) = 0 And hits_atom(atom, x(atom), y(atom), sprite(w,atom)) = 0
    vector atom, direction(atom), 0, x(atom), y(atom)
    Sprite show atom, x(atom), y(atom), 1
  Loop
End Sub                       