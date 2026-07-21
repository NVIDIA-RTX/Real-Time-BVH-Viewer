cmake_minimum_required(VERSION 3.16)

include(FetchContent)

project(NvBvhViewer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(NOT WIN32)
    message(FATAL_ERROR "This project currently supports Windows only.")
endif()

if ((CMAKE_GENERATOR_PLATFORM MATCHES "x64|x86_64"))
    set(ARCHITECTURE "x64")
elseif ((CMAKE_GENERATOR_PLATFORM MATCHES "ARM64EC|aarch64|arm64|ARM64"))
    set(ARCHITECTURE "arm64")
endif()

#Options
option(BVHVIEWER_BUILD_D3D12 "Build D3D12 viewer library" ON)
option(BVHVIEWER_BUILD_VK "Build Vulkan viewer library" ON)
#--------------------------------

# Set externally if necessary. Leave empty to fetch from GitHub
set(BVHVIEWER_IMGUI_PATH "" CACHE PATH "Path to ImGui source (leave empty to fetch from GitHub)")
set(BVHVIEWER_NVAPI_PATH "" CACHE PATH "Path to NVAPI root (leave empty to fetch from GitHub)")
set(BVHVIEWER_NVAPI_LIB_PATH "" CACHE PATH "Specifies explicit path to nvapi dll")
set(BVHVIEWER_AGS_SDK_PATH "" CACHE PATH "Path to AGS_SDK root (leave empty to fetch from GitHub)")
set(BVHVIEWER_MATHLIB_PATH "" CACHE PATH "Path to MathLib root (leave empty to fetch from GitHub)")
set(BVHVIEWER_GLFW_PATH "" CACHE PATH "Path to GLFW root (leave empty to fetch from GitHub)")
set(BVHVIEWER_BINARY_OUTPUT_PATH "" CACHE PATH "Binary output path")
#--------------------------------

#Environment
set(BVHVIEWER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "")
set(BVHVIEWER_SHADER_ROOT "${BVHVIEWER_ROOT}/Source/Shaders" CACHE PATH "")
set(BVHVIEWER_SHADER_COMPILED_DIR "${CMAKE_BINARY_DIR}/BvhViewerShaders" CACHE PATH "")

if(NOT BVHVIEWER_FETCHCONTENT_BASE_DIR)
    if(NOT FETCHCONTENT_BASE_DIR)
        set(BVHVIEWER_FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps")
    else()
        set(BVHVIEWER_FETCHCONTENT_BASE_DIR "${FETCHCONTENT_BASE_DIR}")
    endif()
endif()

if(NOT BVHVIEWER_FETCHCONTENT_BUILD_DIR)
    if(NOT FETCHCONTENT_BASE_DIR)
        set(BVHVIEWER_FETCHCONTENT_BUILD_DIR "${CMAKE_BINARY_DIR}/_deps")
    else()
        set(BVHVIEWER_FETCHCONTENT_BUILD_DIR "${FETCHCONTENT_BASE_DIR}")
    endif()
endif()
#--------------------------------

function(bvhviewer_provide_targets name path provided_target)
    if(NOT provided_target OR TARGET ${provided_target})
        return()
    endif()

    string(TOLOWER "${name}" name_lower)
    add_subdirectory("${path}" "${BVHVIEWER_FETCHCONTENT_BUILD_DIR}/${name_lower}-build")
endfunction()

# Fetches content by tag
function(bvhviewer_fetch_tag name cache_var repo tag provided_target)
    if(${cache_var})
        if(IS_ABSOLUTE "${${cache_var}}")
            set(resolved_path "${${cache_var}}")
        else()
            set(resolved_path "${CMAKE_CURRENT_SOURCE_DIR}/${${cache_var}}")
        endif()

        if(EXISTS "${resolved_path}")
            set(${cache_var} "${resolved_path}" CACHE PATH "" FORCE)
            bvhviewer_provide_targets("${name}" "${resolved_path}" "${provided_target}")
            return()
        else()
            message(WARNING "${name} path does not exist: '${resolved_path}'. Falling back to GitHub fetch.")
        endif()
    endif()

    # Fetch from URL
    message(STATUS "Fetching ${name} (${tag}) from ${repo}...")
    string(TOLOWER "${name}" name_lower)
    FetchContent_Declare(
        ${name_lower}
        GIT_REPOSITORY ${repo}
        GIT_TAG ${tag}
        GIT_SHALLOW TRUE
        SOURCE_DIR "${BVHVIEWER_FETCHCONTENT_BASE_DIR}/${name_lower}-src"
        BINARY_DIR "${BVHVIEWER_FETCHCONTENT_BUILD_DIR}/${name_lower}-build"
    )
    FetchContent_MakeAvailable(${name_lower})
    set(${cache_var} "${${name_lower}_SOURCE_DIR}" CACHE PATH "" FORCE)
endfunction()

# Fetches content by commit
function(bvhviewer_fetch_commit name cache_var repo commit provided_target)
    if(${cache_var})
        if(IS_ABSOLUTE "${${cache_var}}")
            set(resolved_path "${${cache_var}}")
        else()
            set(resolved_path "${CMAKE_CURRENT_SOURCE_DIR}/${${cache_var}}")
        endif()

        if(EXISTS "${resolved_path}")
            set(${cache_var} "${resolved_path}" CACHE PATH "" FORCE)
            bvhviewer_provide_targets("${name}" "${resolved_path}" "${provided_target}")
            return()
        else()
            message(WARNING "${name} path does not exist: '${resolved_path}'. Falling back to GitHub fetch.")
        endif()
    endif()

    # Fetch from URL
    message(STATUS "Fetching ${name} (${commit}) from ${repo}...")
    string(TOLOWER "${name}" name_lower)
    FetchContent_Declare(
        ${name_lower}
        GIT_REPOSITORY ${repo}
        GIT_TAG ${commit}
        GIT_SHALLOW FALSE
        SOURCE_DIR "${BVHVIEWER_FETCHCONTENT_BASE_DIR}/${name_lower}-src"
        BINARY_DIR "${BVHVIEWER_FETCHCONTENT_BUILD_DIR}/${name_lower}-build"
    )
    FetchContent_MakeAvailable(${name_lower})
    set(${cache_var} "${${name_lower}_SOURCE_DIR}" CACHE PATH "" FORCE)
endfunction()
#--------------------------------

#NVAPI
if(NOT BVHVIEWER_NVAPI_LIB_PATH)
    if (ARCHITECTURE MATCHES "x64")
        bvhviewer_fetch_commit(
            NvApi
            BVHVIEWER_NVAPI_PATH
            https://github.com/NVIDIA/nvapi.git
            832a3673d66a0fdf6d6e522468821d5cbd925f23
            ""
        )
        set(BVHVIEWER_NVAPI_LIB_PATH "${BVHVIEWER_NVAPI_PATH}/amd64/nvapi64.lib")
    endif()
endif()

if(BVHVIEWER_NVAPI_LIB_PATH)
    add_compile_definitions(BVHVIEWER_ENABLE_NVAPI=1)
    set(COMILE_NVAPI_SHADERS 1)
endif()
#--------------------------------

#AGS_SDK
if (ARCHITECTURE MATCHES "x64")
    bvhviewer_fetch_tag(
        AgsSdk
        BVHVIEWER_AGS_SDK_PATH
        https://github.com/GPUOpen-LibrariesAndSDKs/AGS_SDK.git
        v6.3.0
        ""
    )
    add_compile_definitions(BVHVIEWER_ENABLE_AMD_AGS=1)
    set(COMILE_AMD_AGS_SHADERS 1)
endif()
#--------------------------------

#MathLib
bvhviewer_fetch_tag(
    MathLib
    BVHVIEWER_MATHLIB_PATH
    https://github.com/NVIDIA-RTX/MathLib.git
    v11
    MathLib
)
if(TARGET MathLib)
    set_property(TARGET MathLib PROPERTY FOLDER "External")
endif()
#--------------------------------

#GLFW
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
bvhviewer_fetch_tag(
    Glfw
    BVHVIEWER_GLFW_PATH
    https://github.com/glfw/glfw.git
    3.4
    glfw
)
if(TARGET glfw)
    set_property(TARGET glfw PROPERTY FOLDER "External")
endif()
if(TARGET update_mappings)
    set_property(TARGET update_mappings PROPERTY FOLDER "External")
endif()
#--------------------------------

#ImGui
bvhviewer_fetch_tag(
    ImGui
    BVHVIEWER_IMGUI_PATH
    https://github.com/ocornut/imgui.git
    v1.92.5
    ""
)

if(BVHVIEWER_IMGUI_PATH)
    file(GLOB IMGUI_CORE_SOURCES
        "${BVHVIEWER_IMGUI_PATH}/*.cpp"
        "${BVHVIEWER_IMGUI_PATH}/*.h"
    )
    set(IMGUI_GLFW_BACKEND
        "${BVHVIEWER_IMGUI_PATH}/backends/imgui_impl_glfw.cpp"
        "${BVHVIEWER_IMGUI_PATH}/backends/imgui_impl_glfw.h"
    )

    if(CMAKE_GENERATOR_PLATFORM MATCHES "ARM64EC")
        add_compile_definitions(IMGUI_DISABLE_SSE)
    endif()
endif()
#--------------------------------

#Common sources
file(GLOB BVHVIEWER_COMMON_SOURCES
    "${BVHVIEWER_ROOT}/Source/Common/*.cpp"
    "${BVHVIEWER_ROOT}/Source/Common/*.h"
)
file(GLOB BVHVIEWER_SHADER_SOURCES
    "${BVHVIEWER_SHADER_ROOT}/*.comp"
    "${BVHVIEWER_SHADER_ROOT}/Include/*.h"
)
set_source_files_properties(${BVHVIEWER_SHADER_SOURCES} PROPERTIES VS_TOOL_OVERRIDE "None")

file(MAKE_DIRECTORY "${BVHVIEWER_SHADER_COMPILED_DIR}")
set(BVHVIEWER_SHADER_INCLUDE_DIR "${BVHVIEWER_SHADER_ROOT}/Include")
#--------------------------------

#Setup target
function(bvhviewer_setup_target target_name)
    target_compile_definitions(${target_name} PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
        UNICODE
        _UNICODE
    )

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /WX /W4)
        target_compile_options(${target_name} PRIVATE /wd4324)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_options(${target_name} PRIVATE -Wimplicit-fallthrough -Wsuggest-override)
        else()
            target_compile_options(${target_name} PRIVATE /Zc:preprocessor)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Werror -Wimplicit-fallthrough -Wsuggest-override)
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND ARCHITECTURE STREQUAL "x64")
        target_compile_options(${target_name} PRIVATE -msse4.2)
    endif()

    target_include_directories(${target_name} PRIVATE
        "${BVHVIEWER_ROOT}"
        "${BVHVIEWER_ROOT}/Source"
        "${BVHVIEWER_ROOT}/Source/Common"
        "${BVHVIEWER_SHADER_ROOT}/Source/Include"
        "${BVHVIEWER_SHADER_ROOT}/Compiled"
    )

    if(BVHVIEWER_IMGUI_PATH)
        target_include_directories(${target_name} PRIVATE "${BVHVIEWER_IMGUI_PATH}")
        target_include_directories(${target_name} PRIVATE "${BVHVIEWER_IMGUI_PATH}/backends")
    endif()

    if(TARGET MathLib)
        target_link_libraries(${target_name} PRIVATE MathLib)
    endif()

    if(TARGET glfw)
        target_link_libraries(${target_name} PRIVATE glfw)
    endif()

    if(BVHVIEWER_MATHLIB_PATH)
        target_include_directories(${target_name} PRIVATE "${BVHVIEWER_MATHLIB_PATH}")
    endif()

    source_group("Common" FILES ${BVHVIEWER_COMMON_SOURCES})
    source_group("Shaders" FILES ${BVHVIEWER_SHADER_SOURCES})
    
    target_compile_definitions(${target_name} PRIVATE IMGUI_DISABLE_OBSOLETE_FUNCTIONS VK_NO_PROTOTYPES)
