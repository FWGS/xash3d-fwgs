//
// Created by RED on 23.01.2024.
//

#ifndef DESTINATION_VBPSLIB_H
#define DESTINATION_VBPSLIB_H

#include "const.h"
#include "bspfile.h"

enum LumpId
{
    VBSP_LUMP_ENTITIES = 0,                          //	Map entities
    VBSP_LUMP_PLANES = 1,                            //	Plane array
    VBSP_LUMP_TEXDATA = 2,                           //	Index to texture names
    VBSP_LUMP_VERTEXES = 3,                          //	Vertex array
    VBSP_LUMP_VISIBILITY = 4,                        //	Compressed visibility bit arrays
    VBSP_LUMP_NODES = 5,                             //	BSP tree nodes
    VBSP_LUMP_TEXINFO = 6,                           //	Face texture array
    VBSP_LUMP_FACES = 7,                             //	Face array
    VBSP_LUMP_LIGHTING = 8,                          //	Lightmap samples
    VBSP_LUMP_OCCLUSION = 9,                         //	Occlusion polygons and vertices
    VBSP_LUMP_LEAFS = 10,                            //		BSP tree leaf nodes
    VBSP_LUMP_FACEIDS = 11,                          //		Correlates between dfaces and Hammer face IDs. Also used as random seed for detail prop placement.
    VBSP_LUMP_EDGES = 12,                            //		Edge array
    VBSP_LUMP_SURFEDGES = 13,                        //		Index of edges
    VBSP_LUMP_MODELS = 14,                           //		Brush models (geometry of brush entities)
    VBSP_LUMP_WORLDLIGHTS = 15,                      //		Internal world lights converted from the entity lump
    VBSP_LUMP_LEAFFACES = 16,                        //		Index to faces in each leaf
    VBSP_LUMP_LEAFBRUSHES = 17,                      //		Index to brushes in each leaf
    VBSP_LUMP_BRUSHES = 18,                          //		Brush array
    VBSP_LUMP_BRUSHSIDES = 19,                       //		Brushside array
    VBSP_LUMP_AREAS = 20,                            //		Area array
    VBSP_LUMP_AREAPORTALS = 21,                      //		Portals between areas
    VBSP_LUMP_PORTALS = 22,                          //
    VBSP_LUMP_CLUSTERS = 23,                         //		Leaves that are enterable by the player
    VBSP_LUMP_PORTALVERTS = 24,                      //		Vertices of portal polygons
    VBSP_LUMP_CLUSTERPORTALS = 25,                   //
    VBSP_LUMP_DISPINFO = 26,                         //		Displacement surface array
    VBSP_LUMP_ORIGINALFACES = 27,                    //		Brush faces array before splitting
    VBSP_LUMP_PHYSDISP = 28,                         //		Displacement physics collision data
    VBSP_LUMP_PHYSCOLLIDE = 29,                      //		Physics collision data
    VBSP_LUMP_VERTNORMALS = 30,                      //		Face plane normals
    VBSP_LUMP_VERTNORMALINDICES = 31,                //		Face plane normal index array
    VBSP_LUMP_DISP_LIGHTMAP_ALPHAS = 32,             //		Displacement lightmap alphas (unused/empty since Source 2006)
    VBSP_LUMP_DISP_VERTS = 33,                       //		Vertices of displacement surface meshes
    VBSP_LUMP_DISP_LIGHTMAP_SAMPLE_POSITIONS = 34,   //		Displacement lightmap sample positions
    VBSP_LUMP_GAME_LUMP = 35,                        //		Game-specific data lump
    VBSP_LUMP_LEAFWATERDATA = 36,                    //		Data for leaf nodes that are inside water
    VBSP_LUMP_PRIMITIVES = 37,                       //		Water polygon data
    VBSP_LUMP_PRIMVERTS = 38,                        //		Water polygon vertices
    VBSP_LUMP_PRIMINDICES = 39,                      //		Water polygon vertex index array
    VBSP_LUMP_PAKFILE = 40,                          //		Embedded uncompressed Zip-format file
    VBSP_LUMP_CLIPPORTALVERTS = 41,                  //		Clipped portal polygon vertices
    VBSP_LUMP_CUBEMAPS = 42,                         //		env_cubemap location array
    VBSP_LUMP_TEXDATA_STRING_DATA = 43,              //		Texture name data
    VBSP_LUMP_TEXDATA_STRING_TABLE = 44,             //		Index array into texdata string data
    VBSP_LUMP_OVERLAYS = 45,                         //		info_overlay data array
    VBSP_LUMP_LEAFMINDISTTOWATER = 46,               //		Distance from leaves to water
    VBSP_LUMP_FACE_MACRO_TEXTURE_INFO = 47,          //		Macro texture info for faces
    VBSP_LUMP_DISP_TRIS = 48,                        //		Displacement surface triangles
    VBSP_LUMP_PHYSCOLLIDESURFACE = 49,               //		Compressed win32-specific Havok terrain surface collision data. Deprecated and no longer used.
    VBSP_LUMP_WATEROVERLAYS = 50,                    //
    VBSP_LUMP_LIGHTMAPPAGES = 51,                    //		Alternate lightdata implementation for Xbox
    VBSP_LUMP_LIGHTMAPPAGEINFOS = 52,                //		Alternate lightdata indices for Xbox
    VBSP_LUMP_LIGHTING_HDR = 53,                     //		HDR lightmap samples
    VBSP_LUMP_WORLDLIGHTS_HDR = 54,                  //		Internal HDR world lights converted from the entity lump
    VBSP_LUMP_LEAF_AMBIENT_LIGHTING_HDR = 55,        //		Per-leaf ambient light samples (HDR)
    VBSP_LUMP_LEAF_AMBIENT_LIGHTING = 56,            //		Per-leaf ambient light samples (LDR)
    VBSP_LUMP_XZIPPAKFILE = 57,                      //		XZip version of pak file for Xbox. Deprecated.
    VBSP_LUMP_FACES_HDR = 58,                        //		HDR maps may have different face data
    VBSP_LUMP_MAP_FLAGS = 59,                        //		Extended level-wide flags. Not present in all levels.
    VBSP_LUMP_OVERLAY_FADES = 60,                    //		Fade distances for overlays
    VBSP_LUMP_OVERLAY_SYSTEM_LEVELS = 61,            //		System level settings (min/max CPU & GPU to render this overlay)
    VBSP_LUMP_PHYSLEVEL = 62,                        //
    VBSP_LUMP_DISP_MULTIBLEND = 63,                  //		Displacement multiblend info
    VBSP_LUMP_COUNT,
};

