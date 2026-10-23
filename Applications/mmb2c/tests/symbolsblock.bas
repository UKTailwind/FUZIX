' A /* block is a command, so a program that opens with one has no OPTION
' SYMBOLS OFF at its top (FUZIX issue #24): it is an error when it runs.
/*
header
*/
Option Symbols Off
Print "ran"
