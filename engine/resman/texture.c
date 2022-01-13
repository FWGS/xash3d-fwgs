#include <common/system.h>
#include <common/common.h>

#include "texture.h"


////////////////////////////////////////////////////////////////////////////////
// Defines, macros

#define MAX_TEXTURES	    (8096)
#define TEXTURES_HASH_SIZE	(MAX_TEXTURES)

#define MAX_LIGHTMAPS	    (256)


////////////////////////////////////////////////////////////////////////////////
// Structs

typedef struct rm_texture_s {
	char name[256];

	rgbdata_t* picture;
	int width;
	int height;
	
	int flags;

	uint number;

	uint   hash_value;
	struct rm_texture_s *next_hash;
} rm_texture_t;


////////////////////////////////////////////////////////////////////////////////
// Global state

static struct {
	rm_texture_t  textures[MAX_TEXTURES];
	rm_texture_t* textures_hash_table[TEXTURES_HASH_SIZE];
	uint          textures_count;

	int default_texture;
	int particle_texture;
	int white_texture;
	int gray_texture;
	int black_texture;
	int solidsky_texture;
	int alphasky_texture;
	int lightmap_textures[MAX_LIGHTMAPS];
	int dlight_texture;
	int cinematic_texture;

	ref_interface_t* ref;
} RM_TextureManager;


////////////////////////////////////////////////////////////////////////////////
// Local

static rm_texture_t* AppendTexture( const char* name, int flags );


////////////////////////////////////////////////////////////////////////////////
// Public methods

void RM_Init()
{
	// Reset texture manager
	memset( &RM_TextureManager, 0, sizeof( RM_TextureManager ));

	// Create unused 0-entry
	const char *name = "*unused*";
	Q_strncpy( RM_TextureManager.textures[0].name, name, sizeof( RM_TextureManager.textures[0].name ));
	
	uint hash_value = COM_HashKey( name, TEXTURES_HASH_SIZE );
	RM_TextureManager.textures[0].hash_value = hash_value;
	RM_TextureManager.textures[0].next_hash = RM_TextureManager.textures_hash_table[hash_value];
	RM_TextureManager.textures_hash_table[hash_value] = &(RM_TextureManager.textures[0]);
	RM_TextureManager.textures_count = 1;

	// TODO: Create internal textures
}

void RM_SetRender( ref_interface_t* ref )
{
	RM_TextureManager.ref = ref;
}

void RM_ReuploadTextures()
{
	// TODO: Unimplemented now, for future render switch
}

int RM_LoadTexture( const char *name, const byte *buf, size_t size, int flags )
{
	rgbdata_t    *picture;
	rm_texture_t *texture;

	Con_Reportf( "RM_LoadTexture. Name %s, size %d\n", name, size );

	// TODO: Check name
	//if( !Common_CheckTexName( name ))
	//	return 0;

	// TODO: Check cache
	//if(( tex = Common_TextureForName( name )))
	//	return (tex - vk_textures);

	// TODO: Bit magic
	//if( FBitSet( flags, TF_NOFLIP_TGA ))
	//	SetBits( picFlags, IL_DONTFLIP_TGA );

	//if( FBitSet( flags, TF_KEEP_SOURCE ) && !FBitSet( flags, TF_EXPAND_SOURCE ))
	//	SetBits( picFlags, IL_KEEP_8BIT );

	// TODO: Flags magic
	//Image_SetForceFlags( picFlags );

	// Load image using engine
	picture = FS_LoadImage( name, buf, size );
	if( !picture ) return 0;

	// Allocate texture
	texture = AppendTexture( name, flags );
	texture->picture = picture;
	texture->width   = picture->width;
	texture->height  = picture->height;

	// TODO: Prepare texture
	//VK_ProcessImage( tex, pic );

	// Upload texture
	if (RM_TextureManager.ref)
	{
		RM_TextureManager.ref->GL_LoadTextureFromBuffer
		(
			&(texture->name),
			texture->picture,
			texture->flags, 
			/* What is update??? */ false
		);
	}

	//if( !uploadTexture( tex, &pic, 1, false ))
	//{
	//	memset( tex, 0, sizeof( vk_texture_t ));
	//	gEngine.FS_FreeImage( pic ); // release source texture
	//	return 0;
	//}

	// TODO: Apply texture params
	//VK_ApplyTextureParams( tex );

	return texture->number;
}

void RM_FreeTexture( unsigned int texnum )
{
	Con_Reportf( "Unimplemented RM_FreeTexture. TexNum %d\n", texnum );
}

int RM_FindTexture( const char *name )
{
	Con_Reportf( "RM_FindTexture. Name %s\n", name );

	return 0;
}

void RM_GetTextureParams( int* w, int* h, int texnum )
{
	ASSERT( texnum >= 0 && texnum < MAX_TEXTURES );

	Con_Reportf( "RM_GetTextureParams. Texnum %d\n", texnum );

	if (w) *w = RM_TextureManager.textures[texnum].width;
	if (h) *h = RM_TextureManager.textures[texnum].height; 
}

////////////////////////////////////////////////////////////////////////////////
// Local implementation

rm_texture_t* AppendTexture( const char* name, int flags )
{
	rm_texture_t* texture;
	uint i;

	// Find a free rm_texture_t slot
	for( 
		i = 0, texture = &(RM_TextureManager.textures[0]); 
		i < RM_TextureManager.textures_count; 
		i++, texture++
	)
	{
		if( !texture->name[0] )
		{
			break;
		}
	}

	// No holes, append to tail
	if( i == RM_TextureManager.textures_count )
	{
		// Check textures
		// TODO: Maybe it's worth realloc with increasing max textures?
		if( RM_TextureManager.textures_count == MAX_TEXTURES )
		{
			Host_Error( "ResMan: The textures limit is exhausted\n" );
			return NULL;
		}

		RM_TextureManager.textures_count++;
	}

	// Setup params
	Q_strncpy( texture->name, name, sizeof( texture->name ) );
	
	// Pointer to picture, width and height will be set later
	texture->picture = NULL; 
	texture->width   = 0;
	texture->height  = 0;
	
	texture->number = i; 
	texture->flags = flags;

	// Add to hash table
	texture->hash_value = COM_HashKey( name, TEXTURES_HASH_SIZE );

	RM_TextureManager.textures_hash_table[texture->hash_value] = texture;
	texture->next_hash = RM_TextureManager.textures_hash_table[texture->hash_value];

	return texture;
}