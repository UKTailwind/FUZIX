' A TAB is one space to LOAD, so a line of a TAB and 247 more
' characters - 248 in all - loads and runs (FUZIX issue #15).  248 is
' what the board takes for this line: LOAD's real limit is the
' tokenised form, which this count cannot model exactly.
	x = 1 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
Print x
