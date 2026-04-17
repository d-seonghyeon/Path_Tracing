#ifndef __NRD_DENOISER_H__
#define __NRD_DENOISER_H__

#include "common.h"

// -------------------------------------------------------
// NrdGBufferInputs — PathTracer G-buffer SRV 묶음
// -------------------------------------------------------
struct NrdGBufferInputs {
    ID3D11ShaderResourceView* diffuseRadiance;   // R16G16B16A16_FLOAT (.rgb=diffuse, .a=hitT)
    ID3D11ShaderResourceView* specularRadiance;  // R16G16B16A16_FLOAT (.rgb=specular, .a=hitT)
    ID3D11ShaderResourceView* viewZ;             // R32_FLOAT
    ID3D11ShaderResourceView* normalRoughness;   // R16G16B16A16_FLOAT (.rg=octa N, .b=roughness)
    ID3D11ShaderResourceView* motionVector;      // R16G16_FLOAT (픽셀 단위 prev-curr)
};

// -------------------------------------------------------
// NrdDenoisedOutputs — NRD 출력 UAV 묶음 (Composite 패스 입력)
// -------------------------------------------------------
struct NrdDenoisedOutputs {
    ID3D11UnorderedAccessView* diffuse;    // R16G16B16A16_FLOAT (denoised diffuse radiance)
    ID3D11UnorderedAccessView* specular;   // R16G16B16A16_FLOAT (denoised specular radiance)
};

// -------------------------------------------------------
// NrdDenoiser
// Phase 2: DX11 + embedded-DXBC NRD (REBLUR_DIFFUSE_SPECULAR) 래퍼.
// NRI 는 사용하지 않는다 (AGENTS.md §7.1).
// -------------------------------------------------------
CLASS_PTR(NrdDenoiser)
class NrdDenoiser {
public:
    static NrdDenoiserUPtr Create(ID3D11Device* device, uint32_t width, uint32_t height);

    bool IsInitialized() const      { return m_isInitialized; }
    bool IsBackendAvailable() const { return m_isBackendAvailable; }

    // Denoise: G-buffer를 읽어 denoised 결과를 outputs에 기록.
    // NRD SDK 미설치 시 G-buffer를 그대로 outputs으로 복사(stub).
    // Returns false if denoising was skipped entirely.
    bool Denoise(ID3D11DeviceContext* ctx,
                 const NrdGBufferInputs& inputs,
                 const NrdDenoisedOutputs& outputs,
                 uint32_t frameIndex);

    bool OnResize(ID3D11Device* device, uint32_t width, uint32_t height);
    void ResetHistory();

private:
    NrdDenoiser() = default;
    bool Init(ID3D11Device* device, uint32_t width, uint32_t height);

    bool     m_isInitialized      { false };
    bool     m_isBackendAvailable { false };
    bool     m_resetRequested     { true };
    uint32_t m_width              { 0 };
    uint32_t m_height             { 0 };
};

#endif
