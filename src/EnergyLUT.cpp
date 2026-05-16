#include "EnergyLUT.h"

EnergyLUTUPtr EnergyLUT::Create(ID3D11Device* device) {
    auto lut = EnergyLUTUPtr(new EnergyLUT());
    if (!lut->Init(device)) return nullptr;
    return std::move(lut);
}

namespace {

// Hammersley 저분산 시퀀스
glm::vec2 Hammersley(uint32_t i, uint32_t N) {
    uint32_t bits = i;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    float vdc = (float)bits * 2.3283064365386963e-10f;
    return glm::vec2((float)i / (float)N, vdc);
}

// GGX 중요도 샘플링 (셰이더와 동일)
glm::vec3 ImportanceSampleGGX(glm::vec2 xi, float roughness) {
    float a        = roughness * roughness;
    float phi      = 2.0f * glm::pi<float>() * xi.x;
    float cosTheta = sqrtf((1.0f - xi.y) / (1.0f + (a*a - 1.0f) * xi.y));
    float sinTheta = sqrtf(1.0f - cosTheta * cosTheta);
    return glm::vec3(cosf(phi) * sinTheta, sinf(phi) * sinTheta, cosTheta);
}

// Smith G1 (IBL용 k = a/2)
float G1(float NdotV, float roughness) {
    float a = roughness * roughness;
    float k = a / 2.0f;
    return NdotV / (NdotV * (1.0f - k) + k);
}

// E(NdotV, roughness): 단일산란 GGX 반구 적분
// F0=1 가정 (Fresnel 분리)
float ComputeGGXEnergy(float NdotV, float roughness) {
    glm::vec3 V(sqrtf(1.0f - NdotV * NdotV), 0.0f, NdotV);

    const uint32_t N = 1024;
    float sum = 0.0f;

    for (uint32_t i = 0; i < N; ++i) {
        glm::vec2 xi = Hammersley(i, N);
        glm::vec3 H  = ImportanceSampleGGX(xi, roughness);
        glm::vec3 L  = glm::reflect(-V, H);

        float NdotL = std::max(L.z, 0.0f);
        float NdotH = std::max(H.z, 0.0f);
        float VdotH = std::max(glm::dot(V, H), 0.0f);

        if (NdotL > 0.0f) {
            float G2   = G1(NdotV, roughness) * G1(NdotL, roughness);
            float Gvis = G2 * VdotH / (NdotH * NdotV + 1e-6f);
            sum += Gvis;
        }
    }
    return sum / (float)N;
}

} // namespace

// BakeLUT 수정 — RG32F (E, Eavg 같이 저장)
void EnergyLUT::BakeLUT(std::vector<float>& outData) {
    // RG32F: 픽셀당 2개 float
    outData.resize(LUT_SIZE * LUT_SIZE * 2);

    for (int y = 0; y < LUT_SIZE; ++y) {
        float roughness = std::max((y + 0.5f) / (float)LUT_SIZE, 0.05f);

        // Eavg 계산 (이 roughness에서 모든 방향 평균)
        // Eavg = 2 * ∫₀¹ E(μ, α) * μ dμ
        float eavg = 0.0f;
        const int EAVG_SAMPLES = 32;
        for (int k = 0; k < EAVG_SAMPLES; ++k) {
            float mu = (k + 0.5f) / (float)EAVG_SAMPLES;
            eavg += ComputeGGXEnergy(mu, roughness) * mu;
        }
        eavg = 2.0f * eavg / (float)EAVG_SAMPLES;

        for (int x = 0; x < LUT_SIZE; ++x) {
            float NdotV = std::max((x + 0.5f) / (float)LUT_SIZE, 0.01f);
            float e = ComputeGGXEnergy(NdotV, roughness);
            outData[(y * LUT_SIZE + x) * 2 + 0] = e;     // R: E
            outData[(y * LUT_SIZE + x) * 2 + 1] = eavg;  // G: Eavg
        }
    }
    SPDLOG_INFO("EnergyLUT: Bake complete ({}x{})", LUT_SIZE, LUT_SIZE);

}

bool EnergyLUT::Init(ID3D11Device* device) {
    // CPU 베이킹
    std::vector<float> lutData;
    BakeLUT(lutData);

    // GPU 텍스처 (t11, R32F)
    {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width            = LUT_SIZE;
        td.Height           = LUT_SIZE;
        td.MipLevels        = 1;
        td.ArraySize        = 1;
        td.Format        = DXGI_FORMAT_R32G32_FLOAT;  // RG32F
        td.SampleDesc.Count = 1;
        td.Usage            = D3D11_USAGE_IMMUTABLE;
        td.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA init = {};
        init.pSysMem     = lutData.data();
        init.SysMemPitch = LUT_SIZE * sizeof(float) * 2;

        HRESULT hr = device->CreateTexture2D(&td, &init,
                                              m_texture.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            SPDLOG_ERROR("EnergyLUT: Failed to create texture (0x{:08x})", (uint32_t)hr);
            return false;
        }
        device->CreateShaderResourceView(m_texture.Get(), nullptr,
                                          m_srv.ReleaseAndGetAddressOf());
    }

    // 샘플러: Linear Clamp (s1)
    {
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD   = D3D11_FLOAT32_MAX;
        device->CreateSamplerState(&sd, m_sampler.ReleaseAndGetAddressOf());
    }

    return true;
}