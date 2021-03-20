## 2021-03-17..20
- [x] rtx: lower resolution framebuffer + upscale
- [x] rtx: importance sample emissive surface
- [ ] rtx: remove entnity-parsed lights

# Next
- [ ] rtx: naive temporal denoise: mix with previous frame
- [ ] rtx: emissive particles
- [ ] rtx: textures
- [ ] rtx: add fps
	- [ ] rtx: don't group brush draws by texture
		-- this has failed: increases BLAS count to ~3000, halves fps
	- [ ] better AS structure (fewer blases, etc)
	- [ ] rasterize into G-buffer, and only then compute lighting with rtx
- [ ] rtx: better random
- [ ] rtx: some studio models have glitchy geometry

# Planned
- [ ] rtx: cull light sources (dlights and light textures) using bsp
- [ ] enable entity-parsed lights by lightstyles
- [ ] dlight for flashlight seems to be broken
- [ ] restore render debug labels
- [ ] make 2nd commad buffer for resource upload
- [ ] fix sprite blending; there are commented out functions that we really need (see tunnel before the helicopter in the very beginning)
- [ ] fix projection matrix differences w/ gl render
- [ ] bad condition for temp vs map-permanent buffer error message
- [ ] draw more types of beams
- [ ] fix brush blending
- [ ] sprite depth offset
- [ ] fix incorrect viewport sprite culling
- [ ] improve g_camera handling; trace SetViewPass vs RenderScene ...
- [ ] loading to the same map breaks geometry
- [ ] studio model lighting
- [ ] move all consts to vk_const
- [ ] what is GL_Backend*/GL_RenderFrame ???
- [ ] particles
- [ ] decals
- [ ] issue: transparent brushes are too transparent (train ride)
- [ ] render skybox
- [ ] mipmaps
- [ ] lightmap dynamic styles
- [ ] flashlight
- [ ] screenshot
- [ ] fog
- [ ] studio models survive NewMap; need to compactify buffers after removing all brushes
- [ ] sometimes it gets very slow (1fps) when ran under lldb (only on stream?)
- [ ] optimize perf: cmdbuf managements and semaphores, upload to gpu, ...
- [ ] rtx: studio models should not pre-transform vertices with modelView matrix

# Someday
- [ ] start building command buffers in beginframe
- [ ] multiple frames in flight (#nd cmdbuf, ...)
- [ ] cleanup unused stuff in vk_studio.c
- [ ] (helps with RTX?) unified rendering (brush/studio models/...), each model is instance, instance data is read from storage buffers, gives info about vertex format, texture bindings, etc; which are read from another set of storage buffers, ..
- [ ] waf shader build step -- get from upstream
- [ ] embed shaders into binary
- [ ] verify resources lifetime: make sure we don't leak and delete all textures, brushes, models, etc between maps
- [ ] custom allocator for vulkan
- [ ] stats
- [ ] better 2d renderer: fill DRAWQUAD(texture, color, ...) command into storage buffer instead of 4 vertices
- [ ] auto-atlas lots of smol textures: most of model texture are tiny (64x64 or less), can we not rebind them all the time? alt: bindless texture array
- [ ] can we also try to coalesce sprite draw calls?
- [ ] not visibly watertight map brushes
- [ ] collect render_draw_t w/o submitting them to cmdbuf, then sort by render_mode, trans depth, and other parameters, trying to batch as much stuff as possible; only then submit

# Previously

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
