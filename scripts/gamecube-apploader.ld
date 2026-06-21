ENTRY(apploader_entry)

SECTIONS
{
	. = 0x81200000;
	.text :
	{
		*(.text.start)
		*(.text*)
	}
	.rodata : { *(.rodata*) }
	.data : { *(.data*) }
	.bss : { *(.bss*) *(COMMON) }
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
