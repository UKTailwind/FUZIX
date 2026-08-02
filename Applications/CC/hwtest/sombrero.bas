Timer =0
RX=MM.HRES: RY=MM.VRES
P=RX/2: Q=216
XP=P*0.9: XR=1.5*Pi
YP=90: YR=1: ZP=90
XF=XR/XP: YF=YP/YR: ZF=XR/XP

For ZI= -Q To Q-1
 If ZI<-ZP Or ZI>ZP Then Continue For
 ZT=ZI*XP/ZP: ZZ=ZI
 XL=Int(0.5+Sqr(XP*XP-ZT*ZT))

 For XI= -XL To XL
   XT=Sqr(XI*XI+ZT*ZT)*XF: XX=XI
   YY=(Sin(XT)+0.4*Sin(3*XT))*YF
   SubPlotXY
 Next XI
Next ZI
Print Timer

Sub SubPlotXY
 X1= XX+ZZ+P
 Y1= RY-(YY-ZZ+Q)
 Pixel X1,Y1
 If Y1=0 Then End Sub
 Line X1,Y1+1,X1,RY, 1,0
End Sub
