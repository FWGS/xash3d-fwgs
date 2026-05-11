/*
img_dds.h - dds format reference
Copyright (C) 2015 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef IMG_DDS_H
#define IMG_DDS_H
/*
========================================================================

.DDS image format

========================================================================
*/
#define DDSHEADER	((' '<<24)+('S'<<16)+('D'<<8)+'D') // little-endian "DDS "

// various four-cc types
#define TYPE_DXT1	(('1'<<24)+('T'<<16)+('X'<<8)+'D') // little-endian "DXT1"
#define TYPE_DXT2	(('2'<<24)+('T'<<16)+('X'<<8)+'D') // little-endian "DXT2"
#define TYPE_DXT3	(('3'<<24)+('T'<<16)+('X'<<8)+'D') // little-endian "DXT3"
#define TYPE_DXT4	(('4'<<24)+('T'<<16)+('X'<<8)+'D') // little-endian "DXT4"
#define TYPE_DXT5	(('5'<<24)+('T'<<16)+('X'<<8)+'D') // little-endian "DXT5"
#define TYPE_DX10	(('0'<<24)+('1'<<16)+('X'<<8)+'D') // little-endian "DX10"
#define TYPE_ATI1	(('1'<<24)+('I'<<16)+('T'<<8)+'A') // little-endian "ATI1"
#define TYPE_ATI2	(('2'<<24)+('I'<<16)+('T'<<8)+'A') // little-endian "ATI2"
#define TYPE_BC5S	(('S'<<24)+('5'<<16)+('C'<<8)+'B') // little-endian "BC5S"
#define TYPE_BC4S	(('S'<<24)+('4'<<16)+('C'<<8)+'B') // little-endian "BC4S"
#define TYPE_BC4U	(('U'<<24)+('4'<<16)+('C'<<8)+'B') // little-endian "BC4U"
#define TYPE_RXGB	(('B'<<24)+('G'<<16)+('X'<<8)+'R') // little-endian "RXGB" doom3 normalmaps
#define TYPE_$	(('\0'<<24)+('\0'<<16)+('\0'<<8)+'$') // little-endian "$"
#define TYPE_o	(('\0'<<24)+('\0'<<16)+('\0'<<8)+'o') // little-endian "o"
#define TYPE_p	(('\0'<<24)+('\0'<<16)+('\0'<<8)+'p') // little-endian "p"
#define TYPE_q	(('\0'<<24)+('\0'<<16)+('\0'<<8)+'q') // little-endian "q"
#define TYPE_r	(('\0'<<24)+('\0'<<16)+('\0'<<8)+'r') // little-endian "r"
#define TYPE_s	(('\0'<<24)+('\0'<<16)+('\0'<<8)+'s') // little-endian "s"
#define TYPE_t	(('\0'<<24)+('\0'<<16)+('\0'<<8)+'t') // little-endian "t"

// dwFlags1
#define DDS_CAPS				0x00000001L
#define DDS_HEIGHT				0x00000002L
#define DDS_WIDTH				0x00000004L
#define DDS_PITCH				0x00000008L
#define DDS_PIXELFORMAT			0x00001000L
#define DDS_MIPMAPCOUNT			0x00020000L
#define DDS_LINEARSIZE			0x00080000L
#define DDS_DEPTH				0x00800000L

// dwFlags2
#define DDS_ALPHAPIXELS			0x00000001L
#define DDS_ALPHA				0x00000002L
#define DDS_FOURCC				0x00000004L
#define DDS_RGB				0x00000040L
#define DDS_RGBA				0x00000041L	// (DDS_RGB|DDS_ALPHAPIXELS)
#define DDS_LUMINANCE			0x00020000L
#define DDS_DUDV				0x00080000L

// dwCaps1
#define DDS_COMPLEX				0x00000008L
#define DDS_TEXTURE				0x00001000L
#define DDS_MIPMAP				0x00400000L

// dwCaps2
#define DDS_CUBEMAP				0x00000200L
#define DDS_CUBEMAP_POSITIVEX			0x00000400L
#define DDS_CUBEMAP_NEGATIVEX			0x00000800L
#define DDS_CUBEMAP_POSITIVEY			0x00001000L
#define DDS_CUBEMAP_NEGATIVEY			0x00002000L
#define DDS_CUBEMAP_POSITIVEZ			0x00004000L
#define DDS_CUBEMAP_NEGATIVEZ			0x00008000L
#define DDS_CUBEMAP_ALL_SIDES			0x0000FC00L
#define DDS_VOLUME				0x00200000L

