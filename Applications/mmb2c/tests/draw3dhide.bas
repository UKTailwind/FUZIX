' DRAW3D HIDE a, b with a already hidden: the reference (caee048, #132)
' skips a and goes on to hide b.  Before, HIDE returned from the whole
' list at a, so b stayed on the screen with its box.
OPTION BASE 0
MODE 1
DIM FLOAT v(2, 7)
DIM INTEGER fc(5) = (4, 4, 4, 4, 4, 4)
DIM INTEGER f(23) = (0,3,2,1, 4,5,6,7, 1,2,6,5, 0,4,7,3, 3,7,6,2, 0,1,5,4)
DIM INTEGER col(0) = (RGB(WHITE))
DIM INTEGER ec(5) = (0, 0, 0, 0, 0, 0)
DIM INTEGER i
FOR i = 0 TO 7
  READ v(0, i), v(1, i), v(2, i)
NEXT i
DATA -50,-50,-50, 50,-50,-50, 50,50,-50, -50,50,-50
DATA -50,-50,50, 50,-50,50, 50,50,50, -50,50,50
DRAW3D CAMERA 1, 400
DRAW3D CREATE 1, 8, 6, 1, v(), fc(), f(), col(), ec(), ec()
DRAW3D CREATE 2, 8, 6, 1, v(), fc(), f(), col(), ec(), ec()
DRAW3D CREATE 3, 8, 6, 1, v(), fc(), f(), col(), ec(), ec()
DRAW3D SHOW 1, -200, 0, 1000
DRAW3D SHOW 2, 0, 0, 1000
DRAW3D SHOW 3, 200, 0, 1000
PRINT "shown"; DRAW3D(XMIN 1); DRAW3D(XMIN 2); DRAW3D(XMIN 3)
DRAW3D HIDE 1
PRINT "hide 1"; DRAW3D(XMIN 1); DRAW3D(XMIN 2); DRAW3D(XMIN 3)
' 1 is hidden already: 2 and 3 are hidden all the same
DRAW3D HIDE 1, 2, 3
PRINT "hide 1,2,3"; DRAW3D(XMIN 1); DRAW3D(XMIN 2); DRAW3D(XMIN 3)
DRAW3D RESTORE 2
' and a hidden one in the middle of the list
DRAW3D SHOW 1, -200, 0, 1000
DRAW3D HIDE 1, 3, 2
PRINT "hide 1,3,2"; DRAW3D(XMIN 1); DRAW3D(XMIN 2); DRAW3D(XMIN 3)
DRAW3D CLOSE ALL
