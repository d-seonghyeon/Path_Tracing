#ifndef __MESH_H__
#define __MESH_H__
#include "common.h"
#include "buffer.h"

struct Vertex {
    glm::vec3 position; float pad0;
    glm::vec3 normal;   float pad1;
    glm::vec2 texCoord; glm::vec2 pad2;
    glm::vec3 tangent;  float pad3;

    Vertex() : position(0.0f), pad0(0.0f), normal(0.0f), pad1(0.0f), texCoord(0.0f), pad2(0.0f), tangent(0.0f), pad3(0.0f) {}
    
    Vertex(const glm::vec3& p, const glm::vec3& n, const glm::vec2& t, const glm::vec3& tg)
        : position(p), pad0(0.0f), normal(n), pad1(0.0f), texCoord(t), pad2(0.0f), tangent(tg), pad3(0.0f) {}
};

struct MaterialData {
    glm::vec3 albedo    { 0.8f, 0.8f, 0.8f };
    float     roughness { 0.5f };
    glm::vec3 emissive  { 0.0f, 0.0f, 0.0f };
    float     metallic  { 0.0f };
};

CLASS_PTR(Mesh);
class Mesh {
public:
    static MeshUPtr Create(
        ID3D11Device* device,
        const std::vector<Vertex>& vertices,
        const std::vector<uint32_t>& indices
    );

    static MeshUPtr CreateBox(ID3D11Device* device);
    static MeshUPtr CreateSphere(ID3D11Device* device, uint32_t lati = 16, uint32_t longi = 32);
    static MeshUPtr CreatePlane(ID3D11Device* device);
    static void ComputeTangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

    // GPU SRV
    ID3D11ShaderResourceView* GetVertexSRV() const { return m_vertexSRV.Get(); }
    ID3D11ShaderResourceView* GetIndexSRV()  const { return m_indexSRV.Get(); }

    // [추가] CPU 데이터 접근자
    const std::vector<Vertex>&   GetVertices() const { return m_cpuVertices; }
    const std::vector<uint32_t>& GetIndices()  const { return m_cpuIndices; }

    uint32_t GetIndexCount()  const { return m_indexCount; }
    uint32_t GetVertexCount() const { return (uint32_t)m_cpuVertices.size(); }

    void SetMaterial(const MaterialData& mat) { m_materialData = mat; }
    const MaterialData& GetMaterial() const { return m_materialData; }

private:
    Mesh() {}
    bool Init(ID3D11Device* device,
              const std::vector<Vertex>& vertices,
              const std::vector<uint32_t>& indices);

    BufferUPtr m_vertexBuffer;
    BufferUPtr m_indexBuffer;
    ComPtr<ID3D11ShaderResourceView> m_vertexSRV;
    ComPtr<ID3D11ShaderResourceView> m_indexSRV;

    // CPU 사본 (통합 버퍼 빌드용)
    std::vector<Vertex>   m_cpuVertices;
    std::vector<uint32_t> m_cpuIndices;

    MaterialData m_materialData;
    uint32_t     m_indexCount { 0 };
};
#endif