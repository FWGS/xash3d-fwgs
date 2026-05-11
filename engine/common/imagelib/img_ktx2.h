/*
img_ktx2.h - ktx2 format reference
Copyright (C) 2023 Provod

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef IMG_KTX2_H
#define IMG_KTX2_H

#include <stdint.h>

#define KTX2_IDENTIFIER_SIZE 12
#define KTX2_IDENTIFIER "\xABKTX 20\xBB\r\n\x1A\n"

/*
static const char k_ktx2_identifier[KTX2_IDENTIFIER_SIZE] =
{
  '\xAB', 'K', 'T', 'X', ' ', '2', '0', '\xBB', '\r', '\n', '\x1A', '\n'
};
*/

typedef struct
{
	uint32_t vkFormat;
	uint32_t typeSize;
	uint32_t pixelWidth;
	uint32_t pixelHeight;
	uint32_t pixelDepth;
	uint32_t layerCount;
	uint32_t faceCount;
	uint32_t levelCount;
	uint32_t supercompressionScheme;
} ktx2_header_t;

typedef struct
{
	uint32_t dfdByteOffset;
	uint32_t dfdByteLength;
	uint32_t kvdByteOffset;
	uint32_t kvdByteLength;
	uint64_t sgdByteOffset;
	uint64_t sgdByteLength;
} ktx2_index_t;

typedef struct
{
	uint64_t byteOffset;
	uint64_t byteLength;
	uint64_t uncompressedByteLength;
} ktx2_level_t;

#define KTX2_LEVELS_OFFSET ( KTX2_IDENTIFIER_SIZE + sizeof( ktx2_header_t ) + sizeof( ktx2_index_t ))

#define KTX2_MINIMAL_HEADER_SIZE ( KTX2_LEVELS_OFFSET + sizeof( ktx2_level_t ))

// These have the same values as enum VkFormat in vulkan_core.h
// There are hundreds of formats which can be contained in KTX2.
// Below are listed the ones which are supported here. This list can be extended.
typedef enum
{
	KTX2_FORMAT_BC4_UNORM_BLOCK = 139,
	KTX2_FORMAT_BC4_SNORM_BLOCK = 140,
	KTX2_FORMAT_BC5_UNORM_BLOCK = 141,
	KTX2_FORMAT_BC5_SNORM_BLOCK = 142,
	KTX2_FORMAT_BC6H_UFLOAT_BLOCK = 143,
	KTX2_FORMAT_BC6H_SFLOAT_BLOCK = 144,
	KTX2_FORMAT_BC7_UNORM_BLOCK = 145,
	KTX2_FORMAT_BC7_SRGB_BLOCK = 146,
} ktx2_format_t;

#endif // IMG_KTX2_H
