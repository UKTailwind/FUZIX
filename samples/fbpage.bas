OPTION EXPLICIT
OPTION DEFAULT FLOAT
DIM r,t,u,v,x,n, TAU
DIM INTEGER i, j, hw, hh, col
MODE 2

n = RND()*100+300
TAU = 6.283185307179586
r = TAU / (RND()*100+235)
hw = MM.HRES / 2
hh = MM.VRES / 2
FRAMEBUFFER CREATE
FRAMEBUFFER WRITE F
DO
  CLS
  FOR i = 0 TO n
    FOR j = 0 TO n
      u = SIN(i + v) + SIN(r * i + x)
      v = COS(i + v) + COS(r * i + x)
      x = u + t
      Col = RGB(i AND &hFF, j AND &hFF, 99)
      PIXEL hw + u * hw * 0.4, hh + v * hh * 0.4, Col
    NEXT
  NEXT
  t = t + 0.01
  FRAMEBUFFER COPY F, N
LOOP UNTIL INKEY$ <>""
