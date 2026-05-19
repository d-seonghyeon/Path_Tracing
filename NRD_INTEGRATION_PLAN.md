# NRD Integration Plan — PT_Object_Loading

**작성일**: 2026-04-17  
**대상**: DX11 컴퓨트 셰이더 기반 Path Tracer (현재 저장소 상태 기준)  
**목표**: NVIDIA Real-time Denoisers(NRD)의 **ReBLUR_DIFFUSE_SPECULAR** 도입으로 1 spp 실시간 품질 달성

---

## 0.1 Phase 6 Addendum (2026-05-19)

- cap_sharing merge is tracked as Phase 6 on top of the original NRD Phase 0-4 plan.
- Completed Phase 6 features: HDRI environment map loading, CDF sampling, environment NEE/MIS, VNDF specular sampling, and Kulla-Conty multi-scatter energy compensation via `g_energyLUT`.
- Final master Phase D policy: keep current NRD emissive values and apply shared `TONE_MAP_EXPOSURE=0.82` for both raw and denoised ToneMap paths.
- Emissive comparison branch exception: `phase6-d-emissive` uses local cap_sharing emissive values and shared `TONE_MAP_EXPOSURE=1.0`.

---

## 0. 현재 구조와 NRD의 불일치 정리 (가장 중요)

현재 파이프라인 (`context.cpp::Render`, `PathTracer.hlsl`):

```
┌─────────────┐     ┌─────────────┐     ┌───────────┐     ┌─────┐
│ PathTracer  │ →→→ │  accumTex   │ →→→ │ ToneMapCS │ →→→ │ RTV │
│ (1 spp/frame│     │ HDR float4  │     │  /N+1     │     │     │
│  누적: +=)  │     │ 계속 누적    │     │  ACES     │     │     │
└─────────────┘     └─────────────┘     └───────────┘     └─────┘
                           ↑
                    frameCount==0 이면 =, 아니면 +=
```

NRD가 원하는 파이프라인:

```
┌────────────┐ noisy diffuse   ┌─────┐ denoised diffuse ┌──────────┐
│ PathTracer │ noisy specular  │ NRD │ denoised specular│ Composite│
│ 1 spp/frame│ ───────────────→│     │─────────────────→│  +Tonemap│→ RTV
│  + Gbuffer │ normal/viewZ/MV │     │                  │          │
└────────────┘ ────────────────→─────→                  └──────────┘
                                  ↑
                         prev/curr view-proj,
                         prev 프레임 내부 히스토리(NRD가 관리)
```

**핵심 차이:**
1. 현재는 **프레임 간 합산 누적 → 마지막에 나누기**. NRD는 **프레임마다 1spp noisy를 생성 → NRD가 시공간 재투영/필터링으로 누적**. 그래서 `g_accum +=` 모델을 **`g_accum =` (또는 아예 accum을 버리고 `out_diff` / `out_spec`)** 로 바꿔야 합니다.
2. NRD는 **diffuse / specular가 분리된 신호**를 원합니다. 현재 `TracePath`는 최종 `totalRadiance` 하나만 반환합니다.
3. NRD는 **first-hit G-buffer + motion vector + linear viewZ + 이전 프레임 카메라 행렬**이 필요합니다. 현재는 전부 없습니다.
4. NRD는 **히트 거리(HitT)** 를 필요로 하며, `REBLUR_FrontEnd_PackRadianceAndNormHitDist` 같은 전용 인코딩을 써야 합니다.
5. 현재 `SurfaceHit`은 이미 roughness/metallic을 갖고 있지만, first-hit을 따로 저장하지 않기 때문에 **"첫 바운스 분리"** 를 해야 합니다.

Phase 0이 전부 여기에 해당합니다. NRD를 빌드 타겟에 추가하기 전에 먼저 끝내야 하는 작업입니다.

---

## 1. 백엔드 선택 — 추천: NRD 셰이더 임베드 + 자체 디스패치 (no NRI)

NRD 도입 방식 옵션:

