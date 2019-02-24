#pragma once
#ifndef VID_COMMON
#define VID_COMMON

#define FCONTEXT_CORE_PROFILE		BIT( 0 )
#define FCONTEXT_DEBUG_ARB		BIT( 1 )

typedef struct vidmode_s
{
	const char	*desc;
	int			width;
	int			height;
} vidmode_t;


typedef enum
{
	SAFE_NO = 0,
	SAFE_NOMSAA,      // skip msaa
	SAFE_NOACC,       // don't set acceleration flag
	SAFE_NOSTENCIL,   // don't set stencil bits
	SAFE_NOALPHA,     // don't set alpha bits
	SAFE_NODEPTH,     // don't set depth bits
	SAFE_NOCOLOR,     // don't set color bits
	SAFE_DONTCARE     // ignore everything, let SDL/EGL decide
} safe_context_t;


typedef struct
{
	void*	context; // handle to GL rendering context
	int		safe;

	int		desktopBitsPixel;
	int		desktopWidth;
	int		desktopHeight;

	qboolean		initialized;	// OpenGL subsystem started
	qboolean		extended;		// extended context allows to GL_Debug
} glwstate_t;


typedef struct vidstate_s
{
	int		width, height;
	int		prev_width, prev_height;
	qboolean		fullScreen;
	qboolean		wideScreen;
} vidstate_t;

// engine will manage opengl contexts with window system (egl/sdl or wgl/glx if needed)
typedef struct glcontext_s
{
	/// make renderapi defs acessible here?
//	gl_context_type_t	context;
//	gles_wrapper_t	wrapper;
	int		color_bits;
	int		alpha_bits;
	int		depth_bits;
	int		stencil_bits;
	int		msaasamples;

	int		max_multisamples;
} glcontext_t;

extern vidstate_t vidState;
extern glwstate_t		glw_state;
extern glcontext_t glContext;


#define VID_MIN_HEIGHT 200
#define VID_MIN_WIDTH 320

extern convar_t	*vid_fullscreen;
extern convar_t	*vid_displayfrequency;
extern convar_t	*vid_highdpi;
extern convar_t	*gl_wgl_msaa_samples;
void R_SaveVideoMode( int w, int h );
void VID_CheckChanges( void );
const char *VID_GetModeString( int vid_mode );
void VID_StartupGamma( void );

#endif // VID_COMMON
