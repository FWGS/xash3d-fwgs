# Parallel frames
- [ ] allocate for N frames:
	- [x] geometries
	- [-] rt models
		- [-] kusochki
		  - [x] same ring buffer alloc as for geometries
		    - [x] extract as a unit
		- [ ] tlas geom --//--
	- [ ] lights
		- [x] make metadata buffer in lights
		- [ ] join lights grid+meta into a single buffer
		- [ ] put lights data into a cpu-side vk buffer
		- [ ] sync+barrier upload

- scratch buffer:
  - should be fine (assuming intra-cmdbuf sync), contents lifetime is single frame only
- accels_buffer:
  - lifetime: multiple frames; dynamic: some b/tlases get rebuilt every frame
  - opt 1: double buffering
  - opt 2: intra-cmdbuf sync (can't write unless previous frame is done)
- uniform_buffer:
  - lifetime: single frame
- tlas_geom_buffer:
  - similar to scratch_buffer
  - BUT: filled on CPU, so it's not properly synchronsized
  - fix: upload using staging? double/ring buffering?
- light_grid_buffer (+ small lights_buffer):
  - lifetime: single frame
  - BUT: populated by CPU, needs sync; can't just ring-buffer it
  - fixes: double-buffering?
    - staging + sync upload? staging needs to be huge or done in chunks. also, cpu needs to wait on staging upload
	- 2x size + wait: won't fit into device-local-host-visible mem
	- decrease size first?
- kusochki_buffer:
  - lifetime: multiple frames
  - ? who uploads?
  - fix: ring-buffer?
- models_cache
  - lifetimes:
    - static; entire map
	- static; single to multiple frames
	- dynamic; multiple frames
  - : intra-cmdbuf


// O. 1 buffer i bardak v nyom
// - [SSSSAAS.SBAS....]
// - can become extremely fragmented

// I. 2 buffer + bit indirection
// - lives longer than 2 frames [SSS.SSS..SS.....]
// - dynamic [AAAAA->.....<-BBBBBB]
// - high bits in shader point to buffer

// II. 1 buffer bi-directional
// - [SSS.SS..S...|AAAABBBB->...]
//    ^ - long-living "static" stuff
//                 ^ - dynamic ring buffer
// - [SSS.SS..S.|.....<-AAAABBBB] (dynamic split)

# Passes
- [ ] better simple sampling
	- [x] all triangles
	- [x] area based on triangles
	- [ ] clipping?
	- [ ] can we pack polygon lights better? e.g.:
		- each light is strictly a triangle
		- index is offset into triangles
		- layout:
			- vec4(plane) // is it really needed? is early culling important? can we shove area into there too? e.g plane_n.xy,plane_d, area
			- vec4(v0xyz, e_r)
			- vec4(v1xyz, e_g)
			- vec4(v2xyz, e_b)
- [ ] additive transparency
- [ ] bounces
- [ ] skybox shadows

# Next
- [ ] remove surface visibility cache
- [ ] rtx: rename point lights to lampochki
- [ ] rtx: rename emissive surface to surface lights
- [ ] rtx: dynamically sized light clusters
	Split into 2 buffers:
		struct LightCluster { uint16 offset, length; }
		uint8_t data[];

# Planned
- [ ] rtx: shrink payload between shaders
- [ ] improve nonuniformEXT usage: https://github.com/KhronosGroup/Vulkan-Samples/pull/243/files#diff-262568ff21d7a618c0069d6a4ddf78e715fe5326c71dd2f5cdf8fc8da929bc4eR31
- [ ] rtx: experiment with refraction index and "refraction roughness"
- [ ] emissive beams
- [ ] emissive particles/sprites
- [ ] issue: transparent brushes are too transparent (train ride)
	- [ ] (test_shaders_basic.bsp) shows that for brushes at least there are the following discrepancies with gl renderer:
		- [ ] traditional:
			- [ ] anything textured transparent is slightly darker in ref_vk
			- [ ] "Color" render mode should not sample texture at all and use just color
			- [ ] "Texture" looks mostly correct, but ~2x darker than it should be
			- [ ] "Glow" looks totally incorrect, it should be the same as "Texture" (as in ref_gl)
			- [ ] "Additive" is way too dark in ref_vk
	- [ ] rtx:
			- [ ] "Color" should use solid color instead of texture
			- [ ] "Color", "Texture", ("Glow"?) should be able to reflect and refract, likely not universally though, as they might be used for different intended effects in game. figure this out on case-by-case basis. maybe we could control it based on texture names and such.
			- [ ] "Additive" should just be emissive and not reflective/refractive
- [ ] rtx: split ray tracing into modules: pipeline mgmt, buffer mgmt
- [ ] rtx: filter things to render, e.g.: some sprites are there to fake bloom, we don't need to draw them in rtx mode
- [ ] possibly split vk_render into (a) rendering/pipeline, (b) buffer management/allocation, (c) render state
- [ ] rtx: light styles: need static lights data, not clear how and what to do
- [ ] studio models: fix lighting: should have white texture instead of lightmap OR we could write nearest surface lightmap coords to fake light
	- [ ] make it look correct lol
- [ ] studio model types:
	- [x] normal
	- [ ] float
	- [x] chrome
- [ ] more beams types
- [ ] more particle types
- [ ] rtx: better mip lods: there's a weird math that operates on fov degrees (not radians) that we copypasted from ray tracing gems 2 chapter 7. When the book is available, get through the math and figure this out.
- [ ] sane texture memory management: do not allocate VKDeviceMemory for every texture
- [ ] rtx: transparency layering issue, possible approaches:
	- [ ]  trace a special transparent-only ray separately from opaque. This can at least be used to remove black texture areas
- [ ] rtx: sky light/emissive skybox:
	- [ ] consider baking it into a single (or a few localized) kusok that has one entry in light cluster
	- [ ] just ignore sky surfaces and treat not hitting anything as hitting sky. importance-sample by sun direction
	- [ ] pre-compute importance sampling direction by searching for ray-miss directions
- [ ] rtx: better memory handling
	- [ ] robust tracking of memory hierarchies: global/static, map, frame
	- or just do a generic allocator with compaction?
- [ ] rtx: coalesce all these buffers
- [ ] rtx: entity lights
- [ ] rtx: do not rebuild static studio models (most of them). BLAS building takes most of the frame time (~12ms where ray tracing itself is just 3ms)
- [ ] rtx: importance-sample sky light; there are sky surfaces that we can consider light sources
- [ ] cull water surfaces (see c3a2a)
- [ ] create water surfaces once in vk_brush
- [ ] consider doing per-geometry rendermode: brushes can be built only once; late transparency depth sorting for vk render;
- [ ] rtx: too many emissive lights in c3a1b
- [ ] studio models: pre-compute buffer sizes and allocate them at once
- [ ] rtx: denoise
	- [ ] non local means ?
	- [ ] reprojection
	- [ ] SVG+
	- [ ] ...
- [ ] rtx: add fps: rasterize into G-buffer, and only then compute lighting with rtx
- [ ] rtx: bake light visibility in compute shader
- [ ] dlight for flashlight seems to be broken
- [ ] make 2nd commad buffer for resource upload
- [ ] fix sprite blending; there are commented out functions that we really need (see tunnel before the helicopter in the very beginning)
- [ ] fix projection matrix differences w/ gl render
- [ ] bad condition for temp vs map-permanent buffer error message
- [ ] fix brush blending
- [ ] sprite depth offset
- [ ] fix incorrect viewport sprite culling
- [ ] improve g_camera handling; trace SetViewPass vs RenderScene ...
- [ ] studio model lighting
- [ ] move all consts to vk_const
- [ ] what is GL_Backend*/GL_RenderFrame ???
- [ ] particles
- [ ] decals
- [ ] render skybox
- [ ] lightmap dynamic styles
- [ ] better flashlight: spotlight instead of dlight point
- [ ] fog
- [ ] studio models survive NewMap; need to compactify buffers after removing all brushes
- [ ] sometimes it gets very slow (1fps) when ran under lldb (only on stream?)
- [ ] optimize perf: cmdbuf managements and semaphores, upload to gpu, ...
- [ ] ? rtx: studio models should not pre-transform vertices with modelView matrix
- [ ] rtx: non-realtime unbiased mode: make "ground truth" screenshots that take 1e5 samples per pixels and seconds to produce. what for: semi-interactive material tuning, comparison w/ denoise, etc.

# Someday
- [ ] more than one lightmap texture. E.g. sponza ends up having 3 lightmaps
- [ ] nvnsight into buffer memory and stuff
- [ ] start building command buffers in beginframe
- [ ] multiple frames in flight (#nd cmdbuf, ...)
- [ ] cleanup unused stuff in vk_studio.c
- [ ] embed shaders into binary
- [ ] verify resources lifetime: make sure we don't leak and delete all textures, brushes, models, etc between maps
- [ ] custom allocator for vulkan
- [ ] stats
- [ ] better 2d renderer: fill DRAWQUAD(texture, color, ...) command into storage buffer instead of 4 vertices
- [ ] auto-atlas lots of smol textures: most of model texture are tiny (64x64 or less), can we not rebind them all the time? alt: bindless texture array
- [ ] can we also try to coalesce sprite draw calls?
- [ ] brush geometry is not watertight
- [ ] collect render_draw_t w/o submitting them to cmdbuf, then sort by render_mode, trans depth, and other parameters, trying to batch as much stuff as possible; only then submit

# Previously
- [x] loading to the same map breaks geometry
- [x] (helps with RTX?) unified rendering (brush/studio models/...), each model is instance, instance data is read from storage buffers, gives info about vertex format, texture bindings, etc; which are read from another set of storage buffers, ..
- [x] waf shader build step -- get from upstream

## 2021-02-06
- [x] alpha test
- [x] compare w/ gl R_SetRendeMode
	- [x] raster state
	- [x] color constants
- [x] culling
- [x] shaders s/map/brush/
- [x] pipeline cache
- [x] swapchain getting stale
- [x] HUD sprites
- [x] issue: lightmap sometimes gets corrupted on map load

## 2021-02-08
- [x] move entity rendering-enumeration into vk_scene

## 2021-02-10
- [x] refactor brush into brushes and separate rendering/buffer management
- [x] animated textures (accept PR)

## 2021-02-13
- [x] move pipelines from brush to render
- [x] render temp buffer api
- [x] draw studio models somehow
- [x] studio models vk debug markers
- [x] studio models white texture as lightmap
- [x] studio models fixes

## 2021-02-15
- [x] weapon models -- viewmodel
- [x] coalesce studio model draw calls
- [x] initual sprite support

## 2021-02-17
- [x] draw some beams

## 2021-02-20
- [x] refactor vk_render interface:
	- [x] move uniform_data_t to global render state ~inside render_draw_t, remove any mentions of uniform/slots from api; alt: global render state?~
	- [x] rename RenderDraw to SubmitDraw
	- [x] ~add debug label to render_draw_t?;~ alt: VK_RenderDebugNameBegin/End
	- [x] perform 3d rendering on corresponding refapi calls, not endframe
- [x] fix sprite blending

## 2021-02-22
- [x] RTX: load extensions with -rtx arg
- [x] vk_render: buffer-alloc-centric upload and draw api

## 2021-03-06
- [x] (RTX; common) Staging vs on-GPU buffers
- [x] rtx: BLAS construction on buffer unlock
- [x] rtx: ray trace compute shader
- [x] dlight test

## 2021-03-08
- [x] studio models normals
- [x] rtx: geometry indexing

## 2021-03-10
- [x] rtx: dlights
- [x] rtx: dlight shadows
- [x] rtx: dlight soft shadows

## 2021-03-13
- [x] rtx: blend normals according to barycentrics
- [x] rtx: (debug/dev) shader reload
- [x] rtx: make projection matrix independent render global/current/static state
- [x] rtx: model matrices
- [x] rtx: light entities -- still not enough to enlight maps :(
- [x] rtx: path tracing

## 2021-03-15
- [x] rtx: control bounces with cvars
- [x] rtx: device-local buffers -- doesn't affect perf noticeably :(
- [x] rtx: emissive materials
	- [x] rtx: emissive textures
	- [x] rtx: emissive beams

## 2021-03-17..20
- [x] rtx: lower resolution framebuffer + upscale
- [x] rtx: importance sample emissive surface
- [x] rtx: remove entnity-parsed lights
- [x] rtx: naive temporal denoise: mix with previous frame

## 2021-03-22
- [x] rtx: traverse bsp for science!

## 2021-03-28
- [x] bake s/d-lights visibility data into bsp leaves

## 2021-04-06..08
- [x] persistent models
	- [x] load brushes into render model
	- [x] destroy brushes when time comes (when?)
	- [x] rasterize models in renderer

## 2021-04-09
- [x] rtx: build AS for model
- [x] rtx: include pre-built models in TLAS

## 2021-04-10
- [x] rtx: fix tlas rebuild
- [x] rtx: upload kusochki metadata ~~w/ leaves~~
- [x] rtx: add fps
	- [x] rtx: don't group brush draws by texture
	- [x] better AS structure (fewer blases, etc)

## 2021-04-11
- [x] vscode build and debug

## 2021-04-12
- [x] rtx: fix surface-kusok index mismatch
- [x] rtx: try to use light visibility data
	- too few slots for light sources
	- some areas have too many naively visible lights
- [x] rtx: fix light shadow artefacts

## 2021-04-13
- [x] rtx: "toilet error": attempting to get AS device address crashes the driver
- [x] rtx: fix blas destruction on exit
- [x] rtx: sometimes we get uninitialized models

## 2021-04-14..16
- [x] rtx: grid-based light clusters

## 2021-04-17
- [x] rtx: read rad file data

## 2021-04-19
- [x] rtx: light intensity-based light clusters visibility
- [x] rtx: check multiple variants of texture name (wad and non-wad)
- [x] rtx: rad liquids/xeno/... textures

## 2021-04-22
- [x] rtx: fix backlight glitch
- [x] rtx: textures

## 2021-04-24, E86
- [x] rtx: restore studio models

## 2021-05-01, E89
- [x] make a wrapper for descriptor sets/layouts

## 2021-05-03, E90
- [x] make map/frame lifetime aware allocator and use it everywhere: render, rtx buffers, etc

## 2021-05-08, E92
- [x] rtx: weird purple bbox-like glitches on dynamic geometry (tlas vs blas memory corruption/aliasing)
- [x] rtx: some studio models have glitchy geometry

## 2021-05-10, E93
- [x] rtx: don't recreate tlas each frame
- [x] rtx: dynamic models AS caching

## 2021-05-..-17, E93, E94
- [x] rtx: improve AS lifetime/management; i.e. pre-cache them, etc
- [x] add debug names to all of the buffers

## 2021-05-22, E97
- [x] add nvidia aftermath sdk

# 2021-05-24, E98
- [x] rtx: simplify AS tracking

## 2021-05-26, E99
- [x] rtx: fix device lost after map load

## 2021-05-28, E100
- [x] rtx: build acceleration structures in a single queue/cmdbuf

## 2021-06-05, E103
- [x] rtx: dynamic surface lights / dynamic light clusters
- [x] rtx: animated textures
- [x] rtx: attenuate surface lights by normal

## 2021-06-07, E104..
- [x] fix CI for vulkan branch

# 2021-06-09..12, E105..106
- [x] c3a2a: no water surfaces in vk (transparent in gl: *45,*24,*19-21)
- [x] water surfaces

# 2021-06-14, E107
- [x] rtx: optimize water normals. now they're very slow because we R/W gpu mem? yes
- [x] cull bottom water surfaces (they're PLANE_Z looking down)
- [x] fix water normals

## 2021-06-23, E109
- [x] rtx: ray tracing shaders specialization, e.g. for light clusters constants
- [x] rtx: restore dynamic stuff like particles, beams, etc
- [x] rtx: c3a1b: assert model->size >= build_size.accelerationStructureSize failed at vk_rtx.c:347

## 2021-07-17, E110..120
- [x] rtx: ray tracing pipeline
- [x] rtx: fix rendering on AMD
- [x] rtx: split models into a separate module
- [x] rtx: alpha test

## 2021-07-31, E121
- [x] rtx: alpha blending -- did a PoC

## 2021-08-02..04, E122-123
- [x] mipmaps
- [x] rtx: better random

## 2021-08-07, E124
- [x] anisotropic texture sampling
- [x] studio model lighting prep
	- [x] copy over R_LightVec from GL renderer
	- [x] add per-vertex color attribute
	- [x] support per-vertex colors
	- [x] disable lightmaps, or use white texture for it instead

## 2021-08-11, E125
- [x] simplify buffer api: do alloc+lock as a single op

## 2021-08-15, E126
- [x] restore render debug labels
- [x] restore draw call concatenation; brush geoms are generated in a way that makes concatenating them impossible

## 2021-08-16, E127
- [x] better device enumeration

## 2021-08-18, E128
- [x] rtx: fix maxVertex for brushes

## 2021-08-22, E129
- [x] fix depth test for glow render mode
- [x] screenshots

## 2021-08-26, E131
- [x] rtx: material flags for kusochki

## 2021-09-01, E132
- [x] rtx: ingest brdfs from ray tracing gems 2
- [x] rtx: directly select a triangle for light sampling

## 2021-09-04, E133
- [x] rtx: different sbts for opaque and alpha mask
- [x] include common headers with struct definitions from both shaders and c code

## 2021-09-06, E134
- [x] rtx: pass alpha for transparency
- [x] rtx: remove additive/refractive flags in favor or probability of ray continuing further instead of bouncing off
- [x] make a list of all possible materials, categorize them and figure out what to do

# E149
- [x] rtx: remove sun
- [x] rtx: point lights:
	- [x] static lights
		- [x] intensity "fix"
	- [x] dlights
	- [ ] elights
	- [x] intensity fix for d/elights?
	- [x] point light clusters
	- [x] bsp:
		- [x] leaf culling
		- [x] pvs

- [x] rtx: better light culling: normal, bsp visibility, (~light volumes and intensity, sort by intensity, etc~)
- [x] rtx: cluster dlights

## 2021-10-24 E155
- [x] rtx: static lights
	- [x] point lights
	- [x] surface lights

## 2021-10-26 E156
- [x] enable entity-parsed lights by lightstyles

## 2021-12-21 DONE SOMEWHEN
- [x] rtx: dynamic rtx/non-rtx switching breaks dynamic models (haven't seen this in a while)
- [x] run under asan
- [x] rtx: map name to rad files mapping
- [x] rtx: live rad file reloading (or other solution for tuning lights)
- [x] rtx: move entity parsing to its own module
- [x] rtx: configuration that includes texture name -> pbr params mapping, etc. Global, per-map, ...
- [x] rtx: simple convolution denoise (bilateral?)
- [x] rtx: cull light sources (dlights and light textures) using bsp
- [-] crash in PM_RecursiveHullCheck. havent seen this in a while
- [x] rtx: remove lbsp
