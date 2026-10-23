' LOAD DATA and SAVE DATA (FileIO.c cmd_load and cmd_save): memory out
' to a file and back into another array, through PEEK(VARADDR a()).
' MMbg loads its neural network weights this way.
Dim a%(3), b%(3), f(2), g(2)
For i = 0 To 3: a%(i) = (i + 1) * 1000003: Next
f(0) = 1.5: f(1) = -2.25: f(2) = 3.125
Save Data "ldtest.bin", Peek(VarAddr a%()), 32
Load Data "ldtest.bin", Peek(VarAddr b%())
Print b%(0); b%(1); b%(2); b%(3)
Save Data "ldtest.bin", Peek(VarAddr f()), 24
Load Data "ldtest.bin", Peek(VarAddr g())
Print g(0); g(1); g(2)
Kill "ldtest.bin"
