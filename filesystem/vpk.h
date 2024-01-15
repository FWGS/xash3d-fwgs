// Reads .VPK files
#pragma once
#ifndef H_VPK
#define H_VPK
#include "const.h"
#include "filesystem_internal.h"

typedef struct VPKHeader2_s
{
	unsigned int sig;
	unsigned int ver;
	unsigned int tree_size;
	unsigned int file_data_section_size;
	unsigned int archive_md5_section_size;
	unsigned int other_md5_section_size;
	unsigned int signature_section_size;
} VPKHeader2_t;

typedef struct VPKEntry_s
{
	unsigned int crc;
	word bytes;
	word archive_index;
	unsigned int entry_offset;
	unsigned int entry_length;

	word terminator;
} VPKEntry_t;

searchpath_t* FS_AddVPK_Fullpath(const char* vpkfile, int flags);

#endif
