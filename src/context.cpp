#include "context.h"
#include <spdlog/spdlog.h>

// -------------------------------------------------------
// 지오메트리 생성 헬퍼 (파일 내부 전용)
// -------------------------------------------------------
namespace {

void AppendQuad(
    const glm::vec3& p0, const glm::vec3& p1,
    const glm::vec3& p2, const glm::vec3& p3,
    const glm::vec3& normal,
    std::vector<Vertex>& verts, std::vector<uint32_t>& inds)
{
    uint32_t b = (uint32_t)verts.size();
    glm::vec3 tang = (p1 - p0 == glm::vec3(0.f))
        ? glm::vec3(1,0,0)
        : glm::normalize(p1 - p0);
    auto push = [&](const glm::vec3& pos) {
        Vertex v{};
        v.position = pos;
        v.normal   = normal;
        v.tangent  = tang;
        verts.push_back(v);
    };
    push(p0); push(p1); push(p2); push(p3);
    inds.push_back(b);   inds.push_back(b+1); inds.push_back(b+2);
    inds.push_back(b);   inds.push_back(b+2); inds.push_back(b+3);
}

void AppendBox(
    const glm::vec3& lo, const glm::vec3& hi,
    std::vector<Vertex>& v, std::vector<uint32_t>& i)
{
    AppendQuad({lo.x,hi.y,lo.z},{hi.x,hi.y,lo.z},{hi.x,hi.y,hi.z},{lo.x,hi.y,hi.z},{0,1,0},v,i);
    AppendQuad({lo.x,lo.y,hi.z},{hi.x,lo.y,hi.z},{hi.x,lo.y,lo.z},{lo.x,lo.y,lo.z},{0,-1,0},v,i);
    AppendQuad({lo.x,lo.y,hi.z},{hi.x,lo.y,hi.z},{hi.x,hi.y,hi.z},{lo.x,hi.y,hi.z},{0,0,1},v,i);
    AppendQuad({hi.x,lo.y,lo.z},{lo.x,lo.y,lo.z},{lo.x,hi.y,lo.z},{hi.x,hi.y,lo.z},{0,0,-1},v,i);
    AppendQuad({hi.x,lo.y,hi.z},{hi.x,lo.y,lo.z},{hi.x,hi.y,lo.z},{hi.x,hi.y,hi.z},{1,0,0},v,i);
    AppendQuad({lo.x,lo.y,lo.z},{lo.x,lo.y,hi.z},{lo.x,hi.y,hi.z},{lo.x,hi.y,lo.z},{-1,0,0},v,i);
}

void FlattenScene(
    const SceneDesc&          desc,
    std::vector<Vertex>&      verts,
    std::vector<uint32_t>&    inds,
    std::vector<GpuMeshInfo>& meshInfos,
    std::vector<GpuMaterial>& materials)
{
    for (const auto& b : desc.boxes) {
        GpuMeshInfo info{};
        info.vertexOffset  = (uint32_t)verts.size();
        info.indexOffset   = (uint32_t)inds.size();
        info.materialIndex = (uint32_t)materials.size();
        AppendBox(b.lo, b.hi, verts, inds);
        info.indexCount = (uint32_t)inds.size() - info.indexOffset;
        meshInfos.push_back(info);
        materials.push_back(b.mat);
    }

    for (const auto& q : desc.quads) {
        GpuMeshInfo info{};
        info.vertexOffset  = (uint32_t)verts.size();
        info.indexOffset   = (uint32_t)inds.size();
        info.materialIndex = (uint32_t)materials.size();
        AppendQuad(q.p0, q.p1, q.p2, q.p3, q.n, verts, inds);
        info.indexCount = (uint32_t)inds.size() - info.indexOffset;
        meshInfos.push_back(info);
        materials.push_back(q.mat);
    }
}

} // namespace

ContextUPtr Context::Create(ID3D11Device *device, ID3D11DeviceContext *ctx) {
    auto context = ContextUPtr(new Context());
    if (!context->Init(device, ctx))
        return nullptr;
    return std::move(context);
}

