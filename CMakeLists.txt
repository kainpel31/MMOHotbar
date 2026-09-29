cmake_minimum_required(VERSION 3.21)

# ---- Cache build vars ----
option(COPY_BUILD "Copy the build output to the Skyrim directory." TRUE)
macro(set_from_environment VARIABLE)
	if (NOT DEFINED ${VARIABLE} AND DEFINED ENV{${VARIABLE}})
		set(${VARIABLE} $ENV{${VARIABLE}})
	endif ()
endmacro()

set_from_environment(MMOHOTBAR_DEPLOY_DIR)
if (NOT DEFINED MMOHOTBAR_DEPLOY_DIR)
	set(
		MMOHOTBAR_DEPLOY_DIR
		"F:/Modding/MO2 Data/Mods/MMO Hotbar/SKSE/Plugins"
		CACHE PATH
		"Directory to copy the built plugin and PDB into after a successful build."
	)
else ()
	set(
		MMOHOTBAR_DEPLOY_DIR
		"${MMOHOTBAR_DEPLOY_DIR}"
		CACHE PATH
		"Directory to copy the built plugin and PDB into after a successful build."
	)
endif ()

# ---- Vcpkg check ----
set_from_environment(VCPKG_ROOT)
if (NOT DEFINED VCPKG_ROOT)
	message(
		FATAL_ERROR
		"Variable VCPKG_ROOT is not set."
	)
endif ()

# ---- Include guards ----
if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
	message(
		FATAL_ERROR
			"In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there."
	)
endif()

set(Boost_USE_STATIC_RUNTIME OFF CACHE BOOL "")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "")
set(MMOHOTBAR_VERSION "2.0.0" CACHE STRING "Project version override")

# ---- Project ----
# MMOHotbar 2.0: full fork of STB Hotkey System (GPL-3.0-only, see LICENSE) +
# MMO hotbar HUD layer. Upstream: https://github.com/STB-Team/STB-Hotkey-System
# SWF helper in src/swfhelper/ adapted from Dynamic Inventory Icon Injector by
# JerryYOJ (GPL-3.0) with attribution headers kept.

project(
	MMOHotbar
	VERSION ${MMOHOTBAR_VERSION}
	LANGUAGES CXX
)

configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.h.in
	${CMAKE_CURRENT_BINARY_DIR}/include/Version.h
	@ONLY
)

configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in
	${CMAKE_CURRENT_BINARY_DIR}/version.rc
	@ONLY
)

# ---- Globals ----

if (MSVC)
	add_compile_definitions(
		_UNICODE
	)

	if (NOT ${CMAKE_GENERATOR} STREQUAL "Ninja")
		add_compile_options(
			/MP	# Build with Multiple Processes
		)
	endif ()
endif ()

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)

set(Boost_USE_STATIC_LIBS ON)




# ---- Dependencies ----

