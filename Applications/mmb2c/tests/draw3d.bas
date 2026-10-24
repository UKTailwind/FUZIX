' DRAW3D - Draw3D.c's engine, in mmb_draw3d.h.  A 100-unit cube 1000
' in front of camera 1, whose viewplane is at 400: only the face toward
' the camera shows, and it projects to x 299-341, y 218-260 on 640x480 -
' worked by hand from display3d.  A quarter turn about Z, or a half turn
' about Y, leaves the same box, give or take the truncation of a
' coordinate that comes back from the unit vector as 49.999...
OPTION BASE 0
MODE 1
DIM FLOAT v(2, 7), q(4)
DIM INTEGER fc(5) = (4, 4, 4, 4, 4, 4)
DIM INTEGER f(23) = (0,3,2,1, 4,5,6,7, 1,2,6,5, 0,4,7,3, 3,7,6,2, 0,1,5,4)
DIM INTEGER col(1) = (RGB(WHITE), RGB(BLUE))
DIM INTEGER ec(5) = (0, 0, 0, 0, 0, 0), fl(5) = (1, 1, 1, 1, 1, 1)
DIM INTEGER i
' the corners: x, y, z of each, -50 or 50
FOR i = 0 TO 7
  READ v(0, i), v(1, i), v(2, i)
NEXT i
DATA -50,-50,-50, 50,-50,-50, 50,50,-50, -50,50,-50
DATA -50,-50,50, 50,-50,50, 50,50,50, -50,50,50
DRAW3D CAMERA 1, 400
DRAW3D CREATE 1, 8, 6, 1, v(), fc(), f(), col(), ec(), fl()
DRAW3D SHOW 1, 0, 0, 1000
PRINT "box"; DRAW3D(XMIN 1); DRAW3D(XMAX 1); DRAW3D(YMIN 1); DRAW3D(YMAX 1)
PRINT "at"; DRAW3D(X 1); DRAW3D(Y 1); DRAW3D(Z 1)
PRINT "distance"; DRAW3D(DISTANCE 1)
' a quarter turn about Z
q(0) = COS(RAD(45)) : q(1) = 0 : q(2) = 0 : q(3) = SIN(RAD(45)) : q(4) = 1
DRAW3D ROTATE q(), 1
DRAW3D SHOW 1, 0, 0, 1000
PRINT "z90"; DRAW3D(XMIN 1); DRAW3D(XMAX 1); DRAW3D(YMIN 1); DRAW3D(YMAX 1)
' a half turn about Y: the back face comes round to the front
q(0) = 0 : q(1) = 0 : q(2) = 1 : q(3) = 0
DRAW3D ROTATE q(), 1
DRAW3D SHOW 1, 0, 0, 1000
PRINT "y180"; DRAW3D(XMIN 1); DRAW3D(XMAX 1); DRAW3D(YMIN 1); DRAW3D(YMAX 1)
' HIDE forgets the box; WRITE draws without keeping one; RESTORE redraws
DRAW3D HIDE 1
PRINT "hidden"; DRAW3D(XMIN 1)
DRAW3D WRITE 1, 0, 0, 1000
PRINT "written"; DRAW3D(XMIN 1)
DRAW3D RESTORE 1
PRINT "restored"; DRAW3D(XMIN 1)
' a wireframe copy, the edge colours left to the current colour
DRAW3D CREATE 2, 8, 6, 1, v(), fc(), f(), col(), , fl()
DRAW3D CLOSE 1, 2
' the errors Elite's object probe and its clean-up meet
ON ERROR SKIP 1
DRAW3D CREATE 33, 8, 6, 1, v(), fc(), f(), col(), ec()
PRINT "create 33: "; MM.ERRMSG$
ON ERROR SKIP 1
DRAW3D SHOW 1, 0, 0, 1000
PRINT "show 1: "; MM.ERRMSG$
' CLOSE ALL forgets the cameras too
DRAW3D CLOSE ALL
DRAW3D CREATE 3, 8, 6, 1, v(), fc(), f(), col(), ec()
ON ERROR SKIP 1
DRAW3D SHOW 3, 0, 0, 1000
PRINT "no camera: "; MM.ERRMSG$
