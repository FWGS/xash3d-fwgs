#pragma once
#ifndef STUDIO2_H
#define STUDIO2_H

#define MAX_NUM_LODS 8
#define MAX_NUM_BONES_PER_VERT 3

#define STUDIO_VERSION2 44

typedef struct
{
	int id;
	int version;
	dword checksum;
	int num_lod;
	int num_lod_vertices[MAX_NUM_LODS];
	int num_fixups;
	int fixup_table_start;
	int vertex_table_start;
	int tangent_table_start;
} studio_vvd_header;

typedef struct
{
	int lod;
	int source_vertex_id;
	int num_vertices;
} studio_vvd_fixup;

typedef struct
{
	float weight[MAX_NUM_BONES_PER_VERT];
	char bone[MAX_NUM_BONES_PER_VERT];
	byte num_bones;
} studio_vvd_bone_weights;


typedef struct
{
	studio_vvd_bone_weights bone_weights;
	vec3_t pos;
	vec3_t normal;
	vec2_t tex_coord;
} studio_vvd_vertex;

typedef struct
{
	int version;
	int vert_cache_size;
	word max_bones_per_strip;
	word max_bones_per_tri;
	int max_bones_per_vert;
	int checksum;
	int num_lods;
	int material_replacement_list_offset;
	int num_body_parts;
	int body_part_offset;
} studio_vtx_header;

typedef struct
{
	int num_models;
	int model_offset;
} studio_vtx_body_part;

typedef struct
{
	int num_lods;
	int lod_offset;
} studio_vtx_model;

typedef struct
{
	int num_meshes;
	int mesh_offset;
} studio_vtx_lod;

typedef struct
{
	int num_strip_groups;
	int strip_group_header_offset;
	byte flags;
} studio_vtx_mesh;



#define STRIPGROUP_IS_FLEXED 1
#define STRIPGROUP_IS_HWSKINNED 2
#define STRIPGROUP_IS_DELTA_FLEXED 4
#define STRIPGROUP_SUPPRESS_HW_MORPH 8

typedef struct
{
	int num_verts;
	int vert_offset;

	int num_indices;
	int index_offset;

	int num_strips;
	int strip_offset;

	byte flags;
} studio_vtx_strip_group;

typedef struct
{
	byte bone_weight_index[3];
	byte num_bones;

	word orig_mesh_vert_id;

	byte bone_id[3];
} studio_vtx_vertex;

typedef struct
{
	studio_vvd_header* vvd;
	studio_vtx_header* vtx;
} studiomdl2;

#endif
