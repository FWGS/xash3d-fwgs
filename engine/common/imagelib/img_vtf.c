#include "img_vtf.h"
#include "imagelib.h"


qboolean Image_LoadVTF(const char* name, const byte* buffer, fs_offset_t filesize)
{
	VTFHeader_t* header = (VTFHeader_t*)buffer;
	byte* imgdata = (byte*)(buffer + header->header_size);
	image.width = header->width;
	image.height = header->height;
	image.palette = NULL;
	switch (header->high_res_image_format)
	{
	case IMAGE_FORMAT_RGBA8888:
		image.type = PF_RGBA_32;
	case IMAGE_FORMAT_DXT5:
		image.type = PF_DXT5;
	case IMAGE_FORMAT_DXT1:
	default:
		image.type = PF_DXT1;
	}
	
	image.depth = 1;
	dword offset = 0;
	for (int i = 0; i < header->mipmap_count-1; i++)
	{
		offset += ((((image.width >> (header->mipmap_count - i - 1 )) + 3) >> 2) * (((image.height >> (header->mipmap_count - i - 1)) + 3) >> 2) * 8)+16;
	}
	offset -= 16;
	image.size = (((image.width + 3) >> 2) * ((image.height + 3) >> 2) * 8);
	image.rgba = Mem_Malloc(host.imagepool, image.size);
	memcpy(image.rgba, imgdata + offset, image.size);
	return 1;
}
