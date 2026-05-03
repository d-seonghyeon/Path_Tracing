#include "EnvMap.h"
#include <stb/stb_image.h>  // STB_IMAGE_IMPLEMENTATION은 image.cpp에서 정의됨


EnvMapUPtr EnvMap::Load(ID3D11Device* device, const std::string& filepath) {
    auto envMap = EnvMapUPtr(new EnvMap());
    if (!envMap->Init(device, filepath))
        return nullptr;
    return std::move(envMap);
}

bool EnvMap::Init(ID3D11Device* device, const std::string& filepath) {
    // -------------------------------------------------------
    // 1. HDRI 파일 로드 (float RGB)
    // -------------------------------------------------------
    int w, h, channels;
    stbi_set_flip_vertically_on_load(false);
    float* pixels = stbi_loadf(filepath.c_str(), &w, &h, &channels, 3);
    if (!pixels) {
        SPDLOG_WARN("EnvMap: HDRI not found [{}] — using procedural sky", filepath);
        return false;
    }

    m_width  = w;
    m_height = h;

    // -------------------------------------------------------
    // 2. HDRI 텍스처 생성 (t7)
    //    D3D11은 R32G32B32_FLOAT 지원이 불안정하므로 RGBA로 변환
    // -------------------------------------------------------
    std::vector<float> rgba(w * h * 4);
    for (int i = 0; i < w * h; ++i) {
        rgba[i*4+0] = pixels[i*3+0];
        rgba[i*4+1] = pixels[i*3+1];
        rgba[i*4+2] = pixels[i*3+2];
        rgba[i*4+3] = 1.0f;
    }
    {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width            = w;
        td.Height           = h;
        td.MipLevels        = 1;
        td.ArraySize        = 1;
        td.Format           = DXGI_FORMAT_R32G32B32A32_FLOAT;
        td.SampleDesc.Count = 1;
        td.Usage            = D3D11_USAGE_IMMUTABLE;
        td.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA init = {};
        init.pSysMem     = rgba.data();
        init.SysMemPitch = w * sizeof(float) * 4;

        HRESULT hr = device->CreateTexture2D(&td, &init,
                                              m_envTexture.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            SPDLOG_ERROR("EnvMap: Failed to create HDRI texture (0x{:08x})", (uint32_t)hr);
            stbi_image_free(pixels);
            return false;
        }
        device->CreateShaderResourceView(m_envTexture.Get(), nullptr,
                                          m_envSRV.ReleaseAndGetAddressOf());
    }

    // -------------------------------------------------------
    // 3. 샘플러 생성 (s0)
    //    U: Wrap (360도 환경맵), V: Clamp (극지방 경계)
    // -------------------------------------------------------
    {
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD   = D3D11_FLOAT32_MAX;
        device->CreateSamplerState(&sd, m_sampler.ReleaseAndGetAddressOf());
    }

    // -------------------------------------------------------
    // 4. CDF 베이킹
    // -------------------------------------------------------
    BakeCDF(device, pixels, w, h);
    stbi_image_free(pixels);

    SPDLOG_INFO("EnvMap: Loaded {}x{} HDRI [{}]", w, h, filepath);
    return true;
}

void EnvMap::BakeCDF(ID3D11Device* device, const float* pixels, int w, int h) {
    // -------------------------------------------------------
    // Step 1: importance = luminance(pixel) × sin(θ)
    //         sin(θ) 보정: equirectangular 맵에서 극지방 픽셀은
    //         실제 구면 면적이 작으므로 가중치를 낮춤
    // -------------------------------------------------------
    std::vector<float> importance(w * h);
    std::vector<float> rowSum(h, 0.0f);

    for (int y = 0; y < h; ++y) {
        float theta = glm::pi<float>() * (y + 0.5f) / h;
        float sinT  = sinf(theta);
        for (int x = 0; x < w; ++x) {
            int   idx = (y * w + x) * 3;
            float lum = 0.2126f * pixels[idx]
                      + 0.7152f * pixels[idx+1]
                      + 0.0722f * pixels[idx+2];
            importance[y * w + x] = lum * sinT;
            rowSum[y] += importance[y * w + x];
        }
    }

    // -------------------------------------------------------
    // Step 2: Conditional CDF (각 행별 누적 확률)
    //         condCDF[y][x] = P(col <= x | row == y)
    // -------------------------------------------------------
    std::vector<float> condCDF(w * h);
    for (int y = 0; y < h; ++y) {
        float inv = (rowSum[y] > 1e-10f) ? 1.0f / rowSum[y] : 0.0f;
        condCDF[y * w] = importance[y * w] * inv;
        for (int x = 1; x < w; ++x)
            condCDF[y * w + x] = condCDF[y * w + x - 1]
                                + importance[y * w + x] * inv;
        condCDF[y * w + w - 1] = 1.0f; // 마지막은 정확히 1.0 보장
    }

    // -------------------------------------------------------
    // Step 3: Marginal CDF (행 선택 누적 확률)
    //         margCDF[y] = P(row <= y)
    // -------------------------------------------------------
    float totalSum = 0.0f;
    for (int y = 0; y < h; ++y) totalSum += rowSum[y];

    std::vector<float> margCDF(h);
    float invTotal = (totalSum > 1e-10f) ? 1.0f / totalSum : 0.0f;
    margCDF[0] = rowSum[0] * invTotal;
    for (int y = 1; y < h; ++y)
        margCDF[y] = margCDF[y-1] + rowSum[y] * invTotal;
    margCDF[h-1] = 1.0f;

    // -------------------------------------------------------
    // Step 4: GPU 텍스처 생성
    // -------------------------------------------------------

    // Conditional CDF (t8): w × h, R32F
    {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = (UINT)w; td.Height = (UINT)h;
        td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R32_FLOAT;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA init = {};
        init.pSysMem     = condCDF.data();
        init.SysMemPitch = (UINT)(w * sizeof(float));

        device->CreateTexture2D(&td, &init, m_condCDFTex.ReleaseAndGetAddressOf());
        device->CreateShaderResourceView(m_condCDFTex.Get(), nullptr,
                                          m_condCDFSRV.ReleaseAndGetAddressOf());
    }

    // Marginal CDF (t9): 1 × h, R32F
    {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = 1; td.Height = (UINT)h;
        td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R32_FLOAT;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA init = {};
        init.pSysMem     = margCDF.data();
        init.SysMemPitch = sizeof(float);

        device->CreateTexture2D(&td, &init, m_margCDFTex.ReleaseAndGetAddressOf());
        device->CreateShaderResourceView(m_margCDFTex.Get(), nullptr,
                                          m_margCDFSRV.ReleaseAndGetAddressOf());
    }

    SPDLOG_INFO("EnvMap: CDF baked ({}x{}, totalSum={:.2f})", w, h, totalSum);
}