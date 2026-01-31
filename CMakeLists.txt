# Cmake project for xash3d. Currently only Linux amd64 is supported, other systems might work but haven't been tested

cmake_minimum_required(VERSION 3.30)

project(xash3d-fwgs
	LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD 99)

set(ENABLED_OPTIMIZATIONS "")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
	# Compile flags specific to GCC or Clang
	add_compile_options(-Wall -fPIC)

	# Flag to make sanitized build for debug
	if (SANITIZER)
		add_compile_options(-fsanitize=address)
		add_link_options(-fsanitize=address)
	endif()

	if( "${CMAKE_BUILD_TYPE}" STREQUAL "Release" )
		add_compile_options(-O3)
	endif()

	# Function to check for specific CPU optimizations
	function(check_and_add_optimization flag)
		message(STATUS "Checking for ${flag}...")
		
		# Create a small test source file
		file(WRITE "${CMAKE_BINARY_DIR}/test.cpp" "
		#include <iostream>
		int main() { return 0; }
		")
		
		# Attempt to compile with the optimization flag
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${flag}")
		try_compile(CPU_OPTIMIZATION_TEST "${CMAKE_BINARY_DIR}"
					"${CMAKE_BINARY_DIR}/test.cpp"
					CMAKE_FLAGS -DCMAKE_CXX_FLAGS="${CMAKE_CXX_FLAGS}")

		if(CPU_OPTIMIZATION_TEST)
			message(STATUS "Enabled: ${flag}")
			add_compile_options(${flag})
		else()
			message(STATUS "Not found: ${flag}")
		endif()

		# Clean up the test file
		file(REMOVE("${CMAKE_BINARY_DIR}/test.cpp"))
	endfunction()

	# List of CPU optimizations to check
	set(OPTIMIZATION_FLAGS
		"-mavx2"
		"-mavx"
		"-mfma"
		"-msse4.2"
		"-msse3"
		"-msse2"
		"-msse"
	)

	# Check for each optimization flag
	foreach(OPT_FLAG ${OPTIMIZATION_FLAGS})
		check_and_add_optimization(${OPT_FLAG})
	endforeach()
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

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/)

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

add_subdirectory(ref/gl)
add_subdirectory(engine)
add_subdirectory(game_launch)