bool Context::Init(ID3D11Device *device, ID3D11DeviceContext *context) {
    // 1. 패스 트레이서 컴퓨트 셰이더 로드
    auto cs = Shader::CreateFromFile("shader/PathTracer.hlsl", "CSMain", "cs_5_0");
    if (!cs) return false;
    m_pathTracerProgram = ComputeProgram::Create(device, std::move(cs));
    if (!m_pathTracerProgram) return false;

    // 2. Composite 컴퓨트 셰이더 로드
    auto compCs = Shader::CreateFromFile("shader/Composite.hlsl", "CSMain", "cs_5_0");
    if (!compCs) return false;
    m_compositeProgram = ComputeProgram::Create(device, std::move(compCs));
    if (!m_compositeProgram) return false;

    // 3. 톤맵 컴퓨트 셰이더 로드
    auto tmCs = Shader::CreateFromFile("shader/Tonemap.hlsl", "CSMain", "cs_5_0");
    if (!tmCs) return false;
    m_toneMapProgram = ComputeProgram::Create(device, std::move(tmCs));
    if (!m_toneMapProgram) return false;

    // 4. 글로벌 상수 버퍼 (PathTracer용)
    m_globalBuffer = Buffer::CreateWithData(
        device, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC,
        nullptr, sizeof(GlobalUniforms), 1);
    if (!m_globalBuffer) return false;

    // 5. 톤맵 상수 버퍼
    m_toneMapBuffer = Buffer::CreateWithData(
        device, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC,
        nullptr, sizeof(ToneMapUniforms), 1);
    if (!m_toneMapBuffer) return false;

    // 6. 모델 (절차적 씬 사용)
    m_model = nullptr;

    // 7. 씬 버퍼 빌드
    if (!BuildSceneBuffers(device)) return false;

    // 8. 출력 텍스처 생성
    OnResize(device, WINDOW_WIDTH, WINDOW_HEIGHT);
    return true;
}

