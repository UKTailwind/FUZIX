'poly in poly
' As supplied, plus the closing "Next n" (the paste ended inside the
' outer loop) and Timer, so the 9.99ms MMBasic figure has something to
' compare against.
Timer =0
CLS
' CONST, not a plain variable: mmb2c fixes array sizes when it
' translates, so a DIM bound has to fold at compile time.  5 is the
' size the 9.99 ms MMBasic figure was taken at.
Const pn=5'poly number >=3

Dim px(pn),py(pn),dx(pn),dy(pn)

For i=0 To pn-1
an=360/pn*i*Pi/180
an=2/pn*i*Pi

px(i)=160+160*Cos(an)
py(i)=160+160*Sin(an)
Next i

l=9'ratio
For n=0 To 10'iterations
For i=0 To pn-1
j=(i+1) Mod pn
Line px(i),py(i),px(j),py(j)
dx(i)=(px(i)+l*px(j))/(1+l)
dy(i)=(py(i)+l*py(j))/(1+l)
Next i

For i=0 To pn-1
px(i)=dx(i)
py(i)=dy(i)
Next i
Next n
Print Timer
