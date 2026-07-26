# GameCube Asset Deployment

This document describes the asset deployment system for Xash3D on GameCube.

## Overview

The GameCube port uses a PK3-based asset deployment system, similar to the PC version, but with GameCube-specific optimizations.

## Asset Types

### 1. Texture Assets
- **Format**: PNG, TGA, or custom texture format
- **Target**: DXT1, DXT3, DXT5, or RGBA8
- **Size**: Power of 2 dimensions
- **Mipmaps**: Generated automatically

### 2. Model Assets
- **Format**: MDL (Half-Life model format)
- **Target**: PowerPC-compatible format
- **Optimization**: Vertex caching, LOD

### 3. Sound Assets
- **Format**: WAV, OGG Vorbis
- **Target**: 16-bit, stereo, 22050 Hz
- **Optimization**: Streaming, caching

### 4. Map Assets
- **Format**: BSP (Binary Space Partitioning)
- **Target**: PowerPC-compatible format
- **Optimization**: PVS, occlusion culling

### 5. Config Assets
- **Format**: CFG (Configuration files)
- **Target**: Text format
- **Optimization**: Binary cache

## Asset Deployment Structure

### PK3 Archive Structure

```
assets.pk3 (PK3 archive)
├─ models/              # 3D models
│  ├─ player/
│  ├─ weapons/
│  ├─ items/
│  └─ world/
├─ sound/               # Sound files
│  ├─ music/
│  ├─ misc/
│  └─ events/
├─ materials/           # Texture materials
│  ├─ sky/
│  ├─ water/
│  ├─ glass/
│  └─ metal/
├─ maps/                # Map files
│  ├─ maps/
│  └─ scripts/
├─ scripts/             # Script files
│  ├─ animations/
│  ├─ effects/
│  └─ ui/
├─ cfg/                 # Configuration files
│  ├─ config.cfg
│  └─ autoexec.cfg
└─ README.txt           # Documentation
```

### Asset Deployment Process

1. **Asset Preparation**: Prepare assets for GameCube
2. **Asset Conversion**: Convert assets to GameCube format
3. **Asset Packaging**: Package assets into PK3 archive
4. **Asset Deployment**: Deploy PK3 archive to GameCube

## Asset Conversion

### Texture Conversion

#### 1. Source Format
- PNG, TGA, or other image formats

#### 2. Conversion Process
```bash
# Convert PNG to GameCube texture format
png2tex input.png output.tex

# Convert TGA to GameCube texture format
tga2tex input.tga output.tex
```

#### 3. Output Format
- DXT1 (8:1 compression)
- DXT3 (4:1 compression)
- DXT5 (4:1 compression)
- RGBA8 (no compression)

### Model Conversion

#### 1. Source Format
- MDL (Half-Life model format)

#### 2. Conversion Process
```bash
# Convert MDL to GameCube model format
mdl2gc input.mdl output.gc
```

#### 3. Output Format
- PowerPC-compatible format
- Optimized vertex cache
- LOD support

### Sound Conversion

#### 1. Source Format
- WAV, OGG Vorbis

#### 2. Conversion Process
```bash
# Convert WAV to GameCube sound format
wav2gc input.wav output.gc

# Convert OGG to GameCube sound format
ogg2gc input.ogg output.gc
```

#### 3. Output Format
- 16-bit, stereo, 22050 Hz
- Streaming support
- Caching support

### Map Conversion

#### 1. Source Format
- BSP (Binary Space Partitioning)

#### 2. Conversion Process
```bash
# Convert BSP to GameCube map format
bsp2gc input.bsp output.gc
```

#### 3. Output Format
- PowerPC-compatible format
- PVS optimization
- Occlusion culling

## Asset Deployment Methods

### Method 1: SD Card Deployment

#### 1. Prepare SD Card
- Format SD card as FAT16 or FAT32
- Create directory structure

#### 2. Deploy Assets
- Copy PK3 archive to SD card
- Insert SD card into GameCube

#### 3. Load Assets
- GameCube reads PK3 archive from SD card
- Assets are loaded into memory

### Method 2: Memory Card Deployment

#### 1. Prepare Memory Card
- Format memory card as GameCube memory card format
- Create directory structure

#### 2. Deploy Assets
- Copy PK3 archive to memory card
- Insert memory card into GameCube

#### 3. Load Assets
- GameCube reads PK3 archive from memory card
- Assets are loaded into memory

### Method 3: Disc Deployment

#### 1. Prepare Disc
- Create ISO image with PK3 archive
- Burn ISO to disc

