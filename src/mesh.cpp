#include "mesh.h"

MeshUPtr Mesh::Create(
    ID3D11Device* device,
    const std::vector<Vertex>& vertices,
    const std::vector<uint32_t>& indices)
{
    auto mesh = MeshUPtr(new Mesh());
    if (!mesh->Init(device, vertices, indices)) return nullptr;
    return std::move(mesh);
}

bool Mesh::Init(
    ID3D11Device* device,
    const std::vector<Vertex>& vertices,
    const std::vector<uint32_t>& indices)
{
    std::vector<Vertex> finalVertices = vertices;
    ComputeTangents(finalVertices, indices);
    m_indexCount = (uint32_t)indices.size();

    // [추가] CPU 사본 저장 - BuildSceneBuffers에서 통합 버퍼 빌드 시 사용
    m_cpuVertices = finalVertices;
    m_cpuIndices  = indices;

    // GPU StructuredBuffer 생성
    m_vertexBuffer = Buffer::CreateWithData(
        device, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT,
        finalVertices.data(), sizeof(Vertex), (uint32_t)finalVertices.size(),
        D3D11_RESOURCE_MISC_BUFFER_STRUCTURED
    );
    if (!m_vertexBuffer) return false;

    m_indexBuffer = Buffer::CreateWithData(
        device, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT,
        indices.data(), sizeof(uint32_t), (uint32_t)indices.size(),
        D3D11_RESOURCE_MISC_BUFFER_STRUCTURED
    );
    if (!m_indexBuffer) return false;

    m_vertexSRV = m_vertexBuffer->CreateSRV(device);
    m_indexSRV  = m_indexBuffer->CreateSRV(device);

    return (m_vertexSRV && m_indexSRV);
}

MeshUPtr Mesh::CreatePlane(ID3D11Device* device) {
    std::vector<Vertex> vertices = {
        { glm::vec3(-5.0f, 0.0f, -5.0f), glm::vec3(0,1,0), glm::vec2(0,0), glm::vec3(0) },
        { glm::vec3( 5.0f, 0.0f, -5.0f), glm::vec3(0,1,0), glm::vec2(1,0), glm::vec3(0) },
        { glm::vec3( 5.0f, 0.0f,  5.0f), glm::vec3(0,1,0), glm::vec2(1,1), glm::vec3(0) },
        { glm::vec3(-5.0f, 0.0f,  5.0f), glm::vec3(0,1,0), glm::vec2(0,1), glm::vec3(0) },
    };
    std::vector<uint32_t> indices = { 0,1,2, 2,3,0 };
    return Create(device, vertices, indices);
}

