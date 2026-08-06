MODE 2
FRAMEBUFFER create
FRAMEBUFFER write F
t=0
orbit=90
sr=30

Do
 CLS
 sun 0
 moon Pi
 Inc t,.01
 FRAMEBUFFER copy F,N,B
Loop While Inkey$=""

Sub sun(angle)
 x=160+Cos(angle+t)*orbit
 y=120+Sin(angle+t)*orbit
 Circle x,y,sr,,,0,RGB(Yellow)
 For i=0 To Pi*2 Step Pi/8
   Circle x+Cos(i+t)*sr,y+Sin(i+t)*sr,5,,,0,0
 Next
End Sub

Sub moon(angle)
 x=160+Cos(angle+t)*orbit
 y=120+Sin(angle+t)*orbit
 Circle x,y,sr,,,0,RGB(Blue)
 Circle x+10,y-10,sr,,,0,0
End Sub
