/*
iso9660_gamecube.c - patched copy of libogc 3.1.0 libiso9660 (devoptab for the DVD)

This is an altered version of libogc's libiso9660/iso9660.c, vendored so the
engine links it instead of -liso9660. Changes from upstream are marked
"xash3d-gc": dirname() no longer underflows on paths without a separator,
entry_from_path() checks its strdup results, and stat/open of a file scans
the parent directory in place instead of leaking a heap copy of its listing.

libogc copyright (C) 2004 - 2025 Michael Wiedenbauer (shagkur), Dave Murphy (WinterMute)

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any
damages arising from the use of this software.

Permission is granted to anyone to use this software for any
purpose, including commercial applications, and to alter it and
redistribute it freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you
   must not claim that you wrote the original software. If you use
   this software in a product, an acknowledgment in the product
   documentation would be appreciated but is not required.
2. Altered source versions must be plainly marked as such, and
   must not be misrepresented as being the original software.
3. This notice may not be removed or altered from any source
   distribution.
*/

#include "build.h"

#if XASH_GAMECUBE && ( !defined( XASH_GAMECUBE_LIBOGC2 ) || !XASH_GAMECUBE_LIBOGC2 )

/* Upstream code, built with the engine's stricter warnings. */
#pragma GCC diagnostic warning "-Wstrict-aliasing"
#pragma GCC diagnostic warning "-Wpacked"
#pragma GCC diagnostic warning "-Wpacked-not-aligned"
#pragma GCC diagnostic warning "-Wtautological-compare"
#pragma GCC diagnostic warning "-Wformat"
#pragma GCC diagnostic warning "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-function"

/****************************************************************************
 * ISO9660 devoptab
 * 
 * Copyright (C) 2008-2010
 * tipoloski, clava, shagkur, Tantric, joedj
 ****************************************************************************/

#include <errno.h>
#include <ogc/lwp_watchdog.h>
#include <ogcsys.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dir.h>
#include <sys/iosupport.h>

#include <iso9660.h>

/* xash3d-gc: keep the local helpers clear of any libc basename/dirname. */
#define basename iso9660_basename
#define dirname iso9660_dirname

#define OFFSET_EXTENDED 1
#define OFFSET_SECTOR 6
#define OFFSET_SIZE 14
#define OFFSET_FLAGS 25
#define OFFSET_NAMELEN 32
#define OFFSET_NAME 33

#define SECTOR_SIZE			0x800
#define BUFFER_SIZE			0x8000

#define DIR_SEPARATOR		'/'

#define FLAG_DIR 2

struct pvd_s
{
	char id[8];
	char system_id[32];
	char volume_id[32];
	char zero[8];
	unsigned long total_sector_le, total_sect_be;
	char zero2[32];
	unsigned long volume_set_size, volume_seq_nr;
	unsigned short sector_size_le, sector_size_be;
	unsigned long path_table_len_le, path_table_len_be;
	unsigned long path_table_le, path_table_2nd_le;
	unsigned long path_table_be, path_table_2nd_be;
	u8 root[34];
	char volume_set_id[128], publisher_id[128], data_preparer_id[128], application_id[128];
	char copyright_file_id[37], abstract_file_id[37], bibliographical_file_id[37];
}__attribute__((packed));

typedef struct
{
	u8 name_length;
	u8 extended_sectors;
	u32 sector;
	u16 parent;
	char name[ISO_MAXPATHLEN];
}__attribute__((packed)) PATHTABLE_ENTRY;

typedef struct pentry_s
{
	u16 index;
	u32 childCount;
	PATHTABLE_ENTRY table_entry;
	struct pentry_s *children;
} PATH_ENTRY;

typedef struct dentry_s
{
	char name[ISO_MAXPATHLEN];
	u32 sector;
	u32 size;
	u8 flags;
	u32 fileCount;
	PATH_ENTRY *path_entry;
	struct dentry_s *children;
} DIR_ENTRY;

typedef struct iso9660mount_s
{
	const DISC_INTERFACE *disc_interface;
	u8 read_buffer[BUFFER_SIZE] __attribute__((aligned(32)));
	u8 cluster_buffer[BUFFER_SIZE] __attribute__((aligned(32)));
	u32 cache_start;
	u32 cache_sectors;
	bool iso_unicode;
	PATH_ENTRY *iso_rootentry;
	PATH_ENTRY *iso_currententry;
	char volume_id[32];
} MOUNT_DESCR;

