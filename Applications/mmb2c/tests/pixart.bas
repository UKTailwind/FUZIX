' A MODE / COLOUR / PIXEL test.  MODE 2 is 320x240 in 16 colours; the
' wandering ribbon and the two mirrored sweeps below are pixel-bound,
' which is the end of the range ripple.bas does not reach.
MODE 2
Timer =0
Dim GCOL(3)=(RGB(YELLOW),RGB(RED),RGB(BLACK),RGB(WHITE))
S=Sin(.03):C=Cos(.03):X=260:Y=20:U=0:V=1
For A=80 To 1200
X=X+U:Y=Y+V:K=Sgn(Cos(A/45)*Cos(A*.0138))
T=U:U=U*C-K*V*S:V=V*C+K*T*S:Z=24-A Mod 10
For B=-Z To Z
R=1-80/A
CI=1+(Int(3*Abs(Cos(B*.3+(A\4 Mod 2)*Pi/2+.4)))=A Mod 4)-(Abs(B)>16)
Colour GCOL(CI)
Pixel 1*Int(X+R*B*V),250-1*(Y-R*B*U)
Next :Next
K=190
For J=0 To K
R=(200-80*Cos(J*3*Pi/K))*(.8+.5*Cos(J*13*Pi/K)*Cos(J*13*Pi/K))
For I=0 To R Step 5
CI=1.2-(I/R)-(J\20=4)*(2*(I\15=3)+1)-(J\12=14)*(I\30=3)
Colour GCOL(Abs(CI))
S=Sin(J*Pi/K):C=Cos(J*Pi/K):V=8*Sin(I/16)
Pixel 160+(I*S+V*C)/4,70-(I*C+V*S)/4
Pixel 160-(I*S+V*C)/4,70-(I*C+V*S)/4
Next : Next
Print Timer
pause 5000
mode 1
end
