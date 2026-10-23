' LOAD masks every byte to seven bits and keeps the printable ones (FUZIX
' issue #27): a Latin-1 e-acute is an i, 0x8A ends a line where it stands,
' and 0x89, a TAB once masked, is a space - in a string too.
Print "café"
Print "a"ŠPrint "b"
Print "c‰d"
