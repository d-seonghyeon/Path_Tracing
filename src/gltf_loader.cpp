// tiny_gltf는 이 파일에서만 구현체 생성
// TINYGLTF_NO_STB_IMAGE: image.cpp에서 이미 STB_IMAGE_IMPLEMENTATION 정의됨
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf.h>

#include "gltf_loader.h"

// -------------------------------------------------------
// 파일 내부 헬퍼 (외부 노출 없음)
// -------------------------------------------------------
namespace {

// accessor → 타입 T 포인터 반환 (비-인터리브드 가정)
// byteStride가 있는 경우 SPDLOG_WARN 발행 후 tight packing으로 처리
template<typename T>
const T* AccessorPtr(const tinygltf::Model& model, int accessorIndex) {
    const auto& acc  = model.accessors[accessorIndex];
    const auto& view = model.bufferViews[acc.bufferView];
    const auto& buf  = model.buffers[view.buffer];

    if (view.byteStride != 0 && view.byteStride != sizeof(T)) {
        SPDLOG_WARN("Interleaved bufferView (stride={}) detected; "
                    "assuming tight packing — data may be incorrect.", view.byteStride);
    }

    return reinterpret_cast<const T*>(
        buf.data.data() + view.byteOffset + acc.byteOffset);
}

size_t AccessorCount(const tinygltf::Model& model, int accessorIndex) {
    return model.accessors[accessorIndex].count;
}

// -------------------------------------------------------
// 재질 추출: pbrMetallicRoughness → MaterialData
// -------------------------------------------------------
MaterialData ExtractMaterial(const tinygltf::Model& model, int matIndex) {
    MaterialData mat; // 기본값: albedo=0.8, roughness=0.5, metallic=0, emissive=0

    if (matIndex < 0 || matIndex >= static_cast<int>(model.materials.size()))
        return mat;

    const auto& src = model.materials[matIndex];
    const auto& pbr = src.pbrMetallicRoughness;

    mat.albedo = glm::vec3(
        static_cast<float>(pbr.baseColorFactor[0]),
        static_cast<float>(pbr.baseColorFactor[1]),
        static_cast<float>(pbr.baseColorFactor[2]));

    mat.metallic  = static_cast<float>(pbr.metallicFactor);
    mat.roughness = static_cast<float>(pbr.roughnessFactor);

    mat.emissive = glm::vec3(
        static_cast<float>(src.emissiveFactor[0]),
        static_cast<float>(src.emissiveFactor[1]),
        static_cast<float>(src.emissiveFactor[2]));

    return mat;
}

// -------------------------------------------------------
// 인덱스 추출: uint16 / uint32 → vector<uint32_t>
// -------------------------------------------------------
bool ExtractIndices(
    const tinygltf::Model& model, int accessorIndex,
    std::vector<uint32_t>& out)
{
    const auto& acc = model.accessors[accessorIndex];
    out.reserve(acc.count);

    switch (acc.componentType) {
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
        const uint16_t* data = AccessorPtr<uint16_t>(model, accessorIndex);
        for (size_t i = 0; i < acc.count; ++i)
            out.push_back(static_cast<uint32_t>(data[i]));
        break;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
        const uint32_t* data = AccessorPtr<uint32_t>(model, accessorIndex);
        for (size_t i = 0; i < acc.count; ++i)
            out.push_back(data[i]);
        break;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
        const uint8_t* data = AccessorPtr<uint8_t>(model, accessorIndex);
        for (size_t i = 0; i < acc.count; ++i)
            out.push_back(static_cast<uint32_t>(data[i]));
        break;
    }
    default:
        SPDLOG_ERROR("Unsupported index component type: {}", acc.componentType);
        return false;
    }
    return true;
}