bool Context::BuildSceneBuffers(ID3D11Device *device) {
    std::vector<Vertex> allVertices;
    std::vector<uint32_t> allIndices;
    std::vector<GpuMeshInfo> meshInfos;
    std::vector<GpuMaterial> materials;

    if (m_model) {
        size_t totalVerts = 0, totalInds = 0;
        for (const auto& m : m_model->GetMeshes()) {
            totalVerts += m->GetVertexCount();
            totalInds  += m->GetIndexCount();
        }
        allVertices.reserve(totalVerts);
        allIndices.reserve(totalInds);
        meshInfos.reserve(m_model->GetMeshes().size());
        materials.reserve(m_model->GetMeshes().size());

        for (const auto &mesh : m_model->GetMeshes()) {
            GpuMeshInfo info;
            info.vertexOffset  = (uint32_t)allVertices.size();
            info.indexOffset   = (uint32_t)allIndices.size();
            info.indexCount    = mesh->GetIndexCount();
            info.materialIndex = (uint32_t)materials.size();
            meshInfos.push_back(info);

            const auto &mat = mesh->GetMaterial();
            GpuMaterial gpuMat;
            gpuMat.albedo    = mat.albedo;
            gpuMat.roughness = mat.roughness;
            gpuMat.emissive  = mat.emissive;
            gpuMat.metallic  = mat.metallic;
            materials.push_back(gpuMat);

            uint32_t vOffset = info.vertexOffset;
            const auto& verts = mesh->GetVertices();
            allVertices.insert(allVertices.end(), verts.begin(), verts.end());

            for (uint32_t idx : mesh->GetIndices())
                allIndices.push_back(vOffset + idx);
        }
    }

    SceneDesc desc;
    if (meshInfos.empty()) {
        SPDLOG_INFO("No model found. Building procedural city scene.");
        desc = MakeCityScene();
        FlattenScene(desc, allVertices, allIndices, meshInfos, materials);
    }

    m_meshCount = (uint32_t)meshInfos.size();

    // BVH 빌드
    {
        uint32_t totalTris = (uint32_t)(allIndices.size() / 3);
        std::vector<uint32_t> triMesh(totalTris);
        uint32_t globalTri = 0;
        for (uint32_t mi = 0; mi < (uint32_t)meshInfos.size(); ++mi) {
            uint32_t triCount = meshInfos[mi].indexCount / 3;
            for (uint32_t t = 0; t < triCount; ++t)
                triMesh[globalTri++] = mi;
        }

        Bvh bvh;
        bvh.Build(allVertices, allIndices, triMesh);

        m_bvhNodeBuffer = Buffer::CreateWithData(
            device, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT,
            bvh.Nodes().data(), sizeof(BvhNode), (uint32_t)bvh.Nodes().size(),
            D3D11_RESOURCE_MISC_BUFFER_STRUCTURED);
        if (!m_bvhNodeBuffer) return false;
        m_bvhNodeSRV = m_bvhNodeBuffer->CreateSRV(device);

        m_bvhPrimBuffer = Buffer::CreateWithData(
            device, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT,
            bvh.Prims().data(), sizeof(BvhPrim), (uint32_t)bvh.Prims().size(),
            D3D11_RESOURCE_MISC_BUFFER_STRUCTURED);
        if (!m_bvhPrimBuffer) return false;
        m_bvhPrimSRV = m_bvhPrimBuffer->CreateSRV(device);

        SPDLOG_INFO("BVH built: {} nodes, {} prims", bvh.Nodes().size(), bvh.Prims().size());
    }

    // GPU 버퍼 생성
    m_vertexBuffer = Buffer::CreateWithData(
        device, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT,
        allVertices.data(), sizeof(Vertex), (uint32_t)allVertices.size(),
        D3D11_RESOURCE_MISC_BUFFER_STRUCTURED);
    if (!m_vertexBuffer) return false;
    m_vertexSRV = m_vertexBuffer->CreateSRV(device);

    m_indexBuffer = Buffer::CreateWithData(
        device, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT,
        allIndices.data(), sizeof(uint32_t), (uint32_t)allIndices.size(),
        D3D11_RESOURCE_MISC_BUFFER_STRUCTURED);
    if (!m_indexBuffer) return false;
    m_indexSRV = m_indexBuffer->CreateSRV(device);

    m_meshInfoBuffer = Buffer::CreateWithData(
        device, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT,
        meshInfos.data(), sizeof(GpuMeshInfo), (uint32_t)meshInfos.size(),
        D3D11_RESOURCE_MISC_BUFFER_STRUCTURED);
    if (!m_meshInfoBuffer) return false;
    m_meshInfoSRV = m_meshInfoBuffer->CreateSRV(device);

    m_materialBuffer = Buffer::CreateWithData(
        device, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT,
        materials.data(), sizeof(GpuMaterial), (uint32_t)materials.size(),
        D3D11_RESOURCE_MISC_BUFFER_STRUCTURED);
    if (!m_materialBuffer) return false;
    m_materialSRV = m_materialBuffer->CreateSRV(device);

    // 광원 버퍼 (t6)
    m_lightCount = (uint32_t)desc.lights.size();
    if (m_lightCount > 0) {
        m_lightBuffer = Buffer::CreateWithData(
            device, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT,
            desc.lights.data(), sizeof(LightDesc), m_lightCount,
            D3D11_RESOURCE_MISC_BUFFER_STRUCTURED);
        if (!m_lightBuffer) return false;
        m_lightSRV = m_lightBuffer->CreateSRV(device);
    }

    SPDLOG_INFO("Scene built: {} meshes, {} vertices, {} indices, {} lights",
                m_meshCount, allVertices.size(), allIndices.size(), m_lightCount);
    return true;
}

