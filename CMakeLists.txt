cmake_minimum_required(VERSION 3.30)

project(xash3d-fwgs
    LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD 99)

add_compile_options(-Wall -fPIC)
if( "${CMAKE_BUILD_TYPE}" STREQUAL "Release" )
    add_compile_options(-O3 -mavx2)
endif()

# Flag to make sanitized build for debug
if (SANITIZER)
    add_compile_options(-fsanitize=address)
    add_link_options(-fsanitize=address)
endif()

add_compile_definitions(
	HAVE_STRNCASECMP=1
	HAVE_STRCASECMP=1
	HAVE_STRCASESTR=1
	HAVE_STRCHRNUL=1
	HAVE_STRLCPY=1
	HAVE_STRLCAT=1
	HAVE_STRNLEN=1
	HAVE_LARGEFILE=1
	XASH_LOW_MEMORY=0
	HAVE_TGMATH_H=1
	HAVE_MEMFD_CREATE=1
	HAVE_DIRENT_D_TYPE=1
	HAVE_OGG=1
	HAVE_OPUSFILE=1
	HAVE_VORBIS=1
	HAVE_VORBISFILE=1
)

# Compile each sub-project

# 3rd party mainui is standalone
function(set_target_postfix ARG)
	# Add function to stub out set_target_postfix and do nothing
endfunction()
add_subdirectory(3rdparty/mainui)

include_directories(
	common/
	engine/
	3rdparty/library_suffix/include/
	public/
	pm_shared/
	engine/common/
	filesystem/
)

add_subdirectory(public)
add_subdirectory(3rdparty/library_suffix)
add_subdirectory(filesystem)
add_subdirectory(ref/soft)

# Externals
set(MULTIEMULATOR_DIR "3rdparty/MultiEmulator/")
file(GLOB MULTIEMULATOR_FILES ${MULTIEMULATOR_DIR}/src/*.c)
add_library(MultiEmulator SHARED
	${MULTIEMULATOR_FILES}
)
target_include_directories(MultiEmulator PRIVATE
	${MULTIEMULATOR_DIR}/src/
	${MULTIEMULATOR_DIR}/include/
)
set_target_properties(MultiEmulator PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${MULTIEMULATOR_DIR}/"
)

add_subdirectory(ref/gl)
add_subdirectory(engine)
add_subdirectory(game_launch)

