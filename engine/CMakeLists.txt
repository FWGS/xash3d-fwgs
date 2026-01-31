find_package(SDL2 REQUIRED)
find_package(ALSA REQUIRED)
find_package(BZip2 REQUIRED)
find_package(CURL REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(OPUS REQUIRED opus)
pkg_check_modules(OPUSFILE REQUIRED opusfile)
pkg_check_modules(VORBIS REQUIRED vorbis)
pkg_check_modules(VORBISFILE REQUIRED vorbisfile)
pkg_check_modules(MPG123 REQUIRED libmpg123)

if(ENABLE_FFMPEG)
	pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
		libavformat
		libavcodec
		libswresample
		libswscale
		libavutil
	)

	# Check if the package was found
	if(FFMPEG_FOUND)
		message(STATUS "FFmpeg found")
		message(STATUS "Include Directory: ${FFMPEG_INCLUDE_DIRS}")
		message(STATUS "enabling AVI FFMPEG Support")

		add_compile_definitions(
			HAVE_FFMPEG=1
			ALLOW_UNSUPPORTED_FFMPEG=1
		)

		include_directories(
			${FFMPEG_INCLUDE_DIRS}
		)

	endif()
endif()

# Optional backtrace (disabled by default), there are currently compile errors with it enabled
if (ENABLE_BACKTRACE)
	find_package(Backtrace REQUIRED)
	add_compile_definitions(
		HAVE_LIBBACKTRACE=1
	)
	include_directories(
		${BACKTRACE_INCLUDE_DIRS}
	)
endif()

# Game client library
file(GLOB ENGINE_FILES
	common/*.c
	common/imagelib/*.c
	common/soundlib/*.c
	platform/linux/*.c
	platform/posix/*.c
	platform/sdl2/*.c
	client/*.c
	client/soundlib/*.c
	# client/soundlib/libmpg/*.c # use system libmpg123 instead
	server/*.c
)

add_library(xash SHARED
	"${ENGINE_FILES}"
	common/http/net_http_xash.c
	client/avi/avi_ffmpeg.c
	client/vgui/vgui_draw.c
)

target_compile_definitions(xash PRIVATE
	ENGINE_DLL=1
	XASH_SDL=2
	XASH_REF_SOFT_ENABLED=1
	XASH_REF_GL_ENABLED=1
	HAVE_CURL=1
)

target_include_directories(xash PUBLIC
	./client/
	./client/vgui/
	./server/
	../3rdparty/MultiEmulator/include/
	./common/imagelib/
	./common/soundlib/
	./platform/
	${SDL2_INCLUDE_DIRS}
	${OPUS_INCLUDE_DIRS}
	${OPUSFILE_INCLUDE_DIRS}
	${VORBIS_INCLUDE_FILES}
	${MPG123_INCLUDE_DIRS}
)

target_link_libraries(xash
	public
	stdc++
	m
	pthread
	SDL2::SDL2
	ALSA::ALSA
	MultiEmulator
	BZip2::BZip2
	curl
	library_suffix
	${VORBISFILE_LIBRARIES}
	${OPUSFILE_LIBRARIES}
	${FFMPEG_LIBRARIES}
	${MPG123_LIBRARIES}
)

file(GLOB SERVER_FILES
	server/*.c
	common/*.c
	common/imagelib/*.c
	common/soundlib/*.c
	platform/linux/*.c
	platform/posix/*.c
)

add_executable(xashded
	${SERVER_FILES}
	common/http/net_http_xash.c
)

target_compile_definitions(xashded PRIVATE
	XASH_ENABLE_MAIN=1
	XASH_DEDICATED=1
	ENGINE_DLL=1
)

target_include_directories(xashded PUBLIC
	./client/
	./server/
)

target_link_libraries(xashded
	stdc++
	m
	public
	filesystem_stdio
	library_suffix
	curl
)

if (ENABLE_BACKTRACE)
	target_link_libraries(xash
		${Backtrace_LIBRARIES}
	)
	target_link_libraries(xashded
		${Backtrace_LIBRARIES}
	)
endif()
