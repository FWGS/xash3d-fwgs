#include <common/system.h>
#include <common/common.h>

#include "xash3d_mathlib.h"

#include "texture.h"


////////////////////////////////////////////////////////////////////////////////
// Defines, macros

#define MAX_TEXTURES	    (8192)
#define TEXTURES_HASH_SIZE	(MAX_TEXTURES >> 2)

#define MAX_NAME_LEN        (256)


////////////////////////////////////////////////////////////////////////////////
// Structs

typedef struct rm_texture_s {
	qboolean used;
	
	char name[MAX_NAME_LEN];

	rgbdata_t* picture;
	int width;
	int height;
	
	int flags;

	uint number;

	struct rm_texture_s *next_same_hash;
} rm_texture_t;


////////////////////////////////////////////////////////////////////////////////
// Global state

static struct {
	rm_texture_t  array[MAX_TEXTURES];
	rm_texture_t* hash_table[TEXTURES_HASH_SIZE];
	uint       count;

	ref_interface_t* ref;
} Textures;


////////////////////////////////////////////////////////////////////////////////
// Local

static byte dot_texture[8][8] =
{
	{0,1,1,0,0,0,0,0},
	{1,1,1,1,0,0,0,0},
	{1,1,1,1,0,0,0,0},
	{0,1,1,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},
};

static size_t CalcImageSize( pixformat_t format, int width, int height, int depth );
static qboolean IsValidTextureName( const char* name );
static rm_texture_t* GetTextureByName( const char* name );
static rm_texture_t* AppendTexture( const char* name, int flags );
static void ProcessFlags( rm_texture_t* tex, rgbdata_t* pic );
static qboolean UploadTexture( rm_texture_t* texture, qboolean update );
static void RemoveTexture( const char* name );

static void CreateUnusedEntry( void );

static void CreateInternalTextures( void );

static rgbdata_t* FakeImage( int width, int height, int depth, int flags );
static int CreateEmoTexture( void );
static int CreateParticleTexture( void );
static int CreateStaticColoredTexture( const char* name, uint32_t color );
static int CreateCinematicDummyTexture( void );
static int CreateDlightTexture( void );


////////////////////////////////////////////////////////////////////////////////
// Public methods

void RM_Init()
{
	memset( &Textures, 0, sizeof( Textures ));
	
	CreateUnusedEntry();

	CreateInternalTextures();
}

void RM_SetRender( ref_interface_t* ref )
{
	Textures.ref = ref;

	RM_ReuploadTextures();
}

void RM_ReuploadTextures()
{
	rm_texture_t* texture;

	if( !Textures.ref )
	{
		Con_Reportf( S_ERROR "Render not found\n" );
		return;
	}

	// For all textures
	for( int i = 1; i < Textures.count; i++ )
	{
		texture = &(Textures.array[i]); 

		if( !texture->used )
			continue;
		
		if( texture->picture != NULL )
			UploadTexture( texture, false );
		else
			Con_Reportf( S_ERROR "Reupload without early saved picture is not supported now\n" );
	}
}

int RM_LoadTexture( const char* name, const byte* buf, size_t size, int flags )
{
	rgbdata_t* picture;
	rm_texture_t* texture;
	uint       picFlags;

	Con_Reportf( "RM_LoadTexture. Name %s\n", name );

	// Check name
	if( !IsValidTextureName( name ))
	{
		Con_Reportf( "Invalid texture name %s\n", name );
		return 0;
	}

	// Check cache
	if(( texture = GetTextureByName( name )))
	{
		//Con_Reportf( "Texture %s is already loaded with number %d\n", name, texture->number );
		return texture->number;
	}

	// Bit manipulation
	if( FBitSet( flags, TF_NOFLIP_TGA ))
		SetBits( picFlags, IL_DONTFLIP_TGA );

	if( FBitSet( flags, TF_KEEP_SOURCE ) && !FBitSet( flags, TF_EXPAND_SOURCE ))
		SetBits( picFlags, IL_KEEP_8BIT );

	Image_SetForceFlags( picFlags );

	// Load image using engine
	picture = FS_LoadImage( name, buf, size );
	if( !picture ) 
	{
		Con_Reportf( "Failed to load texture. FS_LoadImage returns NULL\n" );
		return 0;
	}

	// Allocate texture
	texture = AppendTexture( name, flags );
	texture->picture = picture;
	texture->width   = picture->width;
	texture->height  = picture->height;

	ProcessFlags(texture, picture);

	// And upload texture
	// FIXME: Handle error
	UploadTexture( texture, false );

	return texture->number;
}

