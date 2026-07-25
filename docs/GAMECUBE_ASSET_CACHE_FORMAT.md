# GameCube Asset Cache Format

This document describes the asset cache format for Xash3D on GameCube.

## Overview

The GameCube port uses a custom asset cache format to optimize asset loading and reduce memory usage.

## Cache Format

### Cache Header

```c
// Cache header
typedef struct {
    uint32_t signature;       // Cache signature (0x43414348 = "CACHE")
    uint32_t version;         // Cache version
    uint32_t num_entries;     // Number of entries
    uint32_t data_offset;     // Data offset
    uint32_t data_size;       // Data size
    uint32_t checksum;        // Cache checksum
} CacheHeader;
```

### Cache Entry

```c
// Cache entry
typedef struct {
    char name[64];            // Asset name
    uint32_t offset;          // Data offset
    uint32_t size;            // Data size
    uint32_t type;            // Asset type
    uint32_t flags;           // Asset flags
    uint32_t checksum;        // Asset checksum
    uint32_t lru_order;       // LRU order
} CacheEntry;
```

### Cache Types

```c
// Asset types
#define CACHE_TYPE_TEXTURE    1    // Texture asset
#define CACHE_TYPE_MODEL      2    // Model asset
#define CACHE_TYPE_SOUND      3    // Sound asset
#define CACHE_TYPE_MAP        4    // Map asset
#define CACHE_TYPE_CONFIG     5    // Config asset
```

### Cache Flags

```c
// Asset flags
#define CACHE_FLAG_COMPRESSED  (1 << 0)    // Asset is compressed
#define CACHE_FLAG_ENCRYPTED   (1 << 1)    // Asset is encrypted
#define CACHE_FLAG_STREAMING   (1 << 2)    // Asset is streamed
#define CACHE_FLAG_CACHED      (1 << 3)    // Asset is cached
```

## Cache Structure

### Cache Layout

```
Cache Layout:
├─ Cache Header (256 bytes)
├─ Cache Entries (variable size)
│  ├─ Entry 1 (64 bytes)
│  ├─ Entry 2 (64 bytes)
│  └─ Entry N (64 bytes)
└─ Asset Data (variable size)
```

### Cache File Format

```
Cache File:
├─ Header (256 bytes)
│  ├─ Signature (4 bytes)
│  ├─ Version (4 bytes)
│  ├─ Num Entries (4 bytes)
│  ├─ Data Offset (4 bytes)
│  ├─ Data Size (4 bytes)
│  └─ Checksum (4 bytes)
├─ Entries (N * 64 bytes)
│  ├─ Entry 1 (64 bytes)
│  ├─ Entry 2 (64 bytes)
│  └─ Entry N (64 bytes)
└─ Data (variable size)
```

## Cache Operations

### Cache Creation

```c
// Create cache
void Cache_Create(const char* path, CacheHeader* header);

// Add entry to cache
void Cache_AddEntry(Cache* cache, CacheEntry* entry);

// Add data to cache
void Cache_AddData(Cache* cache, void* data, int size);

// Save cache
void Cache_Save(Cache* cache, const char* path);
```

### Cache Loading

```c
// Load cache
Cache* Cache_Load(const char* path);

// Find entry in cache
CacheEntry* Cache_FindEntry(Cache* cache, const char* name);

// Read data from cache
void* Cache_ReadData(Cache* cache, CacheEntry* entry);

// Unload cache
void Cache_Unload(Cache* cache);
```

### Cache Management

```c
// Update cache
void Cache_Update(Cache* cache);

// Clear cache
void Cache_Clear(Cache* cache);

// Validate cache
int Cache_Validate(Cache* cache);
```

## Cache Optimization

### Cache Compression

#### 1. LZW Compression
- Use LZW compression for assets
- Compression ratio: 2:1 to 3:1

#### 2. Huffman Compression
- Use Huffman compression for assets
- Compression ratio: 1.5:1 to 2:1

#### 3. DXT Compression
- Use DXT compression for textures
- Compression ratio: 4:1 to 8:1

### Cache Caching

#### 1. LRU Cache
- Use LRU (Least Recently Used) cache
- Cache size: 8 MB
- Cache eviction: LRU

#### 2. Cache Hierarchy
- Level 1: Fast cache (1 MB)
- Level 2: Slow cache (4 MB)
- Level 3: Disk cache (3 MB)

### Cache Prefetching

#### 1. Prefetch Strategy
- Prefetch assets based on usage patterns
- Prefetch assets before they are needed

#### 2. Prefetch API
```c
// Prefetch asset
void Cache_Prefetch(const char* name);

// Cancel prefetch
void Cache_CancelPrefetch(const char* name);

// Flush prefetch
void Cache_FlushPrefetch(void);
```

## Cache Testing

### Cache Testing Checklist

- [ ] Cache created correctly
- [ ] Cache entries added correctly
- [ ] Cache data written correctly
- [ ] Cache loaded correctly
- [ ] Cache entries found correctly
- [ ] Cache data read correctly
- [ ] Cache validated correctly
- [ ] No memory leaks detected

### Cache Testing Script

```bash
#!/bin/bash
# Cache testing script

# Create cache
echo "Creating cache..."
./create_cache.sh

# Add entries
echo "Adding entries..."
./add_entries.sh

# Add data
echo "Adding data..."
./add_data.sh

# Save cache
echo "Saving cache..."
./save_cache.sh

# Load cache
echo "Loading cache..."
./load_cache.sh

# Find entries
echo "Finding entries..."
./find_entries.sh

# Read data
echo "Reading data..."
./read_data.sh

# Validate cache
echo "Validating cache..."
./validate_cache.sh

# Summary
echo "Cache testing complete."
```

## Cache Future Enhancements

### Planned Features

1. **Dynamic Cache**: Dynamic cache allocation
2. **Cache Compression**: Cache compression
3. **Cache Prefetching**: Cache prefetching
4. **Cache Hierarchy**: Cache hierarchy

### Roadmap

- **Phase 1**: Basic cache (completed)
- **Phase 2**: Dynamic cache
- **Phase 3**: Cache compression
- **Phase 4**: Cache prefetching
- **Phase 5**: Cache hierarchy