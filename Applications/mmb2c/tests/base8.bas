' BASE$(base, n [, digits]) (PicoMite b2ded8c): HEX$ in any base from 2
' to 36, negative only in base 10, zero-padded with the sign first.
' MM.INFO(TELNET) (94c7a0f) is 0: there is no telnet console.
' MM.INFO(BBC BUFFER [ch]) (6f263f7): free note slots, 16 when the
' queues are empty.  All were "not translated".
Print Base$(2, 10); " "; Base$(16, 255); " "; Base$(36, 35); " "; Base$(36, 36)
Print Base$(3, 100, 8); " "; Base$(10, -42); " "; Base$(10, -42, 6); " "; Base$(16, -1)
Print Base$(7, 0); " "; Base$(7, 0, 0); " "; Base$(36, 9223372036854775807)
Print Base$(2, &HFF, 12); " "; Base$(36, -1)
b = 5 : Print Base$(b + 3, 64 * 8 - 1, b)
Print MM.Info(TELNET); MM.Info$(TELNET)
Print MM.Info(BBC BUFFER); MM.Info(BBC BUFFER 0); MM.Info(BBC BUFFER b - 2)
