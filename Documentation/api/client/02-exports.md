# List of client.dll exported functions

### `int Initialize( cl_enginefuncs_t *enginefuncs, int version )`

> [!IMPORTANT]
> This function is only called once.

Called after loading DLL and exports engine API to the `client.dll`.
* `enginefuncs` must be set to a pointer to a struct filled with engine function pointers, it will be described in next chapters.
* `version` must be always set to `7`. (HLSDK 1.0 uses version `6`, and is binary incompatible with `7`).
Return value: `0` on error, otherwise success.

### `void HUD_PlayerMoveInit( struct playermove_s *ppmove, int server )`

> [!IMPORTANT]
> This function is only called once.

Called on player movement prediction initialization, before HUD. In GoldSrc, engine only runs prediction loop, the players physics are implemented in `client.dll`.
* `ppmove` must be set to a pointer to a client instance of player movement structure, which also exports it's own API and will be discussed in the next chapters.
* `server` must be always set to `0` on client side.
This function is called only once.

### `void HUD_Init( void )`

> [!IMPORTANT]
> This function is only called once.

Called to initialize the HUD. At this moment engine should be ready to register new commands, console variables and user messages. No rendering or loading graphics done at this moment.

### `int HUD_GetStudioModelInterface( int version, struct r_studio_interface_s **ppinterface, struct engine_studio_api_s *pstudio )`

> [!IMPORTANT]
> This function is only called once.

This function is called after initializing HUD and exports studio model interface to the `client.dll`.
* `version` must be always set to `1`.
* `ppinterface` will be set by `client.dll` to a pointer to `r_studio_interface_s` structure.
* `pstudio` must be set to a pointer to `engine_studio_api_s` structure. The studio model interface will be discussed in the next chapters.
Return value: `0` on error, otherwise success.

### `void HUD_Shutdown( void )`

> [!IMPORTANT]
> This function is only called once.

Called at client shutdown.

### `int HUD_GetHullBounds( int hull, vec3_t mins, vec3_t maxs )`

> [!IMPORTANT]
> This function is only called once.

Called by the engine after `HUD_PlayerMoveInit` to let `client.dll` override hull bounds used for in player movement prediction.

* `hull` is the hull index (0: player standing, 1: player crouched, 2: point hull, 3: large hull).
* `mins` will contain hull mins.
* `maxs` will contain hull maxs.
Return value: if 0, don't override this hull and stop reading, non-zero means hull is valid _and_ there is more.

> [!NOTE]
> This function is broken in most implementations. It might return non-zero value, but don't write anything to the `mins` and `maxs` vectors, so be prepared to have some default values.

### `int HUD_VidInit( void )`

Called when client receives `svc_serverdata` message. It is called before any parsing of that message is done, thus the state is preserved from the previous connection. It lets `client.dll` to re-initialize graphics, if required. At this point, engine is expected to have video subsystem running, but no rendering is done here.

### `int HUD_Redraw( float flTime, int intermission )`

Called each frame to redraw the HUD. Only 2D is drawn here.
* `flTime` must be set to `cl.time`, i.e. synchronized with server.
* `intermission` must be set to `1` during intermission (set through `svc_intermission` message), otherwise it's set to `0`.
Return value: ignored.

### `void HUD_Reset( void )`

Called on demo recording or playback start and stop.

### `int HUD_UpdateClientData( client_data_t *cdata, float flTime )`

Called each frame after taking input.
* `cdata` contains pointer to `client_data_t` structure, populated by engine.
* `flTime` must be set to `cl.time`, i.e. synchronized with server.
Return value: if non-zero, will override engine viewangles and FOV value.

### `void HUD_PlayerMove( struct playermove_s *pmove, int server )`

When prediction is enabled, use this function to run player movement prediction. Note that this correlates to QW/Q2's prediction mechanism. It does not include weapon prediction.

* `pmove` is a pointer to playermove object
* `server` must be always set to `0`

### `char HUD_PlayerMoveTexture( char *name )`

Not used in the engine.

### `int HUD_ConnectionlessPacket( const netadr_t *from, const char *args, char *response_buffer, int *response_buffer_size )`

Called by the engine on unknown connectionless packets. Lets `client.dll` and `server.dll` have custom query protocol.