typedef enum
{
	DXGI_FORMAT_UNKNOWN = 0,
	DXGI_FORMAT_R32G32B32A32_TYPELESS = 1,
	DXGI_FORMAT_R32G32B32A32_FLOAT = 2,
	DXGI_FORMAT_R32G32B32A32_UINT = 3,
	DXGI_FORMAT_R32G32B32A32_SINT = 4,
	DXGI_FORMAT_R32G32B32_TYPELESS = 5,
	DXGI_FORMAT_R32G32B32_FLOAT = 6,
	DXGI_FORMAT_R32G32B32_UINT = 7,
	DXGI_FORMAT_R32G32B32_SINT = 8,
	DXGI_FORMAT_R16G16B16A16_TYPELESS = 9,
	DXGI_FORMAT_R16G16B16A16_FLOAT = 10,
	DXGI_FORMAT_R16G16B16A16_UNORM = 11,
	DXGI_FORMAT_R16G16B16A16_UINT = 12,
	DXGI_FORMAT_R16G16B16A16_SNORM = 13,
	DXGI_FORMAT_R16G16B16A16_SINT = 14,
	DXGI_FORMAT_R32G32_TYPELESS = 15,
	DXGI_FORMAT_R32G32_FLOAT = 16,
	DXGI_FORMAT_R32G32_UINT = 17,
	DXGI_FORMAT_R32G32_SINT = 18,
	DXGI_FORMAT_R32G8X24_TYPELESS = 19,
	DXGI_FORMAT_D32_FLOAT_S8X24_UINT = 20,
	DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS = 21,
	DXGI_FORMAT_X32_TYPELESS_G8X24_UINT = 22,
	DXGI_FORMAT_R10G10B10A2_TYPELESS = 23,
	DXGI_FORMAT_R10G10B10A2_UNORM = 24,
	DXGI_FORMAT_R10G10B10A2_UINT = 25,
	DXGI_FORMAT_R11G11B10_FLOAT = 26,
	DXGI_FORMAT_R8G8B8A8_TYPELESS = 27,
	DXGI_FORMAT_R8G8B8A8_UNORM = 28,
	DXGI_FORMAT_R8G8B8A8_UNORM_SRGB = 29,
	DXGI_FORMAT_R8G8B8A8_UINT = 30,
	DXGI_FORMAT_R8G8B8A8_SNORM = 31,
	DXGI_FORMAT_R8G8B8A8_SINT = 32,
	DXGI_FORMAT_R16G16_TYPELESS = 33,
	DXGI_FORMAT_R16G16_FLOAT = 34,
	DXGI_FORMAT_R16G16_UNORM = 35,
	DXGI_FORMAT_R16G16_UINT = 36,
	DXGI_FORMAT_R16G16_SNORM = 37,
	DXGI_FORMAT_R16G16_SINT = 38,
	DXGI_FORMAT_R32_TYPELESS = 39,
	DXGI_FORMAT_D32_FLOAT = 40,
	DXGI_FORMAT_R32_FLOAT = 41,
	DXGI_FORMAT_R32_UINT = 42,
	DXGI_FORMAT_R32_SINT = 43,
	DXGI_FORMAT_R24G8_TYPELESS = 44,
	DXGI_FORMAT_D24_UNORM_S8_UINT = 45,
	DXGI_FORMAT_R24_UNORM_X8_TYPELESS = 46,
	DXGI_FORMAT_X24_TYPELESS_G8_UINT = 47,
	DXGI_FORMAT_R8G8_TYPELESS = 48,
	DXGI_FORMAT_R8G8_UNORM = 49,
	DXGI_FORMAT_R8G8_UINT = 50,
	DXGI_FORMAT_R8G8_SNORM = 51,
	DXGI_FORMAT_R8G8_SINT = 52,
	DXGI_FORMAT_R16_TYPELESS = 53,
	DXGI_FORMAT_R16_FLOAT = 54,
	DXGI_FORMAT_D16_UNORM = 55,
	DXGI_FORMAT_R16_UNORM = 56,
	DXGI_FORMAT_R16_UINT = 57,
	DXGI_FORMAT_R16_SNORM = 58,
	DXGI_FORMAT_R16_SINT = 59,
	DXGI_FORMAT_R8_TYPELESS = 60,
	DXGI_FORMAT_R8_UNORM = 61,
	DXGI_FORMAT_R8_UINT = 62,
	DXGI_FORMAT_R8_SNORM = 63,
	DXGI_FORMAT_R8_SINT = 64,
	DXGI_FORMAT_A8_UNORM = 65,
	DXGI_FORMAT_R1_UNORM = 66,
	DXGI_FORMAT_R9G9B9E5_SHAREDEXP = 67,
	DXGI_FORMAT_R8G8_B8G8_UNORM = 68,
	DXGI_FORMAT_G8R8_G8B8_UNORM = 69,
	DXGI_FORMAT_BC1_TYPELESS = 70,
	DXGI_FORMAT_BC1_UNORM = 71,
	DXGI_FORMAT_BC1_UNORM_SRGB = 72,
	DXGI_FORMAT_BC2_TYPELESS = 73,
	DXGI_FORMAT_BC2_UNORM = 74,
	DXGI_FORMAT_BC2_UNORM_SRGB = 75,
	DXGI_FORMAT_BC3_TYPELESS = 76,
	DXGI_FORMAT_BC3_UNORM = 77,
	DXGI_FORMAT_BC3_UNORM_SRGB = 78,
	DXGI_FORMAT_BC4_TYPELESS = 79,
	DXGI_FORMAT_BC4_UNORM = 80,
	DXGI_FORMAT_BC4_SNORM = 81,
	DXGI_FORMAT_BC5_TYPELESS = 82,
	DXGI_FORMAT_BC5_UNORM = 83,
	DXGI_FORMAT_BC5_SNORM = 84,
	DXGI_FORMAT_B5G6R5_UNORM = 85,
	DXGI_FORMAT_B5G5R5A1_UNORM = 86,
	DXGI_FORMAT_B8G8R8A8_UNORM = 87,
	DXGI_FORMAT_B8G8R8X8_UNORM = 88,
	DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM = 89,
	DXGI_FORMAT_B8G8R8A8_TYPELESS = 90,
	DXGI_FORMAT_B8G8R8A8_UNORM_SRGB = 91,
	DXGI_FORMAT_B8G8R8X8_TYPELESS = 92,
	DXGI_FORMAT_B8G8R8X8_UNORM_SRGB = 93,
	DXGI_FORMAT_BC6H_TYPELESS = 94,
	DXGI_FORMAT_BC6H_UF16 = 95,
	DXGI_FORMAT_BC6H_SF16 = 96,
	DXGI_FORMAT_BC7_TYPELESS = 97,
	DXGI_FORMAT_BC7_UNORM = 98,
	DXGI_FORMAT_BC7_UNORM_SRGB = 99,
	DXGI_FORMAT_AYUV = 100,
	DXGI_FORMAT_Y410 = 101,
	DXGI_FORMAT_Y416 = 102,
	DXGI_FORMAT_NV12 = 103,
	DXGI_FORMAT_P010 = 104,
	DXGI_FORMAT_P016 = 105,
	DXGI_FORMAT_420_OPAQUE = 106,
	DXGI_FORMAT_YUY2 = 107,
	DXGI_FORMAT_Y210 = 108,
	DXGI_FORMAT_Y216 = 109,
	DXGI_FORMAT_NV11 = 110,
	DXGI_FORMAT_AI44 = 111,
	DXGI_FORMAT_IA44 = 112,
	DXGI_FORMAT_P8 = 113,
	DXGI_FORMAT_A8P8 = 114,
	DXGI_FORMAT_B4G4R4A4_UNORM = 115,
	DXGI_FORMAT_P208 = 130,
	DXGI_FORMAT_V208 = 131,
	DXGI_FORMAT_V408 = 132,
	DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE,
	DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE,
	DXGI_FORMAT_FORCE_UINT = 0xffffffff
} dxgi_format_t;

