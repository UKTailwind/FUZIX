' TILEMAP LOAD file$, id, slot, tw, th, tpr (PicoMite dba7552): a map from
' a text file - its width and height, then the tiles, separated by commas,
' spaces, tabs or line ends, with ' or # comments - against the same map
' from DATA.  A bad file is refused and leaves the map it would have
' replaced.  It was "unknown TILEMAP form".  The PicoMite's own
' TilemapLoadTest.bas is pc3host's e2e test.
Option Explicit
Dim Integer n, r, c, a, b
' a 16 by 8 tileset in slot 1: its width and height, then 2 pixels a byte
Open "tiles.bin" For Output As #1
Print #1, Chr$(16); String$(3, 0); Chr$(8); String$(3, 0); String$(64, &H11);
Close #1
FLASH DISK LOAD 1, "tiles.bin", O
Open "tm.map" For Output As #1
Print #1, "' a test map"
Print #1, " 4 , 3   # width and height"
Print #1, "1,2,3,4"
Print #1, "5" + Chr$(9) + "6 7 8 ' row 2"
Print #1, "9,10,11,12";
Close #1
TILEMAP CREATE tm, 1, 1, 8, 8, 2, 4, 3
TILEMAP LOAD "tm", 2, 1, 8, 8, 2
Print TILEMAP(COLS 2); TILEMAP(ROWS 2)
n = 0
For r = 0 To 2
  For c = 0 To 3
    a = TILEMAP(TILE 1, c * 8 + 4, r * 8 + 4)
    b = TILEMAP(TILE 2, c * 8 + 4, r * 8 + 4)
    If a <> b Then n = n + 1
    Print b;
  Next c
Next r
Print
Print n
Bad "tb1.map", "4"
Bad "tb2.map", "0 3 1 2 3"
Bad "tb3.map", "4 3 1 2 3"
Bad "tb4.map", "4 3 1 x 3"
Bad "tb5.map", "4 3 1 70000 3"
Print TILEMAP(COLS 2); TILEMAP(TILE 2, 3 * 8 + 1, 2 * 8 + 1)
TILEMAP CLOSE
FLASH ERASE 1
End

Sub Bad(f$, s$)
  Open f$ For Output As #2
  Print #2, s$;
  Close #2
  On Error Skip 1
  TILEMAP LOAD f$, 2, 1, 8, 8, 2
  Print MM.ErrMsg$
End Sub

tm:
Data 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12
