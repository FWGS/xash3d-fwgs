#pragma once
/*
========================================================================

internal image format

typically expanded to rgba buffer
NOTE: number at end of pixelformat name it's a total bitscount e.g. PF_RGB_24 == PF_RGB_888
========================================================================
*/
#define ImageRAW( type )	(type == PF_RGBA_32 || type == PF_BGRA_32 || type == PF_RGB_24 || type == PF_BGR_24 || type == PF_LUMINANCE)
#define ImageCompressed( type ) \
	(  type == PF_DXT1 \
	|| type == PF_DXT3 \
	|| type == PF_DXT5 \
	|| type == PF_ATI2 \
	|| type == PF_BC4_SIGNED \
	|| type == PF_BC4_UNSIGNED \
	|| type == PF_BC5_SIGNED \
	|| type == PF_BC5_UNSIGNED \
	|| type == PF_BC6H_SIGNED \
	|| type == PF_BC6H_UNSIGNED \
	|| type == PF_BC7_UNORM \
	|| type == PF_BC7_SRGB \
	|| type == PF_KTX2_RAW )

typedef enum
{
	PF_UNKNOWN = 0,
	PF_INDEXED_24,	// inflated palette (768 bytes)
	PF_INDEXED_32,	// deflated palette (1024 bytes)
	PF_RGBA_32,	// normal rgba buffer
	PF_BGRA_32,	// big endian RGBA (MacOS)
	PF_RGB_24,	// uncompressed dds or another 24-bit image
	PF_BGR_24,	// big-endian RGB (MacOS)
	PF_LUMINANCE,
	PF_DXT1,		// s3tc DXT1/BC1 format
	PF_DXT3,		// s3tc DXT3/BC2 format
	PF_DXT5,		// s3tc DXT5/BC3 format
	PF_ATI2,		// latc ATI2N/BC5 format
	PF_BC4_SIGNED,
	PF_BC4_UNSIGNED,
	PF_BC5_SIGNED,
	PF_BC5_UNSIGNED,
	PF_BC6H_SIGNED,	// bptc BC6H signed FP16 format
	PF_BC6H_UNSIGNED, // bptc BC6H unsigned FP16 format
	PF_BC7_UNORM,			// bptc BC7 format
	PF_BC7_SRGB,
	PF_KTX2_RAW, // Raw KTX2 data, used for yet unsupported KTX2 subformats
	PF_TOTALCOUNT,	// must be last
} pixformat_t;

typedef struct bpc_desc_s
{
	int	format;	// pixelformat
	char	name[16];	// used for debug
	uint	glFormat;	// RGBA format
	int	bpp;	// channels (e.g. rgb = 3, rgba = 4)
} bpc_desc_t;

// imagelib global settings
typedef enum
{
	IL_USE_LERPING	= BIT(0),	// lerping images during resample
	IL_KEEP_8BIT	= BIT(1),	// don't expand paletted images
	IL_ALLOW_OVERWRITE	= BIT(2),	// allow to overwrite stored images
	IL_DONTFLIP_TGA	= BIT(3),	// Steam background completely ignore tga attribute 0x20 (stupid lammers!)
	IL_DDS_HARDWARE	= BIT(4),	// DXT compression is support
	IL_LOAD_DECAL	= BIT(5),	// special mode for load gradient decals
	IL_OVERVIEW	= BIT(6),	// overview required some unque operations
	IL_LOAD_PLAYER_DECAL = BIT(7), // special mode for player decals
	IL_KTX2_RAW = BIT(8), // renderer can consume raw KTX2 files (e.g. ref_vk)
} ilFlags_t;

// goes into rgbdata_t->encode
#define DXT_ENCODE_DEFAULT		0	// don't use custom encoders
#define DXT_ENCODE_COLOR_YCoCg	0x1A01	// make sure that value dosn't collide with anything
#define DXT_ENCODE_ALPHA_1BIT		0x1A02	// normal 1-bit alpha
#define DXT_ENCODE_ALPHA_8BIT		0x1A03	// normal 8-bit alpha
#define DXT_ENCODE_ALPHA_SDF		0x1A04	// signed distance field
#define DXT_ENCODE_NORMAL_AG_ORTHO	0x1A05	// orthographic projection
#define DXT_ENCODE_NORMAL_AG_STEREO	0x1A06	// stereographic projection
#define DXT_ENCODE_NORMAL_AG_PARABOLOID	0x1A07	// paraboloid projection
#define DXT_ENCODE_NORMAL_AG_QUARTIC	0x1A08	// newton method
#define DXT_ENCODE_NORMAL_AG_AZIMUTHAL	0x1A09	// Lambert Azimuthal Equal-Area

// rgbdata output flags
typedef enum
{
	// rgbdata->flags
	IMAGE_CUBEMAP	= BIT(0),		// it's 6-sides cubemap buffer
	IMAGE_HAS_ALPHA	= BIT(1),		// image contain alpha-channel
	IMAGE_HAS_COLOR	= BIT(2),		// image contain RGB-channel
	IMAGE_COLORINDEX	= BIT(3),		// all colors in palette is gradients of last color (decals)
	IMAGE_HAS_LUMA	= BIT(4),		// image has luma pixels (q1-style maps)
	IMAGE_SKYBOX	= BIT(5),		// only used by FS_SaveImage - for write right suffixes
	IMAGE_QUAKESKY	= BIT(6),		// it's a quake sky double layered clouds (so keep it as 8 bit)
	IMAGE_DDS_FORMAT	= BIT(7),		// a hint for GL loader
	IMAGE_MULTILAYER	= BIT(8),		// to differentiate from 3D texture
	IMAGE_ONEBIT_ALPHA	= BIT(9),		// binary alpha
	IMAGE_QUAKEPAL	= BIT(10),	// image has quake1 palette

	// Image_Process manipulation flags
	IMAGE_FLIP_X	= BIT(16),	// flip the image by width
	IMAGE_FLIP_Y	= BIT(17),	// flip the image by height
	IMAGE_ROT_90	= BIT(18),	// flip from upper left corner to down right corner
	IMAGE_ROT180	= IMAGE_FLIP_X|IMAGE_FLIP_Y,
	IMAGE_ROT270	= IMAGE_FLIP_X|IMAGE_FLIP_Y|IMAGE_ROT_90,
// reserved
	IMAGE_RESAMPLE	= BIT(20),	// resample image to specified dims
// reserved
// reserved
	IMAGE_FORCE_RGBA	= BIT(23),	// force image to RGBA buffer
	IMAGE_MAKE_LUMA	= BIT(24),	// create luma texture from indexed
	IMAGE_QUANTIZE	= BIT(25),	// make indexed image from 24 or 32- bit image
	IMAGE_LIGHTGAMMA	= BIT(26),	// apply gamma for image
	IMAGE_REMAP	= BIT(27),	// interpret width and height as top and bottom color
} imgFlags_t;

typedef struct rgbdata_s
{
	word	width;		// image width
	word	height;		// image height
	word	depth;		// image depth
	uint	type;		// compression type
	uint	flags;		// misc image flags
	word	encode;		// DXT may have custom encoder, that will be decoded in GLSL-side
	byte	numMips;		// mipmap count
	byte	*palette;		// palette if present
	byte	*buffer;		// image buffer
	rgba_t	fogParams;	// some water textures in hl1 has info about fog color and alpha
	size_t	size;		// for bounds checking
} rgbdata_t;

