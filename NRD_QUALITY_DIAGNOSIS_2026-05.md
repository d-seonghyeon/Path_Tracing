# NRD REBLUR 화질 열화 진단 보고서

> 작성일: 2026-05-02  
> 대상 브랜치: `feature/nrd-phase0`  
> 적용 범위: `shader/PathTracer.hlsl`, `src/nrd_denoiser.cpp`, `src/nrd_denoiser.h`

---

## 1. 증상

F1 ON(REBLUR_DIFFUSE_SPECULAR 활성) 상태에서 확인된 화질 문제 세 가지:

| # | 증상 | 위치 |
|---|------|------|
| S1 | 고주파 텍스처 디테일이 과도하게 뭉개짐 (벽면 텍스처, 바닥 체커보드) | 씬 전반 |
| S2 | 바닥 specular 반사가 가로로 길게 번진 얼룩 (lamp 반사가 점광원이 아닌 긴 줄기) | matPuddle (roughness=0.02) |
| S3 | Fireflies 잔존 | 씬 전반 |

---

## 2. 진단 체크리스트

진단 시 점검한 항목 목록 (A–H):

| 항목 | 설명 |
|------|------|
| A | HitT 정규화 — `NrdReblurGetNormHitDist` 파라미터 일치 여부 |
| B | 채널 분배 — diffuse/specular NEE 로브 가중치 적용 여부 |
| C | Sampler 바인딩 — `nrd::Sampler` enum 인덱스 배열 범위 초과 여부 |
| D | Anti-lag 설정 — sigma/sensitivity 과도한 값 여부 |
| E | Prepass blur radius — specular 채널 과대 blur 여부 |
| F | diffuse hitT 누적 방식 — multi-bounce 합산 vs 첫 secondary hit |
| G | 행렬 컨벤션 — GLM column-major → NRD CommonSettings 일치 여부 |
| H | motionVectorScale — 픽셀 단위 MV에 대한 1/W, 1/H 스케일 적용 여부 |

---

## 3. 발견된 버그와 수정

### C1 — Sampler 배열 OOB (범위 초과 UB)

**파일:** `src/nrd_denoiser.h`, `src/nrd_denoiser.cpp`

**원인:**  
`m_samplers[2]`로 선언했으나 `nrd::Sampler` enum 값이  
`NEAREST_CLAMP=0`, `NEAREST_MIRRORED_REPEAT=1`, `LINEAR_CLAMP=2`, `LINEAR_MIRRORED_REPEAT=3`이므로  
enum 값을 직접 인덱스로 사용하면 `m_samplers[2]`는 `LINEAR_CLAMP` 접근 시 배열 범위를 초과 — **Undefined Behavior**.

**증상 연결:** REBLUR 내부 blur pass에서 잘못된 sampler가 바인딩되어 필터링이 깨짐 → 텍스처 디테일 뭉개짐(S1), 번짐(S2) 모두에 기여.

**수정:**
```cpp
// Before
ComPtr<ID3D11SamplerState> m_samplers[2];

// After
ComPtr<ID3D11SamplerState> m_samplers[4]; // indexed by nrd::Sampler enum value
```
4종 sampler 모두 enum 인덱스에 맞게 초기화:

| 인덱스 | enum | Filter | AddressMode |
|--------|------|--------|-------------|
| 0 | NEAREST_CLAMP | POINT | CLAMP |
| 1 | NEAREST_MIRRORED_REPEAT | POINT | MIRROR |
| 2 | LINEAR_CLAMP | LINEAR | CLAMP |
| 3 | LINEAR_MIRRORED_REPEAT | LINEAR | MIRROR |

---

### C4 — diffuse hitT 누적 방식 오류

**파일:** `shader/PathTracer.hlsl`

**원인:**  
```hlsl
// Before — 모든 bounce 거리를 합산
float diffusePathLength = 0.0f;
// bounce loop 내:
diffusePathLength += hit.t;
```
NRD REBLUR은 IN_DIFF_RADIANCE_HITDIST의 `.w`에 **첫 번째 secondary hit까지의 거리**를 요구하지만,  
기존 코드는 multi-bounce 경로 길이를 전부 합산하여 전달 → hitT 과대평가 → blur radius 오산.

**수정:**
```hlsl
// After — 첫 secondary hit 거리만 캡처
float diffuseFirstHitDist = 0.0f;
// bounce loop 내:
if (diffuseFirstHitDist == 0.0f) diffuseFirstHitDist = hit.t;
```
`specularFirstHitDist`와 대칭 구조로 통일.

---

### C5 — specularPrepassBlurRadius 과대

**파일:** `src/nrd_denoiser.cpp`

