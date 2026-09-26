cmake_minimum_required(VERSION 3.21)

# fo4-anatomy: the Runtime Database build of the engine (cbp.dll): one DLL for Fallout 4 OG 1.10.163, NG 1.10.984
# and AE 1.11.x, on CommonLibF4RD (extern/CommonLibF4RD). The classic OG-only build (OpenCBP_FO4.sln, the F4SE
# 0.6.23 SDK) stays beside it, untouched, until this one is proven on every runtime.

# Static libraries, shared CRT: one DLL, no spdlog.dll or fmt.dll beside it.
set(VCPKG_TARGET_TRIPLET "x64-windows-static-md" CACHE STRING "")

project(
	OCBPC
	VERSION 1.1.0
	LANGUAGES CXX
)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "")
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("extern/CommonLibF4RD/CommonLibF4" "CommonLibF4")

file(GLOB RD_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/CBPSSE-RD/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/CBPSSE-RD/*.h")

add_library(cbp SHARED ${RD_SOURCES})
set_target_properties(cbp PROPERTIES OUTPUT_NAME "cbp")
target_compile_features(cbp PRIVATE cxx_std_23)
target_compile_definitions(
	cbp
	PRIVATE
		"OCBPC_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}"
		"OCBPC_VERSION_MINOR=${PROJECT_VERSION_MINOR}"
		"OCBPC_VERSION_PATCH=${PROJECT_VERSION_PATCH}"
		"OCBPC_VERSION_STRING=\"${PROJECT_VERSION}\""
		OCBPC_RUNTIME_DATABASE=1
		NOMINMAX
		WIN32_LEAN_AND_MEAN
)
# CBPSSE-RD first; CBPSSE for the pure-maths headers both builds share (AimSolve, Tube, Glance, ...)
target_include_directories(cbp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/CBPSSE-RD" "${CMAKE_CURRENT_SOURCE_DIR}/CBPSSE")
target_link_libraries(cbp PRIVATE CommonLibF4::CommonLibF4)
target_precompile_headers(cbp PRIVATE "CBPSSE-RD/PCH.h")

if(MSVC)
	# A PDB for the Release DLL too: a crash log names our functions only with one.
	target_compile_options(cbp PRIVATE "/permissive-" "/Zc:preprocessor" "/utf-8" "/W4" "/wd4201" "$<$<CONFIG:Release>:/Zi>")
	target_link_options(cbp PRIVATE "$<$<CONFIG:Release>:/DEBUG;/OPT:REF;/OPT:ICF>")
endif()