| 방식 | 장점 | 단점 | 이 프로젝트에 대한 적합도 |
|---|---|---|---|
| **A. NRI + NRD Integration** | NVIDIA 공식 경로. 샘플이 그대로 동작. 업데이트 쉬움 | NRI가 DX12 중심이고 DX11 백엔드가 별도 추상화 레이어. 현재 raw `ID3D11*` 코드와 이중 리소스 관리 발생 | △ — 무겁다 |
| **B. NRD 셰이더 임베드(DXBC) + 자체 C++ 디스패치** | 현재 `ComputeProgram` / `Buffer` / `Texture` 구조와 궤를 같이함. NRI 제거로 의존성 축소 | NRD Instance 관리 로직을 직접 작성해야 함(~300~500 LoC). permutation 관리 필요 | ◎ — **추천** |
| **C. NRD 미사용, SVGF/A-SVGF 자체 구현** | 의존성 전혀 없음, 완전한 학습용 | 품질/일반성에서 NRD 대비 떨어짐. 코드량 더 많음 | × — NRD 목표에 어긋남 |

**결정**: **옵션 B**. `Dependency.cmake`에 NRD만 `ExternalProject_Add`로 추가하고, CMake 옵션으로 **DXBC 사전 컴파일 + NRD_EMBEDS_DXBC_SHADERS=ON** 을 켠 뒤, 우리 쪽 `ComputeProgram`으로 로드해 디스패치.

참고:
- NRD 저장소: https://github.com/NVIDIAGameWorks/RayTracingDenoiser
- `NRD.h` / `NRD.hlsli` 만 쓰면 되고, `NRDDescs.h`로 디스패치 정보 얻을 수 있음
- SM 5.0 (DX11) 타깃 DXBC 블롭을 생성하는 경로가 이미 지원됨

---

## 2. 로드맵 (Phase 0 → Phase 4)

### Phase 0 — 파이프라인 재배치 (NRD 추가 전)
NRD 없이도 돌아야 하고, "denoiser 자리만 비워둔 1spp 파이프라인"이 먼저 나와야 합니다.

**0-1. 누적 모델 제거 + 1spp 직접 출력**
- `PathTracer.hlsl`
  - `g_accum += ...` → `g_accum = ...` (또는 이름을 `g_radiance`로 바꾸고 매 프레임 덮어쓰기)
  - `SAMPLES_PER_PIXEL = 1` 고정
  - `frameCount == 0` 분기 삭제
- `Tonemap.hlsl`
  - `/ (frameCount+1)` 제거 (NRD 경로에서는 평균이 아니라 denoised 1프레임 값)
- 검증: 기존과 똑같이 돌지만 프레임별로 지글거려야 함 → NRD 붙일 자리 준비됨

**0-2. G-buffer 출력 추가**
`PathTracer.hlsl`의 첫 바운스(primary hit) 정보를 전용 UAV로 저장.

추가 UAV (레지스터 번호는 현재 `u0`(accum), `u1`(ldr) 이외로 배치):

| 이름 | Register | 포맷 | 내용 |
|---|---|---|---|
| `g_outDiffRadianceHitT`  | `u0` | `R16G16B16A16_FLOAT` | NRD 패킹된 diffuse 신호 |
| `g_outSpecRadianceHitT`  | `u1` | `R16G16B16A16_FLOAT` | NRD 패킹된 specular 신호 |
| `g_outNormalRoughness`   | `u2` | `R10G10B10A2_UNORM`  | octahedral normal + roughness |
| `g_outViewZ`             | `u3` | `R32_FLOAT`          | linear view-space Z |
| `g_outMotion`            | `u4` | `R16G16_FLOAT`       | 2D screen-space motion (NRD 규격) |
| `g_outBaseColorMetallic` | `u5` | `R8G8B8A8_UNORM`     | composition 패스용 albedo/metallic |
| `g_outEmissive`          | `u6` | `R11G11B10_FLOAT`    | primary emission (denoiser 우회) |

