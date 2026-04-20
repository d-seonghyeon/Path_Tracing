#include "nrd_denoiser.h"

#include <spdlog/spdlog.h>

// ===================================================================
// NRD 전용 헬퍼 (PT_ENABLE_NRD + NRD.h 있을 때만 컴파일)
// ===================================================================
#if PT_ENABLE_NRD && __has_include(<NRD.h>)

static const nrd::Identifier REBLUR_ID = 0;

// -------------------------------------------------------
// NRD Format → DXGI Format
// -------------------------------------------------------
static DXGI_FORMAT NrdFormatToDxgi(nrd::Format fmt)
{
    switch (fmt) {
    case nrd::Format::R8_UNORM:              return DXGI_FORMAT_R8_UNORM;
    case nrd::Format::R8_SNORM:              return DXGI_FORMAT_R8_SNORM;
    case nrd::Format::R8_UINT:               return DXGI_FORMAT_R8_UINT;
    case nrd::Format::R8_SINT:               return DXGI_FORMAT_R8_SINT;
    case nrd::Format::RG8_UNORM:             return DXGI_FORMAT_R8G8_UNORM;
    case nrd::Format::RG8_SNORM:             return DXGI_FORMAT_R8G8_SNORM;
    case nrd::Format::RG8_UINT:              return DXGI_FORMAT_R8G8_UINT;
    case nrd::Format::RG8_SINT:              return DXGI_FORMAT_R8G8_SINT;
    case nrd::Format::RGBA8_UNORM:           return DXGI_FORMAT_R8G8B8A8_UNORM;
    case nrd::Format::RGBA8_SNORM:           return DXGI_FORMAT_R8G8B8A8_SNORM;
    case nrd::Format::RGBA8_UINT:            return DXGI_FORMAT_R8G8B8A8_UINT;
    case nrd::Format::RGBA8_SINT:            return DXGI_FORMAT_R8G8B8A8_SINT;
    case nrd::Format::RGBA8_SRGB:            return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case nrd::Format::R16_UNORM:             return DXGI_FORMAT_R16_UNORM;
    case nrd::Format::R16_SNORM:             return DXGI_FORMAT_R16_SNORM;
    case nrd::Format::R16_UINT:              return DXGI_FORMAT_R16_UINT;
    case nrd::Format::R16_SINT:              return DXGI_FORMAT_R16_SINT;
    case nrd::Format::R16_SFLOAT:            return DXGI_FORMAT_R16_FLOAT;
    case nrd::Format::RG16_UNORM:            return DXGI_FORMAT_R16G16_UNORM;
    case nrd::Format::RG16_SNORM:            return DXGI_FORMAT_R16G16_SNORM;
    case nrd::Format::RG16_UINT:             return DXGI_FORMAT_R16G16_UINT;
    case nrd::Format::RG16_SINT:             return DXGI_FORMAT_R16G16_SINT;
    case nrd::Format::RG16_SFLOAT:           return DXGI_FORMAT_R16G16_FLOAT;
    case nrd::Format::RGBA16_UNORM:          return DXGI_FORMAT_R16G16B16A16_UNORM;
    case nrd::Format::RGBA16_SNORM:          return DXGI_FORMAT_R16G16B16A16_SNORM;
    case nrd::Format::RGBA16_UINT:           return DXGI_FORMAT_R16G16B16A16_UINT;
    case nrd::Format::RGBA16_SINT:           return DXGI_FORMAT_R16G16B16A16_SINT;
    case nrd::Format::RGBA16_SFLOAT:         return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case nrd::Format::R32_UINT:              return DXGI_FORMAT_R32_UINT;
    case nrd::Format::R32_SINT:              return DXGI_FORMAT_R32_SINT;
    case nrd::Format::R32_SFLOAT:            return DXGI_FORMAT_R32_FLOAT;
    case nrd::Format::RG32_UINT:             return DXGI_FORMAT_R32G32_UINT;
    case nrd::Format::RG32_SINT:             return DXGI_FORMAT_R32G32_SINT;
    case nrd::Format::RG32_SFLOAT:           return DXGI_FORMAT_R32G32_FLOAT;
    case nrd::Format::RGBA32_UINT:           return DXGI_FORMAT_R32G32B32A32_UINT;
    case nrd::Format::RGBA32_SINT:           return DXGI_FORMAT_R32G32B32A32_SINT;
    case nrd::Format::RGBA32_SFLOAT:         return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case nrd::Format::R10_G10_B10_A2_UNORM:  return DXGI_FORMAT_R10G10B10A2_UNORM;
    case nrd::Format::R10_G10_B10_A2_UINT:   return DXGI_FORMAT_R10G10B10A2_UINT;
    case nrd::Format::R11_G11_B10_UFLOAT:    return DXGI_FORMAT_R11G11B10_FLOAT;
    case nrd::Format::R9_G9_B9_E5_UFLOAT:    return DXGI_FORMAT_R9G9B9E5_SHAREDEXP;
    default:
        SPDLOG_ERROR("NrdFormatToDxgi: unsupported format {}", (uint32_t)fmt);
        return DXGI_FORMAT_UNKNOWN;
    }
}

