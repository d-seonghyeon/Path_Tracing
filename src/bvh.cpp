#include "bvh.h"
#include <algorithm>
#include <cassert>

static constexpr uint32_t LEAF_MAX  = 4;
static constexpr int      SAH_BINS  = 16;

static float HalfArea(glm::vec3 mn, glm::vec3 mx) {
    glm::vec3 e = glm::max(mx - mn, glm::vec3(0.f));
    return e.x * e.y + e.y * e.z + e.z * e.x;
}

// -------------------------------------------------------
uint32_t Bvh::AllocNode() {
    assert(m_nodeCnt < m_nodes.size());
    return m_nodeCnt++;
}

void Bvh::UpdateBounds(uint32_t ni) {
    BvhNode& n = m_nodes[ni];
    glm::vec3 mn(1e30f), mx(-1e30f);
    for (uint32_t i = 0; i < n.primCount; ++i) {
        const TriAux& a = m_aux[n.leftFirst + i];
        mn = glm::min(mn, a.bmin);
        mx = glm::max(mx, a.bmax);
    }
    n.aabbMin = mn;
    n.aabbMax = mx;
}

float Bvh::BestSplit(uint32_t ni, int& outAxis, float& outPos) {
    const BvhNode& n = m_nodes[ni];
    float bestCost = 1e30f;
    outAxis = 0; outPos = 0.f;

    for (int axis = 0; axis < 3; ++axis) {
        float lo = 1e30f, hi = -1e30f;
        for (uint32_t i = 0; i < n.primCount; ++i) {
            float c = m_aux[n.leftFirst + i].centroid[axis];
            lo = std::min(lo, c);
            hi = std::max(hi, c);
        }
        if (hi - lo < 1e-6f) continue;

        float binSize = (hi - lo) / SAH_BINS;

        struct Bin { glm::vec3 bmin{1e30f}; glm::vec3 bmax{-1e30f}; uint32_t cnt{0}; };
        Bin bins[SAH_BINS];

        for (uint32_t i = 0; i < n.primCount; ++i) {
            const TriAux& a = m_aux[n.leftFirst + i];
            int b = std::clamp((int)((a.centroid[axis] - lo) / binSize), 0, SAH_BINS - 1);
            bins[b].bmin = glm::min(bins[b].bmin, a.bmin);
            bins[b].bmax = glm::max(bins[b].bmax, a.bmax);
            bins[b].cnt++;
        }

        float lArea[SAH_BINS - 1], rArea[SAH_BINS - 1];
        uint32_t lCnt[SAH_BINS - 1], rCnt[SAH_BINS - 1];
        glm::vec3 lmn(1e30f), lmx(-1e30f), rmn(1e30f), rmx(-1e30f);
        uint32_t lc = 0, rc = 0;

        for (int i = 0; i < SAH_BINS - 1; ++i) {
            lc += bins[i].cnt;
            lmn = glm::min(lmn, bins[i].bmin);
            lmx = glm::max(lmx, bins[i].bmax);
            lArea[i] = HalfArea(lmn, lmx);
            lCnt[i]  = lc;
        }
        for (int i = SAH_BINS - 2; i >= 0; --i) {
            rc += bins[i + 1].cnt;
            rmn = glm::min(rmn, bins[i + 1].bmin);
            rmx = glm::max(rmx, bins[i + 1].bmax);
            rArea[i] = HalfArea(rmn, rmx);
            rCnt[i]  = rc;
        }

        for (int i = 0; i < SAH_BINS - 1; ++i) {
            float cost = lCnt[i] * lArea[i] + rCnt[i] * rArea[i];
            if (cost < bestCost) {
                bestCost = cost;
                outAxis  = axis;
                outPos   = lo + binSize * (i + 1);
            }
        }
    }
    return bestCost;
}

void Bvh::Subdivide(uint32_t ni) {
    if (m_nodes[ni].primCount <= LEAF_MAX) return;

    int axis = 0; float pos = 0.f;
    BestSplit(ni, axis, pos);

    uint32_t start = m_nodes[ni].leftFirst;
    uint32_t end   = start + m_nodes[ni].primCount;
    uint32_t mid   = start;

    for (uint32_t i = start; i < end; ++i) {
        if (m_aux[i].centroid[axis] < pos) {
            std::swap(m_aux[i],   m_aux[mid]);
            std::swap(m_prims[i], m_prims[mid]);
            mid++;
        }
    }
    if (mid == start || mid == end) return; // 분할 불가 → 리프로 유지

    uint32_t left  = AllocNode();
    uint32_t right = AllocNode();

    m_nodes[ni].leftFirst = left;
    m_nodes[ni].primCount = 0; // 내부 노드로 변환

    m_nodes[left].leftFirst  = start;
    m_nodes[left].primCount  = mid - start;
    m_nodes[right].leftFirst = mid;
    m_nodes[right].primCount = end - mid;

    UpdateBounds(left);
    UpdateBounds(right);
    Subdivide(left);
    Subdivide(right);
}

// -------------------------------------------------------
void Bvh::Build(const std::vector<Vertex>&   verts,
                const std::vector<uint32_t>& inds,
                const std::vector<uint32_t>& triMesh)
{
    uint32_t totalTris = (uint32_t)(inds.size() / 3);
    m_prims.resize(totalTris);
    m_aux.resize(totalTris);

    for (uint32_t i = 0; i < totalTris; ++i) {
        glm::vec3 v0 = verts[inds[i * 3 + 0]].position;
        glm::vec3 v1 = verts[inds[i * 3 + 1]].position;
        glm::vec3 v2 = verts[inds[i * 3 + 2]].position;

        m_aux[i].bmin     = glm::min(glm::min(v0, v1), v2);
        m_aux[i].bmax     = glm::max(glm::max(v0, v1), v2);
        m_aux[i].centroid = (m_aux[i].bmin + m_aux[i].bmax) * 0.5f;

        m_prims[i].triOffset = i * 3;
        m_prims[i].meshIdx   = triMesh[i];
        m_prims[i].pad0 = m_prims[i].pad1 = 0;
    }

    // 최대 노드 수 = 2*N-1 (완전 이진트리)
    m_nodes.resize(2 * totalTris + 1);
    m_nodeCnt = 0;

    uint32_t root = AllocNode();
    m_nodes[root].leftFirst = 0;
    m_nodes[root].primCount = totalTris;
    UpdateBounds(root);
    Subdivide(root);

    m_nodes.resize(m_nodeCnt);
}
