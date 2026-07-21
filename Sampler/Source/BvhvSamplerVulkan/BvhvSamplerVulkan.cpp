/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

#include <string>
#include <codecvt>
#include <vector>
#include <algorithm>
#include <filesystem>

#include <Windows.h>
#define EXPORT extern "C" __declspec(dllexport)

#include "jsonxx.h"
#include "Connection.h"
#include "BvhvSamplerVulkan.h"
#include "Version.h"

namespace fs = std::filesystem;

const wchar_t LAYER_REGISTRY_PATH[ ] = L"SOFTWARE\\Khronos\\Vulkan\\ImplicitLayers";
const wchar_t BVHVIEWER_LAYER_JSON_NAME_MASK[ ] = L"bvhvsampler";

bool InstallLayerForRegistryKey(const HKEY baseKey, const fs::path& layerPath);
bool IsLayerInstalled(std::string layerPath, bool& found);
bool CleanupRegistry(HKEY base);

bool CleanupRegistry(HKEY base)
{
    if(base == 0)
        return true;

    const std::wstring baseName = base == HKEY_CURRENT_USER ? L"HKEY_CURRENT_USER" : L"HKEY_LOCAL_MACHINE";

    HKEY key = {};
    LONG result = RegOpenKeyExW(base, LAYER_REGISTRY_PATH, 0, KEY_READ | KEY_WRITE, &key);

    if (result == ERROR_FILE_NOT_FOUND)
        return true; // no layers installed

    if (result != ERROR_SUCCESS)
    {
        // TODO: LOG
        return false;
    }

    DWORD numValues;
    DWORD maxNameLength;
    RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &numValues, &maxNameLength, nullptr, nullptr, nullptr);

    std::vector<wchar_t> charArray(maxNameLength + 1);
    DWORD nameLength = maxNameLength;

    std::vector<std::wstring> removeList;

    std::wstring name;
    std::wstring referenceName;
    for (DWORD i = 0; i < numValues; i++)
    {
        nameLength = maxNameLength + 1;
        result = RegEnumValueW(key, i, charArray.data(), &nameLength, nullptr, nullptr,
            nullptr, nullptr);

        if (result != ERROR_SUCCESS)
        {
            // TODO: LOG
            return false;
        }

        name = charArray.data();
        referenceName = name;
        std::transform(referenceName.begin(), referenceName.end(), referenceName.begin(), ::towlower);

        const size_t offset = referenceName.rfind(BVHVIEWER_LAYER_JSON_NAME_MASK);
        if (offset != std::string::npos)
        {
            const size_t lastSlash = referenceName.find_last_of(L"/\\");
            if (lastSlash == std::string::npos || offset > lastSlash)
                removeList.push_back(name);
        }
    }


    bool success = true;
    for (size_t i = 0; i < removeList.size(); i++)
    {
        result = RegDeleteValueW(key, removeList[i].c_str());

        if (result != ERROR_SUCCESS)
        {
            // TODO: LOG
            success = false;
        }
    }

    RegCloseKey(key);
    return success;
}

bool IsLayerInstalled(std::string layerPath, bool& found)
{
    for (auto& c : layerPath)
        c = (char)::tolower(c);

    HKEY key;
    LONG res = RegOpenKeyExW(HKEY_CURRENT_USER, LAYER_REGISTRY_PATH, 0, KEY_READ, &key);

    if (res == ERROR_FILE_NOT_FOUND)
    {
        found = false;
        return true;
    }

    if (res != ERROR_SUCCESS)
    {
        // TODO: LOG
        return false;
    }

    DWORD numValues;
    DWORD maxNameLength;
    RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        &numValues, &maxNameLength, nullptr, nullptr, nullptr);

    std::vector<wchar_t> charArray(maxNameLength + 1);
    DWORD nameLength = maxNameLength;

    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> conversion;
    std::wstring name;
    std::string nameUTF8;

    for (DWORD i = 0; i < numValues; i++)
    {
        nameLength = maxNameLength + 1;
        res = RegEnumValueW(key, i, charArray.data(), &nameLength, nullptr, nullptr,
            nullptr, nullptr);

        if (res != ERROR_SUCCESS)
        {
            // TODO: LOG
            return false;
        }

        name = charArray.data();
        std::transform(name.begin(), name.end(), name.begin(), ::towlower);
        nameUTF8 = conversion.to_bytes((const wchar_t*)name.c_str());

        if (nameUTF8 == layerPath)
            found = true;
    }

    RegCloseKey(key);
    return true;
}

