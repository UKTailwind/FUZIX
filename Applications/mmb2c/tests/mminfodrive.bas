' MM.INFO$(SDCARD) is ready (the card is the root); MM.INFO$(USBDRIVE) is
' "Not Configured", as a PicoMite build without C: says (147a93d);
' MM.INFO(FONT) is the font's number.  A drive's root exists (1a7980f),
' and DIR$ takes a drive letter as every other file name does.  SAVE
' COMPRESSED IMAGE is SAVE IMAGE (2abbb53): here it only has to
' translate - the gate's saveimage is a stub that fails.
Print MM.Info$(SDCARD)
Print MM.Info$(USBDRIVE)
Print MM.Info(FONT)
Font 2
Print MM.Info(FONT)
Print MM.Info(EXISTS DIR "A:/"); MM.Info(EXISTS DIR "B:/"); MM.Info(EXISTS DIR "A:")
Open "zz.txt" For Output As #1
Print #1, "x"
Close #1
Print Dir$("A:/zz*", FILE)
Print Dir$("B:zz*", FILE)
Kill "zz.txt"
On Error Skip 1
Save Compressed Image "shot.bmp"
Print "translated"
