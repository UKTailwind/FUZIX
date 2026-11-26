' MMBasic V8's raster commands - FRAMEBUFFER WARPH, WARPV, AFFINE and
' SPAN, and BLIT RESIZE - with the VGA and HDMI PicoMite's framebuffer 2
' and top layer T, and MATH SCALE, ADD and CLAMP between integer and
' float arrays, which is how a raster program makes its tables.
'
' Under the gates there is no display, so what this proves is that the
' forms translate, compile and run, and that the arguments are refused
' as MMBasic refuses them, in its order and in its words.  The pixels
' are checked on a PC: pc3host's tests/seam/warp_core.c against a model
' of each command, and the PicoMite's own Bas/bench/warp_test.bas and
' affine_test.bas through its display server.
OPTION DEFAULT INTEGER
MODE 2
FRAMEBUFFER CREATE
FRAMEBUFFER CREATE 2
FRAMEBUFFER LAYER 9
FRAMEBUFFER LAYER TOP 3
DIM o(239), r(239), u(0), v(0), du(0), dv(0), l(9), rt(9), e(9), e9(8)
DIM f!(9), m(2, 2), big(240)
FRAMEBUFFER WARPH F, N, o()
FRAMEBUFFER WARPH F, 2, o(), 1, 0, 5, -1, 0, r()
FRAMEBUFFER WARPV 2, N, e(), 32, 0, 0, RGB(RED)
FRAMEBUFFER AFFINE F, N, u(), v(), du(), dv(), 240
FRAMEBUFFER AFFINE F, L, u(), v(), du(), dv(), 1, -5, 10, 69, -1, 0
FRAMEBUFFER SPAN F, T, l(), rt(), 2, 10, , 0, 1
FRAMEBUFFER SPAN N, N, l(), rt(), , , RGB(BLUE)
BLIT RESIZE F, 2, 0, 0, 320, 240, 0, 0, 160, 120
BLIT RESIZE 2, 2, 0, 0, 160, 120, 0, 120, 320, 120, 0
FRAMEBUFFER WRITE T : CLS : FRAMEBUFFER WRITE 2 : CLS : FRAMEBUFFER WRITE N
FRAMEBUFFER COPY 2, T, B
FRAMEBUFFER MERGE 0, B
FRAMEBUFFER MERGE
PRINT "the raster commands ran"

ON ERROR SKIP : FRAMEBUFFER WARPH F, F, o()
PRINT "WARPH F, F: "; MM.ERRMSG$
ON ERROR SKIP : FRAMEBUFFER WARPH F, N, o(), 0
PRINT "step 0: "; MM.ERRMSG$
ON ERROR SKIP : FRAMEBUFFER WARPH F, N, f!()
PRINT "a float array: "; MM.ERRMSG$
ON ERROR SKIP : FRAMEBUFFER WARPH F, N, m()
PRINT "a 2D array: "; MM.ERRMSG$
ON ERROR SKIP : FRAMEBUFFER WARPV F, N, e(), , , , , , r()
PRINT "WARPV with rows: "; MM.ERRMSG$
ON ERROR SKIP : FRAMEBUFFER WARPH F, N, big()
PRINT "too many bands: "; MM.ERRMSG$
ON ERROR SKIP : FRAMEBUFFER WARPH F, N, e(), , , , , , e9()
PRINT "rows: "; MM.ERRMSG$
ON ERROR SKIP : FRAMEBUFFER AFFINE F, N, l(), e9(), l(), l()
PRINT "AFFINE: "; MM.ERRMSG$
ON ERROR SKIP : FRAMEBUFFER AFFINE F, N, l(), l(), l()
PRINT "AFFINE short: "; MM.ERRMSG$
ON ERROR SKIP : FRAMEBUFFER SPAN F, N, l(), e9()
PRINT "SPAN: "; MM.ERRMSG$
ON ERROR SKIP : FRAMEBUFFER SPAN F, N, l(), rt(), 1, 0, -1, , 2
PRINT "SPAN outside 2: "; MM.ERRMSG$
ON ERROR SKIP : BLIT RESIZE F, N, 0, 0, 321, 10, 0, 0, 10, 10
PRINT "RESIZE off the screen: "; MM.ERRMSG$
ON ERROR SKIP : BLIT RESIZE F, N, 0, 0, 10, 10, 0, 0, 10, 10, 16
PRINT "RESIZE transparent 16: "; MM.ERRMSG$
ON ERROR SKIP : FRAMEBUFFER CREATE 2
PRINT "CREATE 2 again: "; MM.ERRMSG$
ON ERROR SKIP : FRAMEBUFFER LAYER TOP
PRINT "LAYER TOP again: "; MM.ERRMSG$

