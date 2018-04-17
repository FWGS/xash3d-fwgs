/*
mpg123.h - compact version of famous library mpg123
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

#ifndef MPG123_H
#define MPG123_H

typedef struct mpg123_handle_s	mpg123_handle_t;

#ifdef _MSC_VER
#pragma warning(disable : 4115)	// named type definition in parentheses
#pragma warning(disable : 4057)	// differs in indirection to slightly different base types
#pragma warning(disable : 4244)	// conversion possible loss of data
#pragma warning(disable : 4127)	// conditional expression is constant
#pragma warning(disable : 4706)	// assignment within conditional expression
#pragma warning(disable : 4100)	// unreferenced formal parameter
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "fmt123.h"

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE (!FALSE)
#endif

// configure the lib
#define ACCURATE_ROUNDING
//#define IEEE_FLOAT

// begin used typedefs
typedef unsigned char	byte;
typedef unsigned short	word;
typedef unsigned long	ulong;
typedef unsigned int	uint;
typedef long		mpg_off_t;

#ifdef _MSC_VER // a1ba: MSVC6 don't have ssize_t
typedef long		mpg_ssize_t;
#else
typedef ssize_t		mpg_ssize_t;
#endif

typedef short		int16_t;
typedef unsigned short	uint16_t;

#include "synth.h"
#include "index.h"
#include "reader.h"
#include "frame.h"

#define SEEKFRAME( mh )	((mh)->ignoreframe < 0 ? 0 : (mh)->ignoreframe)
#define track_need_init( mh )	((mh)->num < 0)
#define INDEX_SIZE		1000
#define SBLIMIT		32
#define SSLIMIT		18
#define GAPLESS_DELAY	529
#define SHORT_SCALE		32768

#define MPG_MD_STEREO	0
#define MPG_MD_JOINT_STEREO	1
#define MPG_MD_DUAL_CHANNEL	2
#define MPG_MD_MONO		3

#define SINGLE_STEREO	-1
#define SINGLE_LEFT		0
#define SINGLE_RIGHT	1
#define SINGLE_MIX		3

#define DOUBLE_TO_REAL( x )			(float)(x)
#define DOUBLE_TO_REAL_15( x )		(float)(x)
#define DOUBLE_TO_REAL_POW43( x )		(float)(x)
#define DOUBLE_TO_REAL_SCALE_LAYER12( x )	(float)(x)
#define DOUBLE_TO_REAL_SCALE_LAYER3( x, y )	(float)(x)
#define REAL_TO_DOUBLE( x )			(x)

#define REAL_MUL( x, y )			((x) * (y))
#define REAL_MUL_SYNTH( x, y )		((x) * (y))
#define REAL_MUL_15( x, y )			((x) * (y))
#define REAL_MUL_SCALE_LAYER12( x, y )		((x) * (y))
#define REAL_MUL_SCALE_LAYER3( x, y )		((x) * (y))
#define REAL_SCALE_LAYER12( x )		(x)
#define REAL_SCALE_LAYER3( x )		(x)
#define REAL_SCALE_DCT64( x )			(x)

#ifndef M_PI
#define M_PI	3.14159265358979323846
#endif

#ifndef M_SQRT2
#define M_SQRT2	1.41421356237309504880
#endif

// enumeration of the message and error codes and returned by libmpg123 functions.
enum mpg123_errors
{
	MPG123_DONE	= -12,	/**< Message: Track ended. Stop decoding. */
	MPG123_NEW_FORMAT	= -11,	/**< Message: Output format will be different on next call. Note that some libmpg123 versions between 1.4.3 and 1.8.0 insist on you calling mpg123_getformat() after getting this message code. Newer verisons behave like advertised: You have the chance to call mpg123_getformat(), but you can also just continue decoding and get your data. */
	MPG123_NEED_MORE	= -10,	/**< Message: For feed reader: "Feed me more!" (call mpg123_feed() or mpg123_decode() with some new input data). */
	MPG123_ERR	= -1,	/**< Generic Error */
	MPG123_OK		= 0,	/**< Success */
	MPG123_BAD_OUTFORMAT, 	/**< Unable to set up output format! */
	MPG123_BAD_CHANNEL,		/**< Invalid channel number specified. */
	MPG123_BAD_RATE,		/**< Invalid sample rate specified.  */
	MPG123_ERR_16TO8TABLE,	/**< Unable to allocate memory for 16 to 8 converter table! */
	MPG123_BAD_PARAM,		/**< Bad parameter id! */
	MPG123_BAD_BUFFER,		/**< Bad buffer given -- invalid pointer or too small size. */
	MPG123_OUT_OF_MEM,		/**< Out of memory -- some malloc() failed. */
	MPG123_NOT_INITIALIZED,	/**< You didn't initialize the library! */
	MPG123_BAD_DECODER,		/**< Invalid decoder choice. */
	MPG123_BAD_HANDLE,		/**< Invalid mpg123 handle. */
	MPG123_NO_BUFFERS,		/**< Unable to initialize frame buffers (out of memory?). */
	MPG123_BAD_RVA,		/**< Invalid RVA mode. */
	MPG123_NO_GAPLESS,		/**< This build doesn't support gapless decoding. */
	MPG123_NO_SPACE,		/**< Not enough buffer space. */
	MPG123_BAD_TYPES,		/**< Incompatible numeric data types. */
	MPG123_BAD_BAND,		/**< Bad equalizer band. */
	MPG123_ERR_NULL,		/**< Null pointer given where valid storage address needed. */
	MPG123_ERR_READER,		/**< Error reading the stream. */
	MPG123_NO_SEEK_FROM_END,	/**< Cannot seek from end (end is not known). */
	MPG123_BAD_WHENCE,		/**< Invalid 'whence' for seek function.*/
	MPG123_NO_TIMEOUT,		/**< Build does not support stream timeouts. */
	MPG123_BAD_FILE,		/**< File access error. */
	MPG123_NO_SEEK,		/**< Seek not supported by stream. */
	MPG123_NO_READER,		/**< No stream opened. */
	MPG123_BAD_PARS,		/**< Bad parameter handle. */
	MPG123_BAD_INDEX_PAR,	/**< Bad parameters to mpg123_index() and mpg123_set_index() */
	MPG123_OUT_OF_SYNC,		/**< Lost track in bytestream and did not try to resync. */
	MPG123_RESYNC_FAIL,		/**< Resync failed to find valid MPEG data. */
	MPG123_NO_8BIT,		/**< No 8bit encoding possible. */
	MPG123_BAD_ALIGN,		/**< Stack aligmnent error */
	MPG123_NULL_BUFFER,		/**< NULL input buffer with non-zero size... */
	MPG123_NO_RELSEEK,		/**< Relative seek not possible (screwed up file offset) */
	MPG123_NULL_POINTER,	/**< You gave a null pointer somewhere where you shouldn't have. */
	MPG123_BAD_KEY,		/**< Bad key value given. */
	MPG123_NO_INDEX,		/**< No frame index in this build. */
	MPG123_INDEX_FAIL,		/**< Something with frame index went wrong. */
	MPG123_BAD_DECODER_SETUP,	/**< Something prevents a proper decoder setup */
	MPG123_MISSING_FEATURE,	/**< This feature has not been built into libmpg123. */
	MPG123_BAD_VALUE,		/**< A bad value has been given, somewhere. */
	MPG123_LSEEK_FAILED,	/**< Low-level seek failed. */
	MPG123_BAD_CUSTOM_IO,	/**< Custom I/O not prepared. */
	MPG123_LFS_OVERFLOW,	/**< Offset value overflow during translation of large file API calls -- your client program cannot handle that large file. */
	MPG123_INT_OVERFLOW		/**< Some integer overflow. */
};

