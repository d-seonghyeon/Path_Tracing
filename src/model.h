#ifndef __MODEL_H__
#define __MODEL_H__
#include "common.h"
#include "mesh.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

CLASS_PTR(Model)
class Model {
public:
    static ModelUPtr Load(
        ID3D11Device* device,
        const std::string& filepath
    );

    // Draw 함수 제거: 패스 트레이싱은 Dispatch 호출로 처리됨
    const std::vector<MeshUPtr>& GetMeshes() const { return m_meshes; }

private:
    Model() {}

    bool LoadModel(
        ID3D11Device* device,
        const std::string& filepath
    );

    void ProcessNode(
        ID3D11Device* device,
        aiNode* node,
        const aiScene* scene
    );

    MeshUPtr ProcessMesh(
        ID3D11Device* device,
        aiMesh* mesh,
        const aiScene* scene
    );

    // [제거] LoadTexture: 패스 트레이싱에서 텍스처 SRV는 미사용
    // TexturePtr LoadTexture(ID3D11Device*, aiMaterial*, aiTextureType);

    std::string m_directory;
    std::vector<MeshUPtr> m_meshes;
};
#endif