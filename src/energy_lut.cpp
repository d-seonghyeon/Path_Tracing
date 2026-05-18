#include "energy_lut.h"

#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

namespace {

glm::vec2 Hammersley(uint32_t i, uint32_t sampleCount) {
    uint32_t bits = i;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    float radicalInverse = (float)bits * 2.3283064365386963e-10f;
    return glm::vec2((float)i / (float)sampleCount, radicalInverse);
}

glm::vec3 ImportanceSampleGGX(glm::vec2 xi, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0f * glm::pi<float>() * xi.x;
    float cosTheta = sqrtf((1.0f - xi.y) / (1.0f + (a * a - 1.0f) * xi.y));
    float sinTheta = sqrtf(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    return glm::vec3(cosf(phi) * sinTheta, sinf(phi) * sinTheta, cosTheta);
}

float G1(float nDotV, float roughness) {
    float a = roughness * roughness;
    float k = a / 2.0f;
    return nDotV / (nDotV * (1.0f - k) + k);
}

float ComputeGGXEnergy(float nDotV, float roughness) {
    glm::vec3 V(sqrtf(std::max(0.0f, 1.0f - nDotV * nDotV)), 0.0f, nDotV);

    static const uint32_t SAMPLE_COUNT = 1024;
    float sum = 0.0f;

    for (uint32_t i = 0; i < SAMPLE_COUNT; ++i) {
        glm::vec2 xi = Hammersley(i, SAMPLE_COUNT);
        glm::vec3 H = ImportanceSampleGGX(xi, roughness);
        glm::vec3 L = glm::reflect(-V, H);

        float nDotL = std::max(L.z, 0.0f);
        float nDotH = std::max(H.z, 0.0f);
        float vDotH = std::max(glm::dot(V, H), 0.0f);

        if (nDotL > 0.0f) {
            float g2 = G1(nDotV, roughness) * G1(nDotL, roughness);
            float gVis = g2 * vDotH / (nDotH * nDotV + 1e-6f);
            sum += gVis;
        }
    }

    return sum / (float)SAMPLE_COUNT;
}

} // namespace

EnergyLUTUPtr EnergyLUT::Create(ID3D11Device* device) {
    auto lut = EnergyLUTUPtr(new EnergyLUT());
    if (!lut->Init(device))
        return nullptr;
    return lut;
}

void EnergyLUT::BakeLUT(std::vector<float>& outData) {
    outData.resize((size_t)LUT_SIZE * (size_t)LUT_SIZE * 2);

    for (int y = 0; y < LUT_SIZE; ++y) {
        float roughness = std::max(((float)y + 0.5f) / (float)LUT_SIZE, 0.05f);

        float eavg = 0.0f;
        static const int EAVG_SAMPLES = 32;
        for (int k = 0; k < EAVG_SAMPLES; ++k) {
            float mu = ((float)k + 0.5f) / (float)EAVG_SAMPLES;
            eavg += ComputeGGXEnergy(mu, roughness) * mu;
        }
        eavg = 2.0f * eavg / (float)EAVG_SAMPLES;

        for (int x = 0; x < LUT_SIZE; ++x) {
            float nDotV = std::max(((float)x + 0.5f) / (float)LUT_SIZE, 0.01f);
            float e = ComputeGGXEnergy(nDotV, roughness);
            size_t index = ((size_t)y * (size_t)LUT_SIZE + (size_t)x) * 2;
            outData[index + 0] = e;
            outData[index + 1] = eavg;
        }
    }

    SPDLOG_INFO("EnergyLUT: Bake complete ({}x{})", LUT_SIZE, LUT_SIZE);
}

bool EnergyLUT::Init(ID3D11Device* device) {
    std::vector<float> lutData;
    BakeLUT(lutData);

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = LUT_SIZE;
    td.Height = LUT_SIZE;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R32G32_FLOAT;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = lutData.data();
    init.SysMemPitch = LUT_SIZE * sizeof(float) * 2;

    HRESULT hr = device->CreateTexture2D(&td, &init, m_texture.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        SPDLOG_ERROR("EnergyLUT: CreateTexture2D failed 0x{:08x}", (uint32_t)hr);
        return false;
    }

    hr = device->CreateShaderResourceView(m_texture.Get(), nullptr, m_srv.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        SPDLOG_ERROR("EnergyLUT: CreateShaderResourceView failed 0x{:08x}", (uint32_t)hr);
        return false;
    }

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;

    hr = device->CreateSamplerState(&sd, m_sampler.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        SPDLOG_ERROR("EnergyLUT: CreateSamplerState failed 0x{:08x}", (uint32_t)hr);
        return false;
    }

    return true;
}
