#include "model.h"

ModelUPtr Model::Load(
    ID3D11Device* device,
    const std::string& filepath)
{
    auto model = ModelUPtr(new Model());
    if (!model->LoadModel(device, filepath))
        return nullptr;
    return std::move(model);
}

bool Model::LoadModel(
    ID3D11Device* device,
    const std::string& filepath)
{
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(filepath,
        aiProcess_Triangulate       |
        aiProcess_CalcTangentSpace  |
        aiProcess_FlipUVs           |
        aiProcess_GenSmoothNormals  | // GenNormals → GenSmoothNormals (스무스 법선)
        aiProcess_MakeLeftHanded    |
        aiProcess_FlipWindingOrder
    );

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        SPDLOG_ERROR("Assimp error: {}", importer.GetErrorString());
        return false;
    }

    m_directory = filepath.substr(0, filepath.find_last_of('/'));
    ProcessNode(device, scene->mRootNode, scene);
    return true;
}

void Model::ProcessNode(
    ID3D11Device* device,
    aiNode* node,
    const aiScene* scene)
{
    for (uint32_t i = 0; i < node->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        auto newMesh = ProcessMesh(device, mesh, scene);
        if (newMesh)
            m_meshes.push_back(std::move(newMesh));
    }
    for (uint32_t i = 0; i < node->mNumChildren; i++) {
        ProcessNode(device, node->mChildren[i], scene);
    }
}

MeshUPtr Model::ProcessMesh(
    ID3D11Device* device,
    aiMesh* mesh,
    const aiScene* scene)
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(mesh->mNumVertices);

    // -------------------------------------------------------
    // 1. 버텍스 추출
    // -------------------------------------------------------
    for (uint32_t i = 0; i < mesh->mNumVertices; i++) {
        Vertex v;

        // Position (필수)
        v.position = glm::vec3(
            mesh->mVertices[i].x,
            mesh->mVertices[i].y,
            mesh->mVertices[i].z
        );

        // Normal
        if (mesh->HasNormals()) {
            v.normal = glm::vec3(
                mesh->mNormals[i].x,
                mesh->mNormals[i].y,
                mesh->mNormals[i].z
            );
        } else {
            v.normal = glm::vec3(0.0f, 1.0f, 0.0f); // 폴백
        }

        // UV (첫 번째 채널만 사용)
        if (mesh->mTextureCoords[0]) {
            v.texCoord = glm::vec2(
                mesh->mTextureCoords[0][i].x,
                mesh->mTextureCoords[0][i].y
            );
        } else {
            v.texCoord = glm::vec2(0.0f, 0.0f);
        }

        // Tangent (aiProcess_CalcTangentSpace로 자동 계산됨)
        if (mesh->HasTangentsAndBitangents()) {
            v.tangent = glm::vec3(
                mesh->mTangents[i].x,
                mesh->mTangents[i].y,
                mesh->mTangents[i].z
            );
        } else {
            v.tangent = glm::vec3(1.0f, 0.0f, 0.0f); // 폴백
        }

        vertices.push_back(v);
    }

    // -------------------------------------------------------
    // 2. 인덱스 추출
    // -------------------------------------------------------
    indices.reserve(mesh->mNumFaces * 3);
    for (uint32_t i = 0; i < mesh->mNumFaces; i++) {
        const aiFace& face = mesh->mFaces[i];
        // aiProcess_Triangulate 덕분에 항상 3개
        for (uint32_t j = 0; j < face.mNumIndices; j++) {
            indices.push_back(face.mIndices[j]);
        }
    }

    if (vertices.empty() || indices.empty()) {
        SPDLOG_WARN("Mesh has no geometry, skipping.");
        return nullptr;
    }

    // -------------------------------------------------------
    // 3. Mesh 생성
    // -------------------------------------------------------
    auto newMesh = Mesh::Create(device, vertices, indices);
    if (!newMesh) {
        SPDLOG_ERROR("Failed to create mesh from assimp data.");
        return nullptr;
    }

    // -------------------------------------------------------
    // 4. 재질 설정
    // -------------------------------------------------------
    if (mesh->mMaterialIndex >= 0) {
        aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
        MaterialData matData = {};

        // Albedo
        aiColor3D color(1.f, 1.f, 1.f);
        if (AI_SUCCESS == material->Get(AI_MATKEY_COLOR_DIFFUSE, color)) {
            matData.albedo = glm::vec3(color.r, color.g, color.b);
        } else {
            matData.albedo = glm::vec3(0.8f);
        }

        // Roughness (Shininess → Roughness 변환)
        // Blinn-Phong 지수 n과 GGX roughness α의 관계: α ≈ sqrt(2/(n+2))
        // 선형 매핑보다 물리적으로 정확한 perceptual 변환
        float shininess = 32.0f;
        material->Get(AI_MATKEY_SHININESS, shininess);
        shininess         = glm::max(shininess, 0.0f);
        matData.roughness = glm::clamp(sqrtf(2.0f / (shininess + 2.0f)), 0.05f, 1.0f);

        // Metallic (PBR 확장 속성 시도, 없으면 0)
        float metallic = 0.0f;
        material->Get(AI_MATKEY_METALLIC_FACTOR, metallic);
        matData.metallic = glm::clamp(metallic, 0.0f, 1.0f);

        // Emissive
        aiColor3D emissive(0.f, 0.f, 0.f);
        material->Get(AI_MATKEY_COLOR_EMISSIVE, emissive);
        matData.emissive = glm::vec3(emissive.r, emissive.g, emissive.b);

        newMesh->SetMaterial(matData);
    }

    return newMesh;
}