→ 현재의 `m_accumTexture` 자리는 `g_outDiffRadianceHitT`로 대체, `m_outputTexture`는 그대로 composition 출력.  
→ `context.h`/`context.cpp`에 위 텍스처 7개 + UAV/SRV 생성 로직 추가. `OnResize`에서 모두 재생성.

**0-3. Diffuse/Specular 분리 로직**
`PathTracer.hlsl::TracePath`의 현 구조를 다음과 같이 나눕니다.

```hlsl
// first hit에서 lobe를 강제 선택 대신 "양쪽 다 추적"하거나,
// 확률적으로 한쪽만 추적하되 PDF 가중치를 기록합니다.
//
// 단순화 접근 (추천 시작점):
//   1) first hit의 normal/viewZ/MV/albedo/metallic/roughness/emission → Gbuffer UAV
//   2) first hit 이후 경로의 방향이 "first bounce에서 specular lobe로 샘플됐는가"에 따라
//      Ldiff 또는 Lspec 에 누적.
//   3) 각 경로의 첫 "이벤트 히트 거리"(= first hit에서 2nd hit까지의 거리)를 hitT로 저장.

float3 Ldiff = 0, Lspec = 0;
float hitTDiff = 0, hitTSpec = 0;
bool firstBounceSpec = ...; // ImportanceSampleGGX로 샘플했는가
float3 contrib = TracePathFromSecondHit(...);
if (firstBounceSpec) { Lspec += contrib; hitTSpec = dist_first_to_second; }
else                 { Ldiff += contrib; hitTDiff = dist_first_to_second; }
// NEE도 first hit에서는 Ldiff 쪽에 더함(diffuse 경로로 취급)
//   단, specular NEE가 중요하면 두 버킷 다 분리 가능 (Phase 3)
```

NRD 문서 권장에 맞추어 **primary ray가 맞은 표면의 "직접" emission은 denoiser를 통과시키지 않고** `g_outEmissive` 로 따로 빼서 최종 composition에서 더합니다.

**0-4. ViewZ / Normal / MV 계산**
`PathTracer.hlsl`에 추가:
- `g_currViewProj`, `g_prevViewProj` (행렬 2개)
- `g_invView`, `g_invProj` 는 기존 GlobalUB로 대체 가능(ray 생성용 camera basis가 이미 존재)

```hlsl
float3 worldPos = hit.p;
// viewZ: world→view 변환 후 z (right-handed 기준 보통 -view.z)
float4 viewPos = mul(g_currView, float4(worldPos, 1));
float viewZ = viewPos.z;              // NRD는 RH_NEGATIVE 또는 LH_POSITIVE 둘 다 지원

// motion vector: prev 프레임의 동일 world pos가 이전 스크린 어디였는지
float4 prevClip = mul(g_prevViewProj, float4(worldPos, 1));
float2 prevNDC  = prevClip.xy / prevClip.w;
float2 prevUV   = prevNDC * float2(0.5, -0.5) + 0.5;
float2 currUV   = ((float2)pixelCoord + 0.5) / (float2)screenSize;
float2 motion   = (prevUV - currUV) * (float2)screenSize;  // NRD: 2D screen-pixel MV
g_outMotion[pixelCoord] = motion;
```

*정적 씬이므로 MV는 순수 카메라 운동만 반영됨. 동적 오브젝트는 Phase 3 이상.*

**0-5. Composition 패스 추가 (denoiser 없을 때도 동작)**
새 셰이더 `Composite.hlsl`:

```hlsl
// inputs
Texture2D<float4> g_diff : register(t0);   // NRD denoised 또는 raw
Texture2D<float4> g_spec : register(t1);
Texture2D<float4> g_baseMetallic : register(t2);
Texture2D<float3> g_emissive : register(t3);
Texture2D<float4> g_normalRough : register(t4);

// (diff*albedo*(1-F)*(1-metallic) + spec*F + emission) 조합
// 진입 시점엔 NRD 없이도 1spp raw를 그대로 넣어서 파이프라인 검증
```

