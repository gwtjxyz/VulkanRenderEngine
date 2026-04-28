cmake_minimum_required(VERSION 4.1)
# Module std support requires different values of this depending on cmake version
# Probably not fully correct/comprehensive for now, will update as needed
# Values pulled from https://github.com/Kitware/CMake/blob/master/Help/dev/experimental.rst
# with changing the branch/tag to whatever CMake version we have installed
if(CMAKE_VERSION VERSION_LESS 4.3)
    set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444") # 4.2 (?)
else()
    set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b") # 4.3+
endif()
set(CMAKE_CXX_MODULE_STD ON)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)
project(VulkanHppTutorial LANGUAGES CXX)

set(PROJECT_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD ${PROJECT_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/CMake")

find_package (glfw3 REQUIRED)
find_package (glm REQUIRED)
find_package (Vulkan REQUIRED)
find_package(stb REQUIRED)
find_package (tinyobjloader REQUIRED)
#find_package (tinygltf REQUIRED)
#find_package (KTX REQUIRED)

# Add option to enable/disable C++ 20 module
option(ENABLE_CPP20_MODULE "Enable C++ 20 module support for Vulkan" ON)

# to better handle window resizing with Vulkan
add_compile_definitions(VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS)

# Enable C++ module dependency scanning only if C++ 20 module is enabled
# This thing is super scuffed, will not compile without modules or MSVC (I hate CMake so I'm not fixing it until I absolutely have to)
if(ENABLE_CPP20_MODULE)
    set(CMAKE_CXX_SCAN_FOR_MODULES ON)

    # Mac support wizardry
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        # First, check the compiler
        if (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
            message(FATAL_ERROR "Cannot use Apple Clang together with modules; either switch off module support or use LLVM Clang.")
        endif()
        message("If CMake fails with the error \"libc++.modules.json resource does not exist\", consider applying this workaround:")
        message("https://github.com/llvm/llvm-project/issues/109895#issuecomment-3803292485")

        # TODO remove?
        set(CMAKE_LINKER_TYPE LLD)

        # TODO remove once I figure out how to fix vulkan modules not working
        add_compile_definitions(DISABLE_VULKAN_MODULE)
    endif()

    add_library(VulkanCppModule)
    add_library(Vulkan::cppm ALIAS VulkanCppModule)

    target_compile_definitions(VulkanCppModule
            PUBLIC VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
            VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
            VK_ENABLE_BETA_EXTENSIONS                       # For MacOS support
    )
    target_include_directories(VulkanCppModule
            PUBLIC
            "${Vulkan_INCLUDE_DIR}"
    )
    target_link_libraries(VulkanCppModule
            PUBLIC
            Vulkan::Vulkan
    )

    set_target_properties(VulkanCppModule PROPERTIES CXX_STANDARD ${PROJECT_CXX_STANDARD})

    if(MSVC)
        # Add MSVC-specific compiler options for proper C++ module support
        target_compile_options(VulkanCppModule PRIVATE
                /std:c++latest      # Use latest C++ standard for better module support
                /permissive-        # Standards conformance mode
                /Zc:__cplusplus     # Enable correct __cplusplus macro
                /EHsc               # Enable C++ exception handling
                /Zc:preprocessor    # Use conforming preprocessor
                /translateInclude   # Automatically translate #include to import for standard library
        )
    endif()

    target_sources(VulkanCppModule
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS
        "${Vulkan_INCLUDE_DIR}"
        FILES
        "${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm"
        "${Vulkan_INCLUDE_DIR}/vulkan/vulkan_video.cppm"
    )

    # Add the vulkan.cppm file directly as a source file
    target_sources(VulkanCppModule
        PRIVATE
        "${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm"
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

    # TODO maybe add __cplusplus definition to this too, works fine without it so far though so will do later if problems arise
    add_library(tinyobjloader-module)
    target_sources(tinyobjloader-module PUBLIC
        FILE_SET CXX_MODULES
        FILES
        "${CMAKE_SOURCE_DIR}/modules/tinyobjloader.cppm"
    )
    target_link_libraries(tinyobjloader-module PUBLIC tinyobjloader::tinyobjloader)
endif()


add_executable(VulkanHppTutorial src/main.cpp)
# Compile modules
target_sources(VulkanHppTutorial PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        src/platform.cppm src/render.cppm
)

target_compile_definitions(VulkanHppTutorial PRIVATE USE_CPP20_MODULES=1)

target_link_libraries(VulkanHppTutorial VulkanCppModule glfw glm-module stb::stb tinyobjloader-module)

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
add_dependencies(VulkanHppTutorial SlangShaders)