typedef struct filestruct_s
{
	DIR_ENTRY entry;
	off_t offset;
	bool inUse;
	MOUNT_DESCR *mdescr;
} FILE_STRUCT;

typedef struct dstate_s
{
	DIR_ENTRY entry;
	u32 index;
	bool inUse;
} DIR_STATE_STRUCT;

static MOUNT_DESCR* _ISO9660_getMountDescrFromPath(const char *path, devoptab_t **pdevops);

static __inline__ bool is_dir(DIR_ENTRY *entry)
{
	return entry->flags & FLAG_DIR;
}

static int __read(MOUNT_DESCR *mdescr, void *ptr, u64 offset, size_t len)
{
	u32 sector = offset / SECTOR_SIZE;
	u32 end_sector = (offset + len - 1) / SECTOR_SIZE;
	u32 sectors = MIN(BUFFER_SIZE / SECTOR_SIZE, end_sector - sector + 1);
	u32 sector_offset = offset % SECTOR_SIZE;
	const DISC_INTERFACE *disc = mdescr->disc_interface;

	len = MIN(BUFFER_SIZE - sector_offset, len);
	if (mdescr->cache_sectors && sector >= mdescr->cache_start && (sector + sectors) <= (mdescr->cache_start + mdescr->cache_sectors))
	{
		memcpy(ptr, mdescr->read_buffer + (sector - mdescr->cache_start) * SECTOR_SIZE + sector_offset, len);
		return len;
	}

	if (!disc->readSectors(sector, BUFFER_SIZE / SECTOR_SIZE, mdescr->read_buffer))
	{
		mdescr->cache_sectors = 0;
		return -1;
	}

	mdescr->cache_start = sector;
	mdescr->cache_sectors = BUFFER_SIZE / SECTOR_SIZE;
	memcpy(ptr, mdescr->read_buffer + sector_offset, len);

	return len;
}

static int _read(MOUNT_DESCR *mdescr, void *ptr, u64 offset, size_t len)
{
	int ret, read = 0;
	char *cptr = ptr;
	while (read < len)
	{
		ret = __read(mdescr, cptr + read, offset + read, len - read);
		if (ret > 0)
			read += ret;
		else if (ret == 0)
			break;
		else
			return -1;
	}
	return read;
}

static void stat_entry(DIR_ENTRY *entry, struct stat *st)
{
	st->st_dev = 69;
	st->st_ino = (ino_t) entry->sector;
	st->st_mode = (is_dir(entry) ? S_IFDIR : S_IFREG) | (S_IRUSR | S_IRGRP | S_IROTH);
	st->st_nlink = 1;
	st->st_uid = 1;
	st->st_gid = 2;
	st->st_rdev = st->st_dev;
	st->st_size = entry->size;
	st->st_atime = 0;
	st->st_mtime = 0;
	st->st_ctime = 0;
	st->st_blksize = SECTOR_SIZE;
	st->st_blocks = (entry->size + SECTOR_SIZE - 1) / SECTOR_SIZE;
	st->st_spare4[0] = 0;
	st->st_spare4[1] = 0;
}

static char* basename(char *path)
{
	s32 i;

	for (i = strlen(path) - 1; i >= 0; i--)
	{
		if (path[i] == DIR_SEPARATOR)
			return path + i + 1;
	}
	return path;
}

static char* dirname(char *path)
{
	/* xash3d-gc: the upstream loop used an unsigned index, so a path with
	 * no separator (".", "config.cfg") or an empty one walked backwards
	 * through the heap and wrote a NUL over the first '/' byte it found. */
	char *result = strdup(path);
	char *sep;

	if (!result)
		return NULL;

	sep = strrchr(result, DIR_SEPARATOR);
	if (sep)
		*sep = '\0';
	else
		result[0] = '\0';
	return result;
}