**원인:**  
`specularPrepassBlurRadius = 28.0f` — matPuddle(roughness=0.02)처럼 거의 미러에 가까운 면에서  
prepass가 넓은 반경으로 먼저 blur 처리 → 반사가 가로로 번진 얼룩(S2)의 주요 원인.

**수정:**
```cpp
reblurSettings.specularPrepassBlurRadius = 12.0f; // was 28.0f
```

**효과:** S2(가로 번짐) 증상의 핵심 수정. roughness가 낮은 specular 면에서 prepass가 적절히 좁아짐.

---

## 4. C2 오진 및 복원

### 최초 진단 (오진)

**가설:** bounce=0 NEE를 specular 채널에 분배하면 REBLUR specular temporal lobe와  
방향 불일치가 생겨 가로 번짐(S2)이 발생한다.

**적용한 수정:**
```hlsl
// C2 (잘못된 수정)
if (bounce == 0) {
    result.diffuse += neeContrib;  // 전량 diffuse로
    UpdateRepresentativeHitDistance(diffuseHitDist, diffuseHitWeight, neeContrib, lightHitDist);
}
```

### 왜 틀렸나

씬 소재 matPuddle의 특성:

| 속성 | 값 |
|------|----|
| roughness | 0.02 |
| metallic | 0.0 |
| lobe.pSpec | ≈ 1.0 (거의 전량 specular) |
| lobe.pDiff | ≈ 0.0 |

matPuddle에서의 lamp NEE 기여도(`neeContrib`)는 **거의 100% specular** 로브에 속한다.  
C2를 적용하면 이 기여도가 `diffuse` 채널(maxBlurRadius=18 최대 blur)로 들어가므로  
바닥의 lamp 반사가 **넓은 soft blob**으로 출력된다.

실제로 S2(가로 번짐)를 유발한 원인은 C2가 아니라 **C5 대상인 prepass blur radius**였다.

### 복원 (현재 상태)

```hlsl
// 복원된 lobe-weighted NEE split
if (bounce == 0) {
    float3 diffuseContrib  = neeContrib * lobe.pDiff;
    float3 specularContrib = neeContrib * lobe.pSpec;
    result.diffuse  += diffuseContrib;
    result.specular += specularContrib;
    UpdateRepresentativeHitDistance(diffuseHitDist,  diffuseHitWeight,  diffuseContrib,  lightHitDist);
    UpdateRepresentativeHitDistance(specularHitDist, specularHitWeight, specularContrib, lightHitDist);
}
```

- matPuddle(pSpec≈1.0): NEE → specular 채널 → REBLUR tight blur (roughness=0.02 기반) → 점광원 반사
- matRoad(pDiff≈1.0): NEE → diffuse 채널 → REBLUR diffuse blur → 정상

---

## 5. 최종 커밋 이력

| 커밋 | 내용 |
|------|------|
| `8450ef1` | C1: nrd_denoiser — sampler 배열 OOB 수정 |
| `f08a68f` | C4: PathTracer — diffuse hitT를 첫 secondary hit로 수정 |
| `8655c62` | C2: PathTracer — bounce=0 NEE 전량 diffuse (← 오진, 후에 복원) |
| `4ca5441` | C5: nrd_denoiser — specularPrepassBlurRadius 28→12 |
| `99d1293` | **C2 복원:** bounce=0 NEE lobe-weighted split 재적용 |

---

## 6. 현재 적용 중인 REBLUR 설정

> 마지막 갱신: 2026-05-02 (P5-2 blur-radius sweep 완료 후)

```cpp
// src/nrd_denoiser.cpp — 2026-05-02 P5-2 기준 최종 상태
// HLSL 미러: shader/PathTracer.hlsl REBLUR_HIT_DIST_PARAMS = float4(30,0.1,20,-25)
// A 값을 변경할 경우 반드시 C++·HLSL 양쪽 동시 수정 (bit-identical 유지 필수)
reblurSettings.hitDistanceParameters.A  = 30.0f;  // 3→30: 씬 스케일 83m 대응
reblurSettings.hitDistanceParameters.B  = 0.1f;
reblurSettings.hitDistanceParameters.C  = 20.0f;
reblurSettings.hitDistanceParameters.D  = -25.0f;
reblurSettings.antilagSettings.luminanceSigmaScale   = 3.5f;
reblurSettings.antilagSettings.luminanceSensitivity  = 2.5f;
reblurSettings.maxAccumulatedFrameNum                = 24;
reblurSettings.maxFastAccumulatedFrameNum            = 4;
reblurSettings.maxStabilizedFrameNum                 = 30;   // 0→30: temporal stabilization 재활성화
reblurSettings.historyFixBasePixelStride             = 8;
reblurSettings.diffusePrepassBlurRadius              = 6.0f; // 8→6 (P5-2 sweep 채택)
reblurSettings.specularPrepassBlurRadius             = 6.0f; // 8→6 (P5-2 sweep 채택)
reblurSettings.minBlurRadius                         = 0.5f;
reblurSettings.maxBlurRadius                         = 9.0f; // 12→9 (P5-2 sweep 채택)
reblurSettings.minHitDistanceWeight                  = 0.10f;
reblurSettings.lobeAngleFraction                     = 0.25f;
reblurSettings.roughnessFraction                     = 0.25f;
reblurSettings.planeDistanceSensitivity              = 0.08f;
```

