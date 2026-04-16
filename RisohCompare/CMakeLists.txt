# CMakeLists.txt --- RisohCompare build configuration
##############################################################################
# RisohEditor --- Another free Win32 resource editor
# Copyright (C) 2017-2025 Katayama Hirofumi MZ
# License: GPL-3 or later
##############################################################################

cmake_minimum_required(VERSION 3.10)

# ---- C++11 support -------------------------------------------------------
if (CMAKE_VERSION VERSION_LESS "3.1")
	if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
	endif()
else()
	set(CMAKE_CXX_STANDARD 11)
endif()

# ---- Platform guard -------------------------------------------------------
if (NOT WIN32)
	message(FATAL_ERROR "RisohCompare requires a Win32 compiler")
endif()

# ---- Include paths --------------------------------------------------------
# RisohCompare/ is searched first (future shadowing); src/ provides the
# shared headers and resource constants.
include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_SOURCE_DIR}/src)

# ---- Source files ---------------------------------------------------------
# Only the non-MMainWnd compilation units from the editor are compiled here.
# MMainWnd_EGA.cpp, MMainWnd_Preview.cpp, MMainWnd_Tests.cpp, MSplitterWnd.cpp
# and EgaBridge.cpp are intentionally excluded because they depend on the full
# MMainWnd class which is not part of RisohCompare.
add_executable(RisohCompare WIN32
	# Resource model layer (from src/)
	${CMAKE_SOURCE_DIR}/src/AccelRes.cpp
	${CMAKE_SOURCE_DIR}/src/DialogRes.cpp
	${CMAKE_SOURCE_DIR}/src/DlgInitRes.cpp
	${CMAKE_SOURCE_DIR}/src/IconRes.cpp
	${CMAKE_SOURCE_DIR}/src/MBitmapDx.cpp
	${CMAKE_SOURCE_DIR}/src/MEditMenuDlg.cpp
	${CMAKE_SOURCE_DIR}/src/MenuRes.cpp
	${CMAKE_SOURCE_DIR}/src/MOleHost.cpp
	${CMAKE_SOURCE_DIR}/src/PackedDIB.cpp
	${CMAKE_SOURCE_DIR}/src/Res.cpp
	${CMAKE_SOURCE_DIR}/src/ResToText.cpp
	${CMAKE_SOURCE_DIR}/src/StringRes.cpp
	${CMAKE_SOURCE_DIR}/src/ToolbarRes.cpp
	${CMAKE_SOURCE_DIR}/src/Utils.cpp
	${CMAKE_SOURCE_DIR}/src/VersionRes.cpp
	# RisohCompare-specific main file
	RisohCompare.cpp)

# ---- Compile definitions --------------------------------------------------
target_compile_definitions(RisohCompare PRIVATE
	UNICODE
	_UNICODE
	USE_GLOBALS=1
	# Suppress common Windows CRT warnings
	_CRT_SECURE_NO_WARNINGS
	_CRT_NON_CONFORMING_WCSTOK
	_SCL_SECURE_NO_WARNINGS)

# ---- Link libraries -------------------------------------------------------
target_link_libraries(RisohCompare
	gdi32
	comctl32
	msimg32
	winmm
	gdiplus
	vfw32
	wininet
	oledlg
	advapi32
	shlwapi
	version
	urlmon
	ole32
	oleaut32
	uuid)

# ---- Static linking (mirrors the main editor's approach) -----------------
if (WIN32)
	if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
		target_compile_options(RisohCompare PRIVATE -static -W -Wno-missing-field-initializers)
		target_link_options(RisohCompare PRIVATE -static -municode)
	elseif (MSVC)
		foreach(flag_var
				CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
				CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE)
			string(REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
		endforeach()
	endif()
endif()

# ---- Output name for 64-bit builds ----------------------------------------
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
	set_target_properties(RisohCompare PROPERTIES OUTPUT_NAME "RisohCompare64")
endif()