// -------------------------------------------------------
// 풀 텍스처 생성 (SRV + UAV 동시)
// -------------------------------------------------------
static bool CreatePoolTexture(ID3D11Device* device, DXGI_FORMAT fmt,
                               uint32_t w, uint32_t h, NrdPoolEntry& entry)
{
    D3D11_TEXTURE2D_DESC td = {};
    td.Width            = w;
    td.Height           = h;
    td.MipLevels        = 1;
    td.ArraySize        = 1;
    td.Format           = fmt;
    td.SampleDesc.Count = 1;
    td.Usage            = D3D11_USAGE_DEFAULT;
    td.BindFlags        = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

    HRESULT hr = device->CreateTexture2D(&td, nullptr, entry.texture.ReleaseAndGetAddressOf());
    if (FAILED(hr)) { SPDLOG_ERROR("CreatePoolTexture Texture2D 0x{:08x}", (uint32_t)hr); return false; }

    hr = device->CreateShaderResourceView(entry.texture.Get(), nullptr, entry.srv.ReleaseAndGetAddressOf());
    if (FAILED(hr)) { SPDLOG_ERROR("CreatePoolTexture SRV 0x{:08x}", (uint32_t)hr); return false; }

    hr = device->CreateUnorderedAccessView(entry.texture.Get(), nullptr, entry.uav.ReleaseAndGetAddressOf());
    if (FAILED(hr)) { SPDLOG_ERROR("CreatePoolTexture UAV 0x{:08x}", (uint32_t)hr); return false; }

    return true;
}

#endif // PT_ENABLE_NRD

// ===================================================================
// NrdDenoiser 구현
// ===================================================================

NrdDenoiserUPtr NrdDenoiser::Create(ID3D11Device* device, uint32_t width, uint32_t height)
{
    auto denoiser = NrdDenoiserUPtr(new NrdDenoiser());
    if (!denoiser->Init(device, width, height))
        return nullptr;
    return denoiser;
}

NrdDenoiser::~NrdDenoiser()
{
    DestroyBackend();
}

void NrdDenoiser::DestroyBackend()
{
#if PT_ENABLE_NRD && __has_include(<NRD.h>)
    if (m_nrdInstance) {
        nrd::DestroyInstance(*m_nrdInstance);
        m_nrdInstance = nullptr;
    }
    m_pipelines.clear();
    m_permanentPool.clear();
    m_transientPool.clear();
    for (auto& s : m_samplers) s.Reset();
    m_constantBuffer.Reset();
    m_isBackendAvailable = false;
#endif
}

const char* NrdDenoiser::GetBackendStatusLabel() const
{
#if PT_ENABLE_NRD && __has_include(<NRD.h>)
    return m_isBackendAvailable ? "ready" : "headers-present-stub";
#else
    return "sdk-missing-stub";
#endif
}

