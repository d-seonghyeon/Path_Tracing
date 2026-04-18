#ifndef __NRD_DENOISER_H__
#define __NRD_DENOISER_H__

#include "common.h"

#if PT_ENABLE_NRD && __has_include(<NRD.h>)
#include <NRD.h>
#endif

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
// NrdCameraData — Denoise 호출당 행렬 패키지
// GLM column-major; NRD CommonSettings도 column-major (vector-as-column).
// -------------------------------------------------------
struct NrdCameraData {
    glm::mat4 viewMatrix;      // current frame: world → camera
    glm::mat4 projMatrix;      // current frame: camera → clip (non-jittered)
    glm::mat4 prevViewMatrix;  // previous frame: world → camera
};

// -------------------------------------------------------
// NrdPoolEntry — 풀 텍스처 하나 (SRV + UAV 공용)
// -------------------------------------------------------
#if PT_ENABLE_NRD && __has_include(<NRD.h>)
struct NrdPoolEntry {
    ComPtr<ID3D11Texture2D>           texture;
    ComPtr<ID3D11ShaderResourceView>  srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
};
#endif

// -------------------------------------------------------
// NrdDenoiser
// Phase 2: DX11 + embedded-DXBC NRD (REBLUR_DIFFUSE_SPECULAR) 래퍼.
// NRI 는 사용하지 않는다 (AGENTS.md §7.1).
// -------------------------------------------------------
CLASS_PTR(NrdDenoiser)
class NrdDenoiser {
public:
    static NrdDenoiserUPtr Create(ID3D11Device* device, uint32_t width, uint32_t height);
    ~NrdDenoiser();

    bool IsInitialized() const      { return m_isInitialized; }
    bool IsBackendAvailable() const { return m_isBackendAvailable; }
    bool HasUsableBackend() const   { return m_isInitialized && m_isBackendAvailable; }
    const char* GetBackendStatusLabel() const;

    // Denoise: G-buffer + camera 행렬을 받아 denoised 결과를 outputs에 기록.
    // NRD SDK 미연결 시 G-buffer를 그대로 outputs으로 복사(stub).
    bool Denoise(ID3D11DeviceContext* ctx,
                 const NrdGBufferInputs& inputs,
                 const NrdDenoisedOutputs& outputs,
                 const NrdCameraData& camera,
                 uint32_t frameIndex);

    bool OnResize(ID3D11Device* device, uint32_t width, uint32_t height);
    void ResetHistory();

private:
    NrdDenoiser() = default;
    bool Init(ID3D11Device* device, uint32_t width, uint32_t height);
    void DestroyBackend();

    bool     m_isInitialized      { false };
    bool     m_isBackendAvailable { false };
    bool     m_resetRequested     { true };
    uint32_t m_width              { 0 };
    uint32_t m_height             { 0 };

#if PT_ENABLE_NRD && __has_include(<NRD.h>)
    nrd::Instance*                           m_nrdInstance { nullptr };
    std::vector<ComPtr<ID3D11ComputeShader>> m_pipelines;
    std::vector<NrdPoolEntry>                m_permanentPool;
    std::vector<NrdPoolEntry>                m_transientPool;
    ComPtr<ID3D11SamplerState>               m_samplers[2]; // [0]=nearest clamp, [1]=linear clamp
    ComPtr<ID3D11Buffer>                     m_constantBuffer;

    // 리소스 리졸브 헬퍼
    ID3D11ShaderResourceView*  ResolveSRV(const nrd::ResourceDesc& res,
                                          const NrdGBufferInputs& in) const;
    ID3D11UnorderedAccessView* ResolveUAV(const nrd::ResourceDesc& res,
                                          const NrdDenoisedOutputs& out) const;
#endif
};

#endif
