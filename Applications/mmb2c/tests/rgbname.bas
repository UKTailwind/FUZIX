' RGB's one argument is a colour name as checkstring matches one: the
' word, then anything that cannot go on with a name (FUZIX issue #7).
' No argument is Invalid syntax.  Run on a PicoMitePC3 (RC5 code).
Print Hex$(RGB(RED + 0)); " "; Hex$(RGB(Red%)); " "; Hex$(RGB(blue(1)))
On Error Skip 1
x = RGB(REDDISH)
Print MM.ErrMsg$
On Error Skip 1
x = RGB()
Print MM.ErrMsg$
On Error Skip 1
x = RGB(RED.X)
Print MM.ErrMsg$