// enumeration of the parameters types that it is possible to set/get.
enum mpg123_parms
{
	MPG123_VERBOSE = 0,		/**< set verbosity value for enabling messages to stderr, >= 0 makes sense (integer) */
	MPG123_FLAGS,		/**< set all flags, p.ex val = MPG123_GAPLESS|MPG123_MONO_MIX (integer) */
	MPG123_ADD_FLAGS,		/**< add some flags (integer) */
	MPG123_FORCE_RATE,		/**< when value > 0, force output rate to that value (integer) */
	MPG123_DOWN_SAMPLE,		/**< 0=native rate, 1=half rate, 2=quarter rate (integer) */
	MPG123_RVA,		/**< one of the RVA choices above (integer) */
	MPG123_DOWNSPEED,		/**< play a frame N times (integer) */
	MPG123_UPSPEED,		/**< play every Nth frame (integer) */
	MPG123_START_FRAME,		/**< start with this frame (skip frames before that, integer) */ 
	MPG123_DECODE_FRAMES,	/**< decode only this number of frames (integer) */
	MPG123_OUTSCALE,		/**< the scale for output samples (amplitude - integer according to mpg123 output format) */
	MPG123_TIMEOUT,		/**< timeout for reading from a stream (not supported on win32, integer) */
	MPG123_REMOVE_FLAGS,	/**< remove some flags (inverse of MPG123_ADD_FLAGS, integer) */
	MPG123_RESYNC_LIMIT,	/**< Try resync on frame parsing for that many bytes or until end of stream (<0 ... integer). This can enlarge the limit for skipping junk on beginning, too (but not reduce it).  */
	MPG123_INDEX_SIZE,		/**< Set the frame index size (if supported). Values <0 mean that the index is allowed to grow dynamically in these steps (in positive direction, of course) -- Use this when you really want a full index with every individual frame. */
	MPG123_PREFRAMES,		/**< Decode/ignore that many frames in advance for layer 3. This is needed to fill bit reservoir after seeking, for example (but also at least one frame in advance is needed to have all "normal" data for layer 3). Give a positive integer value, please.*/
	MPG123_FEEDPOOL,		/**< For feeder mode, keep that many buffers in a pool to avoid frequent malloc/free. The pool is allocated on mpg123_open_feed(). If you change this parameter afterwards, you can trigger growth and shrinkage during decoding. The default value could change any time. If you care about this, then set it. (integer) */
	MPG123_FEEDBUFFER,		/**< Minimal size of one internal feeder buffer, again, the default value is subject to change. (integer) */
};

