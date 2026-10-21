' FORMAT$ builds its result with room for the worst case (PicoMite
' issue #71, 02120e4): up to 254 characters of literal text around a
' field up to 250 wide.  It used to overrun its buffer.  A result over
' 255 is refused, not cut short.

Print Format$(3.14159, "pi=%6.3f!")
Print Len(Format$(1, String$(200, "x") + "%50f"))
Print Format$(1, String$(200, "x") + "%250f")
Print "NOT REACHED"
