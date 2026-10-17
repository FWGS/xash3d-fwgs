cmake_minimum_required(VERSION 3.13)
project(xash3d C CXX)


find_package(PkgConfig REQUIRED)
find_package(SDL2 REQUIRED)
find_package(BZip2 REQUIRED)
pkg_check_modules(SDL2 IMPORTED_TARGET sdl2)
pkg_check_modules(OPENGL IMPORTED_TARGET opengl)

string(TIMESTAMP BUILD_DATE "%Y-%m-%d")

add_definitions(
    -DXASH_BUILD_COMMIT="1242"
    -DXASH_BUILD_BRANCH="ogc"
    -DXASH_BUILD_COMMIT_DATE="${BUILD_DATE}"
    -D_GNU_SOURCE=1
    -DALLOCA_H=<alloca.h>
    -DHAVE_TGMATH_H=1
    -DHAVE_STRNCASECMP=1
    -DHAVE_STRCASECMP=1
    -DHAVE_STRCASESTR=1
    -DHAVE_STRCHRNUL=1
    -DHAVE_STRLCPY=1
    -DHAVE_STRLCAT=1
    -DHAVE_STRNLEN=1
    -DCUSTOM_MODES=1
    -DXASH_STATIC=1

    #-DDEFAULT_DEV=2
)
file(GLOB PUBLIC_SOURCES public/*)
list(REMOVE_ITEM PUBLIC_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/public/build_vcs.c")
add_library(public STATIC ${PUBLIC_SOURCES})
target_include_directories(public PUBLIC ${DEPS_INCLUDE_DIRS} 3rdparty/library_suffix/include common engine public)

add_subdirectory(3rdparty/mpg123/mpg123/ports/cmake)

add_subdirectory(3rdparty/opus/opus)

add_subdirectory(3rdparty/library_suffix)

file(GLOB MULTIEMU_SOURCES 3rdparty/MultiEmulator/src/*)
add_library(MultiEmulator STATIC ${MULTIEMU_SOURCES})
target_include_directories(MultiEmulator PRIVATE
	3rdparty/MultiEmulator/include
	3rdparty/library_suffix/include
	common
	engine
	engine/common
	engine/client
	public
	pm_shared
	filesystem
)

file(GLOB FILESYSTEM_SOURCES filesystem/*)
add_library(filesystem_stdio STATIC ${FILESYSTEM_SOURCES})
target_include_directories(filesystem_stdio PUBLIC
	${DEPS_INCLUDE_DIRS}
	3rdparty/library_suffix/include
	common
	engine
	engine/common
	engine/client
	public
	pm_shared
	filesystem
)
target_compile_definitions(filesystem_stdio PRIVATE
	-DXASH_GAMEDIR="valve"
	-DXASH_LOW_MEMORY=2
	-DSTDINT_H=<stdint.h>
	-DHAVE_STRNICMP=0
	-DHAVE_STRICMP=0
	-DXASH_STATIC_LIBS=1
)
target_compile_options(filesystem_stdio PRIVATE
 -fno-omit-frame-pointer -ffunction-sections -fno-asynchronous-unwind-tables
 -fno-exceptions -fpermissive
)

#cl_dll, dlls, menu goes here
include(makelibrary.txt)

file(GLOB REF_GL_SOURCES
	ref/common/*
	ref/gl/*
	)
add_library(ref_gl STATIC ${REF_GL_SOURCES})
target_include_directories(ref_gl PUBLIC
 	3rdparty/library_suffix/include
 	ref/common
 	ref/gl
 	common
 	engine
 	engine/common
 	engine/common/imagelib
 	public
 	pm_shared
 	filesystem
 	)
 target_compile_definitions(ref_gl PRIVATE
 	-DXASH_LOW_MEMORY=2
 	-DXASH_STATIC_LIBS=1
 	-DXASH_GL_STATIC=1
 	-DREF_DLL=1
 	)
 target_compile_options(ref_gl PRIVATE -fpermissive -fno-exceptions)
 target_link_libraries(ref_gl public)

 file(GLOB ENGINE_SOURCES
     engine/client/vgui/*
     engine/client/avi/*
     engine/client/soundlib/libmpg/*
     engine/client/soundlib/*
     engine/client/input/*
     engine/client/dll_int/*
     engine/client/parse/*
     engine/client/sound/*
     engine/client/*

     engine/common/imagelib/*
     engine/common/soundlib/*
     engine/common/*
     public/*

     engine/server/*

     engine/platform/posix/*
     engine/platform/sdl2/*
     engine/platform/ogc/*
     engine/platform/stub/*
     engine/platform/misc/lib_static.c
     )

# Exclude opus
list(REMOVE_ITEM ENGINE_SOURCES
"${CMAKE_CURRENT_SOURCE_DIR}/engine/client/soundlib/snd_ogg_vorbis.c"
"${CMAKE_CURRENT_SOURCE_DIR}/engine/client/soundlib/snd_ogg_opus.c"
"${CMAKE_CURRENT_SOURCE_DIR}/engine/client/soundlib/ogg_filestream.c"
)

add_executable(xash ${ENGINE_SOURCES}
engine/platform/posix/sys_posix.c
engine/common/http/net_http_xash.c
)
target_compile_definitions(xash PUBLIC
#	-DXASH_DEDICATED=1
	-DXASH_GAMEDIR="valve"
	-DXASH_NO_OGG_CONTAINER=1
	-DHAVE_OGG=0
	-DHAVE_OPUSFILE=0
	-DHAVE_VORBIS=0
	-DHAVE_VORBISFILE=0
	-DHAVE_OPUS=1
	-DXASH_LOW_MEMORY=2
	-DXASH_SDL=2
	-DHAVE_GL=1
	-DGL_STATIC=1
	-DXASH_NO_LIBDL=1
	-DXASH_REF_GL_ENABLED=1
	-DSTATIC_LINKING=1
	-DXASH_STATIC_LIBS=1
	-DNO_VGUI=1
	-DENGINE_DLL=1
	-DXASH_ENABLE_MAIN=1
	-DFRAME_POINTERS=1
	-DBIGENDIAN=1


	#-DDEBUG_LOOKUPS_COUNT=1
	#-D_EXI_DEBUG=1
	)

pkg_check_modules(DEPS REQUIRED sdl2 zlib opengl) # bzip2 #freetype2 #zlib

target_include_directories(xash PUBLIC
	${DEPS_INCLUDE_DIRS}
	common
    engine/server
    engine/client/vgui
    engine/client/avi
    engine/client
	engine
	engine/common
    engine/common/soundlib
    engine/common/imagelib
    filesystem
    public
    pm_shared
    engine/platform
    engine/platform/posix
    engine/platform/ogc
    engine/platform/stub
    3rdparty/library_suffix/include
    3rdparty/MultiEmulator/include
    3rdparty/opus/opus/include
    3rdparty/mpg123/mpg123/src/include

	/opt/devkitpro/portlibs/wii/include/GL
	)

target_link_libraries(xash PUBLIC filesystem_stdio hl cl_dll ref_gl menu vgui
 BZip2::BZip2 Opus::opus
libmpg123 library_suffix pthread ${DEPS_LIBRARIES}
MultiEmulator
opengx
)

dkp_target_generate_symbol_list(xash)
ogc_create_dol(xash)

target_compile_options(xash PUBLIC
	-std=gnu11
	-Os
	-fno-omit-frame-pointer
	-fno-strict-aliasing
	-fno-stack-protector
	-ffunction-sections
	-fdata-sections
	-fno-exceptions
	-freorder-blocks
	-fvisibility=hidden
	-flto=auto
	-Wno-implicit-function-declaration
	-Wno-int-conversion
	-Wno-unused-function
)
target_link_options(xash PUBLIC
	-Wl,--gc-sections
	-fwhole-program
	-Wl,--build-id=none
	-Wl,--wrap=KEYBOARD_GetEvent
)