static s32 read_direntry(MOUNT_DESCR *mdescr, DIR_ENTRY *entry, u8 *buf)
{
	u8 extended_sectors = buf[OFFSET_EXTENDED];
	u32 sector = *(u32 *) (buf + OFFSET_SECTOR) + extended_sectors;
	u32 size = *(u32 *) (buf + OFFSET_SIZE);
	u8 flags = buf[OFFSET_FLAGS];
	u8 namelen = buf[OFFSET_NAMELEN];

	if (namelen == 1 && buf[OFFSET_NAME] == 1 && mdescr->iso_rootentry->table_entry.sector == entry->sector)
	{
		// .. at root - do not show
	}
	else if (namelen == 1 && !buf[OFFSET_NAME])
	{
		entry->sector = sector;
		entry->size = size;
		entry->flags = flags;
	}
	else
	{
		DIR_ENTRY *newChildren = realloc(entry->children, sizeof(DIR_ENTRY) * (entry->fileCount + 1));
		if (!newChildren)
			return -1;
		memset(newChildren + entry->fileCount, 0, sizeof(DIR_ENTRY));
		entry->children = newChildren;
		DIR_ENTRY *child = &entry->children[entry->fileCount++];
		child->sector = sector;
		child->size = size;
		child->flags = flags;
		char *name = child->name;

		if (namelen == 1 && buf[OFFSET_NAME] == 1)
		{
			// ..
			sprintf(name, "..");
		}
		else if (mdescr->iso_unicode)
		{
			u32 i;
			for (i = 0; i < (namelen / 2); i++)
				name[i] = buf[OFFSET_NAME + i * 2 + 1];
			name[i] = '\x00';
			namelen = i;
		}
		else
		{
			memcpy(name, buf + OFFSET_NAME, namelen);
			name[namelen] = '\x00';
		}
		if (!(flags & FLAG_DIR) && namelen >= 2 && name[namelen - 2] == ';')
			name[namelen - 2] = '\x00';
	}

	return *buf;
}

static bool read_directory(MOUNT_DESCR *mdescr, DIR_ENTRY *dir_entry, PATH_ENTRY *path_entry)
{
	u32 sector = path_entry->table_entry.sector;
	u32 remaining = 0;
	u32 sector_offset = 0;

	do
	{
		if (__read(mdescr, mdescr->cluster_buffer, (u64) sector * SECTOR_SIZE + sector_offset, (SECTOR_SIZE - sector_offset)) != (SECTOR_SIZE - sector_offset))
			return false;
		int offset = read_direntry(mdescr, dir_entry, mdescr->cluster_buffer);
		if (offset == -1)
			return false;
		if (!remaining)
		{
			remaining = dir_entry->size;
			dir_entry->path_entry = path_entry;
		}
		sector_offset += offset;
		if (sector_offset >= SECTOR_SIZE || !mdescr->cluster_buffer[offset])
		{
			remaining -= SECTOR_SIZE;
			sector_offset = 0;
			sector++;
		}
	} while (remaining > 0);

	return true;
}

static bool path_entry_from_path(MOUNT_DESCR *mdescr, PATH_ENTRY *path_entry, const char *path)
{
	bool found = false;
	bool notFound = false;

	const char *pathPosition = path;
	const char *pathEnd = strchr(path, '\0');

	PATH_ENTRY *entry = mdescr->iso_rootentry;
	while (pathPosition[0] == DIR_SEPARATOR)
		pathPosition++;
	if (pathPosition >= pathEnd)
		found = true;

	PATH_ENTRY *dir = entry;
	while (!found && !notFound)
	{
		const char *nextPathPosition = strchr(pathPosition, DIR_SEPARATOR);
		size_t dirnameLength;
		if (nextPathPosition != NULL)
			dirnameLength = nextPathPosition - pathPosition;
		else
			dirnameLength = strlen(pathPosition);
		if (dirnameLength >= ISO_MAXPATHLEN)
			return false;

		u32 childIndex = 0;
		while (childIndex < dir->childCount && !found && !notFound)
		{
			entry = &dir->children[childIndex];
			if (dirnameLength == strnlen(entry->table_entry.name, ISO_MAXPATHLEN - 1) && !strncasecmp(pathPosition, entry->table_entry.name, dirnameLength))
				found = true;
			if (!found)
				childIndex++;
		}

		if (childIndex >= dir->childCount)
		{
			notFound = true;
			found = false;
		}
		else if (!nextPathPosition || nextPathPosition >= pathEnd)
		{
			found = true;
		}
		else
		{
			dir = entry;
			pathPosition = nextPathPosition;
			while (pathPosition[0] == DIR_SEPARATOR)
				pathPosition++;
			if (pathPosition >= pathEnd)
				found = true;
			else
				found = false;
		}
	}

	if (found)
		memcpy(path_entry, entry, sizeof(PATH_ENTRY));
	return found;
}