void Context::OnResize(ID3D11Device *device, uint32_t width, uint32_t height) {
    m_outputUAV.Reset();
    m_outputSRV.Reset();
    m_outputTexture.Reset();
    m_compositeUAV.Reset();
    m_compositeSRV.Reset();
    m_compositeTexture.Reset();
    m_diffuseRadianceUAV.Reset();
    m_diffuseRadianceSRV.Reset();
    m_diffuseRadianceTexture.Reset();
    m_specularRadianceUAV.Reset();
    m_specularRadianceSRV.Reset();
    m_specularRadianceTexture.Reset();
    m_viewZUAV.Reset();
    m_viewZSRV.Reset();
    m_viewZTexture.Reset();
    m_normalRoughnessUAV.Reset();
    m_normalRoughnessSRV.Reset();
    m_normalRoughnessTexture.Reset();
    m_motionVectorUAV.Reset();
    m_motionVectorSRV.Reset();
    m_motionVectorTexture.Reset();
    m_baseColorMetalnessUAV.Reset();
    m_baseColorMetalnessSRV.Reset();
    m_baseColorMetalnessTexture.Reset();
    m_emissiveUAV.Reset();
    m_emissiveSRV.Reset();
    m_emissiveTexture.Reset();

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width            = width;
    desc.Height           = height;
    desc.MipLevels        = 1;
    desc.ArraySize        = 1;
    desc.SampleDesc.Count = 1;
    desc.Usage            = D3D11_USAGE_DEFAULT;
    desc.BindFlags        = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;

    auto createScreenTexture = [&](DXGI_FORMAT format,
                                   ComPtr<ID3D11Texture2D>& texture,
                                   ComPtr<ID3D11UnorderedAccessView>& uav,
                                   ComPtr<ID3D11ShaderResourceView>& srv,
                                   const char* debugName) -> bool {
        desc.Format = format;

        HRESULT hr = device->CreateTexture2D(&desc, nullptr, texture.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            SPDLOG_ERROR("Failed to create {} texture. HRESULT: 0x{:08x}", debugName, (uint32_t)hr);
            return false;
        }

        hr = device->CreateUnorderedAccessView(texture.Get(), nullptr, uav.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            SPDLOG_ERROR("Failed to create {} UAV. HRESULT: 0x{:08x}", debugName, (uint32_t)hr);
            return false;
        }

        hr = device->CreateShaderResourceView(texture.Get(), nullptr, srv.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            SPDLOG_ERROR("Failed to create {} SRV. HRESULT: 0x{:08x}", debugName, (uint32_t)hr);
            return false;
        }

        return true;
    };

    // LDR 출력 텍스처
    if (!createScreenTexture(
            DXGI_FORMAT_R8G8B8A8_UNORM,
            m_outputTexture, m_outputUAV, m_outputSRV, "output")) {
        return;
    }

    if (!createScreenTexture(
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            m_diffuseRadianceTexture, m_diffuseRadianceUAV, m_diffuseRadianceSRV, "diffuse radiance")) {
        return;
    }

    if (!createScreenTexture(
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            m_specularRadianceTexture, m_specularRadianceUAV, m_specularRadianceSRV, "specular radiance")) {
        return;
    }

    if (!createScreenTexture(
            DXGI_FORMAT_R32_FLOAT,
            m_viewZTexture, m_viewZUAV, m_viewZSRV, "viewZ")) {
        return;
    }

    if (!createScreenTexture(
            DXGI_FORMAT_R10G10B10A2_UNORM,
            m_normalRoughnessTexture, m_normalRoughnessUAV, m_normalRoughnessSRV, "normal roughness")) {
        return;
    }

    if (!createScreenTexture(
            DXGI_FORMAT_R16G16_FLOAT,
            m_motionVectorTexture, m_motionVectorUAV, m_motionVectorSRV, "motion vector")) {
        return;
    }

    if (!createScreenTexture(
            DXGI_FORMAT_R8G8B8A8_UNORM,
            m_baseColorMetalnessTexture, m_baseColorMetalnessUAV, m_baseColorMetalnessSRV, "baseColor metalness")) {
        return;
    }

    if (!createScreenTexture(
            DXGI_FORMAT_R11G11B10_FLOAT,
            m_emissiveTexture, m_emissiveUAV, m_emissiveSRV, "emissive")) {
        return;
    }

    if (!createScreenTexture(
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            m_compositeTexture, m_compositeUAV, m_compositeSRV, "composite")) {
        return;
    }

    m_frameCount = 0;
}

