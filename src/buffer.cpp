#include "buffer.h"

BufferUPtr Buffer::CreateWithData(
    ID3D11Device* device, UINT bindFlags, D3D11_USAGE usage, 
    const void* data, uint32_t stride, uint32_t count, UINT miscFlags) 
{
    auto buffer = BufferUPtr(new Buffer());
    if (!buffer->Init(device, bindFlags, usage, data, stride, count, miscFlags))
        return nullptr;
    return std::move(buffer);
}

bool Buffer::Init(
    ID3D11Device* device, UINT bindFlags, D3D11_USAGE usage, 
    const void* data, uint32_t stride, uint32_t count, UINT miscFlags) 
{
    m_bindFlags = bindFlags;
    m_stride = stride;
    m_count = count;

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = stride * count;
    desc.Usage = usage;
    desc.BindFlags = bindFlags;
    desc.CPUAccessFlags = (usage == D3D11_USAGE_DYNAMIC) ? D3D11_CPU_ACCESS_WRITE : 0;
    desc.MiscFlags = miscFlags;
    
    // [핵심] StructuredBuffer라면 개당 크기를 반드시 명시해야 함
    if (miscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) {
        desc.StructureByteStride = stride;
    }

    D3D11_SUBRESOURCE_DATA initialData = {};
    initialData.pSysMem = data;

    // 데이터가 있을 때만 초기화 데이터 전달
    HRESULT hr = device->CreateBuffer(&desc, data ? &initialData : nullptr, m_buffer.GetAddressOf());
    if (FAILED(hr)) {
        SPDLOG_ERROR("Failed to create buffer!");
        return false;
    }

    return true;
}

// 셰이더에서 t0, t1 레지스터 등으로 읽기 위한 '뷰' 생성
ComPtr<ID3D11ShaderResourceView> Buffer::CreateSRV(ID3D11Device* device) const {
    ComPtr<ID3D11ShaderResourceView> srv;
    
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN; // StructuredBuffer는 UNKNOWN으로 설정
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = m_count;

    HRESULT hr = device->CreateShaderResourceView(m_buffer.Get(), &srvDesc, srv.GetAddressOf());
    if (FAILED(hr)) {
        SPDLOG_ERROR("Failed to create Shader Resource View for buffer!");
        return nullptr;
    }
    return srv;
}

void Buffer::Bind(ID3D11DeviceContext* context, UINT slot) const {
    // 기존 래스터라이저용 바인딩 로직 (필요시 유지)
    if (m_bindFlags & D3D11_BIND_VERTEX_BUFFER) {
        UINT offset = 0;
        context->IASetVertexBuffers(slot, 1, m_buffer.GetAddressOf(), &m_stride, &offset);
    }
    else if (m_bindFlags & D3D11_BIND_INDEX_BUFFER) {
        context->IASetIndexBuffer(m_buffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    }
}