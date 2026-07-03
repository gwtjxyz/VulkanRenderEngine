cmake_minimum_required(VERSION 4.2)
# Module std support requires different values of this depending on cmake version
# Probably not fully correct/comprehensive for now, will update as needed
# Values pulled from https://github.com/Kitware/CMake/blob/master/Help/dev/experimental.rst
# with changing the branch/tag to whatever CMake version we have installed
if(CMAKE_VERSION VERSION_LESS 4.3)
    set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444") # 4.2 (?)
else()
    set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b") # 4.3+
endif()

set(CMAKE_CXX_SCAN_FOR_MODULES ON)
# Need to compile flecs as C code
project(VulkanRenderEngine LANGUAGES C CXX)

# Module std is really difficult to get working on Mac and it's just not worth the hassle
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(CMAKE_CXX_MODULE_STD ON)
else()
    # Hack to make module std work on Mac by explicitly giving CMake the module path it should use
    # Really shouldn't need this since we're no longer using stdlib modules on Mac, but otherwise
    # the fetchcontent version of vulkan-headers fails and I can't be bothered figuring out how to fix that
    # ABI conflicts here don't matter since we're not using the compiled modules despite building them
    set(CMAKE_CXX_STDLIB_MODULES_JSON "/opt/homebrew/opt/llvm/lib/c++/libc++.modules.json")
endif()