int RM_LoadTextureArray( const char** names, int flags )
{
	uint layers_count;
	char name[MAX_NAME_LEN];
	char basename[MAX_NAME_LEN];
	rm_texture_t* texture;
	rgbdata_t* picture;
	int texnum;

	// Validate arguments
	if( !names || !names[0] )
	{
		Con_Reportf( "Empty textures names array\n" );
		return 0;
	}

	// Count layers
	for( size_t i = 0; i < *names[i] != '\0'; i++ )
		layers_count++;

	if( layers_count == 0 )
		return 0;

	// Сreate complexname from layer names
	for( size_t i = 0; i < layers_count; i++ )
	{
		COM_FileBase( names[i], &basename );
		Q_strncat( &name, &basename, MAX_NAME_LEN );
		if( i != ( layers_count - 1 )) Q_strncat( &name, "|", MAX_NAME_LEN);
	}

	Q_strncat( &name, va( "[%i]", layers_count ), MAX_NAME_LEN );

	// Validate texture name
	if( !IsValidTextureName( name ))
	{
		//Con_Reportf( "Invalid texture name %s\n", name );
		return 0;
	}

	// Check cache
	if(( texture = GetTextureByName( name )))
	{
		//Con_Reportf( "Texture %s is already loaded with number %d\n", name, texture->number );
		return texture->number;
	}

	// Load all the images and pack it into single image
	for( size_t i = 0; i < layers_count; i++ )
	{
		rgbdata_t* src;
		size_t src_size, dst_size, mip_size;

		src = FS_LoadImage( names[i], NULL, 0 );
		if( !src )
		{
			//Con_Reportf( "Failed to load texture layer %s\n", names[i] );
			break;
		}

		// Create new image for all layers
		if( !picture )
		{
			picture = malloc( sizeof( rgbdata_t ));
			memcpy( picture, src, sizeof( rgbdata_t ));

			picture->buffer = malloc( picture->size * layers_count );
			picture->depth = 0;
		}
		else
		{
			if( picture->type != src->type )
			{
				Con_Printf( S_ERROR "Mismatch image format for %s and %s\n", names[0], names[i] );
				break;
			}

			if( picture->numMips != src->numMips )
			{
				Con_Printf( S_ERROR "Mismatch mip count for %s and %s\n", names[0], names[i] );
				break;
			}

			if( picture->encode != src->encode )
			{
				Con_Printf( S_ERROR "Mismatch custom encoding for %s and %s\n", names[0], names[i] );
				break;
			}

			// Allow to rescale raw images
			if(
				ImageRAW( picture->type ) && ImageRAW( src->type ) &&
				( picture->width != src->width || picture->height != src->height )
			)
			{
				Image_Process( &src, picture->width, picture->height, IMAGE_RESAMPLE, 0.0f );
			}

			if( picture->size != src->size )
			{
				Con_Printf( S_ERROR "Mismatch image size for %s and %s\n", names[0], names[i] );
				break;
			}
		}

		mip_size = src_size = dst_size = 0;

		for( size_t j = 0; j < Q_max( 1, picture->numMips ); j++ )
		{
			int width  = Q_max( 1, ( picture->width >> j ));
			int height = Q_max( 1, ( picture->height >> j ));

			mip_size = CalcImageSize( picture->type, width, height, 1 );

			memcpy( picture->buffer + dst_size + mip_size * i, src->buffer + src_size, mip_size );
			dst_size += mip_size * layers_count;
			src_size += mip_size;
		}

		FS_FreeImage( src );

		// Increase layers
		picture->depth++;
	}

	// There were errors
	if( !picture || ( picture->depth != layers_count ))
	{
		Con_Printf( S_ERROR "Not all layers were loaded. Texture array is not created\n" );
		if( picture ) FS_FreeImage( picture );
		return 0;
	}

	// It's multilayer image
	SetBits( picture->flags, IMAGE_MULTILAYER );

	// Recalculate size considere layers_count
	picture->size *= layers_count;

	// Load texture
	texnum = RM_LoadTextureFromBuffer( name, picture, flags, false );

	// Release source texture
	FS_FreeImage( picture );

	// NOTE: always return texnum as index in array or engine will stop work !!!
	return texnum;
}