/* xash3d-gc: walk one directory's records like read_directory(), but compare
 * each name against base instead of collecting children on the heap. */
static bool find_file_in_directory(MOUNT_DESCR *mdescr, DIR_ENTRY *entry, PATH_ENTRY *parent, const char *base, u32 nl)
{
	u32 sector = parent->table_entry.sector;
	u32 remaining = 0;
	u32 sector_offset = 0;
	bool first = true;

	do
	{
		u8 *buf = mdescr->cluster_buffer;
		u8 reclen;

		if (__read(mdescr, buf, (u64) sector * SECTOR_SIZE + sector_offset, (SECTOR_SIZE - sector_offset)) != (SECTOR_SIZE - sector_offset))
			return false;
		reclen = buf[0];
		if (reclen)
		{
			u32 esector = *(u32 *) (buf + OFFSET_SECTOR) + buf[OFFSET_EXTENDED];
			u32 esize = *(u32 *) (buf + OFFSET_SIZE);
			u8 flags = buf[OFFSET_FLAGS];
			u8 namelen = buf[OFFSET_NAMELEN];

			if (namelen == 1 && !buf[OFFSET_NAME])
			{
				// . - the directory itself, carries its size
				if (first)
					remaining = esize;
			}
			else if (!(namelen == 1 && buf[OFFSET_NAME] == 1))
			{
				char name[ISO_MAXPATHLEN];
				u32 i;

				if (mdescr->iso_unicode)
				{
					for (i = 0; i < (namelen / 2) && i < ISO_MAXPATHLEN - 1; i++)
						name[i] = buf[OFFSET_NAME + i * 2 + 1];
				}
				else
				{
					for (i = 0; i < namelen && i < ISO_MAXPATHLEN - 1; i++)
						name[i] = buf[OFFSET_NAME + i];
				}
				name[i] = '\x00';
				if (!(flags & FLAG_DIR) && i >= 2 && name[i - 2] == ';')
					name[i - 2] = '\x00';

				if (nl == strnlen(name, ISO_MAXPATHLEN - 1) && !strncasecmp(base, name, nl))
				{
					memset(entry, 0, sizeof(DIR_ENTRY));
					strcpy(entry->name, name);
					entry->sector = esector;
					entry->size = esize;
					entry->flags = flags;
					return true;
				}
			}
		}
		first = false;
		sector_offset += reclen;
		if (sector_offset >= SECTOR_SIZE || !buf[reclen])
		{
			remaining = remaining > SECTOR_SIZE ? remaining - SECTOR_SIZE : 0;
			sector_offset = 0;
			sector++;
		}
	} while (remaining > 0);

	return false;
}

static bool find_in_directory(MOUNT_DESCR *mdescr, DIR_ENTRY *entry, PATH_ENTRY *parent, const char *base)
{
	u32 childIdx;
	u32 nl = strlen(base);

	if (!nl)
		return read_directory(mdescr, entry, parent);

	for (childIdx = 0; childIdx < parent->childCount; childIdx++)
	{
		PATH_ENTRY *child = parent->children + childIdx;
		if (nl == strnlen(child->table_entry.name, ISO_MAXPATHLEN - 1) && !strncasecmp(base, child->table_entry.name, nl))
		{
			return read_directory(mdescr, entry, child);
		}
	}

	/* xash3d-gc: upstream read the whole parent listing into a heap array
	 * (one DIR_ENTRY per file, grown by realloc) and then memcpy'd the match
	 * over entry, leaking that array on every successful stat/open. maps/ on
	 * a retail disc is ~20 KiB of listing; at New Game the heap has less than
	 * that left, so the realloc failed and stat reported ENOENT for a file
	 * that was found moments earlier. Scan the directory records in place and
	 * keep only the match: no heap use, nothing to leak. */
	return find_file_in_directory(mdescr, entry, parent, base, nl);
}

static bool entry_from_path(MOUNT_DESCR *mdescr, DIR_ENTRY *entry, const char *const_path)
{
	u32 len;
	bool found = false;
	char *path, *dir, *base;
	PATH_ENTRY parent_entry;

	memset(entry, 0, sizeof(DIR_ENTRY));

	if (strchr(const_path, ':') != NULL)
		const_path = strchr(const_path, ':') + 1;

	path = strdup(const_path);
	if (!path)
		return false;
	len = strlen(path);
	while (len > 1 && path[len - 1] == DIR_SEPARATOR)
		path[--len] = '\x00';

	dir = dirname(path);
	if (!dir)
	{
		free(path);
		return false;
	}
	base = basename(path);
	if (!path_entry_from_path(mdescr, &parent_entry, dir))
		goto done;

	found = find_in_directory(mdescr, entry, &parent_entry, base);
	if (!found && entry->children)
		free(entry->children);

done:
	free(path);
	free(dir);
	return found;
}