set(PROJECT_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD ${PROJECT_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS ON)

# Debug stuff

include(FetchContent)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/CMake")

find_package (glfw3 REQUIRED)
find_package (glm REQUIRED)
find_package(stb REQUIRED)
find_package (tinyobjloader REQUIRED)
#find_package (KTX REQUIRED)

# Fetch latest Vulkan headers if the installed ones are out of date
# Usually using newer headers with older SDK versions shouldn't be
# a problem as long as the major version is the same
find_package (Vulkan 1.4.351 QUIET)

# to better handle window resizing with Vulkan
add_compile_definitions(VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS)

# Mac support wizardry
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # First, check the compiler
    if (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
        message(FATAL_ERROR "Cannot use Apple Clang together with modules; either switch off module support or use LLVM Clang.")
    endif()
    message("If CMake fails with the error \"libc++.modules.json resource does not exist\", consider applying this workaround:")
    message("https://github.com/llvm/llvm-project/issues/109895#issuecomment-3803292485")

    # Maybe one day module std will be usable on Mac, but not today!
    add_compile_definitions(DISABLE_VULKAN_MODULE DISABLE_IMPORT_STD)
endif()

if (Vulkan_FOUND)
    # Set up Vulkan C++ module as a static library
    add_library(Vulkan-HppModule STATIC)
    add_library(Vulkan::HppModule ALIAS Vulkan-HppModule)

    if(NOT DISABLE_VULKAN_MODULE)
        target_sources(Vulkan-HppModule
                PUBLIC
                FILE_SET CXX_MODULES
                BASE_DIRS ${Vulkan_INCLUDE_DIR}
                FILES
                ${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm
                ${Vulkan_INCLUDE_DIR}/vulkan/vulkan_video.cppm
        )

    endif()
else()
    FetchContent_Declare(vulkan-headers
            GIT_REPOSITORY "https://github.com/KhronosGroup/Vulkan-Headers.git"
            GIT_TAG "v1.4.351"
            GIT_SHALLOW ON
            OVERRIDE_FIND_PACKAGE
            SYSTEM)
    FetchContent_MakeAvailable(vulkan-headers)
    # make sure to set up the include path for CMake
    set(Vulkan_INCLUDE_DIR "${vulkan-headers_SOURCE_DIR}/include")
    message("Vulkan_INCLUDE_DIR is ${Vulkan_INCLUDE_DIR}")
endif()

# Configure the module target (import std currently requires C++23 with CMake)
target_compile_features(Vulkan-HppModule PUBLIC cxx_std_23)
if(NOT DISABLE_VULKAN_MODULE)
   set_target_properties(Vulkan-HppModule PROPERTIES CXX_MODULE_STD ON)
endif()

# Use dynamic dispatcher
target_link_libraries(Vulkan-HppModule PUBLIC Vulkan::Headers)
target_compile_definitions(Vulkan-HppModule PUBLIC VULKAN_HPP_DISPATCH_LOADER_DYNAMIC)

# Static dispatcher usage would look something like this
# (currently doesn't work, would probably need to make changes to find_package(vulkan) line)
#
# target_link_libraries(Vulkan-HppModule PUBLIC Vulkan::Vulkan)

target_compile_definitions(Vulkan-HppModule
        PUBLIC
        VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1             # Designated initializer support
        VK_ENABLE_BETA_EXTENSIONS                       # For MacOS support
)

# GLM hack based on this: https://github.com/g-truc/glm/issues/1372#issuecomment-3042171253
add_library(glm-module)
if(MSVC)
    # Add MSVC-specific compiler options for proper C++ module support
    # Need this so GLM knows what C++ version we have and properly includes its hash header
    target_compile_options(glm-module PRIVATE
            /std:c++latest      # Use latest C++ standard for better module support
            /permissive-        # Standards conformance mode
            /Zc:__cplusplus     # Enable correct __cplusplus macro
            /EHsc               # Enable C++ exception handling
            /Zc:preprocessor    # Use conforming preprocessor
            /translateInclude   # Automatically translate #include to import for standard library
    )
endif()
target_sources(glm-module PUBLIC
    FILE_SET CXX_MODULES
    FILES
    "${CMAKE_SOURCE_DIR}/modules/glm.cppm"
)
target_link_libraries(glm-module PUBLIC glm::glm)

#fastgltf
set(FASTGLTF_COMPILE_AS_CPP20 ON)
set(FASTGLTF_ENABLE_CPP_MODULES ON)
if(MSVC)
    set(FASTGLTF_USE_STD_MODULE ON)
endif()
add_subdirectory(external/fastgltf)

# TODO maybe add __cplusplus definition to this too, works fine without it so far though so will do later if problems arise
add_library(tinyobjloader-module)
target_sources(tinyobjloader-module PUBLIC
    FILE_SET CXX_MODULES
    FILES
    "${CMAKE_SOURCE_DIR}/modules/tinyobjloader.cppm"
)
target_link_libraries(tinyobjloader-module PUBLIC tinyobjloader::tinyobjloader)

# ImGui
add_library(ImGui)
set(IMGUI_DIR external/imgui)
set(IMGUI_SOURCES
        # ImGui sources
        ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_widgets.cpp ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_demo.cpp
        # GLFW and Vulkan backends
        ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        # std::string support
        ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp
)
target_compile_definitions(ImGui PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES)
target_sources(ImGui PUBLIC ${IMGUI_SOURCES})
target_include_directories(ImGui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends ${IMGUI_DIR}/misc/cpp)
target_link_libraries(ImGui PRIVATE glfw Vulkan::Headers)

# DXC
find_library(dxcompiler_LIBRARY NAMES dxcompiler dxcompilerd HINTS "$ENV{VULKAN_SDK}/lib" REQUIRED)

# flecs
add_library(flecs)
set(FLECS_DIR external/flecs)
set(FLECS_SOURCES ${FLECS_DIR}/flecs.c)
set_source_files_properties(${FLECS_SOURCES} PROPERTIES LANGUAGE C)
target_sources(flecs PUBLIC ${FLECS_SOURCES})
target_include_directories(flecs PUBLIC ${FLECS_DIR})

add_executable(VulkanRenderEngine src/main.cpp ${IMGUI_SOURCES}
        # Vulkan dynamic loader without using Vulkan-Hpp C++20 module
        src/misc/vulkan_loader.cpp
        src/misc/vma_usage.cpp
)

# Compile modules
target_sources(VulkanRenderEngine PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        src/core/platform.cppm src/core/input.cppm src/core/hlsl_compiler.cppm src/core/constants.cppm src/core/utils.cppm
        src/entity/entity_system.cppm src/entity/components.cppm
        src/resource/resource.cppm src/resource/model_parser.cppm src/resource/scene_graph_types.cppm
        src/render/render_engine.cppm src/render/camera.cppm
        src/render/render_types.cppm
        src/render/rhi/vulkan_instance.cppm src/render/rhi/vulkan_resource_service.cppm
        src/render/rhi/device_mapper.cppm src/render/rhi/pipeline_manager.cppm
)

target_compile_definitions(VulkanRenderEngine PRIVATE USE_CPP20_MODULES=1)
target_include_directories(VulkanRenderEngine PRIVATE src)

target_include_directories(VulkanRenderEngine PRIVATE BEFORE ${Vulkan_INCLUDE_DIR})
# If I specify the overall vulkan sdk include folder, it overrides the fetched headers on my Mac and I have no idea why
target_include_directories(VulkanRenderEngine PRIVATE AFTER $ENV{VULKAN_SDK}/Include/dxc)
# VMA
target_include_directories(VulkanRenderEngine PRIVATE AFTER $ENV{VULKAN_SDK}/Include/vma)

target_link_libraries(VulkanRenderEngine PRIVATE
        Vulkan-HppModule glfw glm-module stb::stb tinyobjloader-module ImGui flecs fastgltf_module
        ${dxcompiler_LIBRARY}
)

# If on Windows, copy dxcompiler's DLLs into the build folder, otherwise the application will
# use the Windows SDK version of dxcompiler which doesn't have SPIR-V codegen support
# Not sure if this is the "right" way of doing things but oh well
# If necessary, I can copy debug info with this at some point too
if (MSVC)
    add_custom_command(
            TARGET VulkanRenderEngine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            "$ENV{VULKAN_SDK}/Bin/dxcompiler.dll"
            "${CMAKE_CURRENT_BINARY_DIR}/dxcompiler.dll"
            COMMAND ${CMAKE_COMMAND} -E copy
            "$ENV{VULKAN_SDK}/Bin/dxcompilerd.dll"
            "${CMAKE_CURRENT_BINARY_DIR}/dxcompilerd.dll"
    )
endif()

# TODO Should this be somewhere else?
if(MSVC)
    set(SLANGC_EXECUTABLE "$ENV{VULKAN_SDK}\\Bin\\slangc.exe")
else() # TODO Linux support
    set(SLANGC_EXECUTABLE "$ENV{VULKAN_SDK}/bin/slangc")
endif()

# Slang shader compilation
function (add_slang_shader_target TARGET)
    cmake_parse_arguments("SHADER" "" "" "SOURCES" ${ARGN})
    set(SHADERS_DIR ${CMAKE_CURRENT_LIST_DIR}/shaders)
    set(ENTRY_POINTS -entry vertMain -entry fragMain)
    add_custom_command(
            OUTPUT ${SHADERS_DIR}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADERS_DIR}
    )
    message("SLANGC_EXECUTABLE is ${SLANGC_EXECUTABLE}")
    message("SHADER_SOURCES is ${SHADER_SOURCES}")
    add_custom_command(
            OUTPUT ${SHADERS_DIR}/slang.spv
            COMMAND ${SLANGC_EXECUTABLE} shader.slang -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name ${ENTRY_POINTS} -o slang.spv
            WORKING_DIRECTORY ${SHADERS_DIR}
            DEPENDS ${SHADERS_DIR} ${SHADER_SOURCES}
            COMMENT "Compiling Slang Shaders"
            VERBATIM
    )
    add_custom_target (${TARGET} DEPENDS ${SHADERS_DIR}/slang.spv)
endfunction()

file(GLOB SHADER_SLANG_SOURCES shader.slang)
add_slang_shader_target(SlangShaders SOURCES ${SHADER_SLANG_SOURCES})
add_dependencies(VulkanRenderEngine SlangShaders)
