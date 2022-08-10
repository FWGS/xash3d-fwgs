	.set noreorder

#include "pspstub.s"

	STUB_START "KernelAccess",0x40090000,0x00020005
	STUB_FUNC  0x8A5C745F,kaGeEdramSetSize
	STUB_FUNC  0x71570ECF,kaGeEdramGetHwSize
	STUB_END