// flag bits for MPG123_FLAGS, use the usual binary or to combine.
enum mpg123_param_flags
{
	MPG123_FORCE_MONO = 0x7,		/**<     0111 Force some mono mode: This is a test bitmask for seeing if any mono forcing is active. */
	MPG123_MONO_LEFT = 0x1,		/**<     0001 Force playback of left channel only.  */
	MPG123_MONO_RIGHT = 0x2,		/**<     0010 Force playback of right channel only. */
	MPG123_MONO_MIX = 0x4,		/**<     0100 Force playback of mixed mono.         */
	MPG123_FORCE_STEREO = 0x8,		/**<     1000 Force stereo output.                  */
	MPG123_QUIET = 0x20,		/**< 00100000 Suppress any printouts (overrules verbose).                    */
	MPG123_GAPLESS = 0x40,		/**< 01000000 Enable gapless decoding (default on if libmpg123 has support). */
	MPG123_NO_RESYNC = 0x80,		/**< 10000000 Disable resync stream after error.                             */
	MPG123_SEEKBUFFER = 0x100,		/**< 000100000000 Enable small buffer on non-seekable streams to allow some peek-ahead (for better MPEG sync). */
	MPG123_FUZZY = 0x200,		/**< 001000000000 Enable fuzzy seeks (guessing byte offsets or using approximate seek points from Xing TOC) */
	MPG123_IGNORE_STREAMLENGTH = 0x1000,	/**< 1000000000000 Ignore any stream length information contained in the stream, which can be contained in a 'TLEN' frame of an ID3v2 tag or a Xing tag */
	MPG123_IGNORE_INFOFRAME = 0x4000,	/**< 100 0000 0000 0000 Do not parse the LAME/Xing info frame, treat it as normal MPEG data. */
	MPG123_AUTO_RESAMPLE = 0x8000,	/**< 1000 0000 0000 0000 Allow automatic internal resampling of any kind (default on if supported). Especially when going lowlevel with replacing output buffer, you might want to unset this flag. Setting MPG123_DOWNSAMPLE or MPG123_FORCE_RATE will override this. */
};

