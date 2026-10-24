' MM.INFO(FREE SPACE) beside DISK SIZE.  Both are statvfs on the real
' runtimes and 0 in the fcc gate's, so the pins are the shape: free
' space is never more than the drive, and a drive with a size has
' some free - MMbg read 0 there, took the drive for full and turned
' its log off.
Dim Integer f, d
f = Mm.Info(FREE SPACE)
d = Mm.Info(DISK SIZE)
If f >= 0 And f <= d Then Print "free within size"
If d = 0 Or f > 0 Then Print "free answered"
Print "Mm.Info$(free space)"; Mm.Info$(free space) >= 0
