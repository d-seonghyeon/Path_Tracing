#include "context.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <spdlog/spdlog.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>
#include <fstream>
#include <cmath>

// -------------------------------------------------------
// 筌왖??살컭?紐꺿봺 ??밴쉐 ????(???뵬 ??? ?袁⑹뒠)
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
    m_device = device;

    // 1. ??λ뮞 ?紐껋쟿??곴퐣 ?뚮똾踰???怨쀬뵠??嚥≪뮆諭?
    auto cs = Shader::CreateFromFile("shader/PathTracer.hlsl", "CSMain", "cs_5_0");
    if (!cs) return false;
    m_pathTracerProgram = ComputeProgram::Create(device, std::move(cs));
    if (!m_pathTracerProgram) return false;

    // 2. Composite ?뚮똾踰???怨쀬뵠??嚥≪뮆諭?
    auto compCs = Shader::CreateFromFile("shader/Composite.hlsl", "CSMain", "cs_5_0");
    if (!compCs) return false;
    m_compositeProgram = ComputeProgram::Create(device, std::move(compCs));
    if (!m_compositeProgram) return false;

    // 3. ??삠룋 ?뚮똾踰???怨쀬뵠??嚥≪뮆諭?
    auto tmCs = Shader::CreateFromFile("shader/Tonemap.hlsl", "CSMain", "cs_5_0");
    if (!tmCs) return false;
    m_toneMapProgram = ComputeProgram::Create(device, std::move(tmCs));
    if (!m_toneMapProgram) return false;

    // 4. 疫꼲嚥≪뮆苡??怨몃땾 甕곌쑵??(PathTracer??
    m_globalBuffer = Buffer::CreateWithData(
        device, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC,
        nullptr, sizeof(GlobalUniforms), 1);
    if (!m_globalBuffer) return false;

    // 5. NrdDenoiser (Phase 2 ??NRD SDK 沃섎챷苑뺟㎉???stub??곗쨮 ??덉삂)
    m_nrdDenoiser = NrdDenoiser::Create(device, WINDOW_WIDTH, WINDOW_HEIGHT);
    if (!m_nrdDenoiser) return false;
    SPDLOG_INFO("NrdDenoiser backend status: {}", m_nrdDenoiser->GetBackendStatusLabel());

    // 6. 筌뤴뫀??(??됯컧????????
    m_model = nullptr;

    // 7. ??甕곌쑵????슢諭?
    if (!BuildSceneBuffers(device)) return false;

    // 8. ?곗뮆????용뮞筌???밴쉐
    OnResize(device, WINDOW_WIDTH, WINDOW_HEIGHT);

    // P5-3a: luminance histogram buffer (256 uint, log2 scale, UAV for PathTracer u7)
    {
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth           = 256 * sizeof(uint32_t);
        bd.Usage               = D3D11_USAGE_DEFAULT;
        bd.BindFlags           = D3D11_BIND_UNORDERED_ACCESS;
        bd.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = sizeof(uint32_t);
        HRESULT hr = device->CreateBuffer(&bd, nullptr, m_histogramBuffer.ReleaseAndGetAddressOf());
        if (FAILED(hr)) { SPDLOG_ERROR("Histogram buffer 0x{:08x}", (uint32_t)hr); return false; }

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavd = {};
        uavd.Format              = DXGI_FORMAT_UNKNOWN;
        uavd.ViewDimension       = D3D11_UAV_DIMENSION_BUFFER;
        uavd.Buffer.NumElements  = 256;
        hr = device->CreateUnorderedAccessView(m_histogramBuffer.Get(), &uavd, m_histogramUAV.ReleaseAndGetAddressOf());
        if (FAILED(hr)) { SPDLOG_ERROR("Histogram UAV 0x{:08x}", (uint32_t)hr); return false; }
    }

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

    // BVH ??슢諭?
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

    // GPU 甕곌쑵????밴쉐
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

    // ?용쵐??甕곌쑵??(t6)
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

    ComPtr<ID3D11Texture2D>           outputTexture;
    ComPtr<ID3D11UnorderedAccessView> outputUAV;
    ComPtr<ID3D11ShaderResourceView>  outputSRV;
    ComPtr<ID3D11Texture2D>           compositeTexture;
    ComPtr<ID3D11UnorderedAccessView> compositeUAV;
    ComPtr<ID3D11ShaderResourceView>  compositeSRV;
    ComPtr<ID3D11Texture2D>           diffuseRadianceTexture;
    ComPtr<ID3D11UnorderedAccessView> diffuseRadianceUAV;
    ComPtr<ID3D11ShaderResourceView>  diffuseRadianceSRV;
    ComPtr<ID3D11Texture2D>           specularRadianceTexture;
    ComPtr<ID3D11UnorderedAccessView> specularRadianceUAV;
    ComPtr<ID3D11ShaderResourceView>  specularRadianceSRV;
    ComPtr<ID3D11Texture2D>           viewZTexture;
    ComPtr<ID3D11UnorderedAccessView> viewZUAV;
    ComPtr<ID3D11ShaderResourceView>  viewZSRV;
    ComPtr<ID3D11Texture2D>           normalRoughnessTexture;
    ComPtr<ID3D11UnorderedAccessView> normalRoughnessUAV;
    ComPtr<ID3D11ShaderResourceView>  normalRoughnessSRV;
    ComPtr<ID3D11Texture2D>           motionVectorTexture;
    ComPtr<ID3D11UnorderedAccessView> motionVectorUAV;
    ComPtr<ID3D11ShaderResourceView>  motionVectorSRV;
    ComPtr<ID3D11Texture2D>           baseColorMetalnessTexture;
    ComPtr<ID3D11UnorderedAccessView> baseColorMetalnessUAV;
    ComPtr<ID3D11ShaderResourceView>  baseColorMetalnessSRV;
    ComPtr<ID3D11Texture2D>           emissiveTexture;
    ComPtr<ID3D11UnorderedAccessView> emissiveUAV;
    ComPtr<ID3D11ShaderResourceView>  emissiveSRV;
    ComPtr<ID3D11Texture2D>           denoisedDiffuseTexture;
    ComPtr<ID3D11UnorderedAccessView> denoisedDiffuseUAV;
    ComPtr<ID3D11ShaderResourceView>  denoisedDiffuseSRV;
    ComPtr<ID3D11Texture2D>           denoisedSpecularTexture;
    ComPtr<ID3D11UnorderedAccessView> denoisedSpecularUAV;
    ComPtr<ID3D11ShaderResourceView>  denoisedSpecularSRV;

    // Output + G-buffer textures are staged locally and swapped in only after success.
    if (!createScreenTexture(
            DXGI_FORMAT_R8G8B8A8_UNORM,
            outputTexture, outputUAV, outputSRV, "output")) {
        return;
    }

    if (!createScreenTexture(
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            diffuseRadianceTexture, diffuseRadianceUAV, diffuseRadianceSRV, "diffuse radiance")) {
        return;
    }

    if (!createScreenTexture(
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            specularRadianceTexture, specularRadianceUAV, specularRadianceSRV, "specular radiance")) {
        return;
    }

    if (!createScreenTexture(
            DXGI_FORMAT_R32_FLOAT,
            viewZTexture, viewZUAV, viewZSRV, "viewZ")) {
        return;
    }

    if (!createScreenTexture(
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            normalRoughnessTexture, normalRoughnessUAV, normalRoughnessSRV, "normal roughness")) {
        return;
    }

    if (!createScreenTexture(
            DXGI_FORMAT_R16G16_FLOAT,
            motionVectorTexture, motionVectorUAV, motionVectorSRV, "motion vector")) {
        return;
    }

    if (!createScreenTexture(
            DXGI_FORMAT_R8G8B8A8_UNORM,
            baseColorMetalnessTexture, baseColorMetalnessUAV, baseColorMetalnessSRV, "baseColor metalness")) {
        return;
    }

    if (!createScreenTexture(
            DXGI_FORMAT_R11G11B10_FLOAT,
            emissiveTexture, emissiveUAV, emissiveSRV, "emissive")) {
        return;
    }

    if (!createScreenTexture(
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            denoisedDiffuseTexture, denoisedDiffuseUAV, denoisedDiffuseSRV, "denoised diffuse")) {
        return;
    }

    if (!createScreenTexture(
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            denoisedSpecularTexture, denoisedSpecularUAV, denoisedSpecularSRV, "denoised specular")) {
        return;
    }

    if (!createScreenTexture(
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            compositeTexture, compositeUAV, compositeSRV, "composite")) {
        return;
    }

    if (m_nrdDenoiser && !m_nrdDenoiser->OnResize(device, width, height)) {
        SPDLOG_ERROR("Context::OnResize: failed to resize NRD backend. Keeping previous screen resources.");
        return;
    }

    m_outputTexture          = outputTexture;
    m_outputUAV              = outputUAV;
    m_outputSRV              = outputSRV;
    m_compositeTexture       = compositeTexture;
    m_compositeUAV           = compositeUAV;
    m_compositeSRV           = compositeSRV;
    m_diffuseRadianceTexture = diffuseRadianceTexture;
    m_diffuseRadianceUAV     = diffuseRadianceUAV;
    m_diffuseRadianceSRV     = diffuseRadianceSRV;
    m_specularRadianceTexture = specularRadianceTexture;
    m_specularRadianceUAV     = specularRadianceUAV;
    m_specularRadianceSRV     = specularRadianceSRV;
    m_viewZTexture           = viewZTexture;
    m_viewZUAV               = viewZUAV;
    m_viewZSRV               = viewZSRV;
    m_normalRoughnessTexture = normalRoughnessTexture;
    m_normalRoughnessUAV     = normalRoughnessUAV;
    m_normalRoughnessSRV     = normalRoughnessSRV;
    m_motionVectorTexture    = motionVectorTexture;
    m_motionVectorUAV        = motionVectorUAV;
    m_motionVectorSRV        = motionVectorSRV;
    m_baseColorMetalnessTexture = baseColorMetalnessTexture;
    m_baseColorMetalnessUAV     = baseColorMetalnessUAV;
    m_baseColorMetalnessSRV     = baseColorMetalnessSRV;
    m_emissiveTexture        = emissiveTexture;
    m_emissiveUAV            = emissiveUAV;
    m_emissiveSRV            = emissiveSRV;
    m_denoisedDiffuseTexture = denoisedDiffuseTexture;
    m_denoisedDiffuseUAV     = denoisedDiffuseUAV;
    m_denoisedDiffuseSRV     = denoisedDiffuseSRV;
    m_denoisedSpecularTexture = denoisedSpecularTexture;
    m_denoisedSpecularUAV     = denoisedSpecularUAV;
    m_denoisedSpecularSRV     = denoisedSpecularSRV;

    m_frameCount = 0;
}

