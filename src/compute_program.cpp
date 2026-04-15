#include "compute_program.h"

ComputeProgramUPtr ComputeProgram::Create(ID3D11Device* device, ShaderUPtr cs) {
    auto program = ComputeProgramUPtr(new ComputeProgram());
    if (!program->Init(device, std::move(cs))) return nullptr;
    return std::move(program);
}

bool ComputeProgram::Init(ID3D11Device* device, ShaderUPtr cs) {
    auto blob = cs->GetBuffer();
    if (!blob) {
        SPDLOG_ERROR("Shader blob is null.");
        return false;
    }

    HRESULT hr = device->CreateComputeShader(
        blob->GetBufferPointer(),
        blob->GetBufferSize(),
        nullptr,
        m_cs.GetAddressOf()
    );
    if (FAILED(hr)) {
        SPDLOG_ERROR("Failed to create compute shader. HRESULT: 0x{:08x}", (uint32_t)hr);
        return false;
    }
    return true;
}

void ComputeProgram::Dispatch(ID3D11DeviceContext* context, uint32_t x, uint32_t y, uint32_t z) const {
    // CSSetShader + Dispatch를 한 번에 처리
    // context.cpp에서 Use()를 별도로 호출할 필요 없음
    context->CSSetShader(m_cs.Get(), nullptr, 0);
    context->Dispatch(x, y, z);
}