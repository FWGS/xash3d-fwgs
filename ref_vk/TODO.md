## 2021-02-06
- [x] alpha test
- [x] compare w/ gl R_SetRendeMode
	- [x] raster state
	- [x] color constants
- [x] culling
- [x] shaders s/map/brush/
- [x] pipeline cache
- [x] swapchain getting stale
- [ ] sprites
	- [x] HUD sprites

# Next
- [ ] studio models

# Planned
- [ ] issue: lightmap sometimes gets corrupted on map load
- [ ] what is GL_Backend*/GL_RenderFrame ???
- [ ] beams
- [ ] particles
- [ ] decals
- [ ] issue: transparent brushes are too transparent (train ride)
- [ ] render skybox
- [ ] mipmaps
- [ ] animated textures
- [ ] lightmap dynamic styles
- [ ] flashlight
- [ ] screenshot
- [ ] RTX

# Someday
- [ ] waf shader build step -- get from upstream
- [ ] embed shaders into binary
- [ ] verify resources lifetime: make sure we don't leak and delete all textures, brushes, models, etc between maps
- [ ] custom allocator for vulkan
- [ ] stats
- [ ] better 2d renderer: fill DRAWQUAD(texture, color, ...) command into storage buffer instead of 4 vertices