void Context::Render(ID3D11DeviceContext *context, uint32_t width, uint32_t height) {
    // -------------------------------------------------------
    // 燁삳?李??甕겸돧苑??④쑴沅?
    // -------------------------------------------------------
    glm::vec3 front;
    front.x = cos(glm::radians(m_pitch)) * cos(glm::radians(m_yaw));
    front.y = sin(glm::radians(m_pitch));
    front.z = cos(glm::radians(m_pitch)) * sin(glm::radians(m_yaw));
    m_cameraFront = glm::normalize(front);
    glm::vec3 right = glm::normalize(glm::cross(m_cameraFront, m_cameraUp));
    glm::mat4 view = glm::lookAtRH(m_cameraPos, m_cameraPos + m_cameraFront, m_cameraUp);
    glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(45.0f), (float)width / (float)height, 0.1f, 500000.0f);
    glm::mat4 currViewProj = proj * view;
    glm::mat4 prevViewProj = (m_frameCount == 0) ? currViewProj : m_prevViewProj;

    // -------------------------------------------------------
    // ??λ뮞 1: PathTracer ??G-buffer 7????밴쉐
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

        // b0: PathTracer ?怨몃땾 甕곌쑵??
        auto gBuf = m_globalBuffer->GetBuffer();
        context->CSSetConstantBuffers(0, 1, &gBuf);

        // t0~t6: ???怨쀬뵠??SRV
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

        // P5-3a: clear histogram before PathTracer so each frame starts fresh
        UINT histClear[4] = {0, 0, 0, 0};
        context->ClearUnorderedAccessViewUint(m_histogramUAV.Get(), histClear);

        ID3D11UnorderedAccessView *uavs[8] = {
            m_diffuseRadianceUAV.Get(),    // u0
            m_specularRadianceUAV.Get(),   // u1
            m_viewZUAV.Get(),              // u2
            m_normalRoughnessUAV.Get(),    // u3
            m_motionVectorUAV.Get(),       // u4
            m_baseColorMetalnessUAV.Get(), // u5
            m_emissiveUAV.Get(),           // u6
            m_histogramUAV.Get(),          // u7
        };
        context->CSSetUnorderedAccessViews(0, 8, uavs, nullptr);

        // Dispatch (PathTracer: 16x16 ??살쟿??域밸챶竊?
        uint32_t gx = (width  + 15) / 16;
        uint32_t gy = (height + 15) / 16;
        m_pathTracerProgram->Dispatch(context, gx, gy, 1);

        // PathTracer ?귐딅꺖????곸젫
        ID3D11UnorderedAccessView *nullUAV[8] = {};
        context->CSSetUnorderedAccessViews(0, 8, nullUAV, nullptr);
        ID3D11ShaderResourceView *nullSRVs[7] = {};
        context->CSSetShaderResources(0, 7, nullSRVs);
    }

    // -------------------------------------------------------
    // ??λ뮞 2: NRD Denoise ??G-buffer ??denoised diffuse/specular
    //   m_denoiseEnabled=false ????λ뮞 ?袁⑷퍥 ??쎄땁 (A/B ?醫? F1).
    //   NRD SDK 沃섎챷肉겼칰???G-buffer??denoised ??용뮞筌ｌ꼶以?域밸챶?嚥?癰귣벊沅?stub).
    // -------------------------------------------------------
    bool useDenoiser = m_denoiseEnabled && m_nrdDenoiser && m_nrdDenoiser->HasUsableBackend();

    if (useDenoiser) {
        NrdGBufferInputs nrdIn = {};
        nrdIn.diffuseRadiance  = m_diffuseRadianceSRV.Get();
        nrdIn.specularRadiance = m_specularRadianceSRV.Get();
        nrdIn.viewZ            = m_viewZSRV.Get();
        nrdIn.normalRoughness  = m_normalRoughnessSRV.Get();
        nrdIn.motionVector     = m_motionVectorSRV.Get();

        NrdDenoisedOutputs nrdOut = {};
        nrdOut.diffuseSrv = m_denoisedDiffuseSRV.Get();
        nrdOut.specularSrv = m_denoisedSpecularSRV.Get();
        nrdOut.diffuse  = m_denoisedDiffuseUAV.Get();
        nrdOut.specular = m_denoisedSpecularUAV.Get();
        nrdOut.motionVector = m_motionVectorUAV.Get();

        NrdCameraData nrdCam = {};
        nrdCam.viewMatrix     = view;
        nrdCam.projMatrix     = proj;
        nrdCam.prevViewMatrix = (m_frameCount == 0) ? view : m_prevView;

        if (!m_nrdDenoiser->Denoise(context, nrdIn, nrdOut, nrdCam, m_frameCount)) {
            SPDLOG_ERROR("Context::Render: NRD denoise failed. Falling back to raw G-buffer for this frame.");
            m_nrdDenoiser->ResetHistory();
            useDenoiser = false;
        }
    }

    // -------------------------------------------------------
    // ??λ뮞 3: Composite ??(denoised or raw) diffuse * albedo + specular + emissive
    //   m_denoiseEnabled=true  ??denoised ??용뮞筌?(NRD ?곗뮆???癒?뮉 stub 癰귣벊沅쀨퉪?
    //   m_denoiseEnabled=false ???癒?궚 G-buffer 筌욊낯??????(A/B ??딅쓠?怨쀫뮞)
    // -------------------------------------------------------
    {
        ID3D11ShaderResourceView* diffSRV = useDenoiser
            ? m_denoisedDiffuseSRV.Get()
            : m_diffuseRadianceSRV.Get();
        ID3D11ShaderResourceView* specSRV = useDenoiser
            ? m_denoisedSpecularSRV.Get()
            : m_specularRadianceSRV.Get();

        auto gBuf = m_globalBuffer->GetBuffer();
        context->CSSetConstantBuffers(0, 1, &gBuf);

        ID3D11ShaderResourceView *compSRVs[5] = {
            diffSRV,                       // t0: diffuse
            specSRV,                       // t1: specular
            m_baseColorMetalnessSRV.Get(), // t2: albedo/metalness
            m_emissiveSRV.Get(),           // t3: emissive
            m_normalRoughnessSRV.Get(),    // t4: normal/roughness
        };
        context->CSSetShaderResources(0, 5, compSRVs);

        ID3D11UnorderedAccessView *compUAVs[1] = { m_compositeUAV.Get() };
        context->CSSetUnorderedAccessViews(0, 1, compUAVs, nullptr);

        uint32_t gx = (width  + 7) / 8;
        uint32_t gy = (height + 7) / 8;
        m_compositeProgram->Dispatch(context, gx, gy, 1);

        ID3D11UnorderedAccessView *nullUAV[1] = { nullptr };
        context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
        ID3D11ShaderResourceView *nullSRVs[5] = {};
        context->CSSetShaderResources(0, 5, nullSRVs);
    }

    // -------------------------------------------------------
    // ??λ뮞 4: ToneMap ??Composite HDR ??LDR ?곗뮆??
    // -------------------------------------------------------
    {
        // t10: Composite 野껉퀗?든몴?SRV嚥???꾨┛
        ID3D11ShaderResourceView *tmSRVs[1] = { m_compositeSRV.Get() };
        context->CSSetShaderResources(10, 1, tmSRVs);

        // u1: LDR ?곗뮆??UAV
        ID3D11UnorderedAccessView *tmUAVs[2] = { nullptr, m_outputUAV.Get() };
        context->CSSetUnorderedAccessViews(0, 2, tmUAVs, nullptr);

        // Dispatch (ToneMap: 8x8 ??살쟿??域밸챶竊?
        uint32_t gx = (width  + 7) / 8;
        uint32_t gy = (height + 7) / 8;
        m_toneMapProgram->Dispatch(context, gx, gy, 1);

        // ToneMap ?귐딅꺖????곸젫
        ID3D11UnorderedAccessView *nullUAV[2] = { nullptr, nullptr };
        context->CSSetUnorderedAccessViews(0, 2, nullUAV, nullptr);
        ID3D11ShaderResourceView *nullSRV[1] = { nullptr };
        context->CSSetShaderResources(10, 1, nullSRV);
    }

    m_prevViewProj = currViewProj;
    m_prevView     = view;

    // F2 ??쎄쾿?깃퀣爰?筌╈돦荑?(Phase 4 FLIP/SSIM ??쑨???
    CaptureScreenshot(context);

    // ?袁⑥쟿??燁삳똻???筌앹빓?
    m_frameCount++;
}

