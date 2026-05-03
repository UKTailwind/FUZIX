
	.export	sp
	.export hireg
	.export tmp
	.export tmp1
	.export tmp2
	.export tmp3
	.export tmp4
	.export tmp5

	.zp

sp:	.word	0		; must be first
hireg:	.word	0		; Save these 14 bytes on an irq switch
tmp:	.word	0
tmp1:	.word	0
tmp2:	.word	0
tmp3:	.word	0
tmp4:	.word	0
tmp5:	.word	0

