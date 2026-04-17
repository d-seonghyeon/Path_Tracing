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
    // Phase 2 will construct the NRD instance and pipeline descriptions here.
    // We intentionally stay on the embedded-DXBC path and never introduce NRI.
    m_isBackendAvailable = true;
    m_isInitialized      = true;
    SPDLOG_INFO("NRD headers detected. NrdDenoiser scaffold initialized for Phase 2 wiring.");
    return true;
#else
    m_isBackendAvailable = false;
    m_isInitialized      = true;
    SPDLOG_WARN("NRD SDK is not available in this workspace yet. NrdDenoiser remains a stub.");
    return true;
#endif
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