// -------------------------------------------------------
// Init — NRD 인스턴스 + 파이프라인 + 풀 + 샘플러 + cbuffer 생성
// -------------------------------------------------------
bool NrdDenoiser::Init(ID3D11Device* device, uint32_t width, uint32_t height)
{
    if (!device) {
        SPDLOG_ERROR("NrdDenoiser::Init: device is null.");
        return false;
    }

    m_width          = width;
    m_height         = height;
    m_resetRequested = true;

#if PT_ENABLE_NRD && __has_include(<NRD.h>)

    // 1. NRD 인스턴스 생성 (REBLUR_DIFFUSE_SPECULAR)
    nrd::DenoiserDesc denoiserDesc = {};
    denoiserDesc.identifier = REBLUR_ID;
    denoiserDesc.denoiser   = nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR;

    nrd::InstanceCreationDesc instanceCreationDesc = {};
    instanceCreationDesc.denoisers    = &denoiserDesc;
    instanceCreationDesc.denoisersNum = 1;

    nrd::Result nrdResult = nrd::CreateInstance(instanceCreationDesc, m_nrdInstance);
    if (nrdResult != nrd::Result::SUCCESS || !m_nrdInstance) {
        SPDLOG_ERROR("NrdDenoiser::Init: nrd::CreateInstance failed ({}).", (uint32_t)nrdResult);
        m_isInitialized = true;
        return true; // 비치명적: stub 유지
    }

    const nrd::InstanceDesc& instDesc = nrd::GetInstanceDesc(*m_nrdInstance);

    // 2. 파이프라인당 ComputeShader 생성 (DXBC embed)
    m_pipelines.resize(instDesc.pipelinesNum);
    for (uint32_t i = 0; i < instDesc.pipelinesNum; i++) {
        const nrd::ComputeShaderDesc& dxbc = instDesc.pipelines[i].computeShaderDXBC;
        if (!dxbc.bytecode || dxbc.size == 0) {
            SPDLOG_ERROR("NrdDenoiser::Init: pipeline[{}] has no DXBC bytecode.", i);
            DestroyBackend(); m_isInitialized = true; return true;
        }
        HRESULT hr = device->CreateComputeShader(
            dxbc.bytecode, (SIZE_T)dxbc.size, nullptr,
            m_pipelines[i].ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            SPDLOG_ERROR("NrdDenoiser::Init: CreateComputeShader[{}] 0x{:08x}", i, (uint32_t)hr);
            DestroyBackend(); m_isInitialized = true; return true;
        }
    }

    // 3. Permanent pool (리사이즈 시에만 재생성)
    m_permanentPool.resize(instDesc.permanentPoolSize);
    for (uint32_t i = 0; i < instDesc.permanentPoolSize; i++) {
        const nrd::TextureDesc& td = instDesc.permanentPool[i];
        DXGI_FORMAT fmt = NrdFormatToDxgi(td.format);
        uint32_t w = std::max(1u, width  >> td.downsampleFactor);
        uint32_t h = std::max(1u, height >> td.downsampleFactor);
        if (!CreatePoolTexture(device, fmt, w, h, m_permanentPool[i])) {
            SPDLOG_ERROR("NrdDenoiser::Init: permanentPool[{}] failed.", i);
            DestroyBackend(); m_isInitialized = true; return true;
        }
    }

    // 4. Transient pool (매 프레임 재사용 가능)
    m_transientPool.resize(instDesc.transientPoolSize);
    for (uint32_t i = 0; i < instDesc.transientPoolSize; i++) {
        const nrd::TextureDesc& td = instDesc.transientPool[i];
        DXGI_FORMAT fmt = NrdFormatToDxgi(td.format);
        uint32_t w = std::max(1u, width  >> td.downsampleFactor);
        uint32_t h = std::max(1u, height >> td.downsampleFactor);
        if (!CreatePoolTexture(device, fmt, w, h, m_transientPool[i])) {
            SPDLOG_ERROR("NrdDenoiser::Init: transientPool[{}] failed.", i);
            DestroyBackend(); m_isInitialized = true; return true;
        }
    }

    // 5. 샘플러 (NEAREST_CLAMP, LINEAR_CLAMP)
    {
        D3D11_SAMPLER_DESC sd = {};
        sd.AddressU      = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD        = D3D11_FLOAT32_MAX;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;

        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        HRESULT hr = device->CreateSamplerState(&sd, m_samplers[0].ReleaseAndGetAddressOf());
        if (FAILED(hr)) { SPDLOG_ERROR("NrdDenoiser: nearest sampler 0x{:08x}", (uint32_t)hr); DestroyBackend(); m_isInitialized = true; return true; }

        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        hr = device->CreateSamplerState(&sd, m_samplers[1].ReleaseAndGetAddressOf());
        if (FAILED(hr)) { SPDLOG_ERROR("NrdDenoiser: linear sampler 0x{:08x}", (uint32_t)hr); DestroyBackend(); m_isInitialized = true; return true; }
    }

    // 6. 상수 버퍼 (최대 dispatch 크기, 16바이트 정렬)
    {
        uint32_t cbSize = ((instDesc.constantBufferMaxDataSize + 15) & ~15u);
        if (cbSize == 0) cbSize = 16;

        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth      = cbSize;
        bd.Usage          = D3D11_USAGE_DYNAMIC;
        bd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        HRESULT hr = device->CreateBuffer(&bd, nullptr, m_constantBuffer.ReleaseAndGetAddressOf());
        if (FAILED(hr)) { SPDLOG_ERROR("NrdDenoiser: constant buffer 0x{:08x}", (uint32_t)hr); DestroyBackend(); m_isInitialized = true; return true; }
    }

    m_isBackendAvailable = true;
    SPDLOG_INFO("NrdDenoiser: REBLUR_DIFFUSE_SPECULAR ready — {} pipelines, {} perm, {} trans.",
                instDesc.pipelinesNum, instDesc.permanentPoolSize, instDesc.transientPoolSize);

#else
    SPDLOG_WARN("NRD SDK not available. NrdDenoiser running as pass-through stub.");
#endif

    m_isInitialized = true;
    return true;
}