int RM_LoadTextureFromBuffer( const char* name, rgbdata_t* picture, int flags, qboolean update )
{
	rm_texture_t *texture;

	Con_Reportf( "RM_LoadTextureFromBuffer. Name %s\n", name );

	// Check name
	if( !IsValidTextureName( name ))
	{
		Con_Reportf( "Invalid texture name\n" );
		return 0;
	}

	// Check cache
	if(( texture = GetTextureByName( name )))
	{
		Con_Reportf( "Texture is already loaded with number %d\n", texture->number );
		return texture->number;
	}

	// Check picture pointer
	if( !picture )
	{
		Con_Reportf( "Picture is NULL\n" );
		return 0;
	}

	// Create texture
	texture = AppendTexture( name, flags );
	texture->picture = picture;
	texture->width   = picture->width;
	texture->height  = picture->height;

	ProcessFlags( texture, picture );

	// And upload
	// FIXME: Handle error
	UploadTexture( texture, update );

	return texture->number;
}

void RM_FreeTexture( unsigned int texnum )
{
	rm_texture_t *texture;

	//Con_Reportf( "RM_FreeTexture. Number %d\n", texnum );

	if (texnum == 0)
	{
		return;
	}

	texture = &( Textures.array[texnum] );
	if ( texture->used == false )
	{
		return;
	}

	RemoveTexture( texture->name );

	Textures.ref->R_FreeTexture( texnum );
}

const char*	RM_TextureName( unsigned int texnum )
{
	ASSERT( texnum >= 0 && texnum < MAX_TEXTURES );

	return &Textures.array[texnum].name;
}

const byte*	RM_TextureData( unsigned int texnum )
{
	rgbdata_t* pic;

	ASSERT( texnum >= 0 && texnum < MAX_TEXTURES );

	pic = Textures.array[texnum].picture;
	if (pic != NULL)
		return pic->buffer;
	else
		return NULL;
}

int RM_FindTexture( const char *name )
{
	rm_texture_t* texture;

	texture = GetTextureByName( name );
	if( texture == NULL)
	{
		return 0;
	}
	else
	{
		return texture->number;
	}
}

void RM_GetTextureParams( int* w, int* h, int texnum )
{
	ASSERT( texnum >= 0 && texnum < MAX_TEXTURES );

	if (w) *w = Textures.array[texnum].width;
	if (h) *h = Textures.array[texnum].height; 
}

int	RM_CreateTexture( const char* name, int width, int height, const void* buffer, texFlags_t flags )
{
	rgbdata_t picture;
	size_t    datasize;
	qboolean  update;

	update = FBitSet( flags, TF_UPDATE ) ? true : false;
	ClearBits( flags, TF_UPDATE );

	if( FBitSet( flags, TF_ARB_FLOAT ))
		datasize = 4;
	else if( FBitSet( flags, TF_ARB_16BIT ))
		datasize = 2;
	else
		datasize = 1;

	// Fill picture
	memset( &picture, 0, sizeof( rgbdata_t ));
	picture.width  = Q_max( width, 1 );
	picture.height = Q_max( height, 1 );
	picture.depth  = 1;
	picture.type   = PF_RGBA_32;
	picture.size   = picture.width * picture.height * picture.depth * 4 * datasize;
	picture.buffer = (byte*) buffer;

	// Clear invalid combinations
	ClearBits( flags, TF_TEXTURE_3D );

	// If image not luminance and not alphacontrast it will have color
	if( !FBitSet( flags, TF_LUMINANCE ) && !FBitSet( flags, TF_ALPHACONTRAST ))
	{
		SetBits( picture.flags, IMAGE_HAS_COLOR );
	}

	if( FBitSet( flags, TF_HAS_ALPHA ))
	{
		SetBits( picture.flags, IMAGE_HAS_ALPHA );
	}

	if( FBitSet( flags, TF_CUBEMAP ))
	{
		SetBits( picture.flags, IMAGE_CUBEMAP );
		picture.size *= 6;
	}

	return RM_LoadTextureFromBuffer( name, &picture, flags, update );
}

