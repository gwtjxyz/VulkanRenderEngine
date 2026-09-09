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

export struct FileInfo {
    std::string name;
    std::string extension;
    std::filesystem::path absolutePath;
};

export std::filesystem::path pathFromProjectDir(const std::string & relativePath) {
    auto currentPath = std::filesystem::current_path();
    while (currentPath.filename() != std::filesystem::path(PROJECT_DIR)) {
        assert(currentPath != currentPath.parent_path());
        currentPath = currentPath.parent_path();
    }

    auto combinedPath = currentPath / std::filesystem::path(relativePath).make_preferred();
    return combinedPath;
}

export std::filesystem::path pathFromAssetDir(const std::string & relativePath) {
    return pathFromProjectDir("assets/" + relativePath);
}

export FileInfo getFileInfo(const std::string & relativePath) {
    auto absolutePath = pathFromProjectDir(relativePath);
    auto extension = absolutePath.extension().string();
    auto name = absolutePath.stem().string();

    return { name, extension, absolutePath };
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
    explicit StbImageWrapper(FileInfo & fileInfo) {
        load(fileInfo.absolutePath, fileInfo.extension);
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

private:
    void load(const std::filesystem::path & absolutePath, const std::string & extension) {
        // Will expand this logic as necessary, for now just png and jpeg is enough
        int desiredChannels;
        if (extension == ".jpg" || extension == ".jpeg")
            desiredChannels = STBI_rgb;
        else
            desiredChannels = STBI_rgb_alpha;     // PNG

#if defined(_WIN32)
        // Windows uses UTF-16, so I'm pretty sure we need to use wstring here
        auto absolutePathString = absolutePath.wstring();
        char utf8PathBuffer[256] = {}; // TODO use something more safe?
        stbi_convert_wchar_to_utf8(utf8PathBuffer, absolutePathString.size() + 1, absolutePathString.c_str());

        pixels = stbi_load(utf8PathBuffer, &width, &height, &channels, desiredChannels);
#else
        // Everything else uses UTF-8 so a normal string/cstring should work just fine
        pixels = stbi_load(absolutePath.c_str(), &width, &height, &channels, desiredChannels);
#endif
    }

public:
    int width {};
    int height {};
    int channels {};
    unsigned char * pixels {};
};