endfunction()
#--------------------------------

set(DXIL_SHADER_LIST "")
set(SPIRV_SHADER_LIST "")

#Vulkan
if(BVHVIEWER_BUILD_VK)
    set(IMGUI_VK_BACKEND
        "${BVHVIEWER_IMGUI_PATH}/backends/imgui_impl_vulkan.cpp"
        "${BVHVIEWER_IMGUI_PATH}/backends/imgui_impl_vulkan.h"
    )

    file(GLOB BVHVIEWER_VK_SOURCES
        "${BVHVIEWER_ROOT}/Source/VK/*.cpp"
        "${BVHVIEWER_ROOT}/Source/VK/*.h"
    )

    add_library(NvBvhViewerVK STATIC
        ${BVHVIEWER_VK_SOURCES}
        ${BVHVIEWER_COMMON_SOURCES}
        ${BVHVIEWER_SHADER_SOURCES}
        ${IMGUI_CORE_SOURCES}
        ${IMGUI_GLFW_BACKEND}
        ${IMGUI_VK_BACKEND}
    )
    target_compile_definitions(NvBvhViewerVK PRIVATE VIEWER_API_VK VK_NO_PROTOTYPES)
    bvhviewer_setup_target(NvBvhViewerVK)

    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        target_link_libraries(NvBvhViewerVK PRIVATE Vulkan::Vulkan)
    endif()

    source_group("ImGui" FILES ${IMGUI_CORE_SOURCES})
    source_group("ImGui\\Backends" FILES ${IMGUI_VK_BACKEND} ${IMGUI_GLFW_BACKEND})

    find_program(BVHVIEWER_GLSLANG_VALIDATOR glslangValidator
        HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
    )
    if(NOT BVHVIEWER_GLSLANG_VALIDATOR)
        message(FATAL_ERROR "glslangValidator not found. Install Vulkan SDK or set VULKAN_SDK.")
    endif()

    function(bvhviewer_add_glsl_shader shader_list base_name name_postfix)
        set(source_file "${BVHVIEWER_SHADER_ROOT}/${base_name}.comp")
        set(out_header_name "${BVHVIEWER_SHADER_COMPILED_DIR}/${base_name}.spirv${name_postfix}.h")
        set(var_name "g_${base_name}_spirv${name_postfix}")

        add_custom_command(
            OUTPUT "${out_header_name}"
            COMMAND "${BVHVIEWER_GLSLANG_VALIDATOR}"
                "${source_file}"
                -V
                --glsl-version 460
                "-I${BVHVIEWER_SHADER_INCLUDE_DIR}"
                -DGLSL
                -DNVIDIA
                --vn "${var_name}"
                -o "${out_header_name}"
                ${ARGN}
            DEPENDS ${BVHVIEWER_SHADER_SOURCES}
            COMMENT "Compiling GLSL: ${base_name}${name_postfix}"
            VERBATIM
        )

        list(APPEND ${shader_list} "${out_header_name}")
        set(${shader_list} "${${shader_list}}" PARENT_SCOPE)
    endfunction()

    bvhviewer_add_glsl_shader(SPIRV_SHADER_LIST "InstanceDescCopy" "")
    bvhviewer_add_glsl_shader(SPIRV_SHADER_LIST "IndirectOpsParserVK" "")
    bvhviewer_add_glsl_shader(SPIRV_SHADER_LIST "BvhVisualization" "_Base")
    bvhviewer_add_glsl_shader(SPIRV_SHADER_LIST "BvhVisualization" "_LSS" --target-env spirv1.4 -DGLSL_ENABLE_LSS)
    bvhviewer_add_glsl_shader(SPIRV_SHADER_LIST "BvhVisualization" "_PositionFetch" -DGLSL_ENABLE_POSITION_FETCH)
    bvhviewer_add_glsl_shader(SPIRV_SHADER_LIST "BvhVisualization" "_Full" --target-env spirv1.4 -DGLSL_ENABLE_LSS -DGLSL_ENABLE_POSITION_FETCH)

    target_include_directories(NvBvhViewerVK PRIVATE ${BVHVIEWER_SHADER_COMPILED_DIR})
    target_include_directories(NvBvhViewerVK PRIVATE ${BVHVIEWER_SHADER_INCLUDE_DIR})

    add_custom_target(CompileGLSLShaders DEPENDS ${SPIRV_SHADER_LIST})
    add_dependencies(NvBvhViewerVK CompileGLSLShaders)
    set_property(TARGET CompileGLSLShaders PROPERTY FOLDER "Shaders")