// choices for MPG123_RVA
enum mpg123_param_rva
{
	MPG123_RVA_OFF   = 0,		/**< RVA disabled (default).   */
	MPG123_RVA_MIX   = 1,		/**< Use mix/track/radio gain. */
	MPG123_RVA_ALBUM = 2,		/**< Use album/audiophile gain */
	MPG123_RVA_MAX   = MPG123_RVA_ALBUM,	/**< The maximum RVA code, may increase in future. */
};

enum frame_state_flags
{
	FRAME_ACCURATE = 0x1,		/**<     0001 Positions are considered accurate. */
	FRAME_FRANKENSTEIN = 0x2,		/**<     0010 This stream is concatenated. */
	FRAME_FRESH_DECODER = 0x4,		/**<     0100 Decoder is fleshly initialized. */
};

// enumeration of the mode types of Variable Bitrate
enum mpg123_vbr
{
	MPG123_CBR = 0,			/**< Constant Bitrate Mode (default) */
	MPG123_VBR,			/**< Variable Bitrate Mode */
	MPG123_ABR			/**< Average Bitrate Mode */
};

// Data structure for ID3v1 tags (the last 128 bytes of a file).
// Don't take anything for granted (like string termination)!
// Also note the change ID3v1.1 did: comment[28] = 0; comment[29] = track_number
// It is your task to support ID3v1 only or ID3v1.1 ...
typedef struct
{
	char	tag[3];			/**< Always the string "TAG", the classic intro. */
	char	title[30];		/**< Title string.  */
	char	artist[30];		/**< Artist string. */
	char	album[30];		/**< Album string. */
	char	year[4];			/**< Year string. */
	char	comment[30];		/**< Comment string. */
	byte	genre;			/**< Genre index. */
} mpg123_id3v1;

#define MPG123_ID3		0x3		/**< 0011 There is some ID3 info. Also matches 0010 or NEW_ID3. */
#define MPG123_NEW_ID3	0x1		/**< 0001 There is ID3 info that changed since last call to mpg123_id3. */

struct mpg123_handle_s
{
	int		fresh;		// to be moved into flags
	int		new_format;
	float		hybrid_block[2][2][SBLIMIT*SSLIMIT];
	int		hybrid_blc[2];

	// the scratch vars for the decoders, sometimes float, sometimes short... sometimes int/long
	short		*short_buffs[2][2];
	float		*float_buffs[2][2];
	byte		*rawbuffs;
	int		rawbuffss;
	int		bo;		// just have it always here.
	byte		*rawdecwin;	// the block with all decwins
	
	int		rawdecwins;	// size of rawdecwin memory
	float		*decwin;		// _the_ decode table

	// for halfspeed mode
	byte		ssave[34];
	int		halfphase;

	// layer3
	int		longLimit[9][23];
	int		shortLimit[9][14];
	float		gainpow2[256+118+4];// not floatly dynamic, just different for mmx

	synth_t		synths;
	int		verbose;		// 0: nothing, 1: just print chosen decoder, 2: be verbose

	const al_table_t	*alloc;

	// the runtime-chosen decoding, based on input and output format
	func_synth	synth;
	func_synth_stereo	synth_stereo;
	func_synth_mono	synth_mono;

	// yes, this function is runtime-switched, too.
	void (*make_decode_tables)( mpg123_handle_t *fr ); // that is the volume control.