Phase 0 종료 시: denoiser 없이도 "분리된 1spp noisy + composition + tonemap"이 돌아가고, NRD만 꽂으면 됨.

---

### Phase 1 — NRD 의존성 추가

**1-1. `Dependency.cmake`에 NRD 추가**

```cmake
# 6. NRD (NVIDIA Real-time Denoisers) — shader-only, DXBC embed
ExternalProject_Add(
    dep_nrd
    GIT_REPOSITORY "https://github.com/NVIDIAGameWorks/RayTracingDenoiser.git"
    GIT_TAG "v4.14.3"           # 2026-04 기준 최신 안정 태그로 갱신
    GIT_SHALLOW 1
    UPDATE_DISCONNECTED 1
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${DEP_INSTALL_DIR}
        -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDebugDLL
        -DNRD_STATIC_LIBRARY=ON
        -DNRD_EMBEDS_DXBC_SHADERS=ON       # DX11용 DXBC 블롭 내장
        -DNRD_EMBEDS_DXIL_SHADERS=OFF
        -DNRD_EMBEDS_SPIRV_SHADERS=OFF
        -DNRD_DISABLE_INTERPROCEDURAL_OPTIMIZATION=ON
        -DNRD_USE_PRECOMPILED_SHADERS=ON
    BUILD_COMMAND   ${CMAKE_COMMAND} --build <BINARY_DIR> --config Debug
    INSTALL_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Debug --target install
)
set(DEP_LIBS ${DEP_LIBS} NRD)
set(DEP_LIST ${DEP_LIST} dep_nrd)
```

NRD는 독자 shader compiler(ShaderMake)를 씁니다. 빌드 타임이 10~20분 추가됩니다. CMake 옵션은 저장소 상단 `README.md` / `CMakeLists.txt`로 한 번 확인 필요 (버전별로 옵션명이 약간 변합니다).

**1-2. `CMakeLists.txt` 수정**
- `target_link_libraries` 에 `NRD` 추가
- `target_include_directories` 에 NRD include 경로 추가 (`${DEP_INSTALL_DIR}/include/nrd`, `.../include/nrd/Shaders`)

**1-3. NRD 헤더 사용 확인**
간단한 smoke test 코드:
```cpp
#include <NRD.h>
nrd::InstanceCreationDesc desc = {};
nrd::DenoiserDesc denoisers[1] = { { 0u, nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR } };
desc.denoisers = denoisers;
desc.denoisersNum = 1;
// nrd::Instance* instance = nullptr;
// nrd::CreateInstance(desc, instance);  // <- 링크만 되는지 확인
```

---

### Phase 2 — ReBLUR_DIFFUSE_SPECULAR 디스패치

**2-1. NrdDenoiser 클래스**
`src/nrd_denoiser.h / .cpp` 신규:

```cpp
class NrdDenoiser {
public:
    bool Init(ID3D11Device* device, uint32_t w, uint32_t h);
    void OnResize(ID3D11Device* device, uint32_t w, uint32_t h);

    // commonSettings: view/proj 행렬, jitter, frameIndex 등
    // reblurSettings: 히스토리 길이, anti-lag, 히트T 파라미터
    void Denoise(
        ID3D11DeviceContext* ctx,
        const nrd::CommonSettings& common,
        const nrd::ReblurSettings& reblur,
        // 입력 SRV 4개 + 출력 UAV 2개
        ID3D11ShaderResourceView* srvIN_NOISY_DIFF,
        ID3D11ShaderResourceView* srvIN_NOISY_SPEC,
        ID3D11ShaderResourceView* srvIN_NORMAL_ROUGHNESS,
        ID3D11ShaderResourceView* srvIN_VIEWZ,
        ID3D11ShaderResourceView* srvIN_MV,
        ID3D11UnorderedAccessView* uavOUT_DIFF,
        ID3D11UnorderedAccessView* uavOUT_SPEC);

private:
    nrd::Instance* m_instance = nullptr;
    // NRD가 요구하는 transient/permanent 풀 텍스처를 여기에 생성해서 관리
    std::vector<ComPtr<ID3D11Texture2D>> m_transientPool;
    std::vector<ComPtr<ID3D11Texture2D>> m_permanentPool;
    std::vector<ComPtr<ID3D11ShaderResourceView>> m_transientSRV;
    // ... UAV도 마찬가지
    // DXBC 블롭 → ComputeShader 핸들 맵
    std::unordered_map<uint32_t, ComPtr<ID3D11ComputeShader>> m_pipelineShaders;
    // Sampler
    std::array<ComPtr<ID3D11SamplerState>, 4> m_samplers;
};
```

