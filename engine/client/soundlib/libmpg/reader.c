/*
reader.c - compact version of famous library mpg123
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

#include "mpg123.h"
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#define READER_STREAM	0
#define READER_FEED		1
#define READER_BUF_STREAM	2

static int default_init( mpg123_handle_t *fr );
static mpg_off_t get_fileinfo( mpg123_handle_t *fr );

// methods for the buffer chain, mainly used for feed reader, but not just that.
static buffy_t* buffy_new( size_t size, size_t minsize )
{
	buffy_t	*newbuf = malloc( sizeof( buffy_t ));

	if( newbuf == NULL )
		return NULL;

	newbuf->realsize = size > minsize ? size : minsize;
	newbuf->data = malloc( newbuf->realsize );

	if( newbuf->data == NULL )
	{
		free( newbuf );
		return NULL;
	}

	newbuf->size = 0;
	newbuf->next = NULL;

	return newbuf;
}

static void buffy_del( buffy_t *buf )
{
	if( buf )
	{
		free( buf->data );
		free( buf );
	}
}

// delete this buffy and all following buffies.
static void buffy_del_chain( buffy_t *buf )
{
	while( buf )
	{
		buffy_t *next = buf->next;
		buffy_del( buf );
		buf = next;
	}
}

// fetch a buffer from the pool (if possible) or create one.
static buffy_t* bc_alloc( bufferchain_t *bc, size_t size )
{
	// Easy route: Just try the first available buffer.
	// size does not matter, it's only a hint for creation of new buffers.
	if( bc->pool )
	{
		buffy_t *buf = bc->pool;

		bc->pool = buf->next;
		buf->next = NULL; // that shall be set to a sensible value later.
		buf->size = 0;
		bc->pool_fill--;

		return buf;
	}

	return buffy_new( size, bc->bufblock );
}

// either stuff the buffer back into the pool or free it for good.
static void bc_free( bufferchain_t *bc, buffy_t* buf )
{
	if( !buf ) return;

	if( bc->pool_fill < bc->pool_size )
	{
		buf->next = bc->pool;
		bc->pool = buf;
		bc->pool_fill++;
	}
	else buffy_del( buf );
}

// make the buffer count in the pool match the pool size.
static int bc_fill_pool( bufferchain_t *bc )
{
	// remove superfluous ones.
	while( bc->pool_fill > bc->pool_size )
	{
		// lazyness: Just work on the front.
		buffy_t *buf = bc->pool;
		bc->pool = buf->next;
		buffy_del( buf );
		bc->pool_fill--;
	}

	// add missing ones.
	while( bc->pool_fill < bc->pool_size )
	{
		// again, just work on the front.
		buffy_t *buf = buffy_new( 0, bc->bufblock ); // use default block size.
		if( !buf ) return -1;

		buf->next = bc->pool;
		bc->pool = buf;
		bc->pool_fill++;
	}

	return 0;
}

static void bc_init( bufferchain_t *bc )
{
	bc->first = NULL;
	bc->last = bc->first;
	bc->size = 0;
	bc->pos = 0;
	bc->firstpos = 0;
	bc->fileoff = 0;
}

static void bc_reset( bufferchain_t *bc )
{
	// free current chain, possibly stuffing back into the pool.
	while( bc->first )
	{
		buffy_t *buf = bc->first;
		bc->first = buf->next;
		bc_free( bc, buf );
	}

	bc_fill_pool( bc );	// ignoring an error here...
	bc_init( bc );
}

// create a new buffy at the end to be filled.
static int bc_append( bufferchain_t *bc, mpg_ssize_t size )
{
	buffy_t	*newbuf;

	if( size < 1 )
		return -1;

	newbuf = bc_alloc( bc, size );
	if( newbuf == NULL ) return -2;

	if( bc->last != NULL )
		bc->last->next = newbuf;
	else if( bc->first == NULL )
		bc->first = newbuf;

	bc->last  = newbuf;

	return 0;
}

void bc_prepare( bufferchain_t *bc, size_t pool_size, size_t bufblock )
{
	bc_poolsize( bc, pool_size, bufblock );
	bc->pool = NULL;
	bc->pool_fill = 0;
	bc_init( bc );	// ensure that members are zeroed for read-only use.
}

size_t bc_fill( bufferchain_t *bc )
{
	return (size_t)(bc->size - bc->pos);
}

void bc_poolsize( bufferchain_t *bc, size_t pool_size, size_t bufblock )
{
	bc->pool_size = pool_size;
	bc->bufblock = bufblock;
}

void bc_cleanup( bufferchain_t *bc )
{
	buffy_del_chain( bc->pool );
	bc->pool_fill = 0;
	bc->pool = NULL;
}

// append a new buffer and copy content to it.
static int bc_add( bufferchain_t *bc, const byte *data, mpg_ssize_t size )
{
	int	ret = 0;
	mpg_ssize_t	part = 0;

	while( size > 0 )
	{
		// try to fill up the last buffer block.
		if( bc->last != NULL && bc->last->size < bc->last->realsize )
		{
			part = bc->last->realsize - bc->last->size;
			if( part > size ) part = size;

			memcpy( bc->last->data + bc->last->size, data, part );
			bc->last->size += part;
			size -= part;
			bc->size += part;
			data += part;
		}

		// if there is still data left, put it into a new buffer block.
		if( size > 0 && ( ret = bc_append( bc, size )) != 0 )
			break;
	}

	return ret;
}

// common handler for "You want more than I can give." situation.
static mpg_ssize_t bc_need_more( bufferchain_t *bc )
{
	// go back to firstpos, undo the previous reads
	bc->pos = bc->firstpos;

	return MPG123_NEED_MORE;
}

// give some data, advancing position but not forgetting yet.
static mpg_ssize_t bc_give( bufferchain_t *bc, byte *out, mpg_ssize_t size )
{
	buffy_t	*b = bc->first;
	mpg_ssize_t	gotcount = 0;
	mpg_ssize_t	offset = 0;

	if( bc->size - bc->pos < size )
		return bc_need_more( bc );

	// find the current buffer
	while( b != NULL && ( offset + b->size ) <= bc->pos )
	{
		offset += b->size;
		b = b->next;
	}

	// now start copying from there
	while( gotcount < size && ( b != NULL ))
	{
		mpg_ssize_t	loff = bc->pos - offset;
		mpg_ssize_t	chunk = size - gotcount; // amount of bytes to get from here...

		if( chunk > b->size - loff )
			chunk = b->size - loff;

		memcpy( out + gotcount, b->data + loff, chunk );
		gotcount += chunk;
		bc->pos += chunk;
		offset += b->size;
		b = b->next;
	}

	return gotcount;
}

// skip some bytes and return the new position.
// the buffers are still there, just the read pointer is moved!
static mpg_ssize_t bc_skip( bufferchain_t *bc, mpg_ssize_t count )
{
	if( count >= 0 )
	{
		if( bc->size - bc->pos < count )
			return bc_need_more( bc );
		return bc->pos += count;
	}

	return MPG123_ERR;
}

static mpg_ssize_t bc_seekback( bufferchain_t *bc, mpg_ssize_t count )
{
	if( count >= 0 && count <= bc->pos )
		return bc->pos -= count;
	return MPG123_ERR;
}

// throw away buffies that we passed.
static void bc_forget( bufferchain_t *bc )
{
	buffy_t	*b = bc->first;

	// free all buffers that are def'n'tly outdated
	// we have buffers until filepos... delete all buffers fully below it
	while( b != NULL && bc->pos >= b->size )
	{
		buffy_t	*n = b->next;	// != NULL or this is indeed the end and the last cycle anyway

		if( n == NULL )
			bc->last = NULL;	// Going to delete the last buffy...
		bc->fileoff += b->size;
		bc->pos  -= b->size;
		bc->size -= b->size;
		bc_free( bc, b );
		b = n;
	}

	bc->first = b;
	bc->firstpos = bc->pos;
}

// reader for input via manually provided buffers
static int feed_init( mpg123_handle_t *fr )
{
	bc_init( &fr->rdat.buffer );
	bc_fill_pool( &fr->rdat.buffer );
	fr->rdat.filelen = 0;
	fr->rdat.filepos = 0;
	fr->rdat.flags |= READER_BUFFERED;

	return 0;
}

// externally called function, returns 0 on success, -1 on error
int feed_more( mpg123_handle_t *fr, const byte *in, long count )
{
	if( bc_add( &fr->rdat.buffer, in, count ) != 0 )
		return MPG123_ERR;

	return MPG123_OK;
}

static mpg_ssize_t feed_read( mpg123_handle_t *fr, byte *out, mpg_ssize_t count )
{
	mpg_ssize_t	gotcount = bc_give( &fr->rdat.buffer, out, count );

	if( gotcount >= 0 && gotcount != count )
		return MPG123_ERR;

	return gotcount;
}

// returns reached position... negative ones are bad...
static mpg_off_t feed_skip_bytes( mpg123_handle_t *fr, mpg_off_t len )
{
	// this is either the new buffer offset or some negative error value.
	mpg_off_t res = bc_skip( &fr->rdat.buffer, (mpg_ssize_t)len );
	if( res < 0 ) return res;

	return fr->rdat.buffer.fileoff + res;
}

static int feed_back_bytes( mpg123_handle_t *fr, mpg_off_t bytes )
{
	if( bytes >= 0 )
		return bc_seekback(&fr->rdat.buffer, (mpg_ssize_t)bytes) >= 0 ? 0 : MPG123_ERR;
	return feed_skip_bytes( fr, -bytes ) >= 0 ? 0 : MPG123_ERR;
}

static int feed_seek_frame( mpg123_handle_t *fr, mpg_off_t num )
{
	return MPG123_ERR;
}

// not just for feed reader, also for self-feeding buffered reader.
static void buffered_forget( mpg123_handle_t *fr )
{
	bc_forget( &fr->rdat.buffer );
	fr->rdat.filepos = fr->rdat.buffer.fileoff + fr->rdat.buffer.pos;
}

mpg_off_t feed_set_pos( mpg123_handle_t *fr, mpg_off_t pos )
{
	bufferchain_t	*bc = &fr->rdat.buffer;

	if( pos >= bc->fileoff && pos - bc->fileoff < bc->size )
	{
		// we have the position!
		bc->pos = (mpg_ssize_t)(pos - bc->fileoff);

		// next input after end of buffer...
		return bc->fileoff + bc->size;
	}
	else
	{
		// i expect to get the specific position on next feed. Forget what I have now.
		bc_reset( bc );
		bc->fileoff = pos;

		// next input from exactly that position.
		return pos;
	}
}

// the specific stuff for buffered stream reader.
static mpg_ssize_t buffered_fullread( mpg123_handle_t *fr, byte *out, mpg_ssize_t count )
{
	bufferchain_t	*bc = &fr->rdat.buffer;
	mpg_ssize_t		gotcount;

	if( bc->size - bc->pos < count )
	{
		// add more stuff to buffer. If hitting end of file, adjust count.
		byte	readbuf[4096];

		mpg_ssize_t need = count - (bc->size - bc->pos);

		while( need > 0 )
		{
			mpg_ssize_t	got = fr->rdat.fullread( fr, readbuf, sizeof( readbuf ));
			int	ret;

			if( got < 0 )
				return MPG123_ERR;

			if( got > 0 && ( ret = bc_add( bc, readbuf, got )) != 0 )
				return MPG123_ERR;

			need -= got; // may underflow here...

			if( got < sizeof( readbuf )) // that naturally catches got == 0, too.
				break; // end.
		}

		if( bc->size - bc->pos < count )
			count = bc->size - bc->pos; // we want only what we got.
	}

	gotcount = bc_give( bc, out, count );

	if( gotcount != count )
		return MPG123_ERR;
	return gotcount;
}

// stream based operation
static mpg_ssize_t plain_fullread( mpg123_handle_t *fr, byte *buf, mpg_ssize_t count )
{
	mpg_ssize_t	ret, cnt=0;

	// there used to be a check for expected file end here (length value or ID3 flag).
	// this is not needed:
	// 1. EOF is indicated by fdread returning zero bytes anyway.
	// 2. We get false positives of EOF for either files that grew or
	// 3. ... files that have ID3v1 tags in between (stream with intro).
	while( cnt < count )
	{
		ret = fr->rdat.fdread( fr, buf + cnt, count - cnt );

		if( ret < 0 ) return MPG123_ERR;
		if( ret == 0 ) break;

		if(!( fr->rdat.flags & READER_BUFFERED ))
			fr->rdat.filepos += ret;
		cnt += ret;
	}

	return cnt;
}

// wrappers for actual reading/seeking... I'm full of wrappers here.
static mpg_off_t io_seek( reader_data_t *rdat, mpg_off_t offset, int whence )
{
	if( rdat->flags & READER_HANDLEIO )
	{
		if( rdat->r_lseek_handle != NULL )
			return rdat->r_lseek_handle( rdat->iohandle, offset, whence );
		return -1;
	}

	return rdat->lseek( rdat->filept, offset, whence );
}

static mpg_ssize_t io_read( reader_data_t *rdat, void *buf, size_t count )
{
	if( rdat->flags & READER_HANDLEIO )
	{
		if( rdat->r_read_handle != NULL )
			return rdat->r_read_handle( rdat->iohandle, buf, count );
		return -1;
	}

	return rdat->read( rdat->filept, buf, count );
}

// A normal read and a read with timeout.
static mpg_ssize_t plain_read( mpg123_handle_t *fr, void *buf, size_t count )
{
	return io_read( &fr->rdat, buf, count );
}

static mpg_off_t stream_lseek( mpg123_handle_t *fr, mpg_off_t pos, int whence )
{
	mpg_off_t	ret;

	ret = io_seek( &fr->rdat, pos, whence );

	if( ret >= 0 )
	{
		fr->rdat.filepos = ret;
	}
	else
	{
		fr->err = MPG123_LSEEK_FAILED;
		ret = MPG123_ERR;
	}

	return ret;
}

static void stream_close( mpg123_handle_t *fr )
{
	if( fr->rdat.flags & READER_FD_OPENED )
		close( fr->rdat.filept );

	fr->rdat.filept = 0;

	if( fr->rdat.flags & READER_BUFFERED )
		bc_reset( &fr->rdat.buffer );

	if( fr->rdat.flags & READER_HANDLEIO )
	{
		if( fr->rdat.cleanup_handle != NULL )
			fr->rdat.cleanup_handle( fr->rdat.iohandle );
		fr->rdat.iohandle = NULL;
	}
}

static int stream_seek_frame( mpg123_handle_t *fr, mpg_off_t newframe )
{
	// seekable streams can go backwards and jump forwards.
	// non-seekable streams still can go forward, just not jump.
	if(( fr->rdat.flags & READER_SEEKABLE ) || ( newframe >= fr->num ))
	{
		mpg_off_t	preframe;	// a leading frame we jump to
		mpg_off_t	seek_to;	// the byte offset we want to reach
		mpg_off_t	to_skip;	// bytes to skip to get there (can be negative)

		// now seek to nearest leading index position and read from there until newframe is reached.
		// we use skip_bytes, which handles seekable and non-seekable streams
		// (the latter only for positive offset, which we ensured before entering here).
		seek_to = frame_index_find( fr, newframe, &preframe );

		// no need to seek to index position if we are closer already.
		// but I am picky about fr->num == newframe, play safe by reading the frame again.
		// if you think that's stupid, don't call a seek to the current frame.
		if( fr->num >= newframe || fr->num < preframe )
		{
			to_skip = seek_to - fr->rd->tell( fr );
			if( fr->rd->skip_bytes( fr, to_skip ) != seek_to )
				return MPG123_ERR;

			fr->num = preframe - 1; // watch out! I am going to read preframe... fr->num should indicate the frame before!
		}

		while( fr->num < newframe )
		{
			// try to be non-fatal now... frameNum only gets advanced on success anyway
			if( !read_frame( fr )) break;
		}

		// now the wanted frame should be ready for decoding.
		return MPG123_OK;
	}
	else
	{
		fr->err = MPG123_NO_SEEK;
		return MPG123_ERR; // invalid, no seek happened
	}
}

// return FALSE on error, TRUE on success, READER_MORE on occasion
static int generic_head_read( mpg123_handle_t *fr, ulong *newhead )
{
	byte	hbuf[4];
	int	ret = fr->rd->fullread( fr, hbuf, 4 );

	if( ret == MPG123_NEED_MORE )
		return ret;

	if( ret != 4 ) return FALSE;

	*newhead = ((ulong) hbuf[0] << 24) | ((ulong) hbuf[1] << 16) | ((ulong) hbuf[2] << 8) | (ulong) hbuf[3];

	return TRUE;
}

// return FALSE on error, TRUE on success, READER_MORE on occasion
static int generic_head_shift( mpg123_handle_t *fr, ulong *head )
{
	byte	hbuf;
	int	ret = fr->rd->fullread( fr, &hbuf, 1 );

	if( ret == MPG123_NEED_MORE )
		return ret;

	if( ret != 1 ) return FALSE;

	*head <<= 8;
	*head |= hbuf;
	*head &= 0xffffffff;

	return TRUE;
}

// returns reached position... negative ones are bad...
static mpg_off_t stream_skip_bytes( mpg123_handle_t *fr, mpg_off_t len )
{
	if( fr->rdat.flags & READER_SEEKABLE )
	{
		mpg_off_t ret = stream_lseek( fr, len, SEEK_CUR );
		return (ret < 0) ? MPG123_ERR : ret;
	}
	else if( len >= 0 )
	{
		byte	buf[1024]; // ThOr: Compaq cxx complained and it makes sense to me... or should one do a cast? What for?
		mpg_ssize_t	ret;

		while( len > 0 )
		{
			mpg_ssize_t num = len < (mpg_off_t)sizeof( buf ) ? (mpg_ssize_t)len : (mpg_ssize_t)sizeof( buf );
			ret = fr->rd->fullread( fr, buf, num );
			if( ret < 0 ) return ret;
			else if( ret == 0 ) break; // EOF... an error? interface defined to tell the actual position...
			len -= ret;
		}

		return fr->rd->tell( fr );
	}
	else if( fr->rdat.flags & READER_BUFFERED )
	{
		// perhaps we _can_ go a bit back.
		if( fr->rdat.buffer.pos >= -len )
		{
			fr->rdat.buffer.pos += len;
			return fr->rd->tell( fr );
		}
		else
		{
			fr->err = MPG123_NO_SEEK;
			return MPG123_ERR;
		}
	}
	else
	{
		fr->err = MPG123_NO_SEEK;
		return MPG123_ERR;
	}
}

// return 0 on success...
static int stream_back_bytes( mpg123_handle_t *fr, mpg_off_t bytes )
{
	mpg_off_t	want = fr->rd->tell( fr ) - bytes;

	if( want < 0 ) return MPG123_ERR;

	if( stream_skip_bytes( fr, -bytes ) != want )
		return MPG123_ERR;

	return 0;
}


// returns size on success...
static int generic_read_frame_body( mpg123_handle_t *fr, byte *buf, int size )
{
	long	l;

	if(( l = fr->rd->fullread( fr, buf, size )) != size )
		return MPG123_ERR;

	return l;
}

static mpg_off_t generic_tell( mpg123_handle_t *fr )
{
	if( fr->rdat.flags & READER_BUFFERED )
		fr->rdat.filepos = fr->rdat.buffer.fileoff + fr->rdat.buffer.pos;

	return fr->rdat.filepos;
}

// this does not (fully) work for non-seekable streams... You have to check for that flag, pal!
static void stream_rewind( mpg123_handle_t *fr )
{
	if( fr->rdat.flags & READER_SEEKABLE )
	{
		fr->rdat.filepos = stream_lseek( fr, 0, SEEK_SET );
		fr->rdat.buffer.fileoff = fr->rdat.filepos;
	}

	if( fr->rdat.flags & READER_BUFFERED )
	{
		fr->rdat.buffer.pos = 0;
		fr->rdat.buffer.firstpos = 0;
		fr->rdat.filepos = fr->rdat.buffer.fileoff;
	}
}

// returns length of a file (if filept points to a file)
// reads the last 128 bytes information into buffer
// ... that is not totally safe...
static mpg_off_t get_fileinfo( mpg123_handle_t *fr )
{
	mpg_off_t	len;

	if(( len = io_seek( &fr->rdat, 0, SEEK_END )) < 0 )
		return -1;

	if( io_seek( &fr->rdat, -128, SEEK_END ) < 0 )
		return -1;

	if( fr->rd->fullread( fr, (byte *)fr->id3buf, 128 ) != 128 )
		return -1;

	if( !strncmp((char *)fr->id3buf, "TAG", 3 ))
		len -= 128;

	if( io_seek( &fr->rdat, 0, SEEK_SET ) < 0 )
		return -1;

	if( len <= 0 )
		return -1;

	return len;
}

static int bad_init( mpg123_handle_t *mh ) { mh->err = MPG123_NO_READER; return MPG123_ERR; }
static mpg_ssize_t bad_fullread( mpg123_handle_t *mh, byte *data, mpg_ssize_t count ) { mh->err = MPG123_NO_READER; return MPG123_ERR; }
static int bad_head_read( mpg123_handle_t *mh, ulong *newhead ) { mh->err = MPG123_NO_READER; return MPG123_ERR; }
static int bad_head_shift( mpg123_handle_t *mh, ulong *head ) { mh->err = MPG123_NO_READER; return MPG123_ERR; }
static mpg_off_t bad_skip_bytes( mpg123_handle_t *mh, mpg_off_t len ) { mh->err = MPG123_NO_READER; return MPG123_ERR; }
static int bad_read_frame_body( mpg123_handle_t *mh, byte *data, int size ) { mh->err = MPG123_NO_READER; return MPG123_ERR; }
static int bad_back_bytes( mpg123_handle_t *mh, mpg_off_t bytes ) { mh->err = MPG123_NO_READER; return MPG123_ERR; }
static int bad_seek_frame( mpg123_handle_t *mh, mpg_off_t num ) { mh->err = MPG123_NO_READER; return MPG123_ERR; }
static mpg_off_t bad_tell( mpg123_handle_t *mh ) { mh->err = MPG123_NO_READER; return MPG123_ERR; }
static void bad_rewind( mpg123_handle_t *mh ) { }
static void bad_close( mpg123_handle_t *mh ) { }

static reader_t bad_reader =
{
	bad_init,
	bad_close,
	bad_fullread,
	bad_head_read,
	bad_head_shift,
	bad_skip_bytes,
	bad_read_frame_body,
	bad_back_bytes,
	bad_seek_frame,
	bad_tell,
	bad_rewind,
	NULL
};

void open_bad( mpg123_handle_t *mh )
{
	mh->rd = &bad_reader;
	mh->rdat.flags = 0;
	bc_init( &mh->rdat.buffer );
	mh->rdat.filelen = -1;
}

static reader_t readers[] =
{
	{	// READER_STREAM
		default_init,
		stream_close,
		plain_fullread,
		generic_head_read,
		generic_head_shift,
		stream_skip_bytes,
		generic_read_frame_body,
		stream_back_bytes,
		stream_seek_frame,
		generic_tell,
		stream_rewind,
		NULL
	},
	{	// READER_FEED
		feed_init,
		stream_close,
		feed_read,
		generic_head_read,
		generic_head_shift,
		feed_skip_bytes,
		generic_read_frame_body,
		feed_back_bytes,
		feed_seek_frame,
		generic_tell,
		stream_rewind,
		buffered_forget
	},
	{	// READER_BUF_STREAM
		default_init,
		stream_close,
		buffered_fullread,
		generic_head_read,
		generic_head_shift,
		stream_skip_bytes,
		generic_read_frame_body,
		stream_back_bytes,
		stream_seek_frame,
		generic_tell,
		stream_rewind,
		buffered_forget
	}
};

// final code common to open_stream and open_stream_handle.
static int open_finish( mpg123_handle_t *fr )
{
	fr->rd = &readers[READER_STREAM];
	if( fr->rd->init( fr ) < 0 )
		return -1;

	return MPG123_OK;
}

int open_stream_handle( mpg123_handle_t *fr, void *iohandle )
{
	fr->rdat.filelen = -1;
	fr->rdat.filept  = -1;
	fr->rdat.iohandle = iohandle;
	fr->rdat.flags = 0;
	fr->rdat.flags |= READER_HANDLEIO;

	return open_finish( fr );
}

int open_feed( mpg123_handle_t *fr )
{
	fr->rd = &readers[READER_FEED];
	fr->rdat.flags = 0;

	if( fr->rd->init( fr ) < 0 )
		return -1;

	return 0;
}

static mpg_ssize_t read_mpgtypes( int fd, void *buf, size_t count )
{
	return read( fd, buf, count );
}

static mpg_off_t lseek_mpgtypes( int fd, mpg_off_t offset, int whence )
{
	return lseek( fd, offset, whence );
}

static int default_init( mpg123_handle_t *fr )
{
	fr->rdat.fdread = plain_read;
	fr->rdat.read = fr->rdat.r_read  != NULL ? fr->rdat.r_read  : read_mpgtypes;
	fr->rdat.lseek = fr->rdat.r_lseek != NULL ? fr->rdat.r_lseek : lseek_mpgtypes;
	fr->rdat.filelen = get_fileinfo( fr );
	fr->rdat.filepos = 0;

	// don't enable seeking on ICY streams, just plain normal files.
	// this check is necessary since the client can enforce ICY parsing on files that would otherwise be seekable.
	// it is a task for the future to make the ICY parsing safe with seeks ... or not.
	if( fr->rdat.filelen >= 0 )
	{
		fr->rdat.flags |= READER_SEEKABLE;
		if( !strncmp((char *)fr->id3buf,"TAG", 3 ))
		{
			fr->rdat.flags |= READER_ID3TAG;
			fr->metaflags  |= MPG123_NEW_ID3;
		}
	}
	else if( fr->p.flags & MPG123_SEEKBUFFER )
	{
		// switch reader to a buffered one, if allowed.
		if( fr->rd == &readers[READER_STREAM] )
		{
			fr->rd = &readers[READER_BUF_STREAM];
			fr->rdat.fullread = plain_fullread;
		}
		else
		{
			return -1;
		}

		bc_init( &fr->rdat.buffer );
		fr->rdat.filelen = 0; // we carry the offset, but never know how big the stream is.
		fr->rdat.flags |= READER_BUFFERED;
	}

	return 0;
}
