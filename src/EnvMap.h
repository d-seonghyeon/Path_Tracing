#ifndef __ENV_MAP_H__
#define __ENV_MAP_H__

#include "common.h"

CLASS_PTR(EnvMap)
class EnvMap {
public:
    // filepath: "hdri/xxx.hdr"
    // 실패 시 nullptr 반환 (호출자가 폴백 처리)
    static EnvMapUPtr Load(ID3D11Device* device, const std::string& filepath);

    ID3D11ShaderResourceView* GetEnvSRV()     const { return m_envSRV.Get(); }
    ID3D11ShaderResourceView* GetCondCDFSRV() const { return m_condCDFSRV.Get(); }
    ID3D11ShaderResourceView* GetMargCDFSRV() const { return m_margCDFSRV.Get(); }
    ID3D11SamplerState*       GetSampler()    const { return m_sampler.Get(); }

    int  GetWidth()  const { return m_width; }
    int  GetHeight() const { return m_height; }
    bool IsLoaded()  const { return m_envSRV != nullptr; }

private:
    EnvMap() {}
    bool Init(ID3D11Device* device, const std::string& filepath);
    void BakeCDF(ID3D11Device* device, const float* pixels, int w, int h);

    // t7: HDRI 원본 텍스처
    ComPtr<ID3D11Texture2D>          m_envTexture;
    ComPtr<ID3D11ShaderResourceView> m_envSRV;

    // t8: Conditional CDF (width × height, R32F)
    ComPtr<ID3D11Texture2D>          m_condCDFTex;
    ComPtr<ID3D11ShaderResourceView> m_condCDFSRV;

    // t9: Marginal CDF (1 × height, R32F)
    ComPtr<ID3D11Texture2D>          m_margCDFTex;
    ComPtr<ID3D11ShaderResourceView> m_margCDFSRV;

    // s0: Linear Wrap/Clamp 샘플러
    ComPtr<ID3D11SamplerState>       m_sampler;

    int m_width  { 0 };
    int m_height { 0 };
};

#endif