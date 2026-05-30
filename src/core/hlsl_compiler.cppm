module;

#ifdef _WIN32
#include <atlbase.h>
#endif

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE)
#include <vulkan/vulkan_raii.hpp>
#endif

// For non-Windows platforms, dxcapi's WinAdapter should take care of types defined inside atlbase
#include <dxcapi.h>

#ifdef DISABLE_IMPORT_STD
#include <iostream>
#endif

export module hlsl_compiler;

#ifndef DISABLE_IMPORT_STD
import std;
#endif

import platform;

#if !(defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE))
import vulkan;
#endif

export enum class HlslShaderStage {
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

        hres = m_Utils->CreateDefaultIncludeHandler(&m_IncludeHandler);
        if (FAILED(hres)) {
            throw std::runtime_error("Could not create include handler");
        }
    }

    vk::raii::ShaderModule compileShaderModule(const vk::raii::Device & device, const std::string & relativePath, HlslShaderStage shaderStage) const {
        HRESULT hres;

        uint32_t codePage = DXC_CP_ACP;
        CComPtr<IDxcBlobEncoding> sourceBlob;
        auto absolutePath = pathFromProjectDir(relativePath);
        // Have to do it like this for cross-platform compatibility
        // Reasonable operating systems (read: non-Windows) don't use wchar for paths,
        // but dxcompiler's API expects them
        auto absolutePathWstr = absolutePath.wstring();
        auto * absolutePathWstrData = absolutePathWstr.c_str();
        hres = m_Utils->LoadFile(absolutePathWstrData, &codePage, &sourceBlob);
        if (FAILED(hres)) {
            throw std::runtime_error("Could not load shader file with path " + absolutePath.string());
        }

        LPCWSTR targetProfile {}, entryPoint {};
        // Will expand as more shader type support is required
        switch (shaderStage) {
            case HlslShaderStage::VERTEX:
                targetProfile = L"vs_6_4";
                entryPoint = L"VSMain";
                break;
            case HlslShaderStage::PIXEL:
                targetProfile = L"ps_6_4";
                entryPoint = L"PSMain";
                break;
            case HlslShaderStage::COMPUTE:
                targetProfile = L"cs_6_4";
                entryPoint = L"CSMain";
                break;
            default:
                throw std::runtime_error("Unknown HLSL shader type");
        }

        // Configure the compiler arguments for compiling the HLSL shader to SPIR-V
        std::array arguments = {
            // Shader file name for debugging
            absolutePathWstrData,
            L"-H",                              // Process includes
            L"-E", entryPoint,                  // main function (so we can have multiple shader stages per file)
            L"-T", targetProfile,               // Shader model
            L"-spirv"                           // Compile for Vulkan
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
            m_IncludeHandler,
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

    struct ShaderInfo {
        vk::raii::ShaderModule shaderModule = nullptr;
        vk::PipelineShaderStageCreateInfo shaderStageInfo;
    };

    // TODO specialization info support
    ShaderInfo compileAndGetCreateInfo(const vk::raii::Device & device, const std::string & relativePath, const HlslShaderStage shaderStage) const {
        ShaderInfo shaderInfo {};
        shaderInfo.shaderModule = compileShaderModule(device, relativePath, shaderStage);

        vk::PipelineShaderStageCreateInfo info {};
        info.module = shaderInfo.shaderModule;
        switch (shaderStage) {
            case HlslShaderStage::VERTEX:
                info.stage = vk::ShaderStageFlagBits::eVertex;
                info.pName = "VSMain";
                break;
            case HlslShaderStage::PIXEL:
                info.stage = vk::ShaderStageFlagBits::eFragment;
                info.pName = "PSMain";
                break;
            case HlslShaderStage::COMPUTE:
                info.stage = vk::ShaderStageFlagBits::eCompute;
                info.pName = "CSMain";
                break;
            default:
                throw std::runtime_error("Unrecognized HLSL shader type");
        }
        shaderInfo.shaderStageInfo = info;

        return shaderInfo;
    }
private:
    CComPtr<IDxcLibrary> m_Library;
    CComPtr<IDxcCompiler3> m_Compiler;
    CComPtr<IDxcUtils> m_Utils;
    CComPtr<IDxcIncludeHandler> m_IncludeHandler;
};