	int		stereo;		// I _think_ 1 for mono and 2 for stereo
	int		jsbound;

	int		single;
	int		II_sblimit;
	int		down_sample_sblimit;
	int		lsf;		// 0: MPEG 1.0; 1: MPEG 2.0/2.5 -- both used as bool and array index!

	// many flags in disguise as integers... wasting bytes.
	int		mpeg25;
	int		down_sample;
	int		header_change;
	int		lay;
	long		spf;		// cached count of samples per frame

	int (*do_layer)( mpg123_handle_t* );

	int		error_protection;
	int		bitrate_index;
	int		sampling_frequency;
	int		padding;
	int		extension;
	int		mode;
	int		mode_ext;
	int		copyright;
	int		original;
	int		emphasis;
	int		framesize;	// computed framesize
	int		freesize;		// free format frame size
	int		vbr;		// 1 if variable bitrate was detected
	mpg_off_t		num;		// frame offset ...
	mpg_off_t		input_offset;	// byte offset of this frame in input stream
	mpg_off_t		playnum;		// playback offset... includes repetitions, reset at seeks
	mpg_off_t		audio_start;	// The byte offset in the file where audio data begins.
	int		state_flags;
	char		silent_resync;	// Do not complain for the next n resyncs.
	byte		*xing_toc;	// The seek TOC from Xing header.
	int		freeformat;
	long		freeformat_framesize;

	// bitstream info; bsi
	int		bitindex;
	byte		*wordpointer;

	// temporary storage for getbits stuff
	ulong		ultmp;
	byte		uctmp;

	// rva data
	double		maxoutburst;	// the maximum amplitude in current sample represenation.
	double		lastscale;

	struct
	{
		int	level[2];
		float	gain[2];
		float	peak[2];
	} rva;

	// input data
	mpg_off_t		track_frames;
	mpg_off_t		track_samples;
	double		mean_framesize;
	mpg_off_t		mean_frames;
	int		fsizeold;
	int		ssize;

	uint		bitreservoir;
	byte		bsspace[2][MAXFRAMESIZE+512];
	byte		*bsbuf;
	byte		*bsbufold;
	int		bsnum;

	// that is the header matching the last read frame body.
	ulong		oldhead;

	// that is the header that is supposedly the first of the stream.
	ulong		firsthead;
	int		abr_rate;

	frame_index_t	index;

	// output data
	outbuffer_t	buffer;
	audioformat_t	af;

	int		own_buffer;
	size_t		outblock;		// number of bytes that this frame produces (upper bound)
	int		to_decode;	// this frame holds data to be decoded
	int		to_ignore;	// the same, somehow
	mpg_off_t		firstframe;	// start decoding from here
	mpg_off_t		lastframe;	// last frame to decode (for gapless or num_frames limit)
	mpg_off_t		ignoreframe;	// frames to decode but discard before firstframe

	mpg_off_t		gapless_frames;	// frame count for the gapless part
	mpg_off_t		firstoff;		// number of samples to ignore from firstframe
	mpg_off_t		lastoff;		// number of samples to use from lastframe
	mpg_off_t		begin_s;		// overall begin offset in samples
	mpg_off_t		begin_os;
	mpg_off_t		end_s;		// overall end offset in samples
	mpg_off_t		end_os;
	mpg_off_t		fullend_os;	// gapless_frames translated to output samples

	uint		crc;		// well, I need a safe 16bit type, actually. But wider doesn't hurt.

	reader_t		*rd;		// pointer to the reading functions
	reader_data_t	rdat;		// reader data and state info
	mpg123_parm_t	p;

	int		err;
	int		decoder_change;
	int		delayed_change;
	long		clip;

	// the meta crap
	int		metaflags;
	byte		id3buf[128];

	float		*layerscratch;