static bool check_dev_name(const char* name, char *devname, size_t devname_size)
{
    size_t len;
    
	if (!name)
		return false;

	len = strlen(name);
	if (len == 0 || len > devname_size-2)
		return false;

	// append ':' if missing
	strcpy(devname, name);
	if (devname[len-1] != ':')
		strcat(devname, ":");
		
	return true;
}

static int _ISO9660_open_r(struct _reent *r, void *fileStruct, const char *path, int flags, int mode)
{
	DIR_ENTRY entry;
	FILE_STRUCT *file = (FILE_STRUCT *) fileStruct;
	MOUNT_DESCR *mdescr;

	mdescr = _ISO9660_getMountDescrFromPath(path, NULL);
	if (mdescr == NULL)
	{
		r->_errno = ENODEV;
		return -1;
	}

	if (!entry_from_path(mdescr, &entry, path))
	{
		r->_errno = ENOENT;
		return -1;
	}
	else if (is_dir(&entry))
	{
		if (entry.children)
			free(entry.children);
		r->_errno = EISDIR;
		return -1;
	}

	memcpy(&file->entry, &entry, sizeof(DIR_ENTRY));
	file->offset = 0;
	file->inUse = true;
	file->mdescr = mdescr;

	return (int) file;
}

static int _ISO9660_close_r(struct _reent *r, void *fd)
{
	FILE_STRUCT *file = (FILE_STRUCT*) fd;

	if (!file->inUse)
	{
		r->_errno = EBADF;
		return -1;
	}

	file->inUse = false;
	return 0;
}

static ssize_t _ISO9660_read_r(struct _reent *r, void *fd, char *ptr, size_t len)
{
	u64 offset;
	int ret;
	FILE_STRUCT *file = (FILE_STRUCT*) fd;

	if (!file->inUse)
	{
		r->_errno = EBADF;
		return -1;
	}

	if (file->offset >= file->entry.size)
	{
		r->_errno = EOVERFLOW;
		return 0;
	}

	if (len + file->offset > file->entry.size)
	{
		r->_errno = EOVERFLOW;
		len = file->entry.size - file->offset;
	}

	if (len == 0)
		return 0;

	offset = (u64) file->entry.sector * SECTOR_SIZE + file->offset;
	if ((ret = _read(file->mdescr, ptr, offset, len)) < 0)
	{
		r->_errno = EIO;
		return -1;
	}

	len = (size_t)ret;
	file->offset += len;
	return len;
}

static off_t _ISO9660_seek_r(struct _reent *r, void *fd, off_t pos, int dir)
{
	off_t position;
	FILE_STRUCT *file = (FILE_STRUCT*) fd;

	if (!file->inUse)
	{
		r->_errno = EBADF;
		return -1;
	}

	switch (dir)
	{
		case SEEK_SET:
			position = pos;
			break;
		case SEEK_CUR:
			position = file->offset + pos;
			break;
		case SEEK_END:
			position = file->entry.size + pos;
			break;
		default:
			r->_errno = EINVAL;
			return -1;
	}

	if (pos > 0 && position < 0)
	{
		r->_errno = EOVERFLOW;
		return -1;
	}

	if (position < 0 || position > file->entry.size)
	{
		r->_errno = EINVAL;
		return -1;
	}

	file->offset = position;
	return position;
}

static int _ISO9660_fstat_r(struct _reent *r, void *fd, struct stat *st)
{
	FILE_STRUCT *file = (FILE_STRUCT*) fd;

	if (!file->inUse)
	{
		r->_errno = EBADF;
		return -1;
	}

	stat_entry(&file->entry, st);
	return 0;
}

static int _ISO9660_stat_r(struct _reent *r, const char *path, struct stat *st)
{
	DIR_ENTRY entry;
	MOUNT_DESCR *mdescr;

	mdescr = _ISO9660_getMountDescrFromPath(path, NULL);
	if (mdescr == NULL)
	{
		r->_errno = ENODEV;
		return -1;
	}

	if (!entry_from_path(mdescr, &entry, path))
	{
		r->_errno = ENOENT;
		return -1;
	}

	stat_entry(&entry, st);
	if (entry.children)
		free(entry.children);

	return 0;
}