void Context::Render(ID3D11DeviceContext *context, uint32_t width, uint32_t height) {
    // -------------------------------------------------------
    // 카메라 벡터 계산
    // -------------------------------------------------------
    glm::vec3 front;
    front.x = cos(glm::radians(m_pitch)) * cos(glm::radians(m_yaw));
    front.y = sin(glm::radians(m_pitch));
    front.z = cos(glm::radians(m_pitch)) * sin(glm::radians(m_yaw));
    m_cameraFront = glm::normalize(front);
    glm::vec3 right = glm::normalize(glm::cross(m_cameraFront, m_cameraUp));
    glm::mat4 view = glm::lookAtRH(m_cameraPos, m_cameraPos + m_cameraFront, m_cameraUp);
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), (float)width / (float)height, 0.1f, 1000.0f);
    glm::mat4 currViewProj = proj * view;
    glm::mat4 prevViewProj = (m_frameCount == 0) ? currViewProj : m_prevViewProj;

    // -------------------------------------------------------
    // 패스 1: PathTracer — G-buffer 7종 생성
    // -------------------------------------------------------
    {
        GlobalUniforms globalData;
        globalData.cameraPos   = m_cameraPos;
        globalData.fov         = glm::radians(45.0f);
        globalData.cameraFront = m_cameraFront;
        globalData.aspectRatio = (float)width / (float)height;
        globalData.cameraUp    = m_cameraUp;
        globalData.frameCount  = (float)m_frameCount;
        globalData.cameraRight = right;
        globalData.lightCount  = m_lightCount;
        globalData.prevViewProj = glm::transpose(prevViewProj);
        globalData.currViewProj = glm::transpose(currViewProj);
        m_globalBuffer->UpdateData(context, globalData);

        // b0: PathTracer 상수 버퍼
        auto gBuf = m_globalBuffer->GetBuffer();
        context->CSSetConstantBuffers(0, 1, &gBuf);

        // t0~t6: 씬 데이터 SRV
        ID3D11ShaderResourceView *srvs[7] = {
            m_vertexSRV.Get(),
            m_indexSRV.Get(),
            m_meshInfoSRV.Get(),
            m_materialSRV.Get(),
            m_bvhNodeSRV.Get(),
            m_bvhPrimSRV.Get(),
            m_lightSRV.Get(),
        };
        context->CSSetShaderResources(0, 7, srvs);

        ID3D11UnorderedAccessView *uavs[7] = {
            m_diffuseRadianceUAV.Get(),
            m_specularRadianceUAV.Get(),
            m_viewZUAV.Get(),
            m_normalRoughnessUAV.Get(),
            m_motionVectorUAV.Get(),
            m_baseColorMetalnessUAV.Get(),
            m_emissiveUAV.Get(),
        };
        context->CSSetUnorderedAccessViews(0, 7, uavs, nullptr);

        // Dispatch (PathTracer: 16x16 스레드 그룹)
        uint32_t gx = (width  + 15) / 16;
        uint32_t gy = (height + 15) / 16;
        m_pathTracerProgram->Dispatch(context, gx, gy, 1);

        // PathTracer 리소스 해제
        ID3D11UnorderedAccessView *nullUAV[7] = {};
        context->CSSetUnorderedAccessViews(0, 7, nullUAV, nullptr);
        ID3D11ShaderResourceView *nullSRVs[7] = {};
        context->CSSetShaderResources(0, 7, nullSRVs);
    }

    // -------------------------------------------------------
    // 패스 2: Composite — diffuse * albedo + specular + emissive
    // -------------------------------------------------------
    {
        ID3D11ShaderResourceView *compSRVs[4] = {
            m_diffuseRadianceSRV.Get(),
            m_specularRadianceSRV.Get(),
            m_baseColorMetalnessSRV.Get(),
            m_emissiveSRV.Get(),
        };
        context->CSSetShaderResources(0, 4, compSRVs);

        ID3D11UnorderedAccessView *compUAVs[1] = { m_compositeUAV.Get() };
        context->CSSetUnorderedAccessViews(0, 1, compUAVs, nullptr);

        uint32_t gx = (width  + 7) / 8;
        uint32_t gy = (height + 7) / 8;
        m_compositeProgram->Dispatch(context, gx, gy, 1);

        ID3D11UnorderedAccessView *nullUAV[1] = { nullptr };
        context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
        ID3D11ShaderResourceView *nullSRVs[4] = {};
        context->CSSetShaderResources(0, 4, nullSRVs);
    }

    // -------------------------------------------------------
    // 패스 3: ToneMap — Composite HDR → LDR 출력
    // -------------------------------------------------------
    {
        // b0: ToneMap 상수 버퍼
        ToneMapUniforms tmData;
        tmData.frameCount = m_frameCount;
        tmData.pad[0] = tmData.pad[1] = tmData.pad[2] = 0;
        m_toneMapBuffer->UpdateData(context, tmData);

        auto tmBuf = m_toneMapBuffer->GetBuffer();
        context->CSSetConstantBuffers(0, 1, &tmBuf);

        // t10: Composite 결과를 SRV로 읽기
        ID3D11ShaderResourceView *tmSRVs[1] = { m_compositeSRV.Get() };
        context->CSSetShaderResources(10, 1, tmSRVs);

        // u1: LDR 출력 UAV
        ID3D11UnorderedAccessView *tmUAVs[2] = { nullptr, m_outputUAV.Get() };
        context->CSSetUnorderedAccessViews(0, 2, tmUAVs, nullptr);

        // Dispatch (ToneMap: 8x8 스레드 그룹)
        uint32_t gx = (width  + 7) / 8;
        uint32_t gy = (height + 7) / 8;
        m_toneMapProgram->Dispatch(context, gx, gy, 1);

        // ToneMap 리소스 해제
        ID3D11UnorderedAccessView *nullUAV[2] = { nullptr, nullptr };
        context->CSSetUnorderedAccessViews(0, 2, nullUAV, nullptr);
        ID3D11ShaderResourceView *nullSRV[1] = { nullptr };
        context->CSSetShaderResources(10, 1, nullSRV);
    }

    m_prevViewProj = currViewProj;

    // 프레임 카운터 증가
    m_frameCount++;
}

