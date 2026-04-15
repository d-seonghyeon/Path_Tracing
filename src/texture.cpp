#include "texture.h"

// 1. 생성자에서 ID3D11Device가 필요합니다. (GPU 메모리 할당 주체)
TextureUPtr Texture::CreateFromImage(ID3D11Device* device,ID3D11DeviceContext* context ,const Image* image) {
    auto texture = TextureUPtr(new Texture());
    if (!texture->CreateTexture(device, context, image)) {
        return nullptr;
    }
    return std::move(texture);
}

Texture::~Texture() {
    // ComPtr이므로 자동 해제됩니다. 별도의 Release 호출이 필요 없습니다!
}

// 2. 바인딩: 이제 어느 슬롯(채널)에 꽂을지 알려줘야 합니다.
void Texture::Bind(ID3D11DeviceContext* context, uint32_t slot) const {
    // 셰이더 리소스 뷰(SRV)를 픽셀 셰이더 슬롯에 바인딩
    context->PSSetShaderResources(slot, 1, m_srv.GetAddressOf());
}
bool Texture::CreateTexture(ID3D11Device* device, ID3D11DeviceContext* context, const Image* image) {
    // [1] 밉맵을 지원하도록 Desc 설정 변경
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = image->GetWidth();
    desc.Height = image->GetHeight();
    desc.MipLevels = 0; // 0으로 설정하면 GPU가 가능한 최대 단계까지 생성합니다.
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    
    // 중요: 밉맵 자동 생성을 위해 'RENDER_TARGET'과 'MISC_GENERATE_MIPS' 플래그가 필요합니다.
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET; //or연산
    desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

    // [2] 처음에는 0번 레벨(원본) 데이터만 넣을 수 없으므로, 일단 빈 텍스처를 만듭니다.
    HRESULT hr = device->CreateTexture2D(&desc, nullptr, m_texture.GetAddressOf());
    if (FAILED(hr)) return false;

    // [3] 원본 데이터를 0번 밉 레벨에 복사합니다.
    //cpu->gpu로 이미지를 복사
    uint32_t pitch = image->GetWidth() * image->GetChannelCount();
    context->UpdateSubresource(m_texture.Get(), 0, nullptr, image->GetData(), pitch, 0);

    // [4] SRV 생성(텍스처를 shader에서 읽게해줌)
    hr = device->CreateShaderResourceView(m_texture.Get(), nullptr, m_srv.GetAddressOf());
    if (FAILED(hr)) return false;

    // [5] OpenGL의 glGenerateMipmap과 같은 역할!
    context->GenerateMips(m_srv.Get());

    return true;
}