static int _ISO9660_chdir_r(struct _reent *r, const char *path)
{
	DIR_ENTRY entry;
	MOUNT_DESCR *mdescr;

	mdescr = _ISO9660_getMountDescrFromPath(path, NULL);
	if (mdescr == NULL)
	{
		r->_errno = ENODEV;
		return -1;
	}

	if (!entry_from_path(mdescr, &entry, path))
	{
		r->_errno = ENOENT;
		return -1;
	}
	else if (!is_dir(&entry))
	{
		r->_errno = ENOTDIR;
		return -1;
	}

	mdescr->iso_currententry = entry.path_entry;
	if (entry.children)
		free(entry.children);

	return 0;
}

static DIR_ITER* _ISO9660_diropen_r(struct _reent *r, DIR_ITER *dirState, const char *path)
{
	DIR_STATE_STRUCT *state = (DIR_STATE_STRUCT*) (dirState->dirStruct);
	MOUNT_DESCR *mdescr;

	mdescr = _ISO9660_getMountDescrFromPath(path, NULL);
	if (mdescr == NULL)
	{
		r->_errno = ENODEV;
		return NULL;
	}

	if (!entry_from_path(mdescr, &state->entry, path))
	{
		r->_errno = ENOENT;
		return NULL;
	}
	else if (!is_dir(&state->entry))
	{
		r->_errno = ENOTDIR;
		return NULL;
	}

	state->index = 0;
	state->inUse = true;
	return dirState;
}

static int _ISO9660_dirreset_r(struct _reent *r, DIR_ITER *dirState)
{
	DIR_STATE_STRUCT *state = (DIR_STATE_STRUCT*) (dirState->dirStruct);

	if (!state->inUse)
	{
		r->_errno = EBADF;
		return -1;
	}

	state->index = 0;
	return 0;
}

static int _ISO9660_dirnext_r(struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *st)
{
	DIR_ENTRY *entry;
	DIR_STATE_STRUCT *state = (DIR_STATE_STRUCT*) (dirState->dirStruct);

	if (!state->inUse)
	{
		r->_errno = EBADF;
		return -1;
	}

	if (state->index >= state->entry.fileCount)
	{
		r->_errno = ENOENT;
		return -1;
	}

	entry = &state->entry.children[state->index++];
	strncpy(filename, entry->name, ISO_MAXPATHLEN);
	stat_entry(entry, st);
	return 0;
}

static int _ISO9660_dirclose_r(struct _reent *r, DIR_ITER *dirState)
{
	DIR_STATE_STRUCT *state = (DIR_STATE_STRUCT*) (dirState->dirStruct);

	if (!state->inUse)
	{
		r->_errno = EBADF;
		return -1;
	}

	state->inUse = false;
	if (state->entry.children)
		free(state->entry.children);
	return 0;
}

static int _ISO9660_statvfs_r(struct _reent *r, const char *path, struct statvfs *buf)
{
	// FAT clusters = POSIX blocks
	buf->f_bsize = 0x800; // File system block size. 
	buf->f_frsize = 0x800; // Fundamental file system block size. 

	//buf->f_blocks	= totalsectors; // Total number of blocks on file system in units of f_frsize. 
	buf->f_bfree = 0; // Total number of free blocks. 
	buf->f_bavail = 0; // Number of free blocks available to non-privileged process. 

	// Treat requests for info on inodes as clusters
	//buf->f_files = totalentries;	// Total number of file serial numbers. 
	buf->f_ffree = 0; // Total number of free file serial numbers. 
	buf->f_favail = 0; // Number of file serial numbers available to non-privileged process. 

	// File system ID. 32bit ioType value
	buf->f_fsid = 0; //??!!? 

	// Bit mask of f_flag values.
	buf->f_flag = ST_NOSUID // No support for ST_ISUID and ST_ISGID file mode bits
			| ST_RDONLY; // Read only file system
	// Maximum filename length.
	buf->f_namemax = 208;
	return 0;
}

