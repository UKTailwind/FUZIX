' MATH(BASE64 ENCODE/DECODE in$, out$) - returns the length, writes
' the result into the second argument.  Vectors are RFC 4648's own.
Option Explicit
Dim a$, out$, n%

n% = Math(BASE64 ENCODE "f", out$)
Print n%; ":"; out$
n% = Math(BASE64 ENCODE "fo", out$)
Print n%; ":"; out$
n% = Math(BASE64 ENCODE "foo", out$)
Print n%; ":"; out$
n% = Math(BASE64 ENCODE "foob", out$)
Print n%; ":"; out$
n% = Math(BASE64 ENCODE "fooba", out$)
Print n%; ":"; out$
n% = Math(BASE64 ENCODE "foobar", out$)
Print n%; ":"; out$
n% = Math(BASE64 ENCODE "", out$)
Print n%; ":["; out$; "]"

' retic's shape: lower case keywords, the enclosing Function's result
' variable as the output argument
Print b64$("fooba")

' in place - input and output the same variable
a$ = "foob"
n% = Math(BASE64 ENCODE a$, a$)
Print n%; ":"; a$

n% = Math(BASE64 DECODE "Zm9vYmFy", out$)
Print n%; ":"; out$
n% = Math(BASE64 DECODE "Zm8=", out$)
Print n%; ":"; out$
n% = Math(BASE64 DECODE "Zg==", out$)
Print n%; ":"; out$
' a trailing partial group is silently ignored, as the reference does
n% = Math(BASE64 DECODE "Zm9vY", out$)
Print n%; ":"; out$
End

Function b64$(si As String)
  Local integer junk = Math(base64 encode si, b64$)
End Function
