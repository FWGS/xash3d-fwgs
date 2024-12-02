/*
reader.h - compact version of famous library mpg123
Copyright (C) 2017 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef READER_H
#define READER_H

#define READER_FD_OPENED	0x1
#define READER_ID3TAG	0x2
#define READER_SEEKABLE	0x4
#define READER_BUFFERED	0x8
#define READER_NONBLOCK	0x20
#define READER_HANDLEIO	0x40

typedef struct buffy_s
{
	byte		*data;
	mpg_ssize_t		size;
	mpg_ssize_t		realsize;
	struct buffy_s	*next;
} buffy_t;

typedef struct bufferchain_s
{
	struct buffy_s	*first;		// the beginning of the chain.
	struct buffy_s	*last;		// the end...    of the chain.
	mpg_ssize_t		size;		// aggregated size of all buffies.

	// these positions are relative to buffer chain beginning.
	mpg_ssize_t		pos;		// position in whole chain.
	mpg_ssize_t		firstpos;		// the point of return on non-forget()

	// the "real" filepos is fileoff + pos.
	mpg_off_t		fileoff;		// beginning of chain is at this file offset.
	size_t		bufblock;		// default (minimal) size of buffers.
	size_t		pool_size;	// keep that many buffers in storage.
	size_t		pool_fill;	// that many buffers are there.

	// a pool of buffers to re-use, if activated. It's a linked list that is worked on from the front.
	struct buffy_s	*pool;
} bufferchain_t;

// call this before any buffer chain use (even bc_init()).
void bc_prepare( bufferchain_t*, size_t pool_size, size_t bufblock );
// free persistent data in the buffer chain, after bc_reset().
void bc_cleanup( bufferchain_t* );
// change pool size. This does not actually allocate/free anything on itself, just instructs later operations to free less / allocate more buffers.
void bc_poolsize( bufferchain_t*, size_t pool_size, size_t bufblock );
// return available byte count in the buffer.
size_t bc_fill( bufferchain_t *bc );

typedef struct reader_data_s
{
	mpg_off_t		filelen;		// total file length or total buffer size
	mpg_off_t		filepos;		// position in file or position in buffer chain
	int		filept;

	// custom opaque I/O handle from the client.
	void		*iohandle;
	int		flags;
	long		timeout_sec;

	mpg_ssize_t (*fdread)( mpg123_handle_t*, void*, size_t );

	// user can replace the read and lseek functions. The r_* are the stored replacement functions or NULL.
	mpg_ssize_t (*r_read)( int fd, void *buf, size_t count );
	mpg_off_t (*r_lseek)( int fd, mpg_off_t offset, int whence );

	// These are custom I/O routines for opaque user handles.
	// They get picked if there's some iohandle set.
	mpg_ssize_t (*r_read_handle)( void *handle, void *buf, size_t count );
	mpg_off_t (*r_lseek_handle)( void *handle, mpg_off_t offset, int whence );

	// an optional cleaner for the handle on closing the stream.
	void (*cleanup_handle)( void *handle );

	// these two pointers are the actual workers (default map to POSIX read/lseek).
	mpg_ssize_t (*read)( int fd, void *buf, size_t count );
	mpg_off_t (*lseek)( int fd, mpg_off_t offset, int whence );

	// buffered readers want that abstracted, set internally.
	mpg_ssize_t (*fullread)( mpg123_handle_t*, byte*, mpg_ssize_t );

	bufferchain_t	buffer;		// not dynamically allocated, these few struct bytes aren't worth the trouble.
} reader_data_t;

// start to use mpg_off_t to properly do LFS in future ... used to be long
typedef struct reader_s
{
	int	(*init)( mpg123_handle_t* );
	void	(*close)( mpg123_handle_t* );
	mpg_ssize_t	(*fullread)( mpg123_handle_t*, byte*, mpg_ssize_t);
	int	(*head_read)( mpg123_handle_t*, ulong *newhead );		// succ: TRUE, else <= 0 (FALSE or READER_MORE)
	int	(*head_shift)( mpg123_handle_t*, ulong *head );		// succ: TRUE, else <= 0 (FALSE or READER_MORE)
	mpg_off_t	(*skip_bytes)( mpg123_handle_t*, mpg_off_t len );		// succ: >=0, else error or READER_MORE
	int	(*read_frame_body)( mpg123_handle_t*, byte*, int size );
	int	(*back_bytes)( mpg123_handle_t*, mpg_off_t bytes );
	int	(*seek_frame)( mpg123_handle_t*, mpg_off_t num );
	mpg_off_t	(*tell)( mpg123_handle_t* );
	void	(*rewind)( mpg123_handle_t* );
	void	(*forget)( mpg123_handle_t* );
} reader_t;

// open a file by path or use an opened file descriptor
int open_stream( mpg123_handle_t *fr, const char *path, int fd );
// open an external handle.
int open_stream_handle( mpg123_handle_t *fr, void *iohandle );

// feed based operation has some specials
int open_feed( mpg123_handle_t *fr );
// externally called function, returns 0 on success, -1 on error
int feed_more( mpg123_handle_t *fr, const byte *in, long count );
// forget the data that has been read (free some buffers)
void feed_forget( mpg123_handle_t *fr );
// set position (inside available data if possible), return wanted byte offset of next feed.
mpg_off_t feed_set_pos( mpg123_handle_t *fr, mpg_off_t pos );
// error fallback
void open_bad( mpg123_handle_t *fr );

#endif//READER_H
