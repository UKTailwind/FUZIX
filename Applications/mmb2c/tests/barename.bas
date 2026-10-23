' A name alone is no assignment and no SUB: "Unknown command" when the
' line runs - but "Variable name too long" for 32 characters or more,
' checked first.  Run on a PicoMitePC3 (RC5 code).
On Error Ignore
foo
Print "a "; MM.ErrMsg$
On Error Clear
abcdefghijklmnopqrstuvwxyz012345
Print "b "; MM.ErrMsg$
On Error Clear
abcdefghijklmnopqrstuvwxyz01234
Print "c "; MM.ErrMsg$
