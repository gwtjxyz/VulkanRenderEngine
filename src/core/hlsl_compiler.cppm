module;

#ifdef _WIN32
#include <atlbase.h>
#endif

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE)
#include <vulkan/vulkan_raii.hpp>
#endif

// For non-Windows platforms, dxcapi's WinAdapter should take care of types defined inside atlbase
#include <dxc/dxcapi.h>

export module hlsl;

import std.compat;

import platform;

#if !(defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE))
import vulkan;
#endif

export enum class HlslShaderType {
    VERTEX,
    PIXEL,
    COMPUTE
};

export class HlslShaderCompiler {
public:
    HlslShaderCompiler() {
        HRESULT hres;

        hres = DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&m_Library));
        if (FAILED(hres)) {
            throw std::runtime_error("Could not init DXC library");
        }

        hres = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&m_Compiler));
        if (FAILED(hres)) {
            throw std::runtime_error("Could not init DXC Compiler");
        }

        hres = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&m_Utils));
        if (FAILED(hres)) {
            throw std::runtime_error("Could not init DXC Utility");
        }
    }

    vk::raii::ShaderModule compileShaderModule(const vk::raii::Device & device, const std::string & relativePath, HlslShaderType shaderType) const {
        HRESULT hres;

        uint32_t codePage = DXC_CP_ACP;
        CComPtr<IDxcBlobEncoding> sourceBlob;
        auto absolutePath = pathFromProjectDir(relativePath);
        hres = m_Utils->LoadFile(absolutePath.c_str(), &codePage, &sourceBlob);
        if (FAILED(hres)) {
            throw std::runtime_error("Could not load shader file with path " + absolutePath.string());
        }

        LPCWSTR targetProfile {}, entryPoint {};
        // Will expand as more shader type support is required
        switch (shaderType) {
            case HlslShaderType::VERTEX:
                targetProfile = L"vs_6_4";
                entryPoint = L"VSMain";
                break;
            case HlslShaderType::PIXEL:
                targetProfile = L"ps_6_4";
                entryPoint = L"PSMain";
                break;
            case HlslShaderType::COMPUTE:
                targetProfile = L"cs_6_4";
                entryPoint = L"CSMain";
                break;
            default:
                throw std::runtime_error("Unknown HLSL shader type");
        }

        // Configure the compiler arguments for compiling the HLSL shader to SPIR-V
        std::array arguments = {
            // Shader file name for debugging
            absolutePath.c_str(),
            L"-E", entryPoint,
            L"-T", targetProfile,
            L"-spirv"
        };

        DxcBuffer buffer {};
        buffer.Encoding = DXC_CP_ACP;
        buffer.Ptr = sourceBlob->GetBufferPointer();
        buffer.Size = sourceBlob->GetBufferSize();

        CComPtr<IDxcResult> compilationResult {nullptr};
        hres = m_Compiler->Compile(
            &buffer,
            arguments.data(),
            static_cast<uint32_t>(arguments.size()),
            nullptr,
            IID_PPV_ARGS(&compilationResult)
        );

        if (SUCCEEDED(hres)) {
            compilationResult->GetStatus(&hres);
        }

        if (FAILED(hres) && compilationResult) {
            CComPtr<IDxcBlobEncoding> errorBlob;
            hres = compilationResult->GetErrorBuffer(&errorBlob);
            if (SUCCEEDED(hres) && errorBlob) {
                std::cerr << "Shader compilation failed: \n\n" << static_cast<const char *>(errorBlob->GetBufferPointer());
                throw std::runtime_error("Shader compilation failed!");
            }
        }

        CComPtr<IDxcBlob> shaderCode;
        compilationResult->GetResult(&shaderCode);

        vk::ShaderModuleCreateInfo createInfo {
            .codeSize = shaderCode->GetBufferSize(),
            .pCode = static_cast<const uint32_t *>(shaderCode->GetBufferPointer()),
        };

        return vk::raii::ShaderModule { device, createInfo };
    }

private:
    CComPtr<IDxcLibrary> m_Library;
    CComPtr<IDxcCompiler3> m_Compiler;
    CComPtr<IDxcUtils> m_Utils;
};
