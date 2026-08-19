' FRAMEBUFFER LAYER and MERGE.
'
' A layer is just another off-screen framebuffer.  What makes it a
' layer is MERGE, which composites it OVER F on the way to the screen,
' skipping whichever colour index is nominated transparent.  Neither
' source is changed by it, so the background can stay put while the
' thing on top moves.
'
' This is MMBasic's TFT model.  Its VGA and HDMI builds do the same
' compositing inside the scanline builder instead, continuously; that
' one needs the layer in SRAM where core1 can DMA it, which is 40K off
' every program on this machine forever.  PC3-LAYER-MERGE.md argues it
' out.  A program written for a PicoMite driving an ILI9341 runs here
' unchanged.
'
' Under the gates there is no display, so what this proves is that the
' forms translate, compile and run.  The picture is checked on the
' board by samples/layerdemo.bas.
MODE 1
FRAMEBUFFER CREATE
FRAMEBUFFER LAYER
PRINT "both buffers created"

' draw the background into F
FRAMEBUFFER WRITE F
CLS RGB(BLUE)
BOX 10, 10, 100, 60, 1, RGB(WHITE), RGB(RED)

' and something else into the layer
FRAMEBUFFER WRITE L
CLS 0                                   ' 0 is the transparent index
CIRCLE 60, 40, 20, 1, 1, RGB(YELLOW), RGB(YELLOW)

' put them together: the layer over F, onto the screen
FRAMEBUFFER WRITE N
FRAMEBUFFER MERGE 0
PRINT "merged with transparent 0"

' the sources survive a merge, so it can be repeated
FRAMEBUFFER MERGE
PRINT "merged again, default colour"

' COPY takes the layer at either end now
FRAMEBUFFER COPY L, F
FRAMEBUFFER COPY F, L
FRAMEBUFFER COPY L, N
FRAMEBUFFER COPY N, L
PRINT "copy works both ways with L"

' and the layer closes on its own
FRAMEBUFFER CLOSE L
PRINT "layer closed"

' merging without one is refused rather than doing half of it
ON ERROR SKIP 2
FRAMEBUFFER MERGE 0
PRINT "merge without a layer: ";MM.ERRMSG$

' as is drawing into it
ON ERROR SKIP 2
FRAMEBUFFER WRITE L
PRINT "write to a closed layer: ";MM.ERRMSG$

FRAMEBUFFER CLOSE F
PRINT "done"
