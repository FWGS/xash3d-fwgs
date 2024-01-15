#include "vpk.h"
#include "crtlib.h"
#include "common/com_strings.h"
#include "map/map.h"

typedef struct vpk_filepos_s
{
	unsigned int file_pos;
	word file_num;
	unsigned int file_len;
} vpk_filepos_t;

typedef map_t(vpk_filepos_t) map_vpk_filepos_t;

struct vpk_s
{
	map_vpk_filepos_t file_positions;
	word vpk_num;
	file_t** handles; // 0 is always *_dir.vpk
	poolhandle_t mempool;
};

int ReadString(file_t* file, char* out, int maxlen)
{
	out[0] = '\x00';
	int len = 0;
	for(;len<maxlen;++len)
	{
		FS_Read(file, &out[len], 1);
		if (!out[len])
		{
			return len;
		}
	}
}

static vpk_t* VPK_Open(const char* filename)
{
	vpk_t* vpk = (vpk_t*)Mem_Calloc(fs_mempool, sizeof(vpk_t));
	string buf;
	string vpk_path;
	string vpk_filename;
	string vpk_extension;
	string vpk_prefix;
	VPKHeader2_t vpk_header;
	VPKEntry_t vpk_entry;
	vpk_filepos_t vpk_tempfilepos;
	int len;
	char found = 0;
	int amount = 0;

	len = strlen(filename);
	if (len < 4)
	{
		Con_Reportf(S_ERROR "VPK_Open: what the hell is wrong with you???? %s has less than 4 chars\n", filename);
		return 0;
	}
	Q_strncpy(vpk_prefix, filename, sizeof(vpk_prefix));
	vpk_prefix[len - 4] = '\x00';

	Q_snprintf(buf, sizeof(buf), "%s_dir.vpk", vpk_prefix);
	if (!FS_FileExists(buf, false))
	{
		Con_Reportf(S_ERROR "VPK_Open: %s does not exist.\n", buf);
		return 0;
	}
	do
	{
		Q_snprintf(buf, sizeof(buf), "%s_%03d.vpk", vpk_prefix,amount);
		if (!FS_FileExists(buf, false))
		{
			found = false;
		}

		++amount;
	}
	while (found);


	vpk->handles = (file_t**)Mem_Calloc(fs_mempool, sizeof(file_t*)*amount);

	Q_snprintf(buf, sizeof(buf), "%s_dir.vpk", vpk_prefix);
	vpk->handles[0] = FS_Open(buf, "rb", false);

	for (int i = 1; i < amount+1; ++i)
	{
		Q_snprintf(buf, sizeof(buf), "%s_%03d.vpk", vpk_prefix,i-1);
		vpk->handles[i] = FS_Open(buf, "rb", false);
	}
	FS_Read(vpk->handles[0], &vpk_header, sizeof(vpk_header));
	map_init(&vpk->file_positions);
	while (true)
	{
		ReadString(vpk->handles[0],vpk_extension,sizeof(vpk_extension));
		if (!vpk_extension[0])
		{
			break;
		}
		while (true)
		{
			ReadString(vpk->handles[0], vpk_path, sizeof(vpk_path));
			if (!vpk_path[0])
			{
				break;
			}
			while (true)
			{
				ReadString(vpk->handles[0], vpk_filename, sizeof(vpk_filename));
				if (!vpk_filename[0])
				{
					break;
				}
				Q_snprintf(buf, sizeof(buf), "%s/%s.%s",vpk_path,vpk_filename,vpk_extension);

				FS_Read(vpk->handles[0], &vpk_entry, 18);
				vpk_tempfilepos.file_pos = vpk_entry.entry_offset;
				vpk_tempfilepos.file_num = vpk_entry.archive_index+1;
				vpk_tempfilepos.file_len = vpk_entry.entry_length;
				map_set(&vpk->file_positions, buf, vpk_tempfilepos);
				Con_Printf("vpk file: %s @ %i\n", buf, vpk_tempfilepos.file_num);
			}
		}
	}
	return vpk;
}


/*
===========
VPK_FileTime

===========
*/
static int VPK_FileTime(searchpath_t* search, const char* filename)
{
	return 0;
}

/*
===========
VPK_PrintInfo

===========
*/
static void VPK_PrintInfo(searchpath_t* search, char* dst, size_t size)
{
	return;
}


/*
===========
FS_Search_WAD

===========
*/
static void VPK_Search(searchpath_t* search, stringlist_t* list, const char* pattern, int caseinsensitive)
{
	return;
}


static byte* VPK_LoadFile(searchpath_t* search, const char* path, int pack_ind, fs_offset_t* lumpsizeptr)
{
	byte* buf;
	vpk_t* vpk = search->vpk;
	vpk_filepos_t* filepos = map_get(&vpk->file_positions, path);
	if (!filepos)
	{
		return NULL;
	}
	buf = (byte*)Mem_Malloc(vpk->mempool,filepos->file_len);
	FS_Seek(vpk->handles[filepos->file_num], filepos->file_pos, SEEK_SET);
	FS_Read(vpk->handles[filepos->file_num], buf, filepos->file_len);
	*lumpsizeptr = filepos->file_len;
	return buf;
}

static file_t* VPK_OpenFile(searchpath_t* search, const char* filename, const char* mode, int pack_ind)
{
	return NULL;
}

static int VPK_FindFile(searchpath_t* search, const char* path, char* fixedname, size_t len)
{
	vpk_t* vpk = search->vpk;
	vpk_filepos_t* filepos = map_get(&vpk->file_positions, path);
	if (!filepos)
	{
		return -1;
	}
	strncpy(fixedname, path, len);
	return 0;
}

static void VPK_Close(searchpath_t* search)
{

}

/*
====================
FS_AddVPK_Fullpath
====================
*/
searchpath_t* FS_AddVPK_Fullpath(const char* vpkfile, int flags)
{
	searchpath_t* search;
	vpk_t* vpk;
	vpk = VPK_Open(vpkfile);

	if (!vpk)
	{
		Con_Reportf(S_ERROR "FS_AddVPK_Fullpath: unable to load vpk \"%s\"\n", vpkfile);
		return NULL;
	}

	vpk->mempool = Mem_AllocPool(vpkfile);
	search = (searchpath_t*)Mem_Calloc(fs_mempool, sizeof(searchpath_t));
	Q_strncpy(search->filename, vpkfile, sizeof(search->filename));
	search->vpk = vpk;
	search->type = SEARCHPATH_VPK;
	search->flags = flags;

	search->pfnPrintInfo = VPK_PrintInfo;
	search->pfnClose = VPK_Close;
	search->pfnOpenFile = VPK_OpenFile;
	search->pfnFileTime = VPK_FileTime;
	search->pfnFindFile = VPK_FindFile;
	search->pfnSearch = VPK_Search;
	search->pfnLoadFile = VPK_LoadFile;
	
	Con_Reportf("Adding vpk: %s\n", vpkfile);
	return search;
}