**핵심 동작 흐름** (`NrdDenoiser::Init`):

```cpp
nrd::InstanceCreationDesc desc = {};
nrd::DenoiserDesc denoisers[] = { { 0, nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR } };
desc.denoisers = denoisers;
desc.denoisersNum = 1;
nrd::CreateInstance(desc, m_instance);

const nrd::InstanceDesc& inst = nrd::GetInstanceDesc(*m_instance);

// 1) permanent / transient pool 생성
//    inst.permanentPoolSize / transientPoolSize 만큼 텍스처 생성
//    각 pool entry가 요구하는 format/mipNum을 DX11 포맷으로 매핑
for (uint32_t i = 0; i < inst.permanentPoolSize; ++i) {
    const nrd::TextureDesc& t = inst.permanentPool[i];
    // DXGI_FORMAT fmt = MapNrdFormatToDxgi(t.format);
    // ... CreateTexture2D(...)
}
// transient도 동일

// 2) pipeline (ComputeShader) 생성
for (uint32_t i = 0; i < inst.pipelinesNum; ++i) {
    const nrd::PipelineDesc& p = inst.pipelines[i];
    // p.computeShaderDXBC.bytecode / .size 를 device->CreateComputeShader에 넘김
}

// 3) Sampler 생성 (inst.samplers 참조, 보통 4개: linearClamp, nearestClamp, ...)
```

**2-2. Denoise 호출** (`NrdDenoiser::Denoise`):

```cpp
// SetCommonSettings / SetDenoiserSettings
nrd::SetCommonSettings(*m_instance, common);
nrd::SetDenoiserSettings(*m_instance, 0 /*id*/, &reblur);

// GetComputeDispatches
const nrd::DispatchDesc* dispatches = nullptr;
uint32_t dispatchNum = 0;
nrd::GetComputeDispatches(*m_instance,
    /*identifiers*/ &kIdentifier, /*identifiersNum*/ 1,
    dispatches, dispatchNum);

for (uint32_t i = 0; i < dispatchNum; ++i) {
    const nrd::DispatchDesc& d = dispatches[i];
    // d.pipelineIndex → m_pipelineShaders[d.pipelineIndex] 로 CSSetShader
    // d.resources[0..n] → 각 리소스가 RESOURCE_TYPE_IN_... / OUT_... / PERMANENT / TRANSIENT 에 따라
    //                     SRV 또는 UAV로 바인딩
    // d.constantBufferData / .constantBufferDataSize → Map/Unmap으로 CB 업데이트
    // d.gridWidth / .gridHeight → Dispatch
}
```

**2-3. `context.cpp::Render` 변경 후 흐름**:

```
1) PathTracer dispatch        → 7개 Gbuffer UAV + noisy diff/spec
2) (optional) MV sanity check   (Phase 3)
3) NrdDenoiser::Denoise       → denoised_diff / denoised_spec
4) Composite dispatch         → HDR frame (accumulate 아님, 단일 프레임)
5) ToneMap dispatch           → LDR
6) Present                    → RTV 복사
```

---

### Phase 3 — 품질 / 성능 튜닝

**3-1. HitT 인코딩**
`NRD.hlsli` 의 `REBLUR_FrontEnd_PackRadianceAndNormHitDist`, `REBLUR_FrontEnd_GetNormHitDist` 사용. roughness와 `hitDistParams`가 서로 연계되어 있으므로 `ReblurSettings::hitDistanceParameters` 를 씬 스케일에 맞춰 튜닝.