// -------------------------------------------------------
// Primitive 처리: 어트리뷰트 합성 → Mesh 생성
// -------------------------------------------------------
MeshUPtr ProcessPrimitive(
    ID3D11Device* device,
    const tinygltf::Model& model,
    const tinygltf::Primitive& prim)
{
    // POSITION (필수)
    auto posIt = prim.attributes.find("POSITION");
    if (posIt == prim.attributes.end()) {
        SPDLOG_WARN("Primitive missing POSITION attribute, skipping.");
        return nullptr;
    }

    const size_t vertCount = AccessorCount(model, posIt->second);
    std::vector<Vertex> vertices(vertCount);

    // Position
    const float* pos = AccessorPtr<float>(model, posIt->second);
    for (size_t i = 0; i < vertCount; ++i)
        vertices[i].position = glm::vec3(pos[i*3], pos[i*3+1], pos[i*3+2]);

    // Normal
    auto normIt = prim.attributes.find("NORMAL");
    if (normIt != prim.attributes.end()) {
        const float* norm = AccessorPtr<float>(model, normIt->second);
        for (size_t i = 0; i < vertCount; ++i)
            vertices[i].normal = glm::vec3(norm[i*3], norm[i*3+1], norm[i*3+2]);
    } else {
        for (auto& v : vertices) v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
    }

    // TEXCOORD_0
    auto uvIt = prim.attributes.find("TEXCOORD_0");
    if (uvIt != prim.attributes.end()) {
        const float* uv = AccessorPtr<float>(model, uvIt->second);
        for (size_t i = 0; i < vertCount; ++i)
            vertices[i].texCoord = glm::vec2(uv[i*2], uv[i*2+1]);
    }

    // TANGENT (float4: xyz=tangent, w=handedness)
    auto tanIt = prim.attributes.find("TANGENT");
    if (tanIt != prim.attributes.end()) {
        const float* tang = AccessorPtr<float>(model, tanIt->second);
        for (size_t i = 0; i < vertCount; ++i)
            vertices[i].tangent = glm::vec3(tang[i*4], tang[i*4+1], tang[i*4+2]);
    }

    // 인덱스
    std::vector<uint32_t> indices;
    if (prim.indices >= 0) {
        if (!ExtractIndices(model, prim.indices, indices))
            return nullptr;
    } else {
        // 인덱스 없는 경우 순서형 생성
        indices.resize(vertCount);
        for (size_t i = 0; i < vertCount; ++i)
            indices[i] = static_cast<uint32_t>(i);
    }

    if (vertices.empty() || indices.empty()) {
        SPDLOG_WARN("Primitive has no geometry, skipping.");
        return nullptr;
    }

    auto mesh = Mesh::Create(device, vertices, indices);
    if (!mesh) {
        SPDLOG_ERROR("Failed to create Mesh from glTF primitive.");
        return nullptr;
    }

    mesh->SetMaterial(ExtractMaterial(model, prim.material));
    return mesh;
}

} // namespace

// -------------------------------------------------------
// GltfLoader 공개 인터페이스
// -------------------------------------------------------
GltfLoaderUPtr GltfLoader::Load(ID3D11Device* device, const std::string& filepath) {
    auto loader = GltfLoaderUPtr(new GltfLoader());
    if (!loader->LoadGltf(device, filepath))
        return nullptr;
    return std::move(loader);
}

bool GltfLoader::LoadGltf(ID3D11Device* device, const std::string& filepath) {
    tinygltf::TinyGLTF loader;
    tinygltf::Model    model;
    std::string        err, warn;

    // 확장자로 바이너리(.glb) / ASCII(.gltf) 구분
    const bool isBinary =
        filepath.size() >= 4 &&
        filepath.compare(filepath.size() - 4, 4, ".glb") == 0;

    const bool ok = isBinary
        ? loader.LoadBinaryFromFile(&model, &err, &warn, filepath)
        : loader.LoadASCIIFromFile(&model, &err, &warn, filepath);

    if (!warn.empty()) SPDLOG_WARN("glTF [{}]: {}", filepath, warn);
    if (!ok) {
        SPDLOG_ERROR("glTF load failed [{}]: {}", filepath, err);
        return false;
    }

    for (const auto& mesh : model.meshes) {
        for (const auto& prim : mesh.primitives) {
            // 삼각형 프리미티브만 처리
            if (prim.mode != TINYGLTF_MODE_TRIANGLES) {
                SPDLOG_WARN("Non-triangle primitive (mode={}) skipped.", prim.mode);
                continue;
            }
            auto m = ProcessPrimitive(device, model, prim);
            if (m) m_meshes.push_back(std::move(m));
        }
    }

    if (m_meshes.empty()) {
        SPDLOG_ERROR("No valid triangle primitives found in: {}", filepath);
        return false;
    }

    SPDLOG_INFO("glTF loaded: {} meshes from '{}'", m_meshes.size(), filepath);
    return true;
}
