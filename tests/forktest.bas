' Isolating what corrupts the filesystem.
'
' Two corruptions today, both on the next cc after a BASIC program that
' had used SAVE IMAGE and SYSTEM.  The second happened on a freshly
' imaged card with nothing bigger than 230K written, so file size is
' not it.  The error the second time was "swap error", and a fork here
' swaps bcrun out - around 200K of it.
'
' The suspicion is dirty filesystem blocks still in the buffer cache
' when the swap happens.  This runs the fork on its own, twenty times,
' writing nothing at all: if it corrupts, the fork is enough by itself
' and no file writing is needed to trigger it.
'
' Run it in MODE 1 and MODE 2 - the two that failed were MODE 1, the
' ones that passed were MODE 2, which may or may not mean anything.
For i = 1 To 20
  SYSTEM "true"
Next i
Print "twenty forks done"
