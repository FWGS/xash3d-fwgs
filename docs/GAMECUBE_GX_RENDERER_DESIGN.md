# GameCube GX Renderer Design

This document describes the GameCube GX (Flipper) graphics renderer architecture.

## Overview

The GX renderer is the primary graphics system for Xash3D on GameCube, utilizing the Nintendo GameCube's GPU (Flipper) for hardware-accelerated 3D rendering.

## Hardware Architecture

### Flipper GPU Specifications
- **Clock Speed**: 162 MHz
- **Pixel Fillrate**: ~1.3 Gpixels/s
- **Texture Fillrate**: ~4.3 GTexels/s
- **Memory Bandwidth**: 2.7 GB/s (32 MB Rambus)
- **Vertex Processing**: 12 vertices/pixel
- **Texture Units**: 6 texture units
- **Pixel Pipelines**: 4 pipelines

### Memory Architecture
```
Flipper Memory Controller
├── 16 MB DRAM (embedded)
├── 16 MB DRAM (embedded)
└── 32 MB Total VRAM

Main RAM (24 MB)
├── Texture Cache (8 MB)
├── Vertex Buffers
└── Framebuffer
```

## GX Renderer Architecture

### Core Components

#### 1. GX Manager (`renderer/gx/gx_main.c`)
- Initialization and shutdown
- State management
- Frame buffer management
- Display list generation

#### 2. Texture Manager (`renderer/gx/gx_texture.c`)
- Texture loading and caching
- Mipmapping support
- Texture compression (CI8, RGBA8)
- Texture cache management

#### 3. Vertex Manager (`renderer/gx/gx_vertex.c`)
- Vertex buffer management
- Skinned vertex support
- Normal/tangent generation
- Index buffer management

#### 4. Material System (`renderer/gx/gx_material.c`)
- Material state management
- Shader program handling
- Texture binding
- Render state setup

#### 5. Render Target (`renderer/gx/gx_target.c`)
- Framebuffer setup
- Depth buffer management
- Screen clearing
- Post-processing

## Rendering Pipeline

### GX Rendering Flow
```
1. Setup Phase
   ├─ Clear framebuffers
   ├─ Set projection matrix
   ├─ Set view matrix
   └─ Set light parameters

2. Geometry Phase
   ├─ Load vertex data
   ├─ Load texture data
   ├─ Set material state
   └─ Submit draw calls

3. Rasterization Phase
   ├─ Vertex transformation
   ├─ Primitive assembly
   ├─ Rasterization
   └─ Pixel shading

4. Output Phase
   ├─ Depth testing
   ├─ Blending
   ├─ Texture application
   └─ Framebuffer write

5. Presentation Phase
   ├─ EFB copy to XFB
   ├─ Texture cache flush
   └─ Display flip
```

### Display List System

GX uses display lists for efficient command batching:

```c
// Display list structure
typedef struct {
    GXCmdHeader header;
    GXCmdData data;
    GXCmdEnd end;
} GXDisplayList;

// Example display list
GXBegin(GX_TRIANGLES, GX_VTXFMT0, vertex_count);
GXSetTexCoord(GX_TEXCOORD0, texcoord);
GXSetNormal(normal);
GXSetPos3f32(x, y, z);
GXEnd();
```

## Texture System

### Texture Formats

| Format | Size/Pixel | Description |
|--------|------------|-------------|
| I4     | 0.5 bytes  | 4-bit intensity |
| I8     | 1 byte     | 8-bit intensity |
| IA4    | 1 byte     | 4-bit intensity + 4-bit alpha |
| IA8    | 2 bytes    | 8-bit intensity + 8-bit alpha |
| RGB565 | 2 bytes    | 16-bit RGB |
| RGB8   | 3 bytes    | 24-bit RGB |
| RGBA4  | 2 bytes    | 4-bit RGBA |
| RGBA8  | 4 bytes    | 8-bit RGBA |
| CI4    | 0.5 bytes  | 4-bit color index |
| CI8    | 1 byte     | 8-bit color index |
| COMPI  | Variable   | Compressed (DXT1/3/5) |

### Texture Cache Management

```c
// Texture cache structure
typedef struct {
    void* data;           // Texture data
    int width;            // Width
    int height;           // Height
    GXTextureFormat fmt;  // Format
    int mipmap;           // Mipmaps
    u32 cache_id;         // Cache ID
    u32 lru_order;        // LRU order
} TextureCacheEntry;
```

### Texture Loading Process

1. **Load Image**: Load from PK3 archive
2. **Convert Format**: Convert to GX format
3. **Upload to VRAM**: GXLoadTexObj
4. **Update Cache**: Add to texture cache
5. **Generate Mipmaps**: Optional mipmap generation

## Vertex System

### Vertex Format