	// these are significant chunks of memory already...
	struct
	{
		float	(*hybrid_in)[SBLIMIT][SSLIMIT];  // ALIGNED(16) float hybridIn[2][SBLIMIT][SSLIMIT];
		float	(*hybrid_out)[SSLIMIT][SBLIMIT]; // ALIGNED(16) float hybridOut[2][SSLIMIT][SBLIMIT];
	} layer3;

	// a place for storing additional data for the large file wrapper. this is cruft!
	void		*wrapperdata;

	// a callback used to properly destruct the wrapper data.
	void (*wrapperclean)( void* );
};

//
// parse.c
//
void set_pointer( mpg123_handle_t *fr, long backstep );
int get_songlen( mpg123_handle_t *fr, int no );
double compute_bpf( mpg123_handle_t *fr );
long frame_freq( mpg123_handle_t *fr );
double mpg123_tpf( mpg123_handle_t *fr );
int mpg123_spf( mpg123_handle_t *mh );
int read_frame( mpg123_handle_t *fr );

//
// format.c
//
void invalidate_format( audioformat_t *af );
void postprocess_buffer( mpg123_handle_t *fr );
int frame_output_format( mpg123_handle_t *fr );
int mpg123_fmt_all( mpg123_parm_t *mp );
int mpg123_format_none( mpg123_handle_t *mh );
int mpg123_format_all( mpg123_handle_t *mh );
int mpg123_format( mpg123_handle_t *mh, long rate, int channels, int encodings );
mpg_off_t decoder_synth_bytes( mpg123_handle_t *fr, mpg_off_t s );
mpg_off_t bytes_to_samples( mpg123_handle_t *fr, mpg_off_t b );
mpg_off_t samples_to_bytes( mpg123_handle_t *fr, mpg_off_t s );
mpg_off_t outblock_bytes( mpg123_handle_t *fr, mpg_off_t s );

//
// layer3.c
//
extern float COS6_1;
extern float COS6_2;
extern float cos9[3];
extern float cos18[3];
extern float tfcos12[3];
extern float tfcos36[9];
void init_layer3( void );
void init_layer3_stuff( mpg123_handle_t *fr );
int do_layer3( mpg123_handle_t *fr );

//
// dct36.c
//
void dct36( float *inbuf, float *o1, float *o2, float *wintab, float *tsbuf );
void dct12( float *in, float *rawout1, float *rawout2, register float *wi, register float *ts );

//
// dct64.c
//
void dct64( float *out0, float *out1, float *samples );

//
// tabinit.c
//
extern float *pnts[];
void prepare_decode_tables( void );
void make_decode_tables( mpg123_handle_t *fr );

// begin prototypes
mpg123_handle_t *mpg123_new( int *error );
mpg123_handle_t *mpg123_parnew( mpg123_parm_t *mp, int *error );
int mpg123_param( mpg123_handle_t *mh, enum mpg123_parms key, long val );
int mpg123_open_handle( mpg123_handle_t *mh, void *iohandle );
int mpg123_replace_reader_handle( mpg123_handle_t *mh, mpg_ssize_t (*fread)(void*, void*, size_t), mpg_off_t (*lseek)(void*, mpg_off_t, int), void(*fclose)(void*));
int mpg123_decode( mpg123_handle_t *mh, const byte *inmemory, size_t inmemsize, byte *outmemory, size_t outmemsize, size_t *done );
int mpg123_getformat( mpg123_handle_t *mh, int *rate, int *channels, int *encoding );
int mpg123_read( mpg123_handle_t *mh, byte *out, size_t size, size_t *done );
mpg_off_t mpg123_seek( mpg123_handle_t *mh, mpg_off_t sampleoff, int whence );
int mpg123_feed( mpg123_handle_t *mh, const byte *in, size_t size );
const char *mpg123_plain_strerror( int errcode );
int mpg123_open_feed( mpg123_handle_t *mh );
void mpg123_delete( mpg123_handle_t *mh );
mpg_off_t mpg123_tell( mpg123_handle_t *mh );
int mpg123_init( void );
void mpg123_exit( void );

#endif//MPG123_H
