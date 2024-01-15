#include "img_vtf.h"
#include "imagelib.h"

typedef struct
{
	union
	{
		struct
		{
			word R : 5;
			word G : 6;
			word B : 5;
		};
		word val;
	} A;
	

	union
	{
		struct
		{
			word R : 5;
			word G : 6;
			word B : 5;
		};
		word val;
	} B;

	word lookup;
} DXT1;

typedef struct
{
	byte R;
	byte G;
	byte B;
} RGB;


qboolean Image_LoadVTF(const char* name, const byte* buffer, fs_offset_t filesize)
{
	VTFHeader_t* header = (VTFHeader_t*)buffer;
	DXT1* imgdata = (DXT1*)(buffer + header->header_size);
	image.width = header->width;
	image.height = header->height;
	image.palette = NULL;
	image.type = PF_DXT1;
	image.depth = 1;
	image.size = (((image.width + 3) >> 2) * ((image.height + 3) >> 2) * 8);
	image.rgba = Mem_Malloc(host.imagepool, image.size);
	memcpy(image.rgba, imgdata, image.size);
	return 1;
	/*
	RGB* img_rgba = (RGB*)image.rgba;
	
	for (int y = 0; y < image.height; y += 4)
	{
		for (int x = 0; x < image.width; x += 4)
		{
			if (imgdata->A.val > imgdata->B.val)
			{
				RGB c[4];

				c[0].R = ((dword)imgdata->A.R * 134217728) >> 24;
				c[0].G = ((dword)imgdata->A.G * 67108864) >> 24;
				c[0].B = ((dword)imgdata->A.B * 134217728) >> 24;

				c[1].R = ((dword)imgdata->B.R * 134217728) >> 24;
				c[1].G = ((dword)imgdata->B.G * 67108864) >> 24;
				c[1].B = ((dword)imgdata->B.B * 134217728) >> 24;

				c[2].R = (2 * 44739242 * (dword)imgdata->A.R + 44739242 * (dword)imgdata->B.R) >> 24;
				c[2].G = (2 * 22369621 * (dword)imgdata->A.G + 22369621 * (dword)imgdata->B.G) >> 24;
				c[2].B = (2 * 44739242 * (dword)imgdata->A.B + 44739242 * (dword)imgdata->B.B) >> 24;

				c[3].R = (44739242 * (dword)imgdata->A.R + 2 * 44739242 * (dword)imgdata->B.R) >> 24;
				c[3].G = (22369621 * (dword)imgdata->A.G + 2 * 22369621 * (dword)imgdata->B.G) >> 24;
				c[3].B = (44739242 * (dword)imgdata->A.B + 2 * 44739242 * (dword)imgdata->B.B) >> 24;

				for (int dy = 0; dy < 4; dy++)
				{
					for (int dx = 0; dx < 4; dx++)
					{
						img_rgba[(y * image.width + x + dy * image.width + dx)% (filesize/3)] = c[imgdata->lookup & (3 << (dx * 2 + dy * 8))];
					}
				}
			}
		}
	}
	*/
}