**3-2. 히스토리 리셋 / Anti-lag**
카메라가 점프할 때(텔레포트), 리사이즈 시에는 `nrd::CommonSettings::accumulationMode = CLEAR_AND_RESTART` 로 리셋. 일반 운동 중에는 `ReblurSettings::antilagSettings` 로 고스트 최소화.

**3-3. ReLAX로 스위치**
유리·금속이 많은 씬에서 ReBLUR가 specular에서 blur 과한 경향이 있으면 `REBLUR_DIFFUSE_SPECULAR` → `RELAX_DIFFUSE_SPECULAR` 로 교체. 인터페이스는 거의 동일 (input layout 같음).

**3-4. SIGMA (shadows)**
현재 NEE + MIS 구조에서 섀도우만 따로 빼서 SIGMA로 처리 가능 (품질 큰 이득). Phase 3의 서브태스크로 분리.

**3-5. 동적 오브젝트 MV**
SDF/BVH 스키닝 등을 추가하면 각 오브젝트별 prev world matrix를 저장해서 per-pixel MV 계산. 현재 프로젝트는 정적 씬이라 후순위.

---

### Phase 4 — 검증 / 벤치 / 문서

- **A/B 토글**: `cbuffer` 에 `g_useDenoiser` 플래그, 1spp raw vs denoised 를 즉석에서 비교.
- **FLIP / SSIM**: assimp로 참조(수천 spp 누적) 저장 후 denoised 결과와 수치 비교.
- **프레임 타임**: PathTracer / NRD / Composite 각 패스별 `ID3D11Query(D3D11_QUERY_TIMESTAMP_DISJOINT)` 로 측정.
- `CHANGELOG_2026-04-XX.md` 작성 (이 저장소 컨벤션).

---

## 3. 체크리스트 (순서대로 실행)

**Phase 0 (NRD 없이 선행)**
- [ ] `PathTracer.hlsl` 누적 제거 → 매 프레임 덮어쓰기
- [ ] `Tonemap.hlsl` frameCount 나눗셈 제거
- [ ] G-buffer UAV 7개 생성 (`context.h/.cpp` + `OnResize`)
- [ ] `TracePath`를 `Ldiff` / `Lspec` / `hitTDiff` / `hitTSpec` 분리 버전으로 리팩터
- [ ] viewZ / normalRoughness / motion / baseColorMetallic / emissive 출력
- [ ] `g_prevViewProj` 저장 로직 (`context.cpp::Render` 끝에서 curr 복사)
- [ ] `Composite.hlsl` 신규 + 3-pass 파이프라인 (PT → Composite → Tonemap)
- [ ] 검증: 1spp raw가 눈에 띄게 지글거리지만 결과 픽셀값이 기존 누적 1프레임과 일치해야 함

**Phase 1**
- [ ] `Dependency.cmake` 에 `dep_nrd` 추가
- [ ] `CMakeLists.txt` 링크/인클루드 경로 추가
- [ ] `#include <NRD.h>` 빌드 확인

**Phase 2**
- [ ] `src/nrd_denoiser.h/.cpp` 신규
- [ ] permanent/transient 풀 생성 (포맷 매퍼 포함)
- [ ] DXBC → `ID3D11ComputeShader` 파이프라인 빌더
- [ ] `GetComputeDispatches` 루프 → CSSetShader / CSSetSRV / CSSetUAV / CSSetCB / Dispatch
- [ ] `context.cpp::Render` 4-pass로 확장 (PT → NRD → Composite → Tonemap)
- [ ] 시각 확인

**Phase 3**
- [ ] hitDistanceParameters 튜닝
- [ ] accumulationMode = CLEAR_AND_RESTART 트리거(리사이즈, 카메라 텔레포트)
- [ ] SIGMA_SHADOW 추가 (선택)
- [ ] ReLAX 스위치 (선택)

