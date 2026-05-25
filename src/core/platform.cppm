module;

#include <cassert>
#define STB_IMAGE_IMPLEMENTATION
#if defined(_WIN32)
#define STBI_WINDOWS_UTF8
#include <windows.h>
#endif // defined(_WIN32)
#include <stb_image.h>

#ifdef DISABLE_IMPORT_STD
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#endif

export module platform;

#ifndef DISABLE_IMPORT_STD
import std;
#endif

const std::string PROJECT_DIR = "VulkanHppTutorial"; // TODO un-hardcode?

export std::filesystem::path pathFromProjectDir(const std::string & relativePath) {
    auto currentPath = std::filesystem::current_path();
    while (currentPath.filename() != std::filesystem::path(PROJECT_DIR)) {
        assert(currentPath != currentPath.parent_path());
        currentPath = currentPath.parent_path();
    }

    auto combinedPath = currentPath / std::filesystem::path(relativePath).make_preferred();
    return combinedPath;
}

// TODO cleanup this code/swap over to using Slang's compilation API
export void compileShader(const std::string & shaderPathFromProjectFolder, const std::string & outputPathFromProjectFolder) {
#if defined(_WIN32)
    STARTUPINFO si {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi {};
    auto shaderPath = pathFromProjectDir(shaderPathFromProjectFolder);
    auto outputPath = pathFromProjectDir(outputPathFromProjectFolder);
    auto compilerPath = pathFromProjectDir("external/slangc.exe");
    // TODO parametrize this somehow?
    auto arguments = compilerPath.string().append(
        " " + shaderPath.string() +
        " -target spirv -profile spirv_1_4 -emit-spirv-directly"
        " -fvk-use-entrypoint-name -entry vertMain -entry fragMain"
        " -o " + outputPath.string());

    if (CreateProcessA(
        compilerPath.string().c_str(),
        arguments.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        nullptr,
        &si,
        &pi
    )) {
        WaitForSingleObject(pi.hProcess, INFINITE);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
#else
    // TODO other platforms support
    throw std::runtime_error("recompileShader function not defined for this platform!");
#endif
}

export std::vector<char> readFile(const std::string & filename) {
    // start reading at the end of file + read the file as binary
    std::ifstream file(pathFromProjectDir(filename), std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("failed to open file " + filename);
    }

    std::vector<char> buffer(file.tellg());
    file.seekg(0, std::ios::beg);
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    file.close();

    return buffer;
}

export class StbImageWrapper {
public:
    explicit StbImageWrapper(const std::string & pathToTexture) {
#if defined(_WIN32)
        // Windows uses UTF-16, so I'm pretty sure we need to use wstring here
        auto absolutePath = pathFromProjectDir(pathToTexture).wstring();
        char utf8PathBuffer[256] = {}; // TODO use something more safe?
        stbi_convert_wchar_to_utf8(utf8PathBuffer, absolutePath.size() + 1, absolutePath.c_str());
        pixels = stbi_load(utf8PathBuffer, &width, &height, &channels, STBI_rgb_alpha);
#else
        // Everything else uses UTF-8 so a normal string/cstring should work just fine
        auto absolutePath = pathFromProjectDir(pathToTexture);
        pixels = stbi_load(absolutePath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
#endif
    }

    ~StbImageWrapper() {
        if (pixels) stbi_image_free(pixels);
    }

    // delete copy constructor
    StbImageWrapper(const StbImageWrapper & other) = delete;

    // define move constructor
    StbImageWrapper(StbImageWrapper && other) noexcept
        : width(other.width), height(other.height), channels(other.channels), pixels(std::exchange(other.pixels, nullptr)) {}

    // explicitly delete copy assignment operator and move assignment operator
    StbImageWrapper & operator=(const StbImageWrapper & other) = delete;
    StbImageWrapper & operator=(StbImageWrapper && other) = delete;
public:
    int width{};
    int height{};
    int channels{};
    unsigned char * pixels{};
};
