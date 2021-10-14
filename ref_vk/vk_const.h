#pragma once

#define MAX_SCENE_STACK 2
#define MAX_SCENE_ENTITIES 2048

#define MAX_TEXTURES	4096

// TODO count these properly
#define MAX_BUFFER_VERTICES (1 * 1024 * 1024)
#define MAX_BUFFER_INDICES (MAX_BUFFER_VERTICES * 3)

// indexed by uint8_t
#define MAX_SURFACE_LIGHTS 255
// indexed by uint8_t
#define MAX_POINT_LIGHTS 255

#define MAX_VISIBLE_POINT_LIGHTS 31
// indexed by uint8_t
#define MAX_VISIBLE_SURFACE_LIGHTS 255
#define MAX_LIGHT_CLUSTERS 262144 //131072 //32768
#define LIGHT_GRID_CELL_SIZE 128
