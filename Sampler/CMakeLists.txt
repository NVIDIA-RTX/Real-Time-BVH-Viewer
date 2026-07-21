cmake_minimum_required(VERSION 3.16)

set(HOST_PROJECT_NAME BvhvSampler)
project(${HOST_PROJECT_NAME} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(NOT WIN32)
    message(FATAL_ERROR "This project currently supports Windows only.")
endif()

#Options
option(SAMPLER_BUILD_D3D12 "Build D3D12 sampler DLL" ON)
option(SAMPLER_BUILD_VK "Build Vulkan sampler DLL" ON)
#--------------------------------

#Environment
set(SAMPLER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(SAMPLER_SOURCE_DIR "${SAMPLER_ROOT}/Source")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${SAMPLER_ROOT}/_Bin")

set(BVHVIEWER_PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../BvhViewer")
set(BVHVIEWER_FETCHCONTENT_BASE_DIR "${SAMPLER_ROOT}/_External" CACHE PATH "" FORCE)
set(BVHVIEWER_FETCHCONTENT_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/_deps" CACHE PATH "" FORCE)
set(BVHVIEWER_BINARY_OUTPUT_PATH "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}" PATH "Binary output path")
set(BVHVIEWER_SHADER_COMPILED_DIR "${CMAKE_CURRENT_BINARY_DIR}/Shaders" CACHE PATH "" FORCE)
set(BVHVIEWER_BUILD_D3D12 ${SAMPLER_BUILD_D3D12} CACHE BOOL "" FORCE)
set(BVHVIEWER_BUILD_VK ${SAMPLER_BUILD_VK} CACHE BOOL "" FORCE)
add_subdirectory("${BVHVIEWER_PROJECT_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/BvhViewer")
#--------------------------------

#Jsonxx
set(SAMPLER_JSONXX_PATH "" CACHE PATH "Path to jsonxx source files")
bvhviewer_fetch_tag(
    jsonxx
    SAMPLER_JSONXX_PATH
    https://github.com/hjiang/jsonxx.git
    master
    ""
)

#Create jsonxx static library
if(SAMPLER_JSONXX_PATH)
    add_library(jsonxx STATIC
        "${SAMPLER_JSONXX_PATH}/jsonxx.cc"
        "${SAMPLER_JSONXX_PATH}/jsonxx.h"
    )
    target_include_directories(jsonxx PUBLIC "${SAMPLER_JSONXX_PATH}")
    target_compile_definitions(jsonxx PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
    set_property(TARGET jsonxx PROPERTY FOLDER "External")
    source_group("" FILES
        "${SAMPLER_JSONXX_PATH}/jsonxx.cc"
        "${SAMPLER_JSONXX_PATH}/jsonxx.h"
    )
endif()
#--------------------------------

#MinHook
set(SAMPLER_MINHOOK_PATH "" CACHE PATH "Path to minhook source files")
bvhviewer_fetch_tag(
    minhook
    SAMPLER_MINHOOK_PATH
    https://github.com/TsudaKageyu/minhook.git
    v1.3.4
    ""
)

if(SAMPLER_MINHOOK_PATH)
    set_property(TARGET minhook PROPERTY FOLDER "External")
endif()
#--------------------------------

#Common compile definitions
function(samplerapp_setup_target target_name)
    target_compile_definitions(${target_name} PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
        _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
        UNICODE
        _UNICODE
    )

    target_include_directories(${target_name} PRIVATE "${SAMPLER_SOURCE_DIR}")
    target_include_directories(${target_name} PRIVATE "${SAMPLER_SOURCE_DIR}/Common")
endfunction()
#--------------------------------

#Common sources
file(GLOB SAMPLER_COMMON_SOURCES
        "${SAMPLER_SOURCE_DIR}/Common/*.h"
        "${SAMPLER_SOURCE_DIR}/Common/*.cpp")
#--------------------------------

#Vulkan DLL
set(BVHVIEWER_VULKAN_MODULE_NAME ${HOST_PROJECT_NAME}Vulkan)
if(SAMPLER_BUILD_VK AND TARGET NvBvhViewerVK)
    file(GLOB SAMPLER_VK_SOURCES
        "${SAMPLER_SOURCE_DIR}/${BVHVIEWER_VULKAN_MODULE_NAME}/*.cpp"
        "${SAMPLER_SOURCE_DIR}/${BVHVIEWER_VULKAN_MODULE_NAME}/*.h"
        ${SAMPLER_COMMON_SOURCES})
    add_library(${BVHVIEWER_VULKAN_MODULE_NAME} SHARED ${SAMPLER_VK_SOURCES})
    source_group("" FILES ${SAMPLER_VK_SOURCES})

    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        target_link_libraries(${BVHVIEWER_VULKAN_MODULE_NAME} PRIVATE Vulkan::Vulkan)
    else()
        message(FATAL_ERROR "Vulkan not found")
    endif()

    samplerapp_setup_target(${BVHVIEWER_VULKAN_MODULE_NAME})
    target_compile_definitions(${BVHVIEWER_VULKAN_MODULE_NAME} PRIVATE
        SAMPLER_VK_EXPORTS
        VK_NO_PROTOTYPES
    )

    target_link_libraries(${BVHVIEWER_VULKAN_MODULE_NAME} PRIVATE NvBvhViewerVK jsonxx)
    target_include_directories(${BVHVIEWER_VULKAN_MODULE_NAME} PRIVATE "${BVHVIEWER_PROJECT_ROOT}/Source/VK")

    set_property(TARGET NvBvhViewerVK PROPERTY FOLDER "${HOST_PROJECT_NAME}/Vulkan")
    set_property(TARGET ${BVHVIEWER_VULKAN_MODULE_NAME} PROPERTY FOLDER "${HOST_PROJECT_NAME}/Vulkan")
endif()
#--------------------------------

#D3D12 DLL
set(BVHVIEWER_D3D12_MODULE_NAME ${HOST_PROJECT_NAME}D3D12)
if(SAMPLER_BUILD_D3D12 AND TARGET NvBvhViewerD3D12)
    file(GLOB SAMPLER_D3D12_SOURCES
        "${SAMPLER_SOURCE_DIR}/${BVHVIEWER_D3D12_MODULE_NAME}/*.cpp"
        "${SAMPLER_SOURCE_DIR}/${BVHVIEWER_D3D12_MODULE_NAME}/*.h"
        ${SAMPLER_COMMON_SOURCES})
    add_library(${BVHVIEWER_D3D12_MODULE_NAME} SHARED ${SAMPLER_D3D12_SOURCES})
    source_group("" FILES ${SAMPLER_D3D12_SOURCES})

    samplerapp_setup_target(${BVHVIEWER_D3D12_MODULE_NAME})
    target_compile_definitions(${BVHVIEWER_D3D12_MODULE_NAME} PRIVATE
        SAMPLER_D3D12_EXPORTS
    )

    target_compile_options(${BVHVIEWER_D3D12_MODULE_NAME} PRIVATE /Zc:preprocessor)

    target_link_libraries(${BVHVIEWER_D3D12_MODULE_NAME} PRIVATE NvBvhViewerD3D12)

    target_link_libraries(${BVHVIEWER_D3D12_MODULE_NAME} PRIVATE minhook)
    target_include_directories(${BVHVIEWER_D3D12_MODULE_NAME} PRIVATE "${SAMPLER_MINHOOK_PATH}/include")
    target_include_directories(${BVHVIEWER_D3D12_MODULE_NAME} PRIVATE "${BVHVIEWER_PROJECT_ROOT}/Source/D3D12")

    set_property(TARGET NvBvhViewerD3D12 PROPERTY FOLDER "${HOST_PROJECT_NAME}/D3D12")
    set_property(TARGET ${BVHVIEWER_D3D12_MODULE_NAME} PROPERTY FOLDER "${HOST_PROJECT_NAME}/D3D12")

    add_custom_command(TARGET ${BVHVIEWER_D3D12_MODULE_NAME} PRE_LINK
               COMMAND ${CMAKE_COMMAND} -E rename
               "$<TARGET_FILE:${BVHVIEWER_D3D12_MODULE_NAME}>"
               "$<TARGET_FILE:${BVHVIEWER_D3D12_MODULE_NAME}>-%time:~-8,2%-%time:~-5,2%-%time:~-2,2%" || (exit 0)
               COMMENT "Renaming previous ${BVHVIEWER_D3D12_MODULE_NAME} as it might be locked")

    if(BVHVIEWER_NVAPI_PATH)
        target_include_directories(${BVHVIEWER_D3D12_MODULE_NAME} PRIVATE
            "${BVHVIEWER_NVAPI_PATH}"
            "${BVHVIEWER_NVAPI_PATH}/include"
        )
        target_link_libraries(${BVHVIEWER_D3D12_MODULE_NAME} PRIVATE
            "${BVHVIEWER_NVAPI_PATH}/amd64/nvapi64.lib"
        )
    endif()
endif()
#--------------------------------

#Host executable
file(GLOB SAMPLER_HOST_SOURCES
    "${SAMPLER_SOURCE_DIR}/*.cpp"
    "${SAMPLER_SOURCE_DIR}/*.h"
    )

file(GLOB SAMPLER_RESOURCE_SOURCES
        "${SAMPLER_SOURCE_DIR}/Resources/*.h"
        "${SAMPLER_SOURCE_DIR}/Resources/*.rc")

add_executable(${HOST_PROJECT_NAME}
    ${SAMPLER_HOST_SOURCES}
    ${SAMPLER_COMMON_SOURCES}
    ${SAMPLER_RESOURCE_SOURCES}
    )

source_group("" FILES ${SAMPLER_HOST_SOURCES})
source_group("Common" FILES ${SAMPLER_COMMON_SOURCES})
configure_file(.arguments "${CMAKE_CURRENT_BINARY_DIR}/${HOST_PROJECT_NAME}.args.json" COPYONLY)

samplerapp_setup_target(${HOST_PROJECT_NAME})

target_link_libraries(${HOST_PROJECT_NAME} PRIVATE jsonxx)
target_link_libraries(${HOST_PROJECT_NAME} PRIVATE ${BVHVIEWER_VULKAN_MODULE_NAME})
target_link_libraries(${HOST_PROJECT_NAME} PRIVATE ${BVHVIEWER_D3D12_MODULE_NAME})

target_include_directories(${HOST_PROJECT_NAME} PRIVATE "${SAMPLER_SOURCE_DIR}/${BVHVIEWER_VULKAN_MODULE_NAME}")
target_include_directories(${HOST_PROJECT_NAME} PRIVATE "${SAMPLER_SOURCE_DIR}/${BVHVIEWER_D3D12_MODULE_NAME}")
set_property(TARGET ${HOST_PROJECT_NAME} PROPERTY FOLDER "${HOST_PROJECT_NAME}")
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${HOST_PROJECT_NAME})
set_target_properties(${HOST_PROJECT_NAME} PROPERTIES LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator'\"")

add_compile_definitions(BVHVIEWER_VULKAN_MODULE_NAME=${BVHVIEWER_VULKAN_MODULE_NAME})
add_compile_definitions(BVHVIEWER_D3D12_MODULE_NAME=${BVHVIEWER_D3D12_MODULE_NAME})

add_custom_command(TARGET ${HOST_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SAMPLER_ROOT}/.processFilter"
        "$<TARGET_FILE_DIR:${HOST_PROJECT_NAME}>/processFilter.txt"
    COMMENT "Copying processFilter.txt"
)
#--------------------------------