typedef struct
{
    dword offset;
    dword size;
    dword version;
    dword decompressed_size;
} vbsp_lump_t;

typedef struct
{
    dword ident;
    dword version;
    vbsp_lump_t lumps[VBSP_LUMP_COUNT];
    dword map_revision;
} vbsp_header_t;

typedef dplane_t vbsp_dplane_t;

typedef struct
{
    vec3_t mins, maxs;             // bounding box
    vec3_t origin;                // for sounds or lights
    int headnode;              // index into node array
    int firstface, numfaces;   // index into face array
} vbsp_dmodel_t;

typedef vec3_t vbsp_vertex_t;

typedef struct
{
    unsigned short v[2];  // vertex indices
} vbsp_dedge_t;

typedef int vbsp_surfedge_t;

typedef struct
{
    unsigned short planenum;               // the plane number
    byte side;                   // faces opposite to the node's plane direction
    byte onNode;                 // 1 of on node, 0 if in leaf
    int firstedge;              // index into surfedges
    short numedges;               // number of surfedges
    short texinfo;                // texture info
    short dispinfo;               // displacement info
    short surfaceFogVolumeID;     // ?
    byte styles[4];              // switchable lighting info
    int lightofs;               // offset into lightmap lump
    float area;                   // face area in units^2
    int LightmapTextureMinsInLuxels[2]; // texture lighting info
    int LightmapTextureSizeInLuxels[2]; // texture lighting info
    int origFace;               // original face this was split from
    unsigned short numPrims;               // primitives
    unsigned short firstPrimID;
    dword smoothingGroups;        // lightmap smoothing group
} vbsp_dface_t;

