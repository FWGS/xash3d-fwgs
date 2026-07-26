# GameCube Endianness Audit

This document describes the endianness considerations and fixes applied to the GameCube port.

## Overview

The GameCube uses **big-endian** byte order (PowerPC architecture), while most development systems use **little-endian** (x86/x64). This requires careful handling of data serialization and deserialization.

## Endianness Basics

### Big-Endian (GameCube)
```
Memory:  0x80000000  0x80000001  0x80000002  0x80000003
Value:   [MSB]       [B1]        [B2]        [LSB]
         0x12        0x34        0x56        0x78
         = 0x12345678
```

### Little-Endian (x86/x64)
```
Memory:  0x00000000  0x00000001  0x00000002  0x00000003
Value:   [LSB]       [B2]        [B1]        [MSB]
         0x78        0x56        0x34        0x12
         = 0x12345678
```

## GameCube Architecture

### PowerPC 750 (G3) CPU
- **Architecture**: PowerPC 750 (G3)
- **Endianness**: Big-endian (default)
- **Byte Order**: MSB first
- **Alignment**: Strict alignment required

### Flipper GPU
- **Architecture**: Custom PowerPC-based GPU
- **Endianness**: Big-endian
- **Texture Format**: Big-endian

### DSP
- **Architecture**: Custom DSP
- **Endianness**: Big-endian
- **Audio Format**: Big-endian

## Endianness Issues

### Data Serialization

#### Problem
Data written on little-endian systems must be converted to big-endian for GameCube.

#### Solution
Use byte-swapping functions for all data serialization:

```c
// Byte-swapping functions
static inline uint16_t Swap16(uint16_t x) {
    return (x << 8) | (x >> 8);
}

static inline uint32_t Swap32(uint32_t x) {
    return (x << 24) | ((x & 0xFF00) << 8) | ((x & 0xFF0000) >> 8) | (x >> 24);
}

static inline uint64_t Swap64(uint64_t x) {
    return (x << 56) | ((x & 0xFF00) << 40) | ((x & 0xFF0000) << 24) | ((x & 0xFF000000) << 8) |
           ((x & 0xFF00000000) >> 8) | ((x & 0xFF0000000000) >> 24) | ((x & 0xFF000000000000) >> 40) | (x >> 56);
}
```

### File Format Issues

#### PK3 Archive Format
PK3 files are ZIP archives with little-endian headers. GameCube must convert to big-endian.

#### Model Format
MDL files contain little-endian data. GameCube must convert to big-endian.

#### Sound Format
WAV files contain little-endian data. GameCube must convert to big-endian.

### Network Protocol Issues

#### Network Packets
Network packets use little-endian format. GameCube must convert to big-endian.

#### Socket Communication
Socket communication requires byte-order conversion.

## Endianness Fixes

### File I/O

#### PK3 Archive
```c
// Read PK3 header
typedef struct {
    uint32_t signature;
    uint16_t version;
    uint16_t flags;
    uint16_t compression;
    uint16_t mtime;
    uint16_t mdate;
    uint32_t crc32;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint16_t filename_length;
    uint16_t extra_length;
} PK3Header;

// Read PK3 header with endianness conversion
PK3Header ReadPK3Header(FILE* file) {
    PK3Header header;
    fread(&header, sizeof(PK3Header), 1, file);
    
    // Convert to big-endian
    header.signature = Swap32(header.signature);
    header.version = Swap16(header.version);
    header.flags = Swap16(header.flags);
    header.compression = Swap16(header.compression);
    header.mtime = Swap16(header.mtime);
    header.mdate = Swap16(header.mdate);
    header.crc32 = Swap32(header.crc32);
    header.compressed_size = Swap32(header.compressed_size);
    header.uncompressed_size = Swap32(header.uncompressed_size);
    header.filename_length = Swap16(header.filename_length);
    header.extra_length = Swap16(header.extra_length);
    
    return header;
}
```

#### Model File
```c
// Read MDL header
typedef struct {
    uint32_t id;
    uint32_t version;
    uint32_t checksum;
    char name[64];
    uint32_t type;
    uint32_t flags;
    uint32_t num_frames;
    uint32_t num_tags;
    uint32_t num_surfaces;
    uint32_t num_skinrefs;
    uint32_t num_vertices;
    uint32_t num_triangles;
    uint32_t num_commands;
    uint32_t vertex_offset;
    uint32_t triangle_offset;
    uint32_t command_offset;
    uint32_t skin_offset;
    uint32_t frame_offset;
    uint32_t tag_offset;
} MDLHeader;

// Read MDL header with endianness conversion
MDLHeader ReadMDLHeader(FILE* file) {
    MDLHeader header;
    fread(&header, sizeof(MDLHeader), 1, file);
    
    // Convert to big-endian
    header.id = Swap32(header.id);
    header.version = Swap32(header.version);
    header.checksum = Swap32(header.checksum);
    header.type = Swap32(header.type);
    header.flags = Swap32(header.flags);
    header.num_frames = Swap32(header.num_frames);
    header.num_tags = Swap32(header.num_tags);
    header.num_surfaces = Swap32(header.num_surfaces);
    header.num_skinrefs = Swap32(header.num_skinrefs);
    header.num_vertices = Swap32(header.num_vertices);
    header.num_triangles = Swap32(header.num_triangles);
    header.num_commands = Swap32(header.num_commands);
    header.vertex_offset = Swap32(header.vertex_offset);
    header.triangle_offset = Swap32(header.triangle_offset);
    header.command_offset = Swap32(header.command_offset);
    header.skin_offset = Swap32(header.skin_offset);
    header.frame_offset = Swap32(header.frame_offset);
    header.tag_offset = Swap32(header.tag_offset);
    
    return header;
}
```

