find_package(SDL2 REQUIRED)
find_package(ALSA REQUIRED)
find_package(BZip2 REQUIRED)
#find_package(CURL REQUIRED)
find_package(Backtrace REQUIRED)

file(GLOB ENGINE_FILES
	common/*.c
	common/imagelib/*.c
	common/soundlib/*.c'
	server/*.c
	platform/linux/*.c
	platform/posix/*.c
)

add_library(xash SHARED
	common/http/net_http_xash.c
	"${ENGINE_FILES}"
)

target_compile_definitions(xash PRIVATE
	ENGINE_DLL=1
	XASH_SDL=2
	XASH_REF_SOFT_ENABLED=1
	XASH_REF_GL_ENABLED=1
#	HAVE_LIBBACKTRACE=1
)

target_include_directories(xash PUBLIC
	./client/
	./server/
	${SDL2_INCLUDE_DIRS}
#	${Backtrace_INCLUDE_DIRS}
)

#target_link_libraries(xash
#	stdc++
#	m
#	pthread
#	SDL2::SDL2
#	ALSA::ALSA
#	MultiEmulator
#	BZip2::BZip2
#	${Backtrace_LIBRARIES}
#	curl
#)
