#include "nrd_denoiser.h"

#include <spdlog/spdlog.h>

NrdDenoiserUPtr NrdDenoiser::Create(ID3D11Device* device, uint32_t width, uint32_t height) {
    auto denoiser = NrdDenoiserUPtr(new NrdDenoiser());
    if (!denoiser->Init(device, width, height))
        return nullptr;
    return denoiser;
}

bool NrdDenoiser::Init(ID3D11Device* device, uint32_t width, uint32_t height) {
    if (!device) {
        SPDLOG_ERROR("NrdDenoiser::Init failed: device is null.");
        return false;
    }

    m_width          = width;
    m_height         = height;
    m_resetRequested = true;

#if PT_ENABLE_NRD && __has_include(<NRD.h>)
    // Phase 2 wiring point:
    //   nrd::InstanceCreationDesc desc = {};
    //   desc.denoisers    = &denoiserDesc;
    //   desc.denoisersNum = 1;
    //   nrd::CreateInstance(desc, m_nrdInstance);
    // NRI is intentionally absent — use embedded DXBC path only (AGENTS.md §7.1).
    m_isBackendAvailable = true;
    m_isInitialized      = true;
    SPDLOG_INFO("NRD headers detected. NrdDenoiser ready for Phase 2 NRD instance wiring.");
    return true;
#else
    m_isBackendAvailable = false;
    m_isInitialized      = true;
    SPDLOG_WARN("NRD SDK not available. NrdDenoiser running as pass-through stub.");
    return true;
#endif
}

// -------------------------------------------------------
// Denoise — stub path: 입력 SRV를 출력 UAV로 pass-through 복사.
// NRD SDK 연결 후 이 함수 내에서 nrd::Dispatch 를 호출한다.
// -------------------------------------------------------
bool NrdDenoiser::Denoise(ID3D11DeviceContext* ctx,
                          const NrdGBufferInputs& inputs,
                          const NrdDenoisedOutputs& outputs,
                          uint32_t frameIndex) {
    if (!ctx) return false;
    (void)frameIndex;

#if PT_ENABLE_NRD && __has_include(<NRD.h>)
    // Phase 2: nrd::Dispatch 호출 자리.
    // 현재는 SDK 헤더가 있어도 아직 인스턴스가 없으므로 stub으로 처리.
    SPDLOG_WARN("NrdDenoiser::Denoise: NRD instance not yet wired (Phase 2 TODO). Using stub.");
#endif

    // Stub: input SRVs → output UAVs copy.
    // outputs 의 UAV 뒤에 있는 Texture2D 리소스를 가져와 CopyResource.
    ID3D11Resource* outDiffRes  = nullptr;
    ID3D11Resource* outSpecRes  = nullptr;
    ID3D11Resource* inDiffRes   = nullptr;
    ID3D11Resource* inSpecRes   = nullptr;

    if (outputs.diffuse)  outputs.diffuse->GetResource(&outDiffRes);
    if (outputs.specular) outputs.specular->GetResource(&outSpecRes);
    if (inputs.diffuseRadiance)  inputs.diffuseRadiance->GetResource(&inDiffRes);
    if (inputs.specularRadiance) inputs.specularRadiance->GetResource(&inSpecRes);

    if (inDiffRes && outDiffRes) ctx->CopyResource(outDiffRes, inDiffRes);
    if (inSpecRes && outSpecRes) ctx->CopyResource(outSpecRes, inSpecRes);

    if (outDiffRes)  outDiffRes->Release();
    if (outSpecRes)  outSpecRes->Release();
    if (inDiffRes)   inDiffRes->Release();
    if (inSpecRes)   inSpecRes->Release();

    if (m_resetRequested) m_resetRequested = false;
    return true;
}

bool NrdDenoiser::OnResize(ID3D11Device* device, uint32_t width, uint32_t height) {
    if (!device) {
        SPDLOG_ERROR("NrdDenoiser::OnResize failed: device is null.");
        return false;
    }

    m_width          = width;
    m_height         = height;
    m_resetRequested = true;
    return true;
}

void NrdDenoiser::ResetHistory() {
    m_resetRequested = true;
}
