' A runtime function that raises under ON ERROR SKIP hands back a sink,
' and the statement carries on with it.  Under bcrun on a 64-bit host the
' sink was one of bcrun's own statics, outside the program's memory, so
' the program got a truncated pointer and this was a segfault - before
' "start" was even flushed.  The sinks now come from the program's pools.
Print "start"
On Error Skip 1
Print Format$(1, "no spec")
Print "format: " MM.ErrMsg$
On Error Skip 1
Print Tab(0)
Print "tab: " MM.ErrMsg$
