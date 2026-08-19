' Bubble Universe

MODE 2
'MODE 3
Font 8
FRAMEBUFFER create
FRAMEBUFFER write f

Dim Float u(99),w(99),p,q,t,v=0,x=0,b
Dim Integer n(99),m(9,99),xc,yc,xs,ys
Dim Integer a,g,i,j,fr
Const r=(2*Pi)/235,k=255,s=50
CLS RGB(black)
t=Rnd*10
't=1

'calculate centre and scale factor
xc=MM.HRES\2:yc=MM.VRES\2
xs=MM.HRES/4.2:ys=MM.VRES/4.2  'Oval
'xs=MM.HRes/6:ys=MM.VRes/4      'Circular

For a=0 To 99 Step 10
For g=0 To 99
 If a<25 And g<38 Then
  If a<15 Then:m(a\10,g)=RGB(0,255,0):Else:m(a\10,g)=RGB(255,255,0):EndIf
 Else
  m(a\10,g)=RGB(a*2.575,g*2.575,128*(a+g<65))
 EndIf
Next 'g
' Memory pack nn, Peek(varaddr m(0,a)),100,32 'pack pixel colours
Next 'a

Do
CLS
Inc t,0.015:g=0:Print @(10,10)Timer:Timer=0
For i=57To 255Step 2
b=r*i+t
For j=0To 99
 u(j)=Sin(i+v)+Sin(x)
 v=Cos(i+v)+Cos(x)
 x=u(j)+b
 w(j)=v
 If Not(g Mod 10) Then n(j)=m(g\10,j)'copy from colour map
Next 'j
Math Scale u(),xs,u():Math Scale w(),ys,w()
Math add u(),xc,u():Math Add w(),yc,w()
'Memory unpack Peek(varaddr m(0,g)),nn,100,32 'unpack pixel colours
Pixel u(),w(),n()
Inc g
Next 'i
FRAMEBUFFER copy f,n',b
' 272 frames at the measured 73.5ms each is twenty seconds.  A counted
' loop rather than waiting for a key: the running time is then the
' program's own and does not depend on a keystroke arriving.  A key
' still stops it early.
Inc fr
Loop Until fr>=272 Or Inkey$ <> ""
'
Mode 1
End