void Context::CaptureScreenshot(ID3D11DeviceContext* ctx) {
    if (!m_captureRequested) return;
    m_captureRequested = false;

    D3D11_TEXTURE2D_DESC texDesc;
    m_outputTexture->GetDesc(&texDesc);
    texDesc.Usage          = D3D11_USAGE_STAGING;
    texDesc.BindFlags      = 0;
    texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    texDesc.MiscFlags      = 0;

    ComPtr<ID3D11Texture2D> staging;
    HRESULT hr = m_device->CreateTexture2D(&texDesc, nullptr, staging.GetAddressOf());
    if (FAILED(hr)) {
        SPDLOG_ERROR("CaptureScreenshot: CreateTexture2D 0x{:08x}", (uint32_t)hr);
        return;
    }

    ctx->CopyResource(staging.Get(), m_outputTexture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        SPDLOG_ERROR("CaptureScreenshot: Map 0x{:08x}", (uint32_t)hr);
        return;
    }

    std::string label = m_denoiseEnabled ? "denoised" : "raw";
    uint32_t captureIdx = m_captureIndex++;
    std::string filename = "capture_" + std::to_string(captureIdx) + "_" + label + ".png";
    stbi_write_png(filename.c_str(),
                   (int)texDesc.Width, (int)texDesc.Height, 4,
                   mapped.pData, (int)mapped.RowPitch);
    ctx->Unmap(staging.Get(), 0);
    SPDLOG_INFO("Screenshot saved: {}", filename);

    // P5-3a: readback luminance histogram and dump percentiles
    if (m_histogramBuffer) {
        D3D11_BUFFER_DESC stageBD = {};
        stageBD.ByteWidth      = 256 * sizeof(uint32_t);
        stageBD.Usage          = D3D11_USAGE_STAGING;
        stageBD.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Buffer> histStaging;
        HRESULT hr2 = m_device->CreateBuffer(&stageBD, nullptr, histStaging.GetAddressOf());
        if (SUCCEEDED(hr2)) {
            ctx->CopyResource(histStaging.Get(), m_histogramBuffer.Get());
            D3D11_MAPPED_SUBRESOURCE hMapped = {};
            if (SUCCEEDED(ctx->Map(histStaging.Get(), 0, D3D11_MAP_READ, 0, &hMapped))) {
                const uint32_t* bins = static_cast<const uint32_t*>(hMapped.pData);

                uint64_t total = 0;
                for (int i = 0; i < 256; i++) total += bins[i];

                // bin b → luminance center: 2^(b/32) - 1
                auto lumFromBin = [](int b) -> double {
                    return std::pow(2.0, b / 32.0) - 1.0;
                };

                uint64_t cum = 0;
                double lum99 = 0.0, lum999 = 0.0;
                for (int i = 0; i < 256; i++) {
                    cum += bins[i];
                    if (lum99 == 0.0 && total > 0 && cum * 100 >= total * 99)
                        lum99 = lumFromBin(i);
                    if (total > 0 && cum * 1000 >= total * 999) {
                        lum999 = lumFromBin(i);
                        break;
                    }
                }

                ctx->Unmap(histStaging.Get(), 0);

                std::string histFile = "histogram_" + std::to_string(captureIdx) + "_" + label + ".txt";
                std::ofstream ofs(histFile);
                if (ofs) {
                    ofs << "P5-3a Luminance Histogram  frame=" << m_frameCount
                        << "  mode=" << label << "  total_samples=" << total << "\n";
                    ofs << "bin  lum_center  count  cum_pct\n";
                    uint64_t cumW = 0;
                    for (int i = 0; i < 256; i++) {
                        if (bins[i] == 0) continue;
                        cumW += bins[i];
                        double pct = total > 0 ? (cumW * 100.0 / total) : 0.0;
                        ofs << i << "  " << lumFromBin(i) << "  " << bins[i]
                            << "  " << pct << "%\n";
                    }
                    ofs << "99th_percentile_lum=" << lum99 << "\n";
                    ofs << "99.9th_percentile_lum=" << lum999 << "\n";
                }
                SPDLOG_INFO("Histogram saved: {}  99th={:.2f}  99.9th={:.2f}", histFile, lum99, lum999);
            }
        }
    }
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
    if (m_nrdDenoiser) m_nrdDenoiser->ResetHistory();
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

    // F1: A/B ?醫? ??denoise on/off (??륁맄 ??쑵??= ??苡??紐꾪뀱 ?袁㏉돱筌왖 ???죬?遺? ???)
    if (GetAsyncKeyState(VK_F1) & 0x0001) {
        m_denoiseEnabled = !m_denoiseEnabled;
        m_frameCount = 0;
        if (m_nrdDenoiser) m_nrdDenoiser->ResetHistory();
        const bool backendReady = m_nrdDenoiser && m_nrdDenoiser->HasUsableBackend();
        if (m_denoiseEnabled && !backendReady) {
            SPDLOG_INFO("Denoise requested, but NRD backend is unavailable ({}). Raw G-buffer path will be used until Phase 2 wiring is complete.",
                        m_nrdDenoiser ? m_nrdDenoiser->GetBackendStatusLabel() : "no-denoiser");
        } else {
            SPDLOG_INFO("Denoise {}", m_denoiseEnabled ? "ON" : "OFF (raw G-buffer)");
        }
    }

    if (moved) {
        m_frameCount = 0;
        if (m_nrdDenoiser) m_nrdDenoiser->ResetHistory();
    }

    // F2: 筌ㅼ뮇伊?LDR ??쎄쾿?깃퀣爰?(疫꿸퀣??F2 ??덉삂)
    if (GetAsyncKeyState(VK_F2) & 0x0001) {
        m_captureRequested = true;
        SPDLOG_INFO("Screenshot requested (frame={}, denoise={})", m_frameCount, m_denoiseEnabled);
    }
}
