#ifndef __CONTEXT_H__
#define __CONTEXT_H__

#include "common.h"
#include "shader.h"
#include "compute_program.h"
#include "buffer.h"
#include "texture.h"
#include "mesh.h"
#include "model.h"
#include "bvh.h"
#include "scene_desc.h"

// -------------------------------------------------------
// GPU 상수 버퍼 - 카메라 파라미터 (PathTracer cbuffer b0)
// -------------------------------------------------------
struct GlobalUniforms {
    glm::vec3 cameraPos;
    float     fov;
    glm::vec3 cameraFront;
    float     aspectRatio;
    glm::vec3 cameraUp;
    float     frameCount;
    glm::vec3 cameraRight;
    uint32_t  lightCount;
    glm::mat4 prevViewProj;
    glm::mat4 currViewProj;
};

// -------------------------------------------------------
// ToneMap 상수 버퍼 (ToneMapCS cbuffer b0)
// -------------------------------------------------------
struct ToneMapUniforms {
    uint32_t frameCount;
    uint32_t pad[3];
};

CLASS_PTR(Context)
class Context {
public:
    static ContextUPtr Create(ID3D11Device* device, ID3D11DeviceContext* context);
    void Render(ID3D11DeviceContext* context, uint32_t width, uint32_t height);
    void OnResize(ID3D11Device* device, uint32_t width, uint32_t height);
    void Present(ID3D11DeviceContext* context, ID3D11RenderTargetView* rtv);
    void ProcessMouseMenu(float dx, float dy);
    void ProcessKeyboard(float deltaTime);

private:
    Context() {}
    bool Init(ID3D11Device* device, ID3D11DeviceContext* context);
    bool BuildSceneBuffers(ID3D11Device* device);

    // --- Compute Programs ---
    ComputeProgramUPtr m_pathTracerProgram;
    ComputeProgramUPtr m_compositeProgram;
    ComputeProgramUPtr m_toneMapProgram;    // [추가] 톤맵 전용 프로그램

    // 출력 텍스처 (LDR 표시용)
    ComPtr<ID3D11Texture2D>           m_outputTexture;
    ComPtr<ID3D11UnorderedAccessView> m_outputUAV;
    ComPtr<ID3D11ShaderResourceView>  m_outputSRV;

    // HDR 합성 버퍼 (Composite 결과)
    ComPtr<ID3D11Texture2D>           m_compositeTexture;
    ComPtr<ID3D11UnorderedAccessView> m_compositeUAV;
    ComPtr<ID3D11ShaderResourceView>  m_compositeSRV;

    // PathTracer G-buffer (Phase 0)
    ComPtr<ID3D11Texture2D>           m_diffuseRadianceTexture;
    ComPtr<ID3D11UnorderedAccessView> m_diffuseRadianceUAV;
    ComPtr<ID3D11ShaderResourceView>  m_diffuseRadianceSRV;

    ComPtr<ID3D11Texture2D>           m_specularRadianceTexture;
    ComPtr<ID3D11UnorderedAccessView> m_specularRadianceUAV;
    ComPtr<ID3D11ShaderResourceView>  m_specularRadianceSRV;

    ComPtr<ID3D11Texture2D>           m_viewZTexture;
    ComPtr<ID3D11UnorderedAccessView> m_viewZUAV;
    ComPtr<ID3D11ShaderResourceView>  m_viewZSRV;

    ComPtr<ID3D11Texture2D>           m_normalRoughnessTexture;
    ComPtr<ID3D11UnorderedAccessView> m_normalRoughnessUAV;
    ComPtr<ID3D11ShaderResourceView>  m_normalRoughnessSRV;

    ComPtr<ID3D11Texture2D>           m_motionVectorTexture;
    ComPtr<ID3D11UnorderedAccessView> m_motionVectorUAV;
    ComPtr<ID3D11ShaderResourceView>  m_motionVectorSRV;

    ComPtr<ID3D11Texture2D>           m_baseColorMetalnessTexture;
    ComPtr<ID3D11UnorderedAccessView> m_baseColorMetalnessUAV;
    ComPtr<ID3D11ShaderResourceView>  m_baseColorMetalnessSRV;

    ComPtr<ID3D11Texture2D>           m_emissiveTexture;
    ComPtr<ID3D11UnorderedAccessView> m_emissiveUAV;
    ComPtr<ID3D11ShaderResourceView>  m_emissiveSRV;

    // --- Scene Data ---
    ModelUPtr m_model;

    BufferUPtr m_vertexBuffer;
    BufferUPtr m_indexBuffer;
    BufferUPtr m_meshInfoBuffer;
    BufferUPtr m_materialBuffer;
    BufferUPtr m_bvhNodeBuffer;
    BufferUPtr m_bvhPrimBuffer;
    BufferUPtr m_lightBuffer;

    ComPtr<ID3D11ShaderResourceView> m_vertexSRV;
    ComPtr<ID3D11ShaderResourceView> m_indexSRV;
    ComPtr<ID3D11ShaderResourceView> m_meshInfoSRV;
    ComPtr<ID3D11ShaderResourceView> m_materialSRV;
    ComPtr<ID3D11ShaderResourceView> m_bvhNodeSRV;
    ComPtr<ID3D11ShaderResourceView> m_bvhPrimSRV;
    ComPtr<ID3D11ShaderResourceView> m_lightSRV;

    uint32_t m_meshCount  { 0 };
    uint32_t m_lightCount { 0 };

    // 상수 버퍼
    BufferUPtr m_globalBuffer;
    BufferUPtr m_toneMapBuffer;    // [추가] 톤맵 상수 버퍼

    // --- Camera State ---
    glm::vec3 m_cameraPos   { 0.0f, 2.5f, -6.0f };
    glm::vec3 m_cameraFront { 0.0f, 0.0f,  1.0f };
    glm::vec3 m_cameraUp    { 0.0f, 1.0f,  0.0f };
    float m_yaw   = 90.0f;
    float m_pitch =  0.0f;
    float m_cameraSpeed      = 5.0f;
    float m_mouseSensitivity = 0.1f;
    uint32_t m_frameCount = 0;
    glm::mat4 m_prevViewProj { 1.0f };
};

#endif