```c
typedef struct {
    float position[3];    // Position (x, y, z)
    float normal[3];      // Normal (nx, ny, nz)
    float texcoord[2];    // Texture coordinates (u, v)
    float color[4];       // Vertex color (r, g, b, a)
    float tangent[4];     // Tangent (x, y, z, w)
} Vertex;
```

### Vertex Buffer Management

```c
// Vertex buffer structure
typedef struct {
    void* data;           // Buffer data
    int size;             // Buffer size
    int count;            // Vertex count
    int max_vertices;     // Max vertices
    int max_indices;      // Max indices
    u16* indices;         // Index buffer
} VertexBuffer;
```

## Material System

### Material State

```c
typedef struct {
    GXColor ambient;      // Ambient color
    GXColor diffuse;      // Diffuse color
    GXColor specular;     // Specular color
    float shininess;      // Shininess
    GXTexture* texture;   // Main texture
    GXTexture* lightmap;  // Lightmap texture
    int flags;            // Material flags
} Material;
```

### Material Flags

```c
#define MATF_ALPHA_TEST    (1 << 0)  // Alpha testing enabled
#define MATF_BLEND         (1 << 1)  // Blending enabled
#define MATF_DEPTH_WRITE   (1 << 2)  // Depth write enabled
#define MATF_CULL          (1 << 3)  // Culling enabled
#define MATF_LIGHTING      (1 << 4)  // Lighting enabled
#define MATF_SKIN          (1 << 5)  // Skinned animation
```

## Lighting System

### Light Types

#### Directional Light
```c
typedef struct {
    Vector3 direction;    // Light direction
    GXColor ambient;      // Ambient color
    GXColor diffuse;      // Diffuse color
    GXColor specular;     // Specular color
} DirectionalLight;
```

#### Point Light
```c
typedef struct {
    Vector3 position;     // Light position
    float constant_att;   // Constant attenuation
    float linear_att;     // Linear attenuation
    float quad_att;       // Quadratic attenuation
    GXColor ambient;      // Ambient color
    GXColor diffuse;      // Diffuse color
    GXColor specular;     // Specular color
} PointLight;
```

#### Spot Light
```c
typedef struct {
    Vector3 position;     // Light position
    Vector3 direction;    // Light direction
    float cutoff_angle;   // Cutoff angle
    float constant_att;   // Constant attenuation
    float linear_att;     // Linear attenuation
    float quad_att;       // Quadratic attenuation
    GXColor ambient;      // Ambient color
    GXColor diffuse;      // Diffuse color
    GXColor specular;     // Specular color
} SpotLight;
```

## Post-Processing

### Post-Processing Pipeline

1. **EFB Copy**: Copy to XFB
2. **Post-Process**: Apply effects
3. **Display**: Render to screen

### Post-Processing Effects

- **Bloom**: Glow effect
- **Blur**: Motion blur
- **Color Correction**: Color grading
- **Dithering**: Reduce banding

## Performance Optimization

### Rendering Optimizations

1. **Batching**: Combine draw calls
2. **State Minimization**: Reduce state changes
3. **Texture Atlasing**: Combine textures
4. **LOD**: Level of detail
5. **Culling**: Frustum and occlusion culling

### Memory Optimizations

1. **Texture Compression**: DXT compression
2. **Mipmapping**: Reduce texture memory
3. **Vertex Cache**: Optimize vertex order
4. **Index Buffer**: Reduce vertex duplication

### GPU Optimizations

1. **Display List Caching**: Cache display lists
2. **Texture Cache**: Optimize texture access
3. **Vertex Cache**: Optimize vertex processing
4. **Pipeline Optimization**: Minimize pipeline stalls

## Debugging

### GX Debug Tools

1. **Texture Viewer**: View textures in VRAM
2. **Vertex Viewer**: View vertex data
3. **Display List Viewer**: View display lists
4. **Performance Counter**: Monitor GPU usage

### Common Issues

#### Texture Corruption
- Check texture format
- Verify texture size (power of 2)
- Ensure proper alignment

#### Vertex Issues
- Check vertex format
- Verify vertex buffer size
- Ensure proper indexing

#### Performance Issues
- Reduce draw calls
- Optimize texture usage
- Check for state changes

## Future Enhancements

### Planned Features

1. **Shadow Mapping**: Shadow volume rendering
2. **Post-Processing**: Full post-processing pipeline
3. **Hardware Skinning**: GPU-based skinning
4. **HDR Rendering**: High dynamic range
5. **Multi-pass Rendering**: Advanced lighting

### Roadmap

- **Phase 1**: Basic rendering (completed)
- **Phase 2**: Advanced materials
- **Phase 3**: Post-processing
- **Phase 4**: Advanced lighting
- **Phase 5**: Next-gen features