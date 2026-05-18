#include "env_map.h"

#include <stb/stb_image.h>
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace {

float Luminance(float r, float g, float b) {
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

} // namespace

EnvMapUPtr EnvMap::Load(ID3D11Device* device, const std::string& filepath) {
    auto envMap = EnvMapUPtr(new EnvMap());
    if (!envMap->Init(device, filepath))
        return nullptr;
    return envMap;
}

bool EnvMap::Init(ID3D11Device* device, const std::string& filepath) {
    int width = 0;
    int height = 0;
    int channels = 0;

    stbi_set_flip_vertically_on_load(false);
    float* pixels = stbi_loadf(filepath.c_str(), &width, &height, &channels, 3);
    if (!pixels) {
        SPDLOG_WARN("EnvMap: HDRI not found [{}] - using procedural sky", filepath);
        return false;
    }

    m_width = width;
    m_height = height;

    std::vector<float> rgba((size_t)width * (size_t)height * 4);
    for (int i = 0; i < width * height; ++i) {
        rgba[(size_t)i * 4 + 0] = pixels[(size_t)i * 3 + 0];
        rgba[(size_t)i * 4 + 1] = pixels[(size_t)i * 3 + 1];
        rgba[(size_t)i * 4 + 2] = pixels[(size_t)i * 3 + 2];
        rgba[(size_t)i * 4 + 3] = 1.0f;
    }

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = (UINT)width;
    td.Height = (UINT)height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = rgba.data();
    init.SysMemPitch = (UINT)(width * sizeof(float) * 4);

    HRESULT hr = device->CreateTexture2D(&td, &init, m_envTexture.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        SPDLOG_ERROR("EnvMap: CreateTexture2D HDRI failed 0x{:08x}", (uint32_t)hr);
        stbi_image_free(pixels);
        return false;
    }

    hr = device->CreateShaderResourceView(m_envTexture.Get(), nullptr, m_envSRV.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        SPDLOG_ERROR("EnvMap: CreateShaderResourceView HDRI failed 0x{:08x}", (uint32_t)hr);
        stbi_image_free(pixels);
        return false;
    }

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;

    hr = device->CreateSamplerState(&sd, m_sampler.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        SPDLOG_ERROR("EnvMap: CreateSamplerState failed 0x{:08x}", (uint32_t)hr);
        stbi_image_free(pixels);
        return false;
    }

    if (!BakeCDF(device, pixels, width, height)) {
        stbi_image_free(pixels);
        return false;
    }

    stbi_image_free(pixels);
    SPDLOG_INFO("EnvMap: Loaded {}x{} HDRI [{}]", width, height, filepath);
    return true;
}

bool EnvMap::BakeCDF(ID3D11Device* device, const float* pixels, int width, int height) {
    std::vector<float> importance((size_t)width * (size_t)height);
    std::vector<float> rowSum((size_t)height, 0.0f);

    for (int y = 0; y < height; ++y) {
        float theta = glm::pi<float>() * (y + 0.5f) / height;
        float sinTheta = sinf(theta);
        for (int x = 0; x < width; ++x) {
            size_t pixelIndex = ((size_t)y * (size_t)width + (size_t)x) * 3;
            float lum = Luminance(pixels[pixelIndex + 0], pixels[pixelIndex + 1], pixels[pixelIndex + 2]);
            float weighted = lum * sinTheta;
            importance[(size_t)y * (size_t)width + (size_t)x] = weighted;
            rowSum[(size_t)y] += weighted;
        }
    }

    std::vector<float> condCDF((size_t)width * (size_t)height);
    for (int y = 0; y < height; ++y) {
        float invRowSum = rowSum[(size_t)y] > 1e-10f ? 1.0f / rowSum[(size_t)y] : 0.0f;
        condCDF[(size_t)y * (size_t)width] = importance[(size_t)y * (size_t)width] * invRowSum;
        for (int x = 1; x < width; ++x) {
            size_t idx = (size_t)y * (size_t)width + (size_t)x;
            condCDF[idx] = condCDF[idx - 1] + importance[idx] * invRowSum;
        }
        condCDF[(size_t)y * (size_t)width + (size_t)width - 1] = 1.0f;
    }

    float totalSum = 0.0f;
    for (float sum : rowSum)
        totalSum += sum;

    std::vector<float> margCDF((size_t)height);
    float invTotal = totalSum > 1e-10f ? 1.0f / totalSum : 0.0f;
    margCDF[0] = rowSum[0] * invTotal;
    for (int y = 1; y < height; ++y)
        margCDF[(size_t)y] = margCDF[(size_t)y - 1] + rowSum[(size_t)y] * invTotal;
    margCDF[(size_t)height - 1] = 1.0f;

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = (UINT)width;
    td.Height = (UINT)height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R32_FLOAT;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = condCDF.data();
    init.SysMemPitch = (UINT)(width * sizeof(float));

    HRESULT hr = device->CreateTexture2D(&td, &init, m_condCDFTexture.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        SPDLOG_ERROR("EnvMap: CreateTexture2D conditional CDF failed 0x{:08x}", (uint32_t)hr);
        return false;
    }

    hr = device->CreateShaderResourceView(m_condCDFTexture.Get(), nullptr, m_condCDFSRV.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        SPDLOG_ERROR("EnvMap: CreateShaderResourceView conditional CDF failed 0x{:08x}", (uint32_t)hr);
        return false;
    }

    td.Width = 1;
    td.Height = (UINT)height;
    init.pSysMem = margCDF.data();
    init.SysMemPitch = sizeof(float);

    hr = device->CreateTexture2D(&td, &init, m_margCDFTexture.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        SPDLOG_ERROR("EnvMap: CreateTexture2D marginal CDF failed 0x{:08x}", (uint32_t)hr);
        return false;
    }

    hr = device->CreateShaderResourceView(m_margCDFTexture.Get(), nullptr, m_margCDFSRV.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        SPDLOG_ERROR("EnvMap: CreateShaderResourceView marginal CDF failed 0x{:08x}", (uint32_t)hr);
        return false;
    }

    SPDLOG_INFO("EnvMap: CDF baked ({}x{}, totalSum={:.2f})", width, height, totalSum);
    return true;
}