#### 2. Deploy Assets
- Insert disc into GameCube
- GameCube reads PK3 archive from disc

#### 3. Load Assets
- GameCube reads PK3 archive from disc
- Assets are loaded into memory

## Asset Loading

### Asset Loading Process

1. **Open PK3 Archive**: Open PK3 archive
2. **Read Asset Header**: Read asset header
3. **Load Asset Data**: Load asset data
4. **Convert Asset**: Convert asset to GameCube format
5. **Cache Asset**: Cache asset in memory

### Asset Loading API

```c
// Asset loading API
typedef struct {
    // Open PK3 archive
    void (*OpenPK3)(const char* path);
    
    // Close PK3 archive
    void (*ClosePK3)(void);
    
    // Load asset
    void* (*LoadAsset)(const char* name);
    
    // Unload asset
    void (*UnloadAsset)(void* asset);
    
    // Cache asset
    void (*CacheAsset)(const char* name);
    
    // Uncache asset
    void (*UncacheAsset)(const char* name);
} AssetAPI;
```

### Asset Loading Example

```c
// Load texture asset
void* LoadTexture(const char* name) {
    // Open PK3 archive
    AssetAPI.OpenPK3("assets.pk3");
    
    // Load asset
    void* asset = AssetAPI.LoadAsset(name);
    
    // Close PK3 archive
    AssetAPI.ClosePK3();
    
    return asset;
}

// Unload texture asset
void UnloadTexture(void* asset) {
    AssetAPI.UnloadAsset(asset);
}
```

## Asset Caching

### Asset Caching Strategy

#### 1. LRU Cache
- Use LRU (Least Recently Used) cache
- Cache size: 8 MB
- Cache eviction: LRU

#### 2. Asset Cache Structure
```c
// Asset cache structure
typedef struct {
    void* data;           // Asset data
    int size;             // Asset size
    int ref_count;        // Reference count
    int lru_order;        // LRU order
    const char* name;     // Asset name
} AssetCacheEntry;
```

#### 3. Asset Cache Functions
```c
// Asset cache functions
void* Cache_Asset(const char* name);
void Uncache_Asset(const char* name);
void Cache_Update(void);
void Cache_Clear(void);
```

## Asset Optimization

### Texture Optimization

#### 1. Texture Compression
- Use DXT1 compression (8:1)
- Use DXT3 compression (4:1)
- Use DXT5 compression (4:1)

#### 2. Mipmapping
- Generate mipmaps for all textures
- Use smaller mipmaps for distant objects

#### 3. Texture Atlasing
- Combine multiple textures into one
- Reduce texture switches

### Model Optimization

#### 1. Vertex Optimization
- Use shared vertices
- Use index buffers
- Use vertex caching

#### 2. Geometry Optimization
- Use LOD (Level of Detail)
- Use culling
- Use occlusion culling

#### 3. Animation Optimization
- Use skinning
- Use animation caching
- Use animation compression

### Audio Optimization

#### 1. Audio Compression
- Use OGG Vorbis compression
- Use 16-bit audio
- Use stereo audio

#### 2. Audio Caching
- Cache audio data
- Use streaming for large files
- Use preloading for small files

## Asset Deployment Testing

### Asset Deployment Checklist

- [ ] PK3 archive created
- [ ] Assets converted to GameCube format
- [ ] PK3 archive deployed to GameCube
- [ ] Assets loaded correctly
- [ ] Assets cached correctly
- [ ] No memory leaks detected
- [ ] Performance acceptable

### Asset Deployment Script

```bash
#!/bin/bash
# Asset deployment script

# Prepare assets
echo "Preparing assets..."
./prepare_assets.sh

# Convert assets
echo "Converting assets..."
./convert_assets.sh

# Package assets
echo "Packaging assets..."
./package_assets.sh

# Deploy assets
echo "Deploying assets..."
./deploy_assets.sh

# Test assets
echo "Testing assets..."
./test_assets.sh

# Summary
echo "Asset deployment complete."
```

## Asset Deployment Future Enhancements

### Planned Features

1. **Dynamic Asset Loading**: Dynamic asset loading
2. **Asset Streaming**: Asset streaming
3. **Asset Compression**: Asset compression
4. **Asset Caching**: Asset caching

### Roadmap

- **Phase 1**: Basic asset deployment (completed)
- **Phase 2**: Dynamic asset loading
- **Phase 3**: Asset streaming
- **Phase 4**: Asset compression
- **Phase 5**: Asset caching