* `from` is set to network address where this packet is coming from.
* `args` raw network buffer, minus the connectionless packet header (0xFFFFFFFF).
* `response_buffer` set by `client.dll` if there is a response.
* `response_buffer_size` is initialized by the engine with buffer maximum size. Set by `client.dll` if there is a response.
Return value: non-zero if handled.

### `void HUD_Frame( double time )`

Called each frame after sending command to the remote server. No rendering is normally done in this function.

* `time` is the local delta time between previous and this frame (i.e. `host.frametime`)

### `void HUD_PostRunCmd( struct local_state_s *from, struct local_state_s *to, usercmd_t *cmd, int runfuncs, double time, unsigned int random_seed )`

Always called after `HUD_PlayerMove`, even if movement prediction is disabled. Used for weapon prediction stuff.

* `from` is a pointer to `local_state_s` object of the previous predicted frame
* `to` is a pointer to `local_state_s` object of this current frame
* `cmd` is current user command
* `runfuncs` is set to `1` if this frame was never predicted before, and to `0` if it is being predicted again
* `random_seed` is set to `incoming_acknowledged` plus number of predicted frames starting with `1`. This way it's synchronized between client and server.

### `int HUD_Key_Event( int down, int key, const char *current_binding )`

Called on keyboard event.

* `down` is set to `1` if key is being pressed or set to `0` on being released.
* `key` is set to the key number. The key IDs are predefined.
* `current_binding` is set to a null-terminated string with commands bound to this key
Return value: `0` if `client.dll` wants engine to ignore that key.

### `int HUD_AddEntity( int type, struct cl_entity_s *ent, const char *modelname )`

Called before adding entity to the rendering list.

* `type` is set to an entity type, see `entity_state_t::entityType`
* `ent` is a pointer to `cl_entity_t` object.
* `modelname` is a null-terminated string with that entity model name.
Return value: `0` if `client.dll` wants engine to not draw this entity.

### `void HUD_CreateEntities( void )`

Called each frame after all network entities (including players) are linked to let `client.dll` spawn client-side entities.

### `void HUD_StudioEvent( const struct mstudioevent_s *event, const struct cl_entity_s *ent )`

Studio models have events tied to the frame, on which engine calls this function.

* `event` is a pointer to a studio event object
* `ent` is a pointer to a client entity object

### `void HUD_TxferLocalOverrides( struct entity_state_s *state, const struct clientdata_s *client )`

When client processes entity updates, for local client it might be truncated, don't have enough precision, miss some critical info, so engine calls this function as `client.dll` might choose to use client data came from `svc_clientdata` message. Note that it is called on raw, non-interpolated networked entity state.

* `state` is a pointer to the local client entity state coming from the network
* `client` is a pointer to local client data object

### `void HUD_ProcessPlayerState( struct entity_state_s *dst, const struct entity_state_s *src )`

When client processes entity updates, it calls this function for player entities as `client.dll` might want to override some data or fill the missing parts, but usually it just copies from `src` to `dst`.

* `src` is a target pointer to the player entity data (stored in frames, for example)
* `dst` is a source pointer to the player entity data coming from network

### `void HUD_TxferPredictionData( struct entity_state_s *ps, const struct entity_state_s *pps, struct clientdata_s *pcd, const struct clientdata_s *ppcd, struct weapon_data_s *wd, const weapon_data_s *pwd )`

When client receives `svc_clientdata` message, this function is called before any parsing is done, so that the `client.dll` fills in data from prediction.

* `ps`, `pcd`, `wd` are pointers to current network frame data.
* `pps`, `ppcd`, `pwd` are pointers to predicted frame data.

### `void HUD_TempEntUpdate( double frametime, double client_time, double cl_gravity, TEMPENTITY **ppTempEntFree, TEMPENTITY **ppTempEntActive, int ( *AddVisibleEntity )( cl_entity_t *pEntity ), void ( *TempEntPlaySound)( TEMPENTITY *pTemp, float damp ))`

Called each frame after network entities are processed and after `HUD_CreateEntities`, to let `client.dll` process temporary entities logic.

