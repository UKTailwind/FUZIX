' thread8.bas - THREAD as translated (mmb_thread.c, PLAN-threads.md):
' THREAD START's options, MM.INFO(THREAD ...), PAUSE and RESUME, STOP in
' a FUNCTION with LOCAL strings, EXIT from inside a call, the refusals
' and their messages.  The firmware's own tests are the thr*.bas.
Option Explicit
Dim Integer tests, pass, c(16), arr(4), got
Dim s$

Sub Check(n$, g, want)
  tests = tests + 1
  If g = want Then
    pass = pass + 1
    Print "ok  "; n$
  Else
    Print "FAIL "; n$; " got"; g; " want"; want
  EndIf
End Sub

Sub Spin
  Local Integer me = MM.Info(Thread)
  Do : Inc c(me) : Loop
End Sub

Function Deep$(n%)
  Local t$ = "level" + Str$(n%), k$(3) Length 20
  If n% > 0 Then
    Deep$ = Deep$(n% - 1)
  Else
    Do : Inc c(MM.Info(Thread)) : Loop
  EndIf
End Function

Sub InDeep
  s$ = Deep$(5)
End Sub

Sub Leaver(n%)
  Local x$ = "going"
  If n% = 0 Then Thread Exit
  Leaver n% - 1
  c(9) = 99                              ' never reached
End Sub

Sub Waiter
  Pause 100000
End Sub

Sub Filler(a%(), v%, t$)
  Local Integer j
  For j = 0 To 4 : a%(j) = v% * j : Next
  s$ = t$
End Sub

Function NotSub(x)
  NotSub = x
End Function

Sub Mine
  Local Integer m(3)
  On Error Skip
  Thread Start 6, Filler, m(), 1, "x"
  s$ = MM.ErrMsg$
End Sub

Check "the main program is thread 0", MM.Info(Thread), 0
Check "no thread is 0", MM.Info(Thread 3), 0
Check "the main program is 1", MM.Info(Thread 0), 1

Thread Start 16 Size 20 Locals 30 Nest 10 Priority 2, Spin
Thread Start 2 Priority 1, InDeep
Thread Start 3, Waiter
Pause 20
Check "every option taken: it runs", c(16) > 0, 1
Check "a thread in a FUNCTION ran", c(2) > 0, 1
Check "a running thread is 1", MM.Info(Thread 16), 1
Check "a thread in PAUSE is 2", MM.Info(Thread 3), 2
Thread Pause 16
got = c(16)
Pause 20
Check "paused, it does not run", c(16) - got, 0
Check "and is 3", MM.Info(Thread 16), 3
Thread Resume 16
Pause 20
Check "resumed, it runs", c(16) > got, 1
Thread Stop 2
got = c(2)
Pause 20
Check "stopped inside a FUNCTION with LOCAL strings", c(2) - got + MM.Info(Thread 2), 0
Thread Stop 16 : Thread Stop 3
Check "stopped in PAUSE", MM.Info(Thread 3), 0

Thread Start 4, Leaver, 3
Thread Wait 4
Check "THREAD EXIT from three calls down", c(9) + MM.Info(Thread 4), 0

Thread Start 5, Filler, arr(), 10, "from a thread"
Thread Wait 5
Check "a global array by name, and a string by value", arr(4) = 40 And s$ = "from a thread", 1

Mine
Check "a SUB's own array is refused", s$ = "Only a global array can be passed to a thread", 1
On Error Skip
Thread Start 7, NotSub
Check "a FUNCTION is not a SUB", MM.ErrMsg$ = "NOTSUB is not a SUB", 1
On Error Skip
Thread Start 17, Spin
Check "17 is refused", MM.ErrMsg$ = "17 is invalid (valid is 1 to 16)", 1
On Error Skip
Thread Start 1 Size 4, Spin
Check "SIZE 4 is refused", MM.ErrMsg$ = "4 is invalid (valid is 6 to 512)", 1
On Error Skip
Thread Resume 8
Check "RESUME of a thread not running", MM.ErrMsg$ = "Thread 8 is not running", 1
On Error Skip
Thread Exit
Check "EXIT in the main program", MM.ErrMsg$ = "Not in a thread", 1
On Error Skip
Thread Unlock 3
Check "UNLOCK of a lock not held", MM.ErrMsg$ = "Lock 3 is not held by this thread", 1
On Error Skip
Thread Release
Check "RELEASE with no HOLD", MM.ErrMsg$ = "The threads are not held", 1
Thread Lock 3
On Error Skip
Thread Lock 3
Check "LOCK of a lock this thread has", MM.ErrMsg$ = "Lock 3 is already held by this thread", 1
Check "MM.INFO(THREAD LOCK) of it", MM.Info(Thread Lock 3), 0
Thread Unlock 3
Check "and free", MM.Info(Thread Lock 3), -1
Thread Size , 48
Thread Size 30
Thread Slice 2
Thread Start 1, Waiter
On Error Skip
Erase arr()
Check "ERASE while a thread runs", MM.ErrMsg$ = "Not allowed while a thread is running", 1
Thread Stop 1
Thread List
Print "thread8:"; pass; " of"; tests; " passed"