FRAMEBUFFER CLOSE T
ON ERROR SKIP : FRAMEBUFFER WRITE T
PRINT "T closed: "; MM.ERRMSG$
FRAMEBUFFER CLOSE 2
ON ERROR SKIP : FRAMEBUFFER WARPH 2, N, o()
PRINT "2 closed: "; MM.ERRMSG$
' without a 2, MERGE is the layer over F again, and its colour MMBasic's 0-15
ON ERROR SKIP : FRAMEBUFFER MERGE 16
PRINT "MERGE 16: "; MM.ERRMSG$
FRAMEBUFFER MERGE
PRINT "MERGE of the layer"
' a bare CLOSE closes them all
FRAMEBUFFER CLOSE
ON ERROR SKIP : FRAMEBUFFER WRITE F
PRINT "after CLOSE: "; MM.ERRMSG$
ON ERROR SKIP : FRAMEBUFFER WRITE L
PRINT "after CLOSE: "; MM.ERRMSG$
ON ERROR SKIP : BLIT RESIZE F, N, 0, 0, 10, 10, 0, 0, 10, 10
PRINT "RESIZE from F: "; MM.ERRMSG$

MODE 1
FRAMEBUFFER CREATE
ON ERROR SKIP : FRAMEBUFFER WARPH F, N, o()
PRINT "MODE 1: "; MM.ERRMSG$
ON ERROR SKIP : FRAMEBUFFER LAYER TOP
PRINT "MODE 1 LAYER TOP: "; MM.ERRMSG$
FRAMEBUFFER CLOSE

' MATH between float and integer arrays: into integers each result is
' rounded as FloatToInt64 rounds, half away from zero
DIM a!(3) = (1.4, 2.5, -2.5, 3.6), b(3), c!(3), d(3) = (1, 2, 3, 4)
MATH SCALE a!(), 2, b() : PRINT "SCALE float to integer:"; b(0); b(1); b(2); b(3)
MATH ADD a!(), 0.1, b() : PRINT "ADD float to integer:"; b(0); b(1); b(2); b(3)
MATH SCALE d(), 0.5, c!() : PRINT "SCALE integer to float:"; c!(0); c!(1); c!(2); c!(3)
ARRAY ADD d(), 0.5, c!() : PRINT "ADD integer to float:"; c!(0); c!(1); c!(2); c!(3)
MATH CLAMP a!(), 0, 3, b() : PRINT "CLAMP float to integer:"; b(0); b(1); b(2); b(3)
MATH CLAMP d(), 1.6, 3.4, b() : PRINT "CLAMP integer:"; b(0); b(1); b(2); b(3)
MATH CLAMP d(), 1.5, 2.5, c!() : PRINT "CLAMP integer to float:"; c!(0); c!(1); c!(2); c!(3)
MATH CLAMP a!(), -1, 1, c!() : PRINT "CLAMP float:"; c!(0); c!(1); c!(2); c!(3)
ON ERROR SKIP : MATH CLAMP a!(), 3, 1, c!()
PRINT "CLAMP 3 to 1: "; MM.ERRMSG$
ON ERROR SKIP : MATH CLAMP a!(), 0, 1, e()
PRINT "CLAMP sizes: "; MM.ERRMSG$
PRINT "done"
