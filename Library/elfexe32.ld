ENTRY (_start)

PHDRS {
	dynamic PT_DYNAMIC;
	text PT_LOAD;
	data PT_LOAD;
	info PT_LOAD;
	/* Declared so "ld -z stack-size=N" has somewhere to record it.
	   With an explicit PHDRS list ld adds nothing of its own, so
	   without this line the option is silently ignored.  A loader
	   that does not look at it is unaffected: the segment holds no
	   data and its p_memsz is a request, not a mapping. */
	stack PT_GNU_STACK FLAGS(6);
}

SECTIONS {
	.text ALIGN(4) : {
		__text_start = .;
		LONG(__sighandler);
		*(.literal .text)
		*(.literal.* .text.*)
		*(.rodata)
		*(.rodata1)
		*(.rodata.*)
		__text_end = .;
	} : text

	.got ALIGN(4) : { *(.got*) } : text
	.plt ALIGN(4) : { *(.plt*) } : text
	.ARM.exidx ALIGN(4) : { *(.ARM.exidx) } : text

	.data ALIGN(4) : {
		__data_start = .;
		*(.data)
		*(.data1)
		*(.data.*)
		__data_end = .;
	} : data

	.bss ALIGN(4) (NOLOAD) : {
		__bss_start = .;
		*(.dynsbss)
		*(.sbss)
		*(.sbss.*)
		*(.scommon)
		*(.dynbss)
		*(.bss)
		*(.bss.*)
		*(.bss*)
		*(COMMON)
		__bss_end = .;
	} : data

	.dynamic : { *(.dynamic) } :dynamic :info
	.dynsym : { *(.dynsym) } :info
	.rel.dyn : { *(.rel*) } :info
	.hash : { *(.hash*) } :info

	/* The stack request, as a segment size rather than a mapping:
	   link with --defsym __stack_size=N to ask the loader for N
	   bytes of stack.  Last, and NOLOAD, so it shifts nothing and
	   occupies no file space - only its p_memsz is read, by a loader
	   that sizes the fixed stack window at exec.  ld -z stack-size
	   cannot do this job here: an explicit PHDRS list means ld fills
	   in only what the script gives it. */
	PROVIDE(__stack_size = 0);
	.stackreq (NOLOAD) : { . = . + __stack_size; } :stack

	/DISCARD/ : { *(.comment .note) }
}