MeshUPtr Mesh::CreateBox(ID3D11Device* device) {
    std::vector<Vertex> vertices = {
        // Front
        { glm::vec3(-0.5f,-0.5f, 0.5f), glm::vec3(0,0,1), glm::vec2(0,1), glm::vec3(0) },
        { glm::vec3( 0.5f,-0.5f, 0.5f), glm::vec3(0,0,1), glm::vec2(1,1), glm::vec3(0) },
        { glm::vec3( 0.5f, 0.5f, 0.5f), glm::vec3(0,0,1), glm::vec2(1,0), glm::vec3(0) },
        { glm::vec3(-0.5f, 0.5f, 0.5f), glm::vec3(0,0,1), glm::vec2(0,0), glm::vec3(0) },
        // Back
        { glm::vec3( 0.5f,-0.5f,-0.5f), glm::vec3(0,0,-1), glm::vec2(0,1), glm::vec3(0) },
        { glm::vec3(-0.5f,-0.5f,-0.5f), glm::vec3(0,0,-1), glm::vec2(1,1), glm::vec3(0) },
        { glm::vec3(-0.5f, 0.5f,-0.5f), glm::vec3(0,0,-1), glm::vec2(1,0), glm::vec3(0) },
        { glm::vec3( 0.5f, 0.5f,-0.5f), glm::vec3(0,0,-1), glm::vec2(0,0), glm::vec3(0) },
        // Left
        { glm::vec3(-0.5f,-0.5f,-0.5f), glm::vec3(-1,0,0), glm::vec2(0,1), glm::vec3(0) },
        { glm::vec3(-0.5f,-0.5f, 0.5f), glm::vec3(-1,0,0), glm::vec2(1,1), glm::vec3(0) },
        { glm::vec3(-0.5f, 0.5f, 0.5f), glm::vec3(-1,0,0), glm::vec2(1,0), glm::vec3(0) },
        { glm::vec3(-0.5f, 0.5f,-0.5f), glm::vec3(-1,0,0), glm::vec2(0,0), glm::vec3(0) },
        // Right
        { glm::vec3( 0.5f,-0.5f, 0.5f), glm::vec3(1,0,0), glm::vec2(0,1), glm::vec3(0) },
        { glm::vec3( 0.5f,-0.5f,-0.5f), glm::vec3(1,0,0), glm::vec2(1,1), glm::vec3(0) },
        { glm::vec3( 0.5f, 0.5f,-0.5f), glm::vec3(1,0,0), glm::vec2(1,0), glm::vec3(0) },
        { glm::vec3( 0.5f, 0.5f, 0.5f), glm::vec3(1,0,0), glm::vec2(0,0), glm::vec3(0) },
        // Top
        { glm::vec3(-0.5f, 0.5f, 0.5f), glm::vec3(0,1,0), glm::vec2(0,1), glm::vec3(0) },
        { glm::vec3( 0.5f, 0.5f, 0.5f), glm::vec3(0,1,0), glm::vec2(1,1), glm::vec3(0) },
        { glm::vec3( 0.5f, 0.5f,-0.5f), glm::vec3(0,1,0), glm::vec2(1,0), glm::vec3(0) },
        { glm::vec3(-0.5f, 0.5f,-0.5f), glm::vec3(0,1,0), glm::vec2(0,0), glm::vec3(0) },
        // Bottom
        { glm::vec3(-0.5f,-0.5f,-0.5f), glm::vec3(0,-1,0), glm::vec2(0,1), glm::vec3(0) },
        { glm::vec3( 0.5f,-0.5f,-0.5f), glm::vec3(0,-1,0), glm::vec2(1,1), glm::vec3(0) },
        { glm::vec3( 0.5f,-0.5f, 0.5f), glm::vec3(0,-1,0), glm::vec2(1,0), glm::vec3(0) },
        { glm::vec3(-0.5f,-0.5f, 0.5f), glm::vec3(0,-1,0), glm::vec2(0,0), glm::vec3(0) },
    };
    std::vector<uint32_t> indices;
    for (uint32_t f = 0; f < 6; ++f) {
        uint32_t b = f * 4;
        indices.insert(indices.end(), { b,b+1,b+2, b+2,b+3,b });
    }
    return Create(device, vertices, indices);
}

MeshUPtr Mesh::CreateSphere(ID3D11Device* device, uint32_t lati, uint32_t longi) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    for (uint32_t i = 0; i <= lati; ++i) {
        float phi = glm::pi<float>() * i / lati;
        for (uint32_t j = 0; j <= longi; ++j) {
            float theta = 2.0f * glm::pi<float>() * j / longi;
            Vertex v;
            v.position = glm::vec3(sinf(phi)*cosf(theta), cosf(phi), sinf(phi)*sinf(theta));
            v.normal   = v.position;
            v.texCoord = glm::vec2((float)j/longi, (float)i/lati);
            v.tangent  = glm::vec3(0.0f);
            vertices.push_back(v);
        }
    }
    for (uint32_t i = 0; i < lati; ++i) {
        for (uint32_t j = 0; j < longi; ++j) {
            uint32_t a = i*(longi+1)+j, b = a+longi+1;
            indices.insert(indices.end(), { a,b,a+1, b,b+1,a+1 });
        }
    }
    return Create(device, vertices, indices);
}

void Mesh::ComputeTangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    std::vector<glm::vec3> tangents(vertices.size(), glm::vec3(0.0f));
    for (size_t i = 0; i < indices.size(); i += 3) {
        uint32_t i1=indices[i], i2=indices[i+1], i3=indices[i+2];
        glm::vec3 e1 = vertices[i2].position - vertices[i1].position;
        glm::vec3 e2 = vertices[i3].position - vertices[i1].position;
        glm::vec2 d1 = vertices[i2].texCoord - vertices[i1].texCoord;
        glm::vec2 d2 = vertices[i3].texCoord - vertices[i1].texCoord;
        float det = d1.x*d2.y - d2.x*d1.y;
        if (fabsf(det) < 1e-6f) continue;
        float f = 1.0f / det;
        glm::vec3 t = glm::vec3(
            f*(d2.y*e1.x - d1.y*e2.x),
            f*(d2.y*e1.y - d1.y*e2.y),
            f*(d2.y*e1.z - d1.y*e2.z)
        );
        tangents[i1] += t; tangents[i2] += t; tangents[i3] += t;
    }
    for (size_t i = 0; i < vertices.size(); i++) {
        float len = glm::length(tangents[i]);
        vertices[i].tangent = (len > 1e-6f) ? tangents[i]/len : glm::vec3(1,0,0);
    }
}