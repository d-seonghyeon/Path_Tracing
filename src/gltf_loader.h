#ifndef __GLTF_LOADER_H__
#define __GLTF_LOADER_H__
#include "common.h"
#include "mesh.h"

// -------------------------------------------------------
// GltfLoader — tinygltf 기반 .gltf/.glb 로더
// Model::Load와 동일한 인터페이스 유지 (GetMeshes 호환)
// tiny_gltf.h는 구현 파일에서만 include (헤더 오염 방지)
// -------------------------------------------------------
CLASS_PTR(GltfLoader)
class GltfLoader {
public:
    // 기존 Model::Load와 동일한 시그니처
    static GltfLoaderUPtr Load(ID3D11Device* device, const std::string& filepath);

    const std::vector<MeshUPtr>& GetMeshes() const { return m_meshes; }

private:
    GltfLoader() {}
    bool LoadGltf(ID3D11Device* device, const std::string& filepath);

    std::vector<MeshUPtr> m_meshes;
};

#endif
