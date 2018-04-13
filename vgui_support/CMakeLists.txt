#
# Copyright (c) 2015 Pavlo Lavrenenko
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#

cmake_minimum_required(VERSION 2.8.0)
project(VGUI_SUPPORT)

set(VGUI_SUPPORT vgui_support)
fwgs_fix_default_msvc_settings()
cmake_dependent_option(VGUI_SUPPORT_OLD_VGUI_BINARY "Build against old version of VGUI, from HLSDK2.3" OFF "MSVC" OFF)

if(NOT MINGW)
	file(GLOB VGUI_SUPPORT_SOURCES *.cpp *.c)
else()
	# Download prebuilt VGUI_support, as there is no way to have same C++ ABI for VC++ and MinGW.
	# Also there is no way to have a target without source code, so let's just have custom install() rule

	# Prebuilt VGUI support is downloaded from github.com/FWGS/vgui_support_bin
	set(FORCE_UNPACK FALSE)
	message(STATUS "Downloading prebuilt vgui_support for MinGW... See vgui_support/CMakeLists.txt for details")
	if(NOT EXISTS ${CMAKE_BINARY_DIR}/vgui_support.zip)
		file(DOWNLOAD https://github.com/FWGS/vgui_support_bin/archive/master.zip ${CMAKE_BINARY_DIR}/vgui_support.zip)
		set(FORCE_UNPACK TRUE)
	endif()

	if(NOT EXISTS ${CMAKE_BINARY_DIR}/vgui_support_prebuilt)
		set(FORCE_UNPACK TRUE)
	endif()

	if(FORCE_UNPACK)
		fwgs_unpack_file(${CMAKE_BINARY_DIR}/vgui_support.zip vgui_support_prebuilt)
	endif()

	# HACKHACK: create empty target
	execute_process(COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_BINARY_DIR}/u_cant_touch_this.cpp)
	set(VGUI_SUPPORT_SOURCES ${CMAKE_BINARY_DIR}/u_cant_touch_this.cpp)
endif()
include_directories( . ../common ../engine ../engine/common ../engine/client ../engine/client/vgui )

add_library(${VGUI_SUPPORT} SHARED ${VGUI_SUPPORT_SOURCES})

set(VGUI_BRANCH "master")
if(VGUI_SUPPORT_OLD_VGUI_BINARY)
	set(VGUI_BRANCH "pre-2013")
endif()
fwgs_library_dependency(${VGUI_SUPPORT} VGUI
	"https://github.com/FWGS/vgui-dev/archive/${VGUI_BRANCH}.zip" "VGUI.zip" "HL_SDK_DIR" "vgui-dev-${VGUI_BRANCH}")
if(MSVC)
	string(REGEX REPLACE "lib$" "dll" VGUI_DLL "${VGUI_LIBRARY}")
	install(FILES ${VGUI_DLL}
		CONFIGURATIONS Debug
		DESTINATION ${LIB_INSTALL_DIR}/Debug/)
	install(FILES ${VGUI_DLL}
		CONFIGURATIONS Release
		DESTINATION ${LIB_INSTALL_DIR}/Release/)
endif()

fwgs_set_default_properties(${VGUI_SUPPORT})
if(NOT MINGW)
	fwgs_install(${VGUI_SUPPORT})

	if(NOT WIN32 AND NOT XASH_NO_INSTALL_VGUI_BIN)
		install(FILES ${VGUI_LIBRARY} DESTINATION ${LIB_INSTALL_DIR}/${LIB_INSTALL_SUBDIR}
			PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
	endif()
else()
	# Use prebuilt.
	# TODO: this allows only HLSDK 2.4 VGUI
	install(FILES ${CMAKE_BINARY_DIR}/vgui_support_prebuilt/vgui_support_bin-master/vgui_support.dll
		DESTINATION ${LIB_INSTALL_DIR}/${LIB_INSTALL_SUBDIR})
endif()
