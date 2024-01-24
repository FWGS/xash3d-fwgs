#include "img_vtf.h"
#include "imagelib.h"

#ifndef max
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#endif

qboolean Image_LoadVTF(const char* name, const byte* buffer, fs_offset_t filesize)
{
	VTFHeader_t* header = (VTFHeader_t*)buffer;
	byte* imgdata = (byte*)(buffer + header->header_size + header->lri_width*header->lri_height/2);
	//Con_Printf("%x %x %x %i %i\n", header->high_res_image_format, header->mipmap_count, header->lri_format, header->lri_width, header->lri_height);
	image.width = header->width;
	image.height = header->height;
	image.palette = NULL;
	switch (header->high_res_image_format)
	{
	case IMAGE_FORMAT_RGBA8888:
		image.type = PF_RGBA_32;
		break;
	case IMAGE_FORMAT_DXT5:
		image.type = PF_DXT5;
		break;
	case IMAGE_FORMAT_DXT1:
	default:
		image.type = PF_DXT1;
		break;
	}
	image.flags = TF_NOALPHA;
	image.depth = 1;
	dword offset = 0;
	//Con_Printf("%s\n", name);
	if (image.type == PF_DXT1)
	{
		for (int i = header->mipmap_count - 1; i > 0; i--)
		{
			int mipwidth = max(4, image.width >> i);
			int mipheight = max(4, image.height >> i);
			offset += mipwidth * mipheight/2;
		}
		image.size = image.width * image.height/2;
	}
	else if (image.type == PF_DXT5)
	{
		for (int i = header->mipmap_count - 1; i > 0; i--)
		{
			int mipwidth = max(4, image.width >> i);
			int mipheight = max(4, image.height >> i);
			offset += mipwidth * mipheight;
			//Con_Printf("%i %i %i %i\n", mipwidth, mipheight, ((mipwidth + 3) >> 2) * ((mipheight + 3) >> 2) * 16, offset);
		}
		image.size = image.width*image.height;
	}
	image.rgba = Mem_Malloc(host.imagepool, image.size);
	memcpy(image.rgba, imgdata + offset, image.size);
	return 1;
}