**Phase 4**
- [ ] A/B 토글 UI (함수 키로 on/off)
- [ ] FLIP/SSIM 참조 비교 스크립트
- [ ] 타임스탬프 쿼리로 per-pass 프로파일
- [ ] `CHANGELOG_2026-04-XX.md`

---

## 4. 위험 요소 / 미리 알아둘 것

1. **NRD 버전과 DX11 지원**  
   NRD는 DX12/Vulkan 우선 지원. DXBC 경로는 유지되고 있으나, 일부 최신 denoiser(최근 추가분)는 SM5.0으로 컴파일 불가한 셰이더 구문을 쓸 수 있음. 먼저 **REBLUR_DIFFUSE_SPECURAL**과 **REFERENCE**만 활성화해서 DXBC로 빌드되는지 확인.

2. **NRD 리소스 포맷 매핑**  
   NRD의 `nrd::Format`에는 `R16G16B16A16_SFLOAT`, `R10G10B10A2_UNORM` 등이 있는데 전부 DXGI 포맷으로 1:1 매핑됩니다. 단, `R16_SFLOAT` (depth) 같은 항목이 `R16_FLOAT`에 대응하는지 매핑 테이블을 신중히 작성해야 합니다.

3. **히스토리 리셋을 안 하면 ghosting 폭발**  
   `OnResize` / 카메라 워프 때 `accumulationMode` 리셋 빼먹으면 수 초간 잔상이 남습니다.

4. **Motion vector 좌표계 단위**  
   NRD는 기본적으로 **2D screen-pixel MV**를 기대합니다. `CommonSettings::motionVectorScale = {1,1,0}` 이면 pixel. UV로 넣으면 `{w,h,0}`. 실수하면 밝은 유령이 화면을 가로지릅니다. 초기 구현에서는 pixel 단위로 고정.

5. **ViewZ 부호**  
   NRD는 양수/음수 viewZ 모두 지원하지만 `CommonSettings::isMotionVectorInWorldSpace = false` 와 조합해서 `viewZ` 부호를 일관되게 유지해야 합니다. 기존 카메라 매트릭스가 RH 인지 LH 인지 한 번 명시적으로 결정.

6. **Jitter 필수**  
   `CommonSettings::cameraJitter` / `cameraJitterPrev` 를 정확히 넣어야 TAA-like 안정화가 됩니다. 현재 `GenerateCameraRay`에 이미 jitter가 들어가 있으니 그 값을 그대로 NRD에 전달.

7. **Debug 런타임에서 너무 느릴 수 있음**  
   `CMakeLists.txt`가 `MultiThreadedDebugDLL` + `_ITERATOR_DEBUG_LEVEL=2`로 고정되어 있는데, 디노이저 개발 단계에서 60fps를 목표로 한다면 의존성은 Debug로 두되 본 프로젝트를 RelWithDebInfo로 빌드하는 구성을 한 번 더 만드는 게 낫습니다. (`2f5a5bd` 커밋에서 실제로 이런 이슈가 있었음)

---

## 5. 첫 커밋 단위 제안

Phase 0을 한 번에 하지 말고 리뷰 가능한 단위로 쪼개세요:

1. `refactor(pt): accumulation 제거 → 1 spp 직출력` (visual 동일 검증)
2. `feat(pt): Gbuffer(ViewZ/Normal/Roughness) 출력 UAV 추가`  
3. `feat(pt): diffuse/specular 신호 분리 + HitT 출력`
4. `feat(pt): motion vector 계산(prev view-proj 상수버퍼)`
5. `feat(render): Composite pass 도입 (3-pass 파이프라인)`
6. `build: NRD dependency 추가`
7. `feat(denoiser): NrdDenoiser 클래스 + permanent/transient 풀`
8. `feat(denoiser): REBLUR_DIFFUSE_SPECULAR 디스패치 연결`
9. `feat(denoiser): 리셋/정착 트리거 + A/B 토글`

각 단계마다 스크린샷이나 프레임 타임을 찍어두면 어느 단계에서 품질이 어떻게 변했는지 추적하기 좋습니다.