#### P5-2 blur-radius sweep 결과 요약

| prepass | maxBlur | 근거리 체커 (~5m) | 벽 노이즈 | 판정 |
|---------|---------|-------------------|-----------|------|
| 8       | 12      | 소실              | 깨끗      | 기각 |
| 4       | 6       | 명확 보존         | grain 잔존 | 과소 |
| **6**   | **9**   | **보존** ✓        | **깨끗** ✓ | **채택** |

2m × 2m 체커 보존 임계: **prepass ≤ 6 / maxBlur ≤ 9**.

---

## 7. 검증 방법

런타임 확인 순서:

1. 빌드 후 실행
2. `F1` → Denoise ON
3. `F2` → 스크린샷 캡처 (`capture_N_denoised.png`)
4. 바닥 puddle 영역 확인:
   - **기대 결과:** lamp 반사가 작고 선명한 점광원 형태
   - **C2 오진 상태:** lamp 반사가 넓은 soft blob
5. 체커보드 바닥, 벽면 텍스처 디테일 확인 (C1/C4 효과)

---

## 7. P3-5 추가 수정 이력 (2026-05-02)

C1~C5 이후, 사용자 시각 확인 결과 여전히 watercolor blur 잔존 → 추가 진단.

| 커밋 | 내용 | 효과 |
|------|------|------|
| `9352bdf` | hitDistanceParameters.A 3→30 + HLSL 동기화 | 씬 스케일 미스매치 해소 |
| `afebcca` | maxStabilizedFrameNum 0→30 | temporal stabilization 재활성화 |
| `982f1d6` | maxBlurRadius 18→12 | spatial blur 반경 축소 |
| `dc8ce78` | prepass 16/12→8/8, planeDistSens 0.025→0.08, lobeFraction 0.16→0.25 | prepass 기여분 감소 |
| `c93e521` | **GenerateCameraRay 서브픽셀 지터 제거 (ROOT CAUSE)** | 체커/경계 watercolor의 실제 원인 |

### 워터컬러 blur의 실제 메커니즘

```
GenerateCameraRay: jitter = GetRandomSamples(pixelCoord, 0, frameCount) - 0.5f
→ 매 프레임 ±0.5px 랜덤 오프셋
→ maxAccumulatedFrameNum=24: 체커 경계 픽셀이 흰 타일/검 타일 양쪽 누적
→ 24프레임 가중평균 = 회색 → watercolor
→ REBLUR가 이 신호 변동을 "노이즈"로 해석 → spatial blur 추가 악화
```

REBLUR는 `CommonSettings.cameraJitter[]` 필드가 있으나, 우리 NRD v4.14.3 경로에서는
motion vector에 jitter offset이 반영되지 않아 보상이 없음.

**수정**: jitter 제거 → 매 프레임 동일 픽셀 위치 샘플링. REBLUR 자체 temporal accumulation으로 수렴.

---

## 8. 남은 과제

| # | 항목 | 우선순위 | 상태 |
|---|------|---------|------|
| R1 | 지터 제거 후 F2 캡처로 체커/벽면 선명도 확인 | 필수 | ✅ 완료 (P3-5, P5-1) |
| R2 | 카메라 이동 고스팅 프로브 (D키 0.5s → 즉시/4s 캡처 비교) | 필수 | ✅ 완료 (P3-5, P4) |
| R3 | 기하학 에지 aliasing 허용 수준 판단 → Halton jitter + `CommonSettings.cameraJitter` | 조건부 | 미착수 |
| R4 | 어두운 건물 측면 노이즈 — `maxFastAccumulatedFrameNum` 4→2 | 선택 | ✅ 시도 후 기각 (P3-6) |
| R5 | F1=OFF / F1=ON FLIP/SSIM 정량 비교 | 선택 | ✅ 완료 (P4-2~P4-5) |
| R6 | 씬 체커 baseline 확보 및 blur-radius sweep | 필수 | ✅ 완료 (P5-1, P5-2) |