* `frametime` is the delta between `cl.time` and `cl.oldtime`, i.e. server time.
* `client_time` is `cl.time`
* `gravity` is the synchronized gravity value from the server
* `ppTempEntFree` is a pointer to the head of linked list of free temp entities
* `ppTempEntActive` is a pointer to the head of linked list of active temp entities
* `AddVisibleEntity` is a pointer to function that lets `client.dll` add this entity to the rendering list.
* `TempEntPlaySound` is a pointer to function that's called by `client.dll` when temp entity needs to play predefined hit sound. The `damp` argument of this argument only makes sound to NOT play, if it's zero or negative.

### `void HUD_DrawNormalTriangles( void )`

Called each rendering frame to let `client.dll` draw custom solid triangles through TriAPI or direct OpenGL calls.

### `void HUD_DrawTransparentTriangles( void )`

Called each rendering frame to let `client.dll` draw custom transparent triangles through TriAPI or direct OpenGL calls.

### `struct cl_entity_s *HUD_GetUserEntity( int index )`

Called by engine when beam start/end indices are negative, thus allowing attaching beams to a temporary or client-only entity.

* `index` is the fixed up entity index, as beam start/end indices encode real entity index in low 12 bits (i.e. `beament_start & 0xFFF`).

### `void Demo_ReadBuffer( int size, unsigned char *buffer )`

Called by engine on demo playback, if `client.dll` saved some custom data on demo recording prior.

* `size` is the size of buffer in bytes
* `buffer` is the pointer to custom data stored by `client.dll` in demo

### `void CAM_Think( void )`

Called each frame before rendering starts to let `client.dll` run custom camera logic, like advanced thirdperson follow camera for example.

### `int CL_IsThirdPerson( void )`

Returns non-zero value if camera is in thirdperson mode, lets engine figure out whether add local client entity to the rendering list or not.

### `void CL_CameraOffset( vec3_t offset )`

Not used in the engine.

### `void CL_CreateMove( float frametime, usercmd_t *cmd, int active )`

Called when `usercmd_t` is being created to let `client.dll` record user commands before they are being sent over the network.

* `frametime` is the delta time between previous and current frames.
* `cmd` is the pointer to `usercmd_t` object, where player's intentions and impulses are added.
* `active` is set to `1` when client is finished signing on to the server (as movement commands are being sent even if client is not fully spawned yet).

### `void IN_ActivateMouse( void )`

Called from similarly named NQ/QW function.

### `void IN_DeactivateMouse( void )`

Called from similarly named NQ/QW function.

### `void IN_MouseEvent( int mstate )`

Called from similarly named NQ/QW function.

### `void IN_Accumulate( void )`

Called from similarly named NQ/QW function.

### `void IN_ClearStates( void )`

Called from similarly named NQ/QW function.

### `void V_CalcRefdef( struct ref_params_s *params )`

Called each frame before rendering starts. This is close to the similarly named function found in NQ/QW and lets `client.dll` to run custom view logic.

* `params` is the refdef parameters object. It is also used as return value and might request from the engine to not draw anything (by `onlyClientDraws` field) or to run this multiple times (by `nextview` field)

### `kbutton_t *KB_Find( const char *name )`

Called by engine to find extra keys and their state.

Only few are really used by the engine:
* `in_mlook` for mouse look
* `in_jlook` for joystick look
* `in_graph` for net_graph toggle

Return value: returns pointer to `kbutton_t` if found. `kbutton_t` structure matches the same structure that can be found in NQ and QW.

### `void HUD_DirectorMessage( int size, void *buf )`

> [!IMPORTANT]
> This function is optional.

Called to notify `client.dll` about an `svc_director` message. This feature is used for HLTV, though mods use it to spawn text messages or execute commands (bypassing `svc_stufftext` filter in old `client.dll`) but `svc_director` message structure isn't enforced by engine, so it in theory mods might modify it for their own needs.

* `size` is the size of the payload
* `buf` raw `svc_director` payload

### `void HUD_VoiceStatus( int entindex, qboolean talking )`

> [!IMPORTANT]
> This function is optional.

Called to notify `client.dll` about a client status of using voice chat.
* `entindex` an entity index (client index plus one, because zero is always world). When set to -1, notifies `client.dll` about local client recording. When set to -2, notifies `client.dll` about a loopback (i.e. local client's voice message was sent to server and received back)
* `talking` if true, this client is talking

### `void HUD_ChatInputPosition( int *x, int *y )`

> [!IMPORTANT]
> This function is optional.

When called, returns desired X and Y positions of chat box.
