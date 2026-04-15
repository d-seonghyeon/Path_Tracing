#pragma once
#include "mesh.h"
#include <vector>

// GPU 레이아웃과 일치 (Scene.hlsli ShaderBvhNode)
struct BvhNode {
    glm::vec3 aabbMin;
    uint32_t  leftFirst; // internal: 왼쪽 자식 인덱스 / leaf: 첫 번째 prim 인덱스
    glm::vec3 aabbMax;
    uint32_t  primCount; // 0 = 내부 노드, >0 = 리프
};
static_assert(sizeof(BvhNode) == 32, "BvhNode must be 32 bytes");

// GPU 레이아웃과 일치 (Scene.hlsli ShaderBvhPrim)
struct BvhPrim {
    uint32_t triOffset; // g_indices 내 삼각형 시작 인덱스 (triIdx * 3)
    uint32_t meshIdx;   // g_materials 인덱스
    uint32_t pad0, pad1;
};
static_assert(sizeof(BvhPrim) == 16, "BvhPrim must be 16 bytes");

// CPU SAH-BVH 빌더
class Bvh {
public:
    // verts, inds: BuildSceneBuffers의 통합 버퍼와 동일
    // triMesh: 삼각형별 meshIdx (길이 = inds.size()/3)
    void Build(const std::vector<Vertex>&   verts,
               const std::vector<uint32_t>& inds,
               const std::vector<uint32_t>& triMesh);

    const std::vector<BvhNode>& Nodes() const { return m_nodes; }
    const std::vector<BvhPrim>& Prims() const { return m_prims; }

private:
    struct TriAux {
        glm::vec3 bmin, bmax, centroid;
    };

    uint32_t AllocNode();
    void     UpdateBounds(uint32_t ni);
    float    BestSplit(uint32_t ni, int& outAxis, float& outPos);
    void     Subdivide(uint32_t ni);

    std::vector<BvhNode> m_nodes;
    std::vector<BvhPrim> m_prims;
    std::vector<TriAux>  m_aux;
    uint32_t             m_nodeCnt{ 0 };
};
