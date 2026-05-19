#ifndef __ENV_MAP_H__
#define __ENV_MAP_H__

#include "common.h"

CLASS_PTR(EnvMap)
class EnvMap {
public:
    static EnvMapUPtr Load(ID3D11Device* device, const std::string& filepath);

    ID3D11ShaderResourceView* GetEnvSRV() const { return m_envSRV.Get(); }
    ID3D11ShaderResourceView* GetCondCDFSRV() const { return m_condCDFSRV.Get(); }
    ID3D11ShaderResourceView* GetMargCDFSRV() const { return m_margCDFSRV.Get(); }
    ID3D11SamplerState* GetSampler() const { return m_sampler.Get(); }

    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    bool IsLoaded() const { return m_envSRV != nullptr; }

private:
    EnvMap() {}

    bool Init(ID3D11Device* device, const std::string& filepath);
    bool BakeCDF(ID3D11Device* device, const float* pixels, int width, int height);

    ComPtr<ID3D11Texture2D> m_envTexture;
    ComPtr<ID3D11ShaderResourceView> m_envSRV;

    ComPtr<ID3D11Texture2D> m_condCDFTexture;
    ComPtr<ID3D11ShaderResourceView> m_condCDFSRV;

    ComPtr<ID3D11Texture2D> m_margCDFTexture;
    ComPtr<ID3D11ShaderResourceView> m_margCDFSRV;

    ComPtr<ID3D11SamplerState> m_sampler;

    int m_width { 0 };
    int m_height { 0 };
};

#endif