void Context::Present(ID3D11DeviceContext *context, ID3D11RenderTargetView *rtv) {
    ID3D11Resource *backBufferRes = nullptr;
    rtv->GetResource(&backBufferRes);
    if (backBufferRes) {
        context->CopyResource(backBufferRes, m_outputTexture.Get());
        backBufferRes->Release();
    }
}

void Context::ProcessMouseMenu(float dx, float dy) {
    m_yaw   += dx * m_mouseSensitivity;
    m_pitch -= dy * m_mouseSensitivity;
    m_pitch  = glm::clamp(m_pitch, -89.0f, 89.0f);
    m_frameCount = 0;
}

void Context::ProcessKeyboard(float deltaTime) {
    glm::vec3 right = glm::normalize(glm::cross(m_cameraFront, m_cameraUp));

    float currentSpeed = m_cameraSpeed;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
        currentSpeed *= 5.0f;
    float speed = currentSpeed * deltaTime;

    bool moved = false;

    if (GetAsyncKeyState('W') & 0x8000) { m_cameraPos += m_cameraFront * speed; moved = true; }
    if (GetAsyncKeyState('S') & 0x8000) { m_cameraPos -= m_cameraFront * speed; moved = true; }
    if (GetAsyncKeyState('A') & 0x8000) { m_cameraPos -= right * speed;          moved = true; }
    if (GetAsyncKeyState('D') & 0x8000) { m_cameraPos += right * speed;          moved = true; }
    if (GetAsyncKeyState('E') & 0x8000) { m_cameraPos += m_cameraUp * speed;     moved = true; }
    if (GetAsyncKeyState('Q') & 0x8000) { m_cameraPos -= m_cameraUp * speed;     moved = true; }

    if (moved)
        m_frameCount = 0;
}
