#include <common/system.h>
#include <common/common.h>

#include "texture.h"


////////////////////////////////////////////////////////////////////////////////
// Defines, macros

#define MAX_TEXTURES	    (8192)
#define TEXTURES_HASH_SIZE	(MAX_TEXTURES >> 2)

#define MAX_LIGHTMAPS	    (256)

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
static void          RemoveTexture     ( const char* name );

void dumb( uint i, const char *name )
{
	Q_strncpy( RM_TextureManager.textures[i].name, name, MAX_NAME_LEN );
	RM_TextureManager.textures[i].used = true;
	
	uint hash_value = COM_HashKey( name, TEXTURES_HASH_SIZE );
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
}

void RM_SetRender( ref_interface_t* ref )
{
	RM_TextureManager.ref = ref;
}

void RM_ReuploadTextures()
{
	// TODO: Implement this for future render switch
}

int RM_LoadTexture( const char *name, const byte *buf, size_t size, int flags )
{
	rgbdata_t   * picture;
	rm_texture_t* texture;
	uint		  picFlags;

	Con_Reportf( "RM_LoadTexture. Name %s\n", name );

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
		qboolean status = RM_TextureManager.ref->R_LoadTextureFromBuffer
		(
			texture->number,
			texture->picture,
			texture->flags, 
			false
		);

		if (!status)
		{
			Con_Reportf( "Ref return error on upload!" );
		}
	}
	else
	{
		Con_Printf("Load texture without REF???");
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

int RM_LoadTextureArray( const char **names, int flags )
{
	// TODO: Implement
	Con_Printf( "Unimplemented RM_LoadTextureArray\n" );
}

int RM_LoadTextureFromBuffer( const char *name, rgbdata_t *picture, int flags, qboolean update )
{
	rm_texture_t *texture;

	Con_Reportf( "RM_LoadTextureFromBuffer. Name %s\n", name );

	if (!RM_TextureManager.ref) return 0;

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

	// Upload texture
	qboolean status = RM_TextureManager.ref->R_LoadTextureFromBuffer
	(
		texture->number,
		texture->picture,
		texture->flags, 
		update
	);

	if (!status)
	{
		Con_Reportf( "Ref return error on upload!" );
	}

	return texture->number;
}

void RM_FreeTexture( unsigned int texnum )
{
	rm_texture_t *texture;

	if (texnum == 0)
	{
		return;
	}

	Con_Reportf( "RM_FreeTexture. TexNum %d\n", texnum );

	texture = &( RM_TextureManager.textures[texnum] );
	if ( texture->used == false )
	{
		return;
	}

	RemoveTexture( texture->name );

	RM_TextureManager.ref->R_FreeTexture( texnum );
}

const char*	RM_TextureName( unsigned int texnum )
{
	// TODO: Implement
	Con_Printf( "Unimplemented RM_TextureName\n" );
}

const byte*	RM_TextureData( unsigned int texnum )
{
	// TODO: Implement
	Con_Printf( "Unimplemented RM_TextureData\n" );
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

int	RM_CreateTexture( const char *name, int width, int height, const void *buffer, texFlags_t flags )
{
	// TODO: Implement
	Con_Printf( "Unimplemented RM_CreateTexture\n" );
}

int RM_CreateTextureArray( const char *name, int width, int height, int depth, const void *buffer, texFlags_t flags )
{
	// TODO: Implement
	Con_Printf( "Unimplemented RM_CreateTextureArray\n" );
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
	size_t hash;
	rm_texture_t* texture;

	hash = COM_HashKey( name, TEXTURES_HASH_SIZE );
	for (
		texture = RM_TextureManager.textures_hash_table[hash];
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
	if ( RM_TextureManager.textures_count == TEXTURES_HASH_SIZE )
	{
		return NULL;
	}

	// Found hole
	for ( i = 0; i < RM_TextureManager.textures_count; i++ )
	{
		if ( RM_TextureManager.textures[i].used == false )
		{
			break;
		}
	}

	// Initialize
	texture = &( RM_TextureManager.textures[i] );
	memset( texture, 0, sizeof(rm_texture_t) );

	texture->used = true;

	// Fill some fields
	Q_strncpy( texture->name, name, sizeof( texture->name ) );
	
	// Pointer to picture, width and height will be set later
	texture->number = i; 
	texture->flags  = flags;

	// Insert into hash table
	hash = COM_HashKey( name, TEXTURES_HASH_SIZE );
	if ( RM_TextureManager.textures_hash_table[hash] == NULL )
	{
		RM_TextureManager.textures_hash_table[hash] = texture;
	}
	else
	{
		for (
			ft = RM_TextureManager.textures_hash_table[hash];
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

	// Inc count
	RM_TextureManager.textures_count++;

	// Return element
	return texture;
}

void RemoveTexture( const char* name )
{
	size_t hash;
	rm_texture_t* tex;
	rm_texture_t* prev;

	hash = COM_HashKey( name, TEXTURES_HASH_SIZE );
	
	// If remove head, just change hash table pointer
	if ( strcmp( RM_TextureManager.textures_hash_table[hash]->name, name ) == 0 ) {
		
		tex = RM_TextureManager.textures_hash_table[hash]->next_same_hash;

		memset( RM_TextureManager.textures_hash_table[hash], 0, sizeof(rm_texture_t) );
		RM_TextureManager.textures_hash_table[hash] = tex;
	}
	else
	{
		prev = RM_TextureManager.textures_hash_table[hash];
		for (
			tex = RM_TextureManager.textures_hash_table[hash]->next_same_hash;
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

	// Dec count
	RM_TextureManager.textures_count--;
}