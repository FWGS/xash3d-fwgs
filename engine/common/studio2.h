#pragma once
#ifndef STUDIO2_H
#define STUDIO2_H
#include "common.h"

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
	byte numbones;
} studio_vvd_bone_weights;


typedef struct
{
	studio_vvd_bone_weights bone_weights;
	vec3_t pos;
	vec3_t normal;
	vec2_t tex_coord;
} studio_vvd_vertex;

#endif