endif()
#--------------------------------

#D3D12
if(BVHVIEWER_BUILD_D3D12)
    set(IMGUI_D3D12_BACKEND
        "${BVHVIEWER_IMGUI_PATH}/backends/imgui_impl_dx12.cpp"
        "${BVHVIEWER_IMGUI_PATH}/backends/imgui_impl_dx12.h"
    )

    file(GLOB BVHVIEWER_D3D12_SOURCES
        "${BVHVIEWER_ROOT}/Source/D3D12/*.cpp"
        "${BVHVIEWER_ROOT}/Source/D3D12/*.h"
    )

    add_library(NvBvhViewerD3D12 STATIC
        ${BVHVIEWER_D3D12_SOURCES}
        ${BVHVIEWER_COMMON_SOURCES}
        ${BVHVIEWER_SHADER_SOURCES}
        ${IMGUI_CORE_SOURCES}
        ${IMGUI_GLFW_BACKEND}
        ${IMGUI_D3D12_BACKEND}
    )
    target_compile_definitions(NvBvhViewerD3D12 PRIVATE VIEWER_API_D3D12)
    bvhviewer_setup_target(NvBvhViewerD3D12)

    if(BVHVIEWER_NVAPI_PATH)
        target_include_directories(NvBvhViewerD3D12 PRIVATE
            "${BVHVIEWER_NVAPI_PATH}"
            "${BVHVIEWER_NVAPI_PATH}/include"
        )
    endif()

    if(BVHVIEWER_NVAPI_LIB_PATH)
        target_link_libraries(NvBvhViewerD3D12 PRIVATE "${BVHVIEWER_NVAPI_LIB_PATH}")
    endif()

    target_link_libraries(NvBvhViewerD3D12 PRIVATE dxgi d3d12 dxguid)
    set_target_properties(NvBvhViewerD3D12 PROPERTIES LINK_FLAGS "/DELAYLOAD:d3d12.dll")

    if(BVHVIEWER_AGS_SDK_PATH)
        target_include_directories(NvBvhViewerD3D12 PRIVATE
                "${BVHVIEWER_AGS_SDK_PATH}"
                "${BVHVIEWER_AGS_SDK_PATH}/ags_lib/inc"
            )
        target_link_libraries(NvBvhViewerD3D12 PRIVATE "$<$<CONFIG:Debug>:${BVHVIEWER_AGS_SDK_PATH}/ags_lib/lib/amd_ags_x64_2022_MTd.lib>")
        target_link_libraries(NvBvhViewerD3D12 PRIVATE "$<$<NOT:$<CONFIG:Debug>>:${BVHVIEWER_AGS_SDK_PATH}/ags_lib/lib/amd_ags_x64_2022_MT.lib>")
    endif()

    source_group("ImGui" FILES ${IMGUI_CORE_SOURCES})
    source_group("ImGui\\Backends" FILES ${IMGUI_D3D12_BACKEND} ${IMGUI_GLFW_BACKEND})

    find_program(BVHVIEWER_DXC dxc
        HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
    )
    if(NOT BVHVIEWER_DXC)
        message(FATAL_ERROR "dxc not found. Install Vulkan SDK or set VULKAN_SDK.")
    endif()

    function(bvhviewer_add_dxc_shader shader_list base_name name_postfix)
        set(source_file "${BVHVIEWER_SHADER_ROOT}/${base_name}.comp")
        set(out_header_name "${BVHVIEWER_SHADER_COMPILED_DIR}/${base_name}.dxil${name_postfix}.h")
        set(var_name "g_${base_name}_dxil${name_postfix}")

        add_custom_command(
            OUTPUT "${out_header_name}"
            COMMAND "${BVHVIEWER_DXC}"
                -Fh "${out_header_name}"
                -Vn "${var_name}"
                -I "${BVHVIEWER_SHADER_INCLUDE_DIR}"
                -I "${BVHVIEWER_NVAPI_PATH}"
                -I "${BVHVIEWER_AGS_SDK_PATH}/ags_lib/hlsl"
                -T cs_6_5
                -E main
                -D D3D12_API
                -D HLSL
                ${ARGN}
                -HV 2021
                -enable-16bit-types
                "${source_file}"
            DEPENDS ${BVHVIEWER_SHADER_SOURCES}
            COMMENT "Compiling HLSL: ${base_name}${name_postfix}"
            VERBATIM
        )
        list(APPEND ${shader_list} "${out_header_name}")
        set(${shader_list} "${${shader_list}}" PARENT_SCOPE)
    endfunction()

    bvhviewer_add_dxc_shader(DXIL_SHADER_LIST InstanceDescCopy "")
    bvhviewer_add_dxc_shader(DXIL_SHADER_LIST IndirectOpsDispatcher "")
    bvhviewer_add_dxc_shader(DXIL_SHADER_LIST IndirectOpsParser "")
    bvhviewer_add_dxc_shader(DXIL_SHADER_LIST MultiIndirectParseClasBuild "")
    bvhviewer_add_dxc_shader(DXIL_SHADER_LIST MultiIndirectParseMove "")
    bvhviewer_add_dxc_shader(DXIL_SHADER_LIST MultiIndirectParseInstantiation "")
    bvhviewer_add_dxc_shader(DXIL_SHADER_LIST MultiIndirectBlasBuildDispatcher "")
    bvhviewer_add_dxc_shader(DXIL_SHADER_LIST MultiIndirectBlasBuildParser "")
    bvhviewer_add_dxc_shader(DXIL_SHADER_LIST MultiIndirectResolveIndirection "")
    bvhviewer_add_dxc_shader(DXIL_SHADER_LIST BvhVisualization "_Base")
    if(COMILE_NVAPI_SHADERS)
        bvhviewer_add_dxc_shader(DXIL_SHADER_LIST BvhVisualization "_NV" -D NV_GPU)
    endif()
    if(COMILE_AMD_AGS_SHADERS)
        bvhviewer_add_dxc_shader(DXIL_SHADER_LIST BvhVisualization "_AMD" -D AMD_GPU)
    endif()

    target_include_directories(NvBvhViewerD3D12 PRIVATE ${BVHVIEWER_SHADER_COMPILED_DIR})
    target_include_directories(NvBvhViewerD3D12 PRIVATE ${BVHVIEWER_SHADER_INCLUDE_DIR})

    add_custom_target(CompileDXCShaders DEPENDS ${DXIL_SHADER_LIST})
    add_dependencies(NvBvhViewerD3D12 CompileDXCShaders)
    set_property(TARGET CompileDXCShaders PROPERTY FOLDER "Shaders")