int RM_CreateTextureArray( const char* name, int width, int height, int depth, const void* buffer, texFlags_t flags )
{
	rgbdata_t picture;

	// Fill picture
	memset( &picture, 0, sizeof( rgbdata_t ));
	picture.width  = Q_max( width,  1 );
	picture.height = Q_max( height, 1 );
	picture.depth  = Q_max( depth,  1 );
	picture.type   = PF_RGBA_32;
	picture.size   = picture.width * picture.height * picture.depth * 4;
	picture.buffer = (byte*) buffer;

	// Clear invalid combinations
	ClearBits( flags, TF_CUBEMAP|TF_SKYSIDE|TF_HAS_LUMA|TF_MAKELUMA|TF_ALPHACONTRAST );

	// If image not luminance it will have color
	if( !FBitSet( flags, TF_LUMINANCE ))
		SetBits( picture.flags, IMAGE_HAS_COLOR );

	if( FBitSet( flags, TF_HAS_ALPHA ))
		SetBits( picture.flags, IMAGE_HAS_ALPHA );

	if( !FBitSet( flags, TF_TEXTURE_3D ))
		SetBits( picture.flags, IMAGE_MULTILAYER );

	return RM_LoadTextureFromBuffer( name, &picture, flags, false );
}


////////////////////////////////////////////////////////////////////////////////
// Local implementation

size_t CalcImageSize( pixformat_t format, int width, int height, int depth )
{
	size_t	size = 0;

	// check the depth error
	depth = Q_max( 1, depth );

	switch( format )
	{
	case PF_LUMINANCE:
		size = width * height * depth;
		break;
	case PF_RGB_24:
	case PF_BGR_24:
		size = width * height * depth * 3;
		break;
	case PF_BGRA_32:
	case PF_RGBA_32:
		size = width * height * depth * 4;
		break;
	case PF_DXT1:
		size = (((width + 3) >> 2) * ((height + 3) >> 2) * 8) * depth;
		break;
	case PF_DXT3:
	case PF_DXT5:
	case PF_BC6H_SIGNED:
	case PF_BC6H_UNSIGNED:
	case PF_BC7:
	case PF_ATI2:
		size = (((width + 3) >> 2) * ((height + 3) >> 2) * 16) * depth;
		break;
	}

	return size;
}

qboolean IsValidTextureName( const char *name )
{
	if (name == NULL)
		return false;
	
	if( !COM_CheckString( name ) )
		return false;
	else
		return Q_strlen( name ) < MAX_NAME_LEN;
}

rm_texture_t* GetTextureByName( const char *name )
{
	size_t hash;
	rm_texture_t* texture;

	hash = COM_HashKey( name, TEXTURES_HASH_SIZE );
	for (
		texture = Textures.hash_table[hash];
		texture != NULL;
		texture = texture->next_same_hash
	)
	{
		if ( strcmp( texture->name, name ) == 0 )
		{
			return texture;
		}
	}

	return NULL;
}

rm_texture_t* AppendTexture( const char* name, int flags )
{
	size_t     i;
	size_t     hash;
	rm_texture_t* texture;
	rm_texture_t* ft;

	// Check free places
	if ( Textures.count == TEXTURES_HASH_SIZE )
	{
		Con_Reportf( S_ERROR "Memory is full, no more textures can be loaded" );
		return NULL;
	}

	// Found hole
	for ( i = 0; i < Textures.count; i++ )
	{
		if ( Textures.array[i].used == false )
		{
			break;
		}
	}

	// Initialize
	texture = &(Textures.array[i]);
	memset( texture, 0, sizeof(rm_texture_t) );

	texture->used = true;

	// Fill some fields
	Q_strncpy( texture->name, name, sizeof( texture->name ) );
	
	// Pointer to picture, width and height will be set later
	texture->number = i; 
	texture->flags  = flags;

	// Insert into hash table
	hash = COM_HashKey( name, TEXTURES_HASH_SIZE );
	if ( Textures.hash_table[hash] == NULL )
	{
		Textures.hash_table[hash] = texture;
	}
	else
	{
		// Rare, but hash collisions do happen
		// Insert at the end of a single linked list
		for (
			ft = Textures.hash_table[hash];
			;
			ft = ft->next_same_hash
		)
		{
			if (ft->next_same_hash == NULL)
			{
				ft->next_same_hash = texture;
				break;
			}
		}
	}

	Textures.count++;

	return texture;
}