// -------------------------------------------------------
// Denoise — 매 프레임 NRD 디스패치 실행 (또는 stub CopyResource)
// -------------------------------------------------------
bool NrdDenoiser::Denoise(ID3D11DeviceContext* ctx,
                           const NrdGBufferInputs& inputs,
                           const NrdDenoisedOutputs& outputs,
                           const NrdCameraData& camera,
                           uint32_t frameIndex)
{
    if (!ctx) return false;

#if PT_ENABLE_NRD && __has_include(<NRD.h>)
    if (m_isBackendAvailable && m_nrdInstance) {
        const nrd::InstanceDesc& instDesc = nrd::GetInstanceDesc(*m_nrdInstance);

        // --- CommonSettings ---
        nrd::CommonSettings cs = {};

        // GLM은 column-major; NRD도 column-major (vector-as-column) → 직접 memcpy
        static_assert(sizeof(glm::mat4) == 64, "mat4 size");
        memcpy(cs.worldToViewMatrix,     glm::value_ptr(camera.viewMatrix),     64);
        memcpy(cs.worldToViewMatrixPrev, glm::value_ptr(camera.prevViewMatrix), 64);
        memcpy(cs.viewToClipMatrix,      glm::value_ptr(camera.projMatrix),     64);
        memcpy(cs.viewToClipMatrixPrev,  glm::value_ptr(camera.projMatrix),     64);

        // MV는 픽셀 단위 (prevPixel − currPixel). scale = 1/screenSize → UV 단위로 변환
        cs.motionVectorScale[0] = 1.0f / (float)m_width;
        cs.motionVectorScale[1] = 1.0f / (float)m_height;
        cs.motionVectorScale[2] = 0.0f;

        cs.resourceSize[0]     = cs.rectSize[0]     = (uint16_t)m_width;
        cs.resourceSize[1]     = cs.rectSize[1]     = (uint16_t)m_height;
        cs.resourceSizePrev[0] = cs.rectSizePrev[0] = (uint16_t)m_width;
        cs.resourceSizePrev[1] = cs.rectSizePrev[1] = (uint16_t)m_height;

        cs.denoisingRange           = 500000.0f;
        cs.frameIndex               = frameIndex;
        cs.isMotionVectorInWorldSpace = false;
        cs.accumulationMode = m_resetRequested
                              ? nrd::AccumulationMode::CLEAR_AND_RESTART
                              : nrd::AccumulationMode::CONTINUE;

        nrd::SetCommonSettings(*m_nrdInstance, cs);
        if (m_resetRequested) m_resetRequested = false;

        // --- DenoiserSettings (REBLUR 기본값 사용) ---
        nrd::ReblurSettings reblurSettings = {};
        reblurSettings.hitDistanceParameters.A = 3.0f;
        reblurSettings.hitDistanceParameters.B = 0.1f;
        reblurSettings.hitDistanceParameters.C = 20.0f;
        reblurSettings.hitDistanceParameters.D = -25.0f;
        // Third quality pass: keep the moderated anti-lag/history values, but loosen a
        // few spatial rejection terms so dark areas are less likely to collapse away.
        reblurSettings.antilagSettings.luminanceSigmaScale   = 3.5f;
        reblurSettings.antilagSettings.luminanceSensitivity  = 2.5f;
        reblurSettings.maxAccumulatedFrameNum                = 28;
        reblurSettings.maxFastAccumulatedFrameNum            = 5;
        reblurSettings.maxStabilizedFrameNum                 = 0;
        reblurSettings.historyFixBasePixelStride             = 8;
        reblurSettings.diffusePrepassBlurRadius              = 24.0f;
        reblurSettings.specularPrepassBlurRadius             = 42.0f;
        reblurSettings.minHitDistanceWeight                  = 0.18f;
        reblurSettings.lobeAngleFraction                     = 0.20f;
        reblurSettings.roughnessFraction                     = 0.20f;
        reblurSettings.planeDistanceSensitivity              = 0.03f;
        nrd::SetDenoiserSettings(*m_nrdInstance, REBLUR_ID, &reblurSettings);

        // --- GetComputeDispatches ---
        const nrd::DispatchDesc* dispatches     = nullptr;
        uint32_t                 numDispatches  = 0;
        nrd::Result r = nrd::GetComputeDispatches(*m_nrdInstance, &REBLUR_ID, 1,
                                                   dispatches, numDispatches);
        if (r != nrd::Result::SUCCESS || !dispatches) {
            SPDLOG_ERROR("NrdDenoiser::Denoise: GetComputeDispatches failed ({}).", (uint32_t)r);
            return false;
        }

        // --- 샘플러 바인딩 (모든 dispatch 공용) ---
        {
            std::vector<ID3D11SamplerState*> sampArr(instDesc.samplersNum, nullptr);
            for (uint32_t i = 0; i < instDesc.samplersNum; i++)
                sampArr[i] = m_samplers[(uint32_t)instDesc.samplers[i]].Get();
            ctx->CSSetSamplers(instDesc.samplersBaseRegisterIndex,
                               instDesc.samplersNum, sampArr.data());
        }

        // --- Dispatch 루프 ---
        for (uint32_t d = 0; d < numDispatches; d++) {
            const nrd::DispatchDesc& disp = dispatches[d];
            const nrd::PipelineDesc& pipe = instDesc.pipelines[disp.pipelineIndex];

            // Compute Shader 설정
            ctx->CSSetShader(m_pipelines[disp.pipelineIndex].Get(), nullptr, 0);

            // 상수 버퍼 업데이트
            if (!disp.constantBufferDataMatchesPreviousDispatch &&
                disp.constantBufferData && disp.constantBufferDataSize > 0) {
                D3D11_MAPPED_SUBRESOURCE mapped = {};
                if (SUCCEEDED(ctx->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                    memcpy(mapped.pData, disp.constantBufferData, disp.constantBufferDataSize);
                    ctx->Unmap(m_constantBuffer.Get(), 0);
                }
            }
            {
                ID3D11Buffer* cb = m_constantBuffer.Get();
                ctx->CSSetConstantBuffers(instDesc.constantBufferRegisterIndex, 1, &cb);
            }

            // 리소스 바인딩 (각 ResourceRange 순회)
            uint32_t resourceOffset = 0;
            for (uint32_t ri = 0; ri < pipe.resourceRangesNum; ri++) {
                const nrd::ResourceRangeDesc& range = pipe.resourceRanges[ri];

                if (range.descriptorType == nrd::DescriptorType::TEXTURE) {
                    std::vector<ID3D11ShaderResourceView*> srvs(range.descriptorsNum, nullptr);
                    for (uint32_t k = 0; k < range.descriptorsNum; k++)
                        srvs[k] = ResolveSRV(disp.resources[resourceOffset + k], inputs);
                    ctx->CSSetShaderResources(range.baseRegisterIndex,
                                              range.descriptorsNum, srvs.data());
                } else {
                    std::vector<ID3D11UnorderedAccessView*> uavs(range.descriptorsNum, nullptr);
                    for (uint32_t k = 0; k < range.descriptorsNum; k++)
                        uavs[k] = ResolveUAV(disp.resources[resourceOffset + k], outputs);
                    ctx->CSSetUnorderedAccessViews(range.baseRegisterIndex,
                                                   range.descriptorsNum, uavs.data(), nullptr);
                }
                resourceOffset += range.descriptorsNum;
            }

            // Dispatch
            ctx->Dispatch(disp.gridWidth, disp.gridHeight, 1);

            // null-clear (hazard 방지)
            resourceOffset = 0;
            for (uint32_t ri = 0; ri < pipe.resourceRangesNum; ri++) {
                const nrd::ResourceRangeDesc& range = pipe.resourceRanges[ri];
                if (range.descriptorType == nrd::DescriptorType::TEXTURE) {
                    std::vector<ID3D11ShaderResourceView*>  nullSRV(range.descriptorsNum, nullptr);
                    ctx->CSSetShaderResources(range.baseRegisterIndex, range.descriptorsNum, nullSRV.data());
                } else {
                    std::vector<ID3D11UnorderedAccessView*> nullUAV(range.descriptorsNum, nullptr);
                    ctx->CSSetUnorderedAccessViews(range.baseRegisterIndex, range.descriptorsNum, nullUAV.data(), nullptr);
                }
                resourceOffset += range.descriptorsNum;
            }
        }

        // 공통 슬롯 null-clear
        {
            ID3D11Buffer* nullCB = nullptr;
            ctx->CSSetConstantBuffers(instDesc.constantBufferRegisterIndex, 1, &nullCB);
        }
        {
            std::vector<ID3D11SamplerState*> nullSamp(instDesc.samplersNum, nullptr);
            ctx->CSSetSamplers(instDesc.samplersBaseRegisterIndex, instDesc.samplersNum, nullSamp.data());
        }
        ctx->CSSetShader(nullptr, nullptr, 0);

        return true;
    }
#endif

    // -------------------------------------------------------
    // Stub: input SRVs → output UAVs pass-through 복사
    // -------------------------------------------------------
    (void)camera;
    (void)frameIndex;

    ID3D11Resource* outDiffRes = nullptr;
    ID3D11Resource* outSpecRes = nullptr;
    ID3D11Resource* inDiffRes  = nullptr;
    ID3D11Resource* inSpecRes  = nullptr;

    if (outputs.diffuse)  outputs.diffuse->GetResource(&outDiffRes);
    if (outputs.specular) outputs.specular->GetResource(&outSpecRes);
    if (inputs.diffuseRadiance)  inputs.diffuseRadiance->GetResource(&inDiffRes);
    if (inputs.specularRadiance) inputs.specularRadiance->GetResource(&inSpecRes);

    if (inDiffRes && outDiffRes) ctx->CopyResource(outDiffRes, inDiffRes);
    if (inSpecRes && outSpecRes) ctx->CopyResource(outSpecRes, inSpecRes);

    if (outDiffRes) outDiffRes->Release();
    if (outSpecRes) outSpecRes->Release();
    if (inDiffRes)  inDiffRes->Release();
    if (inSpecRes)  inSpecRes->Release();

    if (m_resetRequested) m_resetRequested = false;
    return true;
}

// -------------------------------------------------------
// OnResize — 풀/파이프라인 재생성
// -------------------------------------------------------
bool NrdDenoiser::OnResize(ID3D11Device* device, uint32_t width, uint32_t height)
{
    if (!device) {
        SPDLOG_ERROR("NrdDenoiser::OnResize: device is null.");
        return false;
    }

    DestroyBackend();
    m_isInitialized = false;
    return Init(device, width, height);
}

void NrdDenoiser::ResetHistory()
{
    m_resetRequested = true;
}

// ===================================================================
// ResolveSRV / ResolveUAV
// ===================================================================
#if PT_ENABLE_NRD && __has_include(<NRD.h>)

ID3D11ShaderResourceView* NrdDenoiser::ResolveSRV(const nrd::ResourceDesc& res,
                                                    const NrdGBufferInputs& in) const
{
    switch (res.type) {
    case nrd::ResourceType::IN_MV:                    return in.motionVector;
    case nrd::ResourceType::IN_NORMAL_ROUGHNESS:       return in.normalRoughness;
    case nrd::ResourceType::IN_VIEWZ:                  return in.viewZ;
    case nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST:  return in.diffuseRadiance;
    case nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST:  return in.specularRadiance;
    case nrd::ResourceType::TRANSIENT_POOL:
        return (res.indexInPool < m_transientPool.size())
               ? m_transientPool[res.indexInPool].srv.Get() : nullptr;
    case nrd::ResourceType::PERMANENT_POOL:
        return (res.indexInPool < m_permanentPool.size())
               ? m_permanentPool[res.indexInPool].srv.Get() : nullptr;
    default:
        return nullptr;
    }
}

ID3D11UnorderedAccessView* NrdDenoiser::ResolveUAV(const nrd::ResourceDesc& res,
                                                     const NrdDenoisedOutputs& out) const
{
    switch (res.type) {
    case nrd::ResourceType::IN_MV:                   return out.motionVector;
    case nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST: return out.diffuse;
    case nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST: return out.specular;
    case nrd::ResourceType::TRANSIENT_POOL:
        return (res.indexInPool < m_transientPool.size())
               ? m_transientPool[res.indexInPool].uav.Get() : nullptr;
    case nrd::ResourceType::PERMANENT_POOL:
        return (res.indexInPool < m_permanentPool.size())
               ? m_permanentPool[res.indexInPool].uav.Get() : nullptr;
    default:
        return nullptr;
    }
}

#endif // PT_ENABLE_NRD