# CommonLibSSE-NG: one DLL for SE 1.5.97, AE 1.6.x and 1.7.x.
#
# The library is a checkout, not a vcpkg dependency. alandtse/CommonLibSSE-NG (the
# maintained fork) is what CI builds against, pinned by COMMONLIBSSE_NG_COMMIT in
# .github/workflows/build.yml, and the same fork is what a local build should point at.
# The vcpkg port (commonlibsse-ng 3.7.0, from the colorglass registry) is deliberately
# NOT a dependency any more: it classifies the runtime by MINOR version (case 6 -> AE),
# so an AE 1.7.x game is mistaken for SE and the plugin would resolve SE-only offsets
# (see src/RE/Offsets.Ext.h) against an AE binary. The fork classifies "minor >= 6" as
# AE, which is what we need.
#
# Where the checkout comes from, in order:
#   1. -DCommonLibSSEPath_NG=... or the CommonLibSSEPath_NG environment variable;
#   2. _tmp/CommonLibSSE-NG-ng inside this repo, so unpacking the archive there is
#      enough and no environment variable is needed. _tmp/ is gitignored, as is
#      "Asset dont push to git" where the archive is kept.
# Set STB_USE_VCPKG_COMMONLIB=ON to force the vcpkg port instead; it is no longer
# installed by vcpkg.json, so that route expects you to have installed it yourself.
#
# Building it from source means OUR vcpkg manifest has to carry what the library needs:
# its own manifest is not read by add_subdirectory. spdlog and directxtk come from its
# find_package calls, but rapidcsv is only a find_path -- and a NOTFOUND variable that
# the library then feeds to target_include_directories is a fatal generate error, not a
# warning. It is in vcpkg.json because of exactly that.
option(STB_USE_VCPKG_COMMONLIB "Use a self-installed vcpkg commonlibsse-ng port instead of a CommonLibSSE-NG checkout" OFF)
set_from_environment(CommonLibSSEPath_NG)
if (NOT STB_USE_VCPKG_COMMONLIB AND NOT DEFINED CommonLibSSEPath_NG AND
	EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/_tmp/CommonLibSSE-NG-ng")
	set(CommonLibSSEPath_NG "${CMAKE_CURRENT_SOURCE_DIR}/_tmp/CommonLibSSE-NG-ng")
endif ()
if (NOT STB_USE_VCPKG_COMMONLIB AND DEFINED CommonLibSSEPath_NG AND EXISTS "${CommonLibSSEPath_NG}")
	message(STATUS "CommonLibSSE-NG from: ${CommonLibSSEPath_NG}")
	set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
	# The library publishes a prebuilt CommonLibSSE.lib for its release tags: SHA256-checked,
	# accepted only when the bundle toolset matches the consumer cl.exe, falling back to a
	# source build otherwise. Left ON because compiling it is ~400 translation units and dominates
	# the run, and the pin is that same release commit. -DCOMMONLIB_PREBUILT=OFF compiles it instead.
	set(COMMONLIB_PREBUILT ON CACHE BOOL "" FORCE)
	# Skyrim VR off, and this one is not optional: the library takes ENABLE_SKYRIM_VR ON by
	# default and BSVRInterface.h does #include "openvr.h" inside that guard, while the
	# headers come from extern/openvr -- a folder that is present in the checkout but EMPTY
	# (no openvr.h, no openvr_api.lib). The prebuilt bundle is where those normally come
	# from, which a source build cannot find. We are a
	# Skyrim SE/AE plugin: BSOpenVR.cpp compiles to an empty TU with VR off, and the
	# SE+AE pair still defines HAS_SKYRIM_MULTI_TARGETING, which is what lets one DLL
	# serve SE 1.5.97 and AE 1.6/1.7.
	set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
	add_subdirectory("${CommonLibSSEPath_NG}" ./CommonLibSSE/build)
else ()
	message(WARNING
		"No CommonLibSSE-NG checkout found (CommonLibSSEPath_NG). Falling back to the "
		"vcpkg port, which misclassifies AE 1.7.x as SE -- see the comment block above. "
		"Pass -DCommonLibSSEPath_NG=<path>, set the environment variable, or unpack the "
		"archive into _tmp/CommonLibSSE-NG-ng."
	)
	find_package(CommonLibSSE CONFIG REQUIRED)
endif ()

find_package(spdlog REQUIRED CONFIG)
find_package(nlohmann_json REQUIRED CONFIG)

# ---- Add source files ----

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

source_group(
	TREE
		${CMAKE_CURRENT_SOURCE_DIR}
	FILES
		${SOURCES}
)

source_group(
	TREE
		${CMAKE_CURRENT_BINARY_DIR}
	FILES
		${CMAKE_CURRENT_BINARY_DIR}/include/Version.h
)

# ---- Create DLL ----

add_library(
	${PROJECT_NAME}
	SHARED
	${SOURCES}
	${CMAKE_CURRENT_BINARY_DIR}/include/Version.h
	${CMAKE_CURRENT_BINARY_DIR}/version.rc
	.clang-format
	.editorconfig
)

# Vendored SWF-injection helpers (ported from Dynamic-Inventory-Icon-Injector):
# don't hold third-party code to our /WX bar.
set_source_files_properties(
	${CMAKE_CURRENT_SOURCE_DIR}/src/swfhelper/ImportData.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/swfhelper/SWF/TagFactory.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/swfhelper/SWF/SWFOutputStream.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/swfhelper/SWF/ActionGenerator.cpp
	PROPERTIES COMPILE_OPTIONS "/WX-"
)

target_compile_features(
	${PROJECT_NAME}
	PRIVATE
		cxx_std_23
)

target_include_directories(
	${PROJECT_NAME}
	PRIVATE
		${CMAKE_CURRENT_BINARY_DIR}/include
		${CMAKE_CURRENT_SOURCE_DIR}/include
		${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(
	${PROJECT_NAME}
	PRIVATE
		CommonLibSSE::CommonLibSSE
		nlohmann_json::nlohmann_json
		spdlog::spdlog
)



if (MSVC)
	target_compile_options(
		${PROJECT_NAME}
		PRIVATE
      /std:c17
      
			/sdl	# Enable Additional Security Checks
			/utf-8	# Set Source and Executable character sets to UTF-8
			/Zi	# Debug Information Format

			/permissive-	# Standards conformance

			/Zc:alignedNew	# C++17 over-aligned allocation
			/Zc:auto	# Deduce Variable Type
			/Zc:char8_t
			/Zc:__cplusplus	# Enable updated __cplusplus macro
			/Zc:externC
			/Zc:externConstexpr	# Enable extern constexpr variables
			/Zc:forScope	# Force Conformance in for Loop Scope
			/Zc:hiddenFriend
			/Zc:implicitNoexcept	# Implicit Exception Specifiers
			/Zc:lambda
			/Zc:noexceptTypes	# C++17 noexcept rules
			/Zc:preprocessor	# Enable preprocessor conformance mode
			/Zc:referenceBinding	# Enforce reference binding rules
			/Zc:rvalueCast	# Enforce type conversion rules
			/Zc:sizedDealloc	# Enable Global Sized Deallocation Functions
			/Zc:strictStrings	# Disable string literal type conversion
			/Zc:ternary	# Enforce conditional operator rules
			/Zc:threadSafeInit	# Thread-safe Local Static Initialization
			/Zc:tlsGuards
			/Zc:trigraphs	# Trigraphs Substitution
			/Zc:wchar_t	# wchar_t Is Native Type

			/external:anglebrackets
			/external:W0

			/W4	# Warning level
			/WX	# Warning level (warnings are errors)

			"$<$<CONFIG:DEBUG>:>"
			"$<$<CONFIG:RELEASE>:/Zc:inline;/JMC-;/Ob3>"
	)

	target_link_options(
		${PROJECT_NAME}
		PRIVATE
			/WX	# Treat Linker Warnings as Errors

			"$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
			"$<$<CONFIG:RELEASE>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>"
	)
endif ()

target_precompile_headers(
	${PROJECT_NAME}
	PRIVATE
		src/PCH.h
)
if (COPY_BUILD)
	message(STATUS "Post-build copy destination: ${MMOHOTBAR_DEPLOY_DIR}")
	add_custom_command(
		TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${PROJECT_NAME}> "${MMOHOTBAR_DEPLOY_DIR}"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_PDB_FILE:${PROJECT_NAME}> "${MMOHOTBAR_DEPLOY_DIR}"
	)
endif ()
