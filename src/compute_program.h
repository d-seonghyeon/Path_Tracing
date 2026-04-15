#ifndef __COMPUTE_PROGRAM_H__
#define __COMPUTE_PROGRAM_H__
#include "common.h"
#include "shader.h"

CLASS_PTR(ComputeProgram)
class ComputeProgram {
public:
    // [수정] ShaderPtr(shared_ptr) → ShaderUPtr(unique_ptr)
    // CreateFromFile이 UPtr을 반환하므로 타입 일치
    static ComputeProgramUPtr Create(ID3D11Device* device, ShaderUPtr cs);

    void Dispatch(ID3D11DeviceContext* context, uint32_t x, uint32_t y, uint32_t z) const;

private:
    ComputeProgram() {}
    bool Init(ID3D11Device* device, ShaderUPtr cs);

    ComPtr<ID3D11ComputeShader> m_cs;
};
#endif