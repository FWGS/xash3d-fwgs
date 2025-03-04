cmake_minimum_required(VERSION 3.22)

# Only used to build Android project

project(XASH_ANDROID)

# armeabi-v7a requires cpufeatures library
if(ANDROID)
	include_directories(${ANDROID_NDK}/sources/android/cpufeatures)
	add_library(cpufeatures ${ANDROID_NDK}/sources/android/cpufeatures/cpu-features.c)
	target_link_libraries(cpufeatures dl)
endif()

include(FindPython)

if("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
	set(BUILD_TYPE "debug")
else()
	set(BUILD_TYPE "release")
	list(APPEND WAF_EXTRA_ARGS --enable-poly-opt --enable-lto --enable-limited-debuginfo)
endif()

if(ANDROID_ABI STREQUAL "x86")
	# HACKHACK: I don't know why but engine gets built as 64-bit binary here
	list(APPEND WAF_EXTRA_ARGS -4)
endif()

set(CMAKE_VERBOSE_MAKEFILE ON)


# not cleanest way to get upper directory
set(ENGINE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../")

set(WAF_CC "${CMAKE_C_COMPILER} --target=${CMAKE_C_COMPILER_TARGET}")
set(WAF_CXX "${CMAKE_CXX_COMPILER} --target=${CMAKE_CXX_COMPILER_TARGET}")
set(WAF ${Python_EXECUTABLE} ${ENGINE_SOURCE_DIR}waf -t ${ENGINE_SOURCE_DIR} -o ${CMAKE_CURRENT_BINARY_DIR}/xash3d-fwgs)

# try to build minimal SDL. Enable features as we're gonna use them
set(SDL_RENDER OFF)
set(SDL_POWER OFF)
set(SDL_VULKAN OFF)
set(SDL_DISKAUDIO OFF)
set(SDL_DUMMYAUDIO OFF)
set(SDL_DUMMYVIDEO OFF)
set(SDL_VULKAN OFF)
set(SDL_OFFSCREEN OFF)
set(SDL_STATIC OFF)
add_subdirectory("${ENGINE_SOURCE_DIR}/3rdparty/SDL" SDL)

include(ExternalProject)

# gradle passes backslashes to cmake, how does this even work for everybody else?
string(REPLACE "\\" "/" CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})

ExternalProject_Add(
	Xash3DFWGS
	SOURCE_DIR ${ENGINE_SOURCE_DIR}
	INSTALL_DIR ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
	BUILD_IN_SOURCE TRUE
	DEPENDS SDL2
	BUILD_ALWAYS TRUE
	LOG_CONFIGURE TRUE
	LOG_BUILD TRUE
	LOG_INSTALL TRUE
	LOG_OUTPUT_ON_FAILURE TRUE
	LOG_MERGED_STDOUTERR TRUE

#	USES_TERMINAL_CONFIGURE TRUE
#	USES_TERMINAL_BUILD TRUE
#	USES_TERMINAL_INSTALL TRUE

	# NOTE: setting up WAFLOCK is important to avoid possible race conditions
	CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
		ANDROID_NDK=${ANDROID_NDK}
		BUILD_CMAKE_LIBRARY_OUTPUT_DIRECTORY=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
		WAFLOCK=.lock-waf_android_${ANDROID_ABI}_build
		${WAF} configure -T ${BUILD_TYPE} --android=${ANDROID_ABI},,${CMAKE_SYSTEM_VERSION}
		-s "${ENGINE_SOURCE_DIR}/3rdparty/SDL" --enable-bundled-deps ${WAF_EXTRA_ARGS}

	BUILD_COMMAND ${CMAKE_COMMAND} -E env
		WAFLOCK=.lock-waf_android_${ANDROID_ABI}_build
		${WAF} build -v

	INSTALL_COMMAND ${CMAKE_COMMAND} -E env
		WAFLOCK=.lock-waf_android_${ANDROID_ABI}_build
		${WAF} install --destdir=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
)

add_subdirectory("${ENGINE_SOURCE_DIR}/3rdparty/hlsdk-portable" hlsdk-portable)

# a1ba: without this, xash3d target will be ignored as nothing depends on it
add_dependencies(client Xash3DFWGS)
add_dependencies(server Xash3DFWGS)