typedef struct
{
    int firstside;     // first brushside
    int numsides;      // number of brushsides
    int contents;      // contents flags
} vbsp_dbrush_t;

typedef struct
{
    unsigned short planenum;     // facing out of the leaf
    short texinfo;      // texture info
    short dispinfo;     // displacement info
    short bevel;        // is the side a bevel plane?
} vbsp_dbrushside_t;

typedef struct
{
    int planenum;       // index into plane array
    int children[2];    // negative numbers are -(leafs + 1), not nodes
    short mins[3];        // for frustum culling
    short maxs[3];
    unsigned short firstface;      // index into face array
    unsigned short numfaces;       // counting both sides
    short area;           // If all leaves below this node are in the same area, then
    // this is the area index. If not, this is -1.
    short paddding;       // pad to 32 bytes length
} vbsp_dnode_t;

typedef struct
{
    int contents;             // OR of all brushes (not needed?)
    short cluster;              // cluster this leaf is in
    short area: 9;               // area this leaf is in
    short flags: 7;              // flags
    short mins[3];              // for frustum culling
    short maxs[3];
    unsigned short firstleafface;        // index into leaffaces
    unsigned short numleaffaces;
    unsigned short firstleafbrush;       // index into leafbrushes
    unsigned short numleafbrushes;
    short leafWaterDataID;      // -1 for not in water

    //!!! NOTE: for lump version 0 (usually in maps of version 19 or lower) uncomment the next line
    //CompressedLightCube   ambientLighting;      // Precaculated light info for entities.
    short padding;              // padding to 4-byte boundary
} vbsp_dleaf_t;

typedef unsigned short vbsp_leaf_face_t;
typedef unsigned short vbsp_leaf_brush_t;
typedef struct
{
    float textureVecs[2][4];    // [s/t][xyz offset]
    float lightmapVecs[2][4];   // [s/t][xyz offset] - length is in units of texels/area
    int flags;                // miptex flags overrides
    int texdata;              // Pointer to texture name, size, etc.
} vbsp_texinfo_t;

typedef struct
{
    vec3_t reflectivity;            // RGB reflectivity
    int nameStringTableID;       // index into TexdataStringTable
    int width, height;           // source image
    int view_width, view_height;
} vbsp_dtexdata_t;

typedef dword vbsp_texdata_stringtable_t;
typedef char *vbsp_texdata_stringdata_t;

typedef struct
{
    int numclusters;
    int *byteofs; // byteofs[numclusters][2]
} vbsp_dvis_t;


typedef struct
{
    vbsp_header_t header;

    char *entities;
    dword entities_size;

    vbsp_dplane_t *planes;
    dword plane_count;

    vbsp_dmodel_t *models;
    dword model_count;

    vbsp_vertex_t *vertices;
    dword vertex_count;

    vbsp_dedge_t *edges;
    dword edge_count;

    vbsp_surfedge_t *surf_edges;
    dword surf_edge_count;

    vbsp_dface_t *faces;
    dword face_count;

    vbsp_dface_t *orig_faces;
    dword orig_face_count;

    vbsp_dbrush_t *brushes;
    dword brush_count;

    vbsp_dbrushside_t *brush_sides;
    dword brush_sides_count;

    vbsp_dnode_t *nodes;
    dword node_count;

    vbsp_dleaf_t *leafs;
    dword leaf_count;

    vbsp_leaf_face_t *leaf2face_map;
    dword leaf2face_map_size;

    vbsp_leaf_brush_t *leaf2brush_map;
    dword leaf2brush_map_size;

    vbsp_texinfo_t *texture_info;
    dword texture_info_count;

    vbsp_dtexdata_t *texture_data;
    dword texture_data_count;

    vbsp_texdata_stringtable_t* string_indices;
    dword string_indices_count;

    vbsp_texdata_stringdata_t string_data;
    dword string_data_size;

    vbsp_dvis_t* vis_info;
    dword vis_info_count;

} vbsp_t;

typedef struct model_s model_t;

void VBSPLib_loadBSP(vbsp_t *vbsp, const byte* data, model_t *mod);

#endif //DESTINATION_VBPSLIB_H
