#ifndef __NRD_DENOISER_H__
#define __NRD_DENOISER_H__

#include "common.h"

// NrdDenoiser
// Phase 1/2 scaffold for the DX11 + embedded-DXBC NRD path.
// This class intentionally avoids any NRI dependency.
CLASS_PTR(NrdDenoiser)
class NrdDenoiser {
public:
    static NrdDenoiserUPtr Create(ID3D11Device* device, uint32_t width, uint32_t height);

    bool IsInitialized() const { return m_isInitialized; }
    bool IsBackendAvailable() const { return m_isBackendAvailable; }

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