bool InstallLayerForRegistryKey(const HKEY baseKey, const fs::path& layerPath)
{
    HKEY key;
    LONG res = RegOpenKeyExW(baseKey, LAYER_REGISTRY_PATH, 0, KEY_READ | KEY_WRITE, &key);

    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> conversion;
    const std::string layerRegistryPath = conversion.to_bytes(LAYER_REGISTRY_PATH);

    if (res == ERROR_FILE_NOT_FOUND)
    {
        res = RegCreateKeyExW(baseKey, LAYER_REGISTRY_PATH, 0, nullptr,
            REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &key, nullptr);

        if (res != ERROR_SUCCESS)
        {
            // TODO: LOG
            return false;
        }
    }
    else if (res != ERROR_SUCCESS)
    {
        // TODO: LOG
        return false;
    }

    RegCloseKey(key);

    const DWORD value = 0;
    res = RegSetKeyValueW(baseKey, LAYER_REGISTRY_PATH, layerPath.c_str(), REG_DWORD, &value, sizeof(value));

    if (res != ERROR_SUCCESS)
    {
        // TODO: LOG
        return false;
    }

    bool found = false;
    const bool success = IsLayerInstalled(layerPath.string().c_str(), found);

    if (success && !found)
    {
        //TODO: LOG
        return false;
    }

    return true;
}

EXPORT void SamplerVulkan_DisableLayer(SamplerConnection* connection)
{
    if (connection != nullptr)
        connection->SetVkLayerState(false);

    CleanupRegistry(HKEY_CURRENT_USER);
    CleanupRegistry(HKEY_LOCAL_MACHINE);
}

EXPORT uint32_t SamplerVulkan_EnableLayer(SamplerConnection* bvhViewerConnection)
{
    { // unregister bvh viewer layer in case it was left there from before
        const bool user = CleanupRegistry(HKEY_CURRENT_USER);
        CleanupRegistry(HKEY_LOCAL_MACHINE);
        if (!user)
            return 0;
    }

    fs::path layerPath;
    {
        const fs::path currentPath = bvhViewerConnection->GetHostExecutablePath();
        const fs::path currentDirectory = currentPath.parent_path();
        const fs::path layerDllPath =  currentDirectory.string() + std::string("\\") + std::string(STR(BVHVIEWER_VULKAN_MODULE_NAME)) + std::string(".dll");
        layerPath =  currentDirectory.string() + std::string("\\") + std::string(STR(BVHVIEWER_VULKAN_MODULE_NAME)) + std::string(".json");

        if (fs::exists(layerPath))
        { // recreate the json each session to be safe
            if(fs::remove(layerPath) == false)
            {
                // TODO: LOG
                return 0;
            }
        }

        jsonxx::Object layer;
        layer << "name" << "VK_LAYER_NV_nvbvhviewer";
        layer << "type" << "GLOBAL";
        layer << "library_path" << layerDllPath.string().c_str();
        layer << "api_version" << "1.3.0";
        layer << "implementation_version" << VERSION_STRING;
        layer << "description" << "Raytracing BVH Visualization Layer";

        jsonxx::Object disable_environment;
        disable_environment << "DISABLE_BVHVSAMPLER_VK_LAYER" << " ";
        layer << "disable_environment" << disable_environment;

        jsonxx::Object functions;
        functions << "vkNegotiateLoaderLayerInterfaceVersion" << "BvhvSampler_vkNegotiateLoaderLayerInterfaceVersion";
        functions << "vkGetInstanceProcAddr" << "BvhvSampler_vkGetInstanceProcAddr";
        functions << "vkGetDeviceProcAddr" << "BvhvSampler_vkGetDeviceProcAddr";
        layer << "functions" << functions;

        jsonxx::Object parser;
        parser << "file_format_version" << "1.2.0";
        parser << "layer" << layer;

        const std::string content = parser.json();

        constexpr DWORD share = FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE;
        constexpr DWORD access = 0;
        const HANDLE handle = CreateFileW(layerPath.c_str(), GENERIC_WRITE, share, nullptr, CREATE_ALWAYS, access, NULL);
        if (handle == INVALID_HANDLE_VALUE)
        {
            // TODO: LOG
            return 0;
        }

        DWORD written = 0;
        const DWORD bufferSize = (DWORD)content.size();
        WriteFile(handle, (const uint8_t*)content.data(), bufferSize, &written, nullptr);
        CloseHandle(handle);

        if (written != bufferSize)
        {
            // TODO: LOG
            return 0;
        }
    }

    { // install the layer
        const bool user = InstallLayerForRegistryKey(HKEY_CURRENT_USER, layerPath);
        InstallLayerForRegistryKey(HKEY_LOCAL_MACHINE, layerPath);
        if(user == false)
        { // installation must succeed for user. clean up registry
            // TODO: LOG
            SamplerVulkan_DisableLayer(bvhViewerConnection);
            return 0;
        }
    }

    bvhViewerConnection->SetVkLayerState(true); // great success

    return 1;
}