static const devoptab_t dotab_iso9660 =
{
	NULL,
	sizeof(FILE_STRUCT),
	_ISO9660_open_r,
	_ISO9660_close_r,
	NULL,
	_ISO9660_read_r,
	_ISO9660_seek_r,
	_ISO9660_fstat_r,
	_ISO9660_stat_r,
	NULL,
	NULL,
	_ISO9660_chdir_r,
	NULL,
	NULL,
	sizeof(DIR_STATE_STRUCT),
	_ISO9660_diropen_r,
	_ISO9660_dirreset_r,
	_ISO9660_dirnext_r,
	_ISO9660_dirclose_r,
	_ISO9660_statvfs_r,
	NULL, // device ftruncate_r
	NULL, // device fsync_r
	NULL // device data
};

static MOUNT_DESCR* _ISO9660_getMountDescrFromPath(const char *path, devoptab_t **pdevops)
{
	devoptab_t *devops;

	if (!path)
		return NULL;

	devops = (devoptab_t *) GetDeviceOpTab(path);
	if (!devops)
		return NULL;

	// Perform a quick check to make sure we're dealing with a libiso9660 controlled device
	if (devops->open_r != dotab_iso9660.open_r)
		return NULL;

	if (pdevops)
		*pdevops = devops;

	return (MOUNT_DESCR*) devops->deviceData;
}

static PATH_ENTRY* entry_from_index(PATH_ENTRY *entry, u16 index)
{
	u32 i;

	if (entry->index == index)
		return entry;

	for (i = 0; i < entry->childCount; i++)
	{
		PATH_ENTRY *match = entry_from_index(&entry->children[i], index);
		if (match)
			return match;
	}
	return NULL;
}

static PATH_ENTRY* add_child_entry(PATH_ENTRY *dir)
{
	PATH_ENTRY *child;
	PATH_ENTRY *newChildren = NULL;

	newChildren = realloc(dir->children, (dir->childCount + 1) * sizeof(PATH_ENTRY));
	if (newChildren == NULL)
		return NULL;

	memset(newChildren + dir->childCount, 0, sizeof(PATH_ENTRY));
	dir->children = newChildren;

	child = &dir->children[dir->childCount++];
	return child;
}

static void cleanup_recursive(PATH_ENTRY *entry)
{
	u32 i;

	for (i = 0; i < entry->childCount; i++)
		cleanup_recursive(&entry->children[i]);
	if (entry->children)
		free(entry->children);
}

static struct pvd_s* read_volume_descriptor(MOUNT_DESCR *mdescr, u8 descriptor)
{
	u8 sector;
	const DISC_INTERFACE *disc = mdescr->disc_interface;

	for (sector = 16; sector < 32; sector++)
	{
		if (!disc->readSectors(sector, 1, mdescr->read_buffer))
			return NULL;
		if (!memcmp(mdescr->read_buffer + 1, "CD001\1", 6))
		{
			if (*mdescr->read_buffer == descriptor)
				return (struct pvd_s*) mdescr->read_buffer;
			else if (*mdescr->read_buffer == 0xff)
				return NULL;
		}
	}

	return NULL;
}

static bool read_directories(MOUNT_DESCR *mdescr)
{
	struct pvd_s *volume = read_volume_descriptor(mdescr, 2);
	if (volume)
		mdescr->iso_unicode = true;
	else if (!(volume = read_volume_descriptor(mdescr, 1)))
		return false;

	if (!(mdescr->iso_rootentry = malloc(sizeof(PATH_ENTRY))))
		return false;
	memset(mdescr->iso_rootentry, 0, sizeof(PATH_ENTRY));
	mdescr->iso_rootentry->table_entry.name_length = 1;
	mdescr->iso_rootentry->table_entry.extended_sectors = volume->root[OFFSET_EXTENDED];
	mdescr->iso_rootentry->table_entry.sector = *(u32 *) (volume->root + OFFSET_SECTOR);
	mdescr->iso_rootentry->table_entry.parent = 0;
	mdescr->iso_rootentry->table_entry.name[0] = '\x00';
	mdescr->iso_rootentry->index = 1;
	mdescr->iso_currententry = mdescr->iso_rootentry;
	
    strncpy(mdescr->volume_id, volume->volume_id, 32);
    mdescr->volume_id[31] = '\0';

	u32 path_table = volume->path_table_be;
	u32 path_table_len = volume->path_table_len_be;
	u16 i = 1;
	u64 offset = sizeof(PATHTABLE_ENTRY) - ISO_MAXPATHLEN + 2;
	PATH_ENTRY *parent = mdescr->iso_rootentry;
	while (i < 0xffff && offset < path_table_len)
	{
		PATHTABLE_ENTRY entry;
		if (__read(mdescr, &entry, (u64) path_table * SECTOR_SIZE + offset, sizeof(PATHTABLE_ENTRY)) != sizeof(PATHTABLE_ENTRY))
			return false; // kinda dodgy - could be reading too far
		if (parent->index != entry.parent)
			parent = entry_from_index(mdescr->iso_rootentry, entry.parent);
		if (!parent)
			return false;
		PATH_ENTRY *child = add_child_entry(parent);
		if (!child)
			return false;
		memcpy(&child->table_entry, &entry, sizeof(PATHTABLE_ENTRY));
		offset += sizeof(PATHTABLE_ENTRY) - ISO_MAXPATHLEN + child->table_entry.name_length;
		if (child->table_entry.name_length % 2)
			offset++;
		child->index = ++i;

		if (mdescr->iso_unicode)
		{
			u32 i;
			for (i = 0; i < (child->table_entry.name_length / 2); i++)
				child->table_entry.name[i] = entry.name[i * 2 + 1];
			child->table_entry.name[i] = '\x00';
			child->table_entry.name_length = i;
		}
		else
		{
			child->table_entry.name[child->table_entry.name_length] = '\x00';
		}

	}

	return true;
}