void ProcessFlags( rm_texture_t* tex, rgbdata_t* pic )
{
	uint img_flags = 0;

	// Force upload texture as RGB or RGBA (detail textures requires this)
	if( tex->flags & TF_FORCE_COLOR ) pic->flags |= IMAGE_HAS_COLOR;
	if( pic->flags & IMAGE_HAS_ALPHA ) tex->flags |= TF_HAS_ALPHA;

	if( ImageDXT( pic->type ))
	{
		if( !pic->numMips )
		{
			tex->flags |= TF_NOMIPMAP; // Disable mipmapping by user request
		}

		// Clear all the unsupported flags
		tex->flags &= ~TF_KEEP_SOURCE;
	}
	else
	{
		// Copy flag about luma pixels
		if( pic->flags & IMAGE_HAS_LUMA )
			tex->flags |= TF_HAS_LUMA;

		if( pic->flags & IMAGE_QUAKEPAL )
			tex->flags |= TF_QUAKEPAL;

		// Create luma texture from quake texture
		if( tex->flags & TF_MAKELUMA )
		{
			img_flags |= IMAGE_MAKE_LUMA;
			tex->flags &= ~TF_MAKELUMA;
		}

		if( !FBitSet( tex->flags, TF_IMG_UPLOADED ) && FBitSet( tex->flags, TF_KEEP_SOURCE ))
			tex->picture = FS_CopyImage( pic );  // Because current pic will be expanded to rgba

		// We need to expand image into RGBA buffer
		if( pic->type == PF_INDEXED_24 || pic->type == PF_INDEXED_32 )
			img_flags |= IMAGE_FORCE_RGBA;

		// Processing image before uploading (force to rgba, make luma etc)
		if( pic->buffer )
			Image_Process( &pic, 0, 0, img_flags, 0 );

		if( FBitSet( tex->flags, TF_LUMINANCE ))
			ClearBits( pic->flags, IMAGE_HAS_COLOR );
	}
}

qboolean UploadTexture( rm_texture_t* texture, qboolean update )
{
	qboolean status;

	//Con_Reportf( "Upload texture %s at %d\n", texture->name, texture->number );

	if( Textures.ref == NULL )
	{
		//Con_Reportf( S_ERROR "Ref is not loaded!\n" );
		return false;
	}

	status = Textures.ref->R_LoadTextureFromBuffer
	(
		texture->number,
		texture->picture,
		texture->flags, 
		update
	);

	if( status )
	{
		//Con_Reportf( "Texture successfully loaded\n" );
	}
	else
	{
		//Con_Reportf( S_ERROR "Ref return error on upload\n" );
	}

	return status;
}

void RemoveTexture( const char* name )
{
	size_t hash;
	rm_texture_t* tex;
	rm_texture_t* prev;

	hash = COM_HashKey( name, TEXTURES_HASH_SIZE );
	
	// If remove head, just change hash table pointer
	if ( strcmp( Textures.hash_table[hash]->name, name ) == 0 ) {
		
		tex = Textures.hash_table[hash]->next_same_hash;

		memset( Textures.hash_table[hash], 0, sizeof(rm_texture_t) );
		Textures.hash_table[hash] = tex;
	}
	else
	{
		// Or we are looking for our texture in a single linked list
		prev = Textures.hash_table[hash];
		for (
			tex = Textures.hash_table[hash]->next_same_hash;
			tex != NULL;
			tex = tex->next_same_hash
		)
		{
			if ( Q_strcmp( tex->name, name ) == 0 )
			{
				prev->next_same_hash = tex->next_same_hash;
				memset( tex, 0, sizeof(rm_texture_t) );
			}
			prev = tex;
		}
	}

	Textures.count--;
}


////////////////////////////////////////////////////////////////////////////////
// Internal textures

void CreateUnusedEntry( void )
{
	const char* name = "*unused";

	Q_strncpy( Textures.array[0].name, name, strlen(name) );
	Textures.array[0].used = true;
	
	uint hash_value = COM_HashKey( name, TEXTURES_HASH_SIZE );
	Textures.hash_table[hash_value] = &(Textures.array[0]);
	Textures.count++;
}

