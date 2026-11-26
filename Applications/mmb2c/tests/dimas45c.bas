' FUZIX #45 under OPTION DEFAULT FLOAT: AS was a FLOAT, n an implied one,
' and the program printed 3.5 where MMBasic prints 4
Option Default Float
Dim As Integer n
n = 7 / 2
Print n