endif()
#--------------------------------

set(BVHVIEWER_IMGUI_WARNING_SOURCES
    ${IMGUI_CORE_SOURCES}
    ${IMGUI_GLFW_BACKEND}
    ${IMGUI_VK_BACKEND}
    ${IMGUI_D3D12_BACKEND}
)
if(BVHVIEWER_IMGUI_WARNING_SOURCES)
    if(MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set_source_files_properties(${BVHVIEWER_IMGUI_WARNING_SOURCES} PROPERTIES COMPILE_OPTIONS /wd5262)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        include(CheckCXXCompilerFlag)
        set(BVHVIEWER_IMGUI_CLANG_DISABLED_WARNINGS "")
        check_cxx_compiler_flag(-Wuninitialized-const-pointer BVHVIEWER_HAS_WUNINITIALIZED_CONST_POINTER)
        if(BVHVIEWER_HAS_WUNINITIALIZED_CONST_POINTER)
            list(APPEND BVHVIEWER_IMGUI_CLANG_DISABLED_WARNINGS -Wno-uninitialized-const-pointer)
        endif()
        check_cxx_compiler_flag(-Wunused-but-set-variable BVHVIEWER_HAS_WUNUSED_BUT_SET_VARIABLE)
        if(BVHVIEWER_HAS_WUNUSED_BUT_SET_VARIABLE)
            list(APPEND BVHVIEWER_IMGUI_CLANG_DISABLED_WARNINGS -Wno-unused-but-set-variable)
        endif()
        if(BVHVIEWER_IMGUI_CLANG_DISABLED_WARNINGS)
            set_source_files_properties(${BVHVIEWER_IMGUI_WARNING_SOURCES} PROPERTIES COMPILE_OPTIONS "${BVHVIEWER_IMGUI_CLANG_DISABLED_WARNINGS}")
        endif()
    endif()
endif()
#--------------------------------

#Compiled_Shaders.h
function(bvhviewer_append_shader_includes output_file)
    foreach(header IN LISTS ARGN)
        get_filename_component(name "${header}" NAME)
        file(APPEND "${output_file}" "#include \"${name}\"\n")
    endforeach()
endfunction()

set(BVHVIEWER_COMPILED_SHADERS_HEADER "${BVHVIEWER_SHADER_COMPILED_DIR}/Compiled_Shaders.h")
file(WRITE  "${BVHVIEWER_COMPILED_SHADERS_HEADER}" "#pragma once\n\n")

file(APPEND "${BVHVIEWER_COMPILED_SHADERS_HEADER}" "#if defined(VIEWER_API_D3D12)\n")
    bvhviewer_append_shader_includes("${BVHVIEWER_COMPILED_SHADERS_HEADER}" ${DXIL_SHADER_LIST})
file(APPEND "${BVHVIEWER_COMPILED_SHADERS_HEADER}" "#endif\n\n")

file(APPEND "${BVHVIEWER_COMPILED_SHADERS_HEADER}" "#if defined(VIEWER_API_VK)\n")
    bvhviewer_append_shader_includes("${BVHVIEWER_COMPILED_SHADERS_HEADER}" ${SPIRV_SHADER_LIST})
file(APPEND "${BVHVIEWER_COMPILED_SHADERS_HEADER}" "#endif\n")
#--------------------------------
