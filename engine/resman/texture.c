#include <common/system.h>
#include <common/common.h>

#include "texture.h"


////////////////////////////////////////////////////////////////////////////////
// Defines, macros

#define MAX_TEXTURES	    (8096)
#define TEXTURES_HASH_SIZE	(MAX_TEXTURES)

#define MAX_LIGHTMAPS	    (256)

#define MAX_NAME_LEN        (256)


////////////////////////////////////////////////////////////////////////////////
// Structs

typedef struct rm_texture_s {
	char name[MAX_NAME_LEN];

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

static qboolean      IsValidTextureName( const char *name );
static rm_texture_t* GetTextureByName  ( const char *name );
static rm_texture_t* AppendTexture     ( const char* name, int flags );

void dumb( uint i, const char *name )
{
	Q_strncpy( RM_TextureManager.textures[i].name, name, MAX_NAME_LEN );
	
	uint hash_value = COM_HashKey( name, TEXTURES_HASH_SIZE );
	RM_TextureManager.textures[i].hash_value = hash_value;
	RM_TextureManager.textures[i].next_hash = RM_TextureManager.textures_hash_table[hash_value];
	RM_TextureManager.textures_hash_table[hash_value] = &(RM_TextureManager.textures[i]);
	RM_TextureManager.textures_count++;
}


////////////////////////////////////////////////////////////////////////////////
// Public methods

void RM_Init()
{
	// Reset texture manager
	memset( &RM_TextureManager, 0, sizeof( RM_TextureManager ));

	// Create unused 0-entry
	dumb( 0, "*unused*" );

	// Create internal textures
	// FIXME: Create textures, not dumb entries
	dumb( 1, REF_DEFAULT_TEXTURE  );
	dumb( 2, REF_PARTICLE_TEXTURE );
	dumb( 3, REF_WHITE_TEXTURE    );
	dumb( 4, REF_GRAY_TEXTURE     );
	dumb( 5, REF_BLACK_TEXTURE    );
	dumb( 6, REF_CINEMA_TEXTURE   );
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
	rgbdata_t   * picture;
	rm_texture_t* texture;
	uint		  picFlags;

	Con_Reportf( "RM_LoadTexture. Name %s, size %d\n", name, size );

	// Check name
	if( !IsValidTextureName( name ))
	{
		Con_Reportf( "Invalid texture name\n", name );
		return 0;
	}

	// Check cache
	if(( texture = GetTextureByName( name )))
	{
		Con_Reportf( "Texture is already loaded with number %d\n", texture->number );
		return texture->number;
	}

	// Bit magic
	if( FBitSet( flags, TF_NOFLIP_TGA ))
		SetBits( picFlags, IL_DONTFLIP_TGA );

	if( FBitSet( flags, TF_KEEP_SOURCE ) && !FBitSet( flags, TF_EXPAND_SOURCE ))
		SetBits( picFlags, IL_KEEP_8BIT );

	// Flags magic
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

	// TODO: Prepare texture
	//VK_ProcessImage( tex, pic );

	// Upload texture
	if (RM_TextureManager.ref)
	{
		Con_Reportf( "Upload texture %s to render\n", &(texture->name) );

		RM_TextureManager.ref->GL_LoadTextureFromBuffer
		(
			&(texture->name),
			texture->picture,
			texture->flags, 
			/* What is update??? */ false
		);

		Con_Reportf( "Uploaded at %d, our index %d\n", RM_TextureManager.ref->FindTexture( &(texture->name)), texture->number );
	}

	//if( !uploadTexture( tex, &pic, 1, false ))
	//{
	//	memset( tex, 0, sizeof( vk_texture_t ));
	//	gEngine.FS_FreeImage( pic ); // release source texture
	//	return 0;
	//}

	// TODO: Apply texture params
	//VK_ApplyTextureParams( tex );

	Con_Reportf( "Texture successfully loaded with number %d\n", texture->number );

	return texture->number;
}

void RM_FreeTexture( unsigned int texnum )
{
	Con_Reportf( "Unimplemented RM_FreeTexture. TexNum %d\n", texnum );
}

int RM_FindTexture( const char *name )
{
	rm_texture_t* texture;

	Con_Reportf( "RM_FindTexture. Name %s\n", name );

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

	//Con_Reportf( "RM_GetTextureParams. Texnum %d\n", texnum );

	if (w) *w = RM_TextureManager.textures[texnum].width;
	if (h) *h = RM_TextureManager.textures[texnum].height; 
}

////////////////////////////////////////////////////////////////////////////////
// Local implementation

qboolean IsValidTextureName( const char *name )
{
	if( !COM_CheckString( name ) )
		return false;
	else
		return Q_strlen( name ) < MAX_NAME_LEN;
}

rm_texture_t* GetTextureByName( const char *name )
{
	uint hash_value = COM_HashKey( name, TEXTURES_HASH_SIZE );

	return RM_TextureManager.textures_hash_table[hash_value];
}

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