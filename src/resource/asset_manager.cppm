module;

#ifdef DISABLE_IMPORT_STD
#include <filesystem>
#include <ranges>
#include <string>
#include <unordered_map>
#endif

export module asset_manager;

import model_parser;
import platform;

#ifndef DISABLE_IMPORT_STD
import std;
#endif

export class AssetManager {
public:
    AssetManager() = default;

    SceneGraphAsset * getOrCreateAsset(const std::string & assetName) {
        const auto it = m_SceneGraphAssets.find(assetName);
        if (it != m_SceneGraphAssets.end()) {
            return &it->second;
        }

        auto * parser = ModelParserLocator::locate();
        auto asset = parser->load(pathFromAssetDir(assetName));
        if (asset.has_value()) {
            m_SceneGraphAssets.insert({ assetName, asset.value() });
        } else {
            return nullptr;
        }

        return &m_SceneGraphAssets.at(assetName);
    }

    void loadAll() {
        for (auto & asset : m_SceneGraphAssets | std::views::values) {
            asset.load();
        }
    }

    void unloadAll() {
        for (auto & asset : m_SceneGraphAssets | std::views::values) {
            asset.unload();
        }
    }

private:
    std::unordered_map<std::string, SceneGraphAsset> m_SceneGraphAssets {};
};
