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
} studio_vertex;

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
	int num_indices;
	int index_offset;

	int num_verts;
	int vert_offset;

	word num_bones;

	byte flags;

	int num_bone_tate_changes;
	int bone_state_change_offset;
} studio_vtx_strip;

typedef struct
{
	byte bone_weight_index[3];
	byte num_bones;

	word orig_mesh_vert_id;

	byte bone_id[3];
} studio_vtx_vertex;


typedef struct
{
	int id;
	int ver;
	int checksum;
	char name[64];
	int data_len;

	vec3_t eye_pos;
	vec3_t illum_pos;
	vec3_t hull_min;
	vec3_t hull_max;
	vec3_t view_bboxmin;
	vec3_t view_bboxmax;

	int flags;

	int bone_count;
	int bone_offset;

	int bonecontroller_count;
	int bonecontroller_offset;

	int hitbox_count;
	int hitbox_offset;

	int localanim_count;
	int localanim_offset;

	int localseq_count;
	int localseq_offset;

	int activity_list_ver;
	int events_indexed;

	int texture_count;
	int texture_offset;

	int texturedir_count;
	int texturedir_offset;

	int skin_reference_count;
	int skin_rfamily_count;
	int skin_reference_index;

	int body_part_count;
	int body_part_offset;

	int attachment_count;
	int attachment_offset;

	int local_node_count;
	int local_node_index;
	int local_node_name_index;

	int flex_desc_count;
	int flex_desc_index;

	int flex_controller_count;
	int flex_controller_index;

	int flex_rules_count;
	int flex_rules_index;

	int ik_chain_count;
	int ik_chain_index;

	int mouths_count;
	int mouths_index;

	int local_pose_param_count;
	int local_pose_param_index;

	int surfaceprop_index;

	int keyvalue_index;
	int keyvalue_count;

	int ik_lock_count;
	int ik_lock_index;

	float mass;

	int contents;

	int include_model_count;
	int include_model_index;

	int virtual_model;

	int anim_blocks_name_index;
	int anim_blocks_count;
	int anim_blocks_index;

	int anim_block_model;

	int bone_table_name_index;

	int vertex_base;
	int offset_base;

	byte directional_dot_product;

	byte root_lod;

	byte num_allowed_root_lods;

	byte unused_a;
	int unused_b;

	int flexcontrollerui_count;
	int flexcontrollerui_index;

	float vertAnimFixedPointScale;

	int unused_c;

	int studio_hdr_2_index;

	int unused_d;
} studio_mdl_header;

typedef struct
{
	int name_index;
	int num_models;
	int base;
	int model_index;
} studio_mdl_body_part;

typedef struct
{
	char name[64];
	int type;
	float bounding_radius;

	int num_meshes;
	int mesh_index;

	int num_vertices;
	int vertex_index;
	int tangents_index;

	int num_attachments;
	int attachment_index;

	int num_eyeballs;
	int eyeball_index;

	int num_lod_vertices[MAX_NUM_LODS];

	int unused[8];
} studio_mdl_model;

typedef struct
{
	int material;
	int model_index;

	int num_vertices;
	int vertex_offset;

	int num_flexes;
	int flex_index;

	int material_type;
	int material_param;

	int mesh_id;

	vec3_t center;

	int stupid;

	int num_lod_vertices[MAX_NUM_LODS];

	int unused[8];

} studio_mdl_mesh;

typedef struct
{
	dword name_index;
	dword flags;
	dword used;
	union
	{
		struct
		{
			dword unused_A;
			dword unused_B;
			dword unused_C;
			int unused[10];
		} unused;
		int texturenum;
	} t;

} studio_mdl_texture;

typedef struct
{
	studio_mdl_header* mdl;
	studio_vvd_header* vvd;
	studio_vtx_header* vtx;
} studiomdl2;

#endif
