#ifndef __TEXTURE_H__
#define __TEXTURE_H__

#include "image.h"
#include <d3d11.h>
#include <wrl/client.h> // ComPtr 사용을 위해

CLASS_PTR(Texture)

class Texture {
public:
    // DX11은 생성 시 Device가 반드시 필요합니다!
    static TextureUPtr CreateFromImage(ID3D11Device* device,ID3D11DeviceContext* context ,const Image* image);
    
    ~Texture();

    // Getter: 셰이더 바인딩에 핵심인 SRV를 반환합니다.
    ID3D11ShaderResourceView* GetSRV() const { return m_srv.Get(); }
    
    // Bind: 이제는 슬롯 번호가 필요합니다.
    void Bind(ID3D11DeviceContext* context, uint32_t slot) const;

private:
    Texture() {}
    bool CreateTexture(ID3D11Device* device,ID3D11DeviceContext* context, const Image* image);
    // DX11의 핵심 리소스들
    ComPtr<ID3D11Texture2D> m_texture;          // 실제 메모리 덩어리
    ComPtr<ID3D11ShaderResourceView> m_srv;    // 셰이더가 읽는 통로
    
};

#endif