static MOUNT_DESCR *_ISO9660_mdescr_constructor(const DISC_INTERFACE *disc_interface)
{
	MOUNT_DESCR *mdescr = NULL;

	mdescr = malloc(sizeof(MOUNT_DESCR));
	if (!mdescr)
		return NULL;

	mdescr->disc_interface = disc_interface;
	mdescr->cache_start = 0;
	mdescr->cache_sectors = 0;
	mdescr->iso_unicode = false;
	mdescr->iso_rootentry = NULL;
	mdescr->iso_currententry = NULL;

	if (!read_directories(mdescr))
	{
		free(mdescr);
		return NULL;
	}
	return mdescr;
}

bool ISO9660_Mount(const char* name, const DISC_INTERFACE *disc_interface)
{
	char *nameCopy;
	devoptab_t *devops = NULL;
	MOUNT_DESCR *mdescr = NULL;
	char devname[10];

	if (!name || strlen(name) > 8 || !disc_interface)
		return false;

	if (!disc_interface->startup())
		return false;

	if (!disc_interface->isInserted())
		return false;

	sprintf(devname, "%s:", name);
	if (FindDevice(devname) >= 0)
		return false;

	devops = malloc(sizeof(dotab_iso9660) + strlen(name) + 1);
	if (!devops)
		return false;

	// Use the space allocated at the end of the devoptab struct for storing the name
	nameCopy = (char*) (devops + 1);

	// Initialize the file system
	mdescr = _ISO9660_mdescr_constructor(disc_interface);
	if (!mdescr)
	{
		free(devops);
		return false;
	}

	// Add an entry for this device to the devoptab table
	memcpy(devops, &dotab_iso9660, sizeof(dotab_iso9660));
	strcpy(nameCopy, name);
	devops->name = nameCopy;
	devops->deviceData = mdescr;

	if (AddDevice(devops) < 0)
	{
		free(mdescr);
		free(devops);
		return false;
	}
	return true;
}


bool ISO9660_Unmount(const char* name)
{
	devoptab_t *devops;
	MOUNT_DESCR *mdescr;
	char devname[11];

    if (! check_dev_name(name, devname, sizeof(devname)))
		return false;

	mdescr = _ISO9660_getMountDescrFromPath(devname, &devops);
	if (!mdescr)
		return false;

	if (RemoveDevice(devname) == -1)
		return false;

	if (mdescr->iso_rootentry)
	{
		cleanup_recursive(mdescr->iso_rootentry);
		free(mdescr->iso_rootentry);
	}

	free(mdescr);
	free(devops);
	return true;
}

const char *ISO9660_GetVolumeLabel(const char *name)
{
	MOUNT_DESCR *mdescr;
	char devname[11];

    if (! check_dev_name(name, devname, sizeof(devname)))
		return NULL;

	mdescr = _ISO9660_getMountDescrFromPath(devname, NULL);
	if (!mdescr)
		return NULL;

    return mdescr->volume_id;
}

#endif /* XASH_GAMECUBE && !XASH_GAMECUBE_LIBOGC2 */
