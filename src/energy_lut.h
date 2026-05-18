#ifndef __ENERGY_LUT_H__
#define __ENERGY_LUT_H__

#include "common.h"

CLASS_PTR(EnergyLUT)
class EnergyLUT {
public:
    static EnergyLUTUPtr Create(ID3D11Device* device);

    ID3D11ShaderResourceView* GetSRV() const { return m_srv.Get(); }
    ID3D11SamplerState* GetSampler() const { return m_sampler.Get(); }

    static constexpr int LUT_SIZE = 32;

private:
    EnergyLUT() {}

    bool Init(ID3D11Device* device);
    void BakeLUT(std::vector<float>& outData);

    ComPtr<ID3D11Texture2D> m_texture;
    ComPtr<ID3D11ShaderResourceView> m_srv;
    ComPtr<ID3D11SamplerState> m_sampler;
};

#endif