### Network Protocol

#### Network Packet
```c
// Network packet structure
typedef struct {
    uint32_t type;
    uint32_t length;
    uint32_t sequence;
    uint32_t checksum;
    uint8_t data[1024];
} NetworkPacket;

// Send network packet with endianness conversion
void SendNetworkPacket(SOCKET socket, NetworkPacket* packet) {
    // Convert to big-endian
    packet->type = Swap32(packet->type);
    packet->length = Swap32(packet->length);
    packet->sequence = Swap32(packet->sequence);
    packet->checksum = Swap32(packet->checksum);
    
    // Send packet
    send(socket, packet, sizeof(NetworkPacket), 0);
    
    // Convert back to little-endian
    packet->type = Swap32(packet->type);
    packet->length = Swap32(packet->length);
    packet->sequence = Swap32(packet->sequence);
    packet->checksum = Swap32(packet->checksum);
}
```

### Audio Format

#### WAV File
```c
// WAV file header
typedef struct {
    uint32_t chunk_id;
    uint32_t chunk_size;
    uint32_t format;
    uint32_t subchunk1_id;
    uint32_t subchunk1_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    uint32_t subchunk2_id;
    uint32_t subchunk2_size;
} WAVHeader;

// Read WAV header with endianness conversion
WAVHeader ReadWAVHeader(FILE* file) {
    WAVHeader header;
    fread(&header, sizeof(WAVHeader), 1, file);
    
    // Convert to big-endian
    header.chunk_id = Swap32(header.chunk_id);
    header.chunk_size = Swap32(header.chunk_size);
    header.format = Swap32(header.format);
    header.subchunk1_id = Swap32(header.subchunk1_id);
    header.subchunk1_size = Swap32(header.subchunk1_size);
    header.audio_format = Swap16(header.audio_format);
    header.num_channels = Swap16(header.num_channels);
    header.sample_rate = Swap32(header.sample_rate);
    header.byte_rate = Swap32(header.byte_rate);
    header.block_align = Swap16(header.block_align);
    header.bits_per_sample = Swap16(header.bits_per_sample);
    header.subchunk2_id = Swap32(header.subchunk2_id);
    header.subchunk2_size = Swap32(header.subchunk2_size);
    
    return header;
}
```

## Endianness Testing

### Test Cases

#### 1. File I/O Test
- Read PK3 archive
- Read MDL model
- Read WAV sound
- Verify endianness conversion

#### 2. Network Test
- Send network packet
- Receive network packet
- Verify endianness conversion

#### 3. Audio Test
- Load WAV file
- Play audio
- Verify audio quality

### Test Script

```bash
#!/bin/bash
# Endianness test script

# Test file I/O
echo "Testing file I/O..."
./test_file_io

# Test network
echo "Testing network..."
./test_network

# Test audio
echo "Testing audio..."
./test_audio

# Summary
echo "Endianness tests complete."
```

## Endianness Best Practices

### 1. Use Standard Functions
```c
#include <byteswap.h>

// Use standard byte-swap functions
uint16_t x = bswap_16(value);
uint32_t x = bswap_32(value);
uint64_t x = bswap_64(value);
```

### 2. Use Network Functions
```c
#include <arpa/inet.h>

// Use network byte order functions
uint32_t x = htonl(value);
uint16_t x = htons(value);
uint32_t x = ntohl(value);
uint16_t x = ntohs(value);
```

### 3. Use Platform Functions
```c
#include <ppc_intrinsics.h>

// Use PowerPC-specific functions
uint32_t x = __builtin_bswap32(value);
uint64_t x = __builtin_bswap64(value);
```

## Endianness Performance

### Optimization

#### 1. Inline Functions
```c
static inline uint32_t Swap32(uint32_t x) {
    return __builtin_bswap32(x);
}
```

#### 2. SIMD Instructions
```c
// Use Altivec for bulk conversion
vector unsigned int Swap32Vector(vector unsigned int x) {
    return vec_reve(x);
}
```

#### 3. Hardware Acceleration
```c
// Use PowerPC-specific instructions
uint32_t Swap32(uint32_t x) {
    asm volatile ("rlwimi %0, %0, 24, 16, 23" : "+r" (x));
    asm volatile ("rlwimi %0, %0, 8, 8, 15" : "+r" (x));
    asm volatile ("rlwimi %0, %0, 24, 0, 7" : "+r" (x));
    return x;
}
```

## Endianness Future Enhancements

### Planned Features

1. **Automatic Detection**: Auto-detect endianness
2. **Runtime Conversion**: Runtime endianness conversion
3. **Optimized Conversion**: Optimized conversion functions
4. **Testing Framework**: Endianness testing framework

### Roadmap

- **Phase 1**: Basic endianness handling (completed)
- **Phase 2**: Runtime conversion
- **Phase 3**: Optimized conversion
- **Phase 4**: Testing framework
- **Phase 5**: Automatic detection