typedef enum
{
	D3D10_RESOURCE_DIMENSION_UNKNOWN = 0,
	D3D10_RESOURCE_DIMENSION_BUFFER = 1,
	D3D10_RESOURCE_DIMENSION_TEXTURE1D = 2,
	D3D10_RESOURCE_DIMENSION_TEXTURE2D = 3,
	D3D10_RESOURCE_DIMENSION_TEXTURE3D = 4
} dds_resource_dimension_t;

typedef struct dds_pf_s
{
	uint32_t    dwSize;
	uint32_t    dwFlags;
	uint32_t    dwFourCC;
	uint32_t    dwRGBBitCount;
	uint32_t    dwRBitMask;
	uint32_t    dwGBitMask;
	uint32_t    dwBBitMask;
	uint32_t    dwABitMask;
} dds_pixf_t;

//  DDCAPS2
typedef struct dds_caps_s
{
	uint32_t    dwCaps1;
	uint32_t    dwCaps2;
	uint32_t    dwCaps3;			// currently unused
	uint32_t    dwCaps4;			// currently unused
} dds_caps_t;

typedef struct dds_header_dxt10_s
{
	dxgi_format_t dxgiFormat;
	dds_resource_dimension_t resourceDimension;
	uint32_t    miscFlag;
	uint32_t    arraySize;
	uint32_t    miscFlags2;
} dds_header_dxt10_t;

typedef struct dds_s
{
	uint32_t    dwIdent;		// must matched with DDSHEADER
	uint32_t    dwSize;
	uint32_t    dwFlags;		// determines what fields are valid
	uint32_t    dwHeight;
	uint32_t    dwWidth;
	uint32_t    dwLinearSize;	// Formless late-allocated optimized surface size
	uint32_t    dwDepth;		// depth if a volume texture
	uint32_t    dwMipMapCount;	// number of mip-map levels requested
	uint32_t    dwAlphaBitDepth;	// depth of alpha buffer requested
	uint32_t    dwReserved1[10];	// reserved for future expansions
	dds_pixf_t  dsPixelFormat;
	dds_caps_t  dsCaps;
	uint32_t    dwTextureStage;
} dds_t;
#endif // IMG_DDS_H

