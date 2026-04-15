#ifndef __BUFFER_H__
#define __BUFFER_H__

#include "common.h"

CLASS_PTR(Buffer)
class Buffer {
public:
    // miscFlags 인자를 추가하여 D3D11_RESOURCE_MISC_BUFFER_STRUCTURED를 받을 수 있게 함
    static BufferUPtr CreateWithData(
        ID3D11Device* device, UINT bindFlags, D3D11_USAGE usage, 
        const void* data, uint32_t stride, uint32_t count, 
        UINT miscFlags = 0); // [추가] 패스 트레이싱용 플래그
    
    ~Buffer() = default;

    // 래스터라이저용 Bind 외에 SRV를 생성하고 반환하는 기능 추가
    ComPtr<ID3D11ShaderResourceView> CreateSRV(ID3D11Device* device) const;

    void Bind(ID3D11DeviceContext* context, UINT slot = 0) const;
    
    uint32_t GetStride() const { return m_stride; }
    uint32_t GetCount() const { return m_count; }
    uint32_t GetTotalSize() const { return m_stride * m_count; }
    ID3D11Buffer* GetBuffer() const { return m_buffer.Get(); }

    template<typename T>
    void UpdateData(ID3D11DeviceContext* context, const T& data) {
        D3D11_MAPPED_SUBRESOURCE mapped {};
        if (SUCCEEDED(context->Map(m_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            memcpy(mapped.pData, &data, sizeof(T));
            context->Unmap(m_buffer.Get(), 0);
        }
    }

private:
    Buffer() = default;
    bool Init(ID3D11Device* device, UINT bindFlags, D3D11_USAGE usage, 
              const void* data, uint32_t stride, uint32_t count, UINT miscFlags);

    ComPtr<ID3D11Buffer> m_buffer;
    UINT m_bindFlags{ 0 };
    uint32_t m_stride{ 0 };
    uint32_t m_count { 0 };
};

#endif