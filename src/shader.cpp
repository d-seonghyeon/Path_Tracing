#include "shader.h"
#include <filesystem>

ShaderUPtr Shader::CreateFromFile(const std::string& filename, const std::string& entry, const std::string& profile) {
    auto shader = ShaderUPtr(new Shader());
    if (!shader->LoadFile(filename, entry, profile)) {
        return nullptr;
    }
    return std::move(shader);
}

Shader::~Shader() {}

bool Shader::LoadFile(const std::string& filename, const std::string& entry, const std::string& profile) {
    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
    
#if defined(DEBUG) || defined(_DEBUG)
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    std::filesystem::path p(filename);
    std::wstring wPath = p.wstring();
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3DCompileFromFile(
        wPath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entry.c_str(), profile.c_str(), compileFlags, 0,
        &m_shaderBlob, &errorBlob
    );//HRESULT = 이 함수가 성공했냐 실패했냐 반환

    if (FAILED(hr)) {
        if (errorBlob) {
            SPDLOG_ERROR("Shader Compile Error in [{}]:\n{}", filename, (char*)errorBlob->GetBufferPointer());
        } else {
            SPDLOG_ERROR("Shader file access error (HRESULT: 0x{:08x}): \"{}\"", (uint32_t)hr, filename);
        }
        return false;
    }
    return true;
}