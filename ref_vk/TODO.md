## 2021-02-10
- [x] refactor brush into brushes and separate rendering/buffer management

# Next
- [ ] draw studio models as bounding boxes
- [ ] studio models
- [ ] animated textures (accept PR)

# Planned
- [ ] move all consts to vk_const
- [ ] sprites
- [ ] what is GL_Backend*/GL_RenderFrame ???
- [ ] beams
- [ ] particles
- [ ] decals
- [ ] issue: transparent brushes are too transparent (train ride)
- [ ] render skybox
- [ ] mipmaps
- [ ] lightmap dynamic styles
- [ ] flashlight
- [ ] screenshot
- [ ] fog
- [ ] RTX
- [ ] studio models survive NewMap; need to compactify buffers after removing all brushes

# Someday
- [ ] (helps with RTX?) unified rendering (brush/studio models/...), each model is instance, instance data is read from storage buffers, gives info about vertex format, texture bindings, etc; which are read from another set of storage buffers, ..
- [ ] waf shader build step -- get from upstream
- [ ] embed shaders into binary
- [ ] verify resources lifetime: make sure we don't leak and delete all textures, brushes, models, etc between maps
- [ ] custom allocator for vulkan
- [ ] stats
- [ ] better 2d renderer: fill DRAWQUAD(texture, color, ...) command into storage buffer instead of 4 vertices

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