void CreateInternalTextures( void )
{
	CreateEmoTexture();
	CreateParticleTexture();

	CreateStaticColoredTexture( REF_WHITE_TEXTURE, 0xFFFFFFFF );
	CreateStaticColoredTexture( REF_GRAY_TEXTURE,  0xFF7F7F7F );
	CreateStaticColoredTexture( REF_BLACK_TEXTURE, 0xFF000000 );

	CreateCinematicDummyTexture();

	CreateDlightTexture();
}

rgbdata_t* FakeImage( int width, int height, int depth, int flags )
{
	rgbdata_t* r_image;

	r_image = malloc(sizeof(rgbdata_t));

	r_image->width   = Q_max( 1, width );
	r_image->height  = Q_max( 1, height );
	r_image->depth   = Q_max( 1, depth );
	r_image->flags   = flags;
	r_image->type    = PF_RGBA_32;
	r_image->size    = r_image->width * r_image->height * r_image->depth * 4;
	r_image->buffer  = malloc(r_image->size);
	r_image->palette = NULL;
	r_image->numMips = 1;
	r_image->encode  = 0;

	if( FBitSet( r_image->flags, IMAGE_CUBEMAP ))
	{
		r_image->size *= 6;
	}

	memset( r_image->buffer, 0xFF, r_image->size );

	return r_image;
}

int CreateEmoTexture( void )
{
	int w, h;
	int	x, y;
	rgbdata_t* pic;

	w = 16;
	h = 16;

	pic = FakeImage( w, h, 1, IMAGE_HAS_COLOR );

	for ( y = 0; y < h; y++ )
	{
		for ( x = 0; x < w; x++ )
		{
			if (( y < 8 ) ^ ( x < 8 ))
			{
				((uint *)pic->buffer)[y * 16 + x] = 0xFFFF00FF;  // Magenta
			}
			else
			{
				((uint *)pic->buffer)[y * 16 + x] = 0xFF000000;  // Black
			}
		}
	}

	return RM_LoadTextureFromBuffer( REF_DEFAULT_TEXTURE, pic, TF_COLORMAP, false );
}

int CreateParticleTexture( void )
{
	int w, h;
	int	x, y;
	rgbdata_t* pic;

	w = 8;
	h = 8;

	pic = FakeImage( w, h, 1, IMAGE_HAS_COLOR | IMAGE_HAS_ALPHA );

	for ( y = 0; y < 8; y++ )
	{
		for ( x = 0; x < 8; x++ )
		{
			if ( dot_texture[x][y] )
			{
				pic->buffer[( y * 8 + x ) * 4 + 3] = 255;
			}
			else
			{
				pic->buffer[( y * 8 + x ) * 4 + 3] = 0;
			}
		}
	}

	return RM_LoadTextureFromBuffer( REF_PARTICLE_TEXTURE, pic, TF_CLAMP, false );
}

int CreateStaticColoredTexture( const char* name, uint32_t color )
{
	int	w, h;
	int	x, y;
	rgbdata_t* pic;

	w = 4;
	h = 4;

	pic = FakeImage( w, h, 1, IMAGE_HAS_COLOR );

	for ( x = 0; x < (w * h); x++ )
	{
		((uint *)pic->buffer)[x] = color;
	}
	
	return RM_LoadTextureFromBuffer( name, pic, TF_COLORMAP, false );
}

int CreateCinematicDummyTexture( void )
{
	int	w, h;
	rgbdata_t* pic;

	w = 640;
	h = 100;

	pic = FakeImage( w, h, 1, IMAGE_HAS_COLOR );

	return RM_LoadTextureFromBuffer( REF_CINEMA_TEXTURE, pic, TF_NOMIPMAP | TF_CLAMP, false );
}

int CreateDlightTexture( void )
{
	int	w, h;
	rgbdata_t* pic;

	w = 128;
	h = 128;

	pic = FakeImage( w, h, 1, IMAGE_HAS_COLOR );

	return RM_LoadTextureFromBuffer( REF_DLIGHT_TEXTURE, pic, TF_NOMIPMAP | TF_CLAMP | TF_ATLAS_PAGE, false );
}