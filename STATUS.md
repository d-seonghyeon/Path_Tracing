# STATUS — NRD Integration

> 이 문서는 Codex / Claude Code 두 툴이 **같은 상태를 공유**하기 위한 단일 소스(single source of truth)다.
> 세션을 시작할 때 맨 먼저 읽고, **세션을 끝낼 때 반드시 갱신**한다.
> 커밋·푸시 후에는 "마지막 커밋" 칸을 최신 해시로 바꾼다.

---
### [비상 인수인계] 2026-04-17 토큰 고갈로 인한 세션 중단
- **현재 위치:** `feature/nrd-phase0` 브랜치
- **중단 시점:** Phase 0 작업 중 중단됨. (예: PathTracer.hlsl 누적 로직 제거까지는 했으나 C++ UAV 바인딩 중에 끊김)
- **코드 상태:** 현재 코드는 미완성 상태이며 "WIP: Phase 0 작업 중단" 메시지로 커밋(또는 stash)되어 있음.
- **다음 AI의 첫 번째 임무:** 1. 현재 코드를 분석하여 미완성된 부분을 파악할 것.
  2. 코드를 마저 완성하고 컴파일이 가능한 상태로 복구할 것.

## 0. 현재 단계

- **활성 단계**: `Phase 0 — 구현 완료 / 빌드 검증 완료`
- **상세 서브 단계**: `P0-1 · G-buffer 7종 + Composite 3-pass 파이프라인 연결`
- **차단(block) 상태**: 없음
- **브랜치**: `feature/nrd-phase0`

### Phase 진행 체크리스트

> 체크박스 앞의 태그 `[C]` = Claude Code 주 작업, `[X]` = Codex 주 작업, `[R]` = 리뷰만.

Phase 0 — 렌더 파이프라인 재정리 (NRD 전제조건)

- [ ] `[C]` `PathTracer.hlsl` 누적 모델 제거 + `g_accum += ...` 로직 제거
- [ ] `[C]` `Tonemap.hlsl` 에서 `/(frameCount+1)` 나눗셈 제거
- [ ] `[C]` `TracePath` 를 `diffuse` / `specular` radiance 로 분리 반환
- [ ] `[C]` G-buffer UAV 7개 (u0~u6) `context.h` / `context.cpp` 추가
- [ ] `[X]` `GlobalUB` 에 `prevViewProj` / `currViewProj` 추가 + C++ 사이드 업로드
- [ ] `[X]` Motion vector 계산 셰이더 라이트 패스 (또는 PathTracer 출력에 포함)
- [ ] `[C]` `Composite.hlsl` 신규 — diffuse*albedo + specular + emissive 합성
- [ ] `[R]` Codex 가 Claude 의 위 커밋들을 diff 리뷰

Phase 1 — NRD 의존성 통합

- [ ] `[X]` `Dependency.cmake` 에 `dep_nrd` (ExternalProject_Add, `v4.13.3`, DXBC embed)
- [ ] `[X]` `CMakeLists.txt` 링크·include 경로 추가
- [ ] `[R]` Claude 가 CMake diff 를 리뷰 (정적 링크·런타임 일관성)

Phase 2 — NrdDenoiser 래퍼 + DXBC 파이프라인

- [ ] `[X]` `src/nrd_denoiser.{h,cpp}` 스캐폴딩 (permanent / transient pool, identifier)
- [ ] `[X]` NRD `PipelineDesc` → `ID3D11ComputeShader` 빌더
- [ ] `[C]` DX11 바인딩 테이블 매핑 리뷰 (slot 충돌 · 리소스 수명)

Phase 3 — 품질 튜닝 / 선택 알고리즘

- [ ] `[C]` hitT 정규화 · NRD helper (`REBLUR_FrontEnd_PackRadianceAndNormHitDist`)
- [ ] `[C]` 안티래그 / disocclusion 임계값 스윕
- [ ] (optional) `[X]` SIGMA 그림자 · `[X]` ReLAX 대안 실험

Phase 4 — 검증 · A/B

- [ ] `[C]` A/B 토글 (`F1` = denoise on/off)
- [ ] `[C]` FLIP / SSIM 스크립트 (오프라인 비교)
- [ ] `[X]` Timestamp query 로 경로추적 vs. 디노이즈 비용 측정

---

## 1. 마지막 커밋

| 항목 | 값 |
| --- | --- |
| Hash | `(이 세션의 P0 커밋 직후 갱신 예정)` |
| Author | Codex |
| Date | 2026-04-17 |
| Scope | `P0` Phase 0 마무리 — G-buffer / motion vector / Composite 연결 |
| 요약 | PathTracer → Composite → Tonemap 3-pass 경로 및 Phase 0 G-buffer 생성 완료 |

> 매 세션 종료 시 `git log -1 --pretty=format:"%h %an %ad %s"` 결과를 여기에 붙여 넣는다.

---

## 2. 다음 구체적 행동 (next concrete action)

**지금 해야 할 한 가지만 적는다.** 애매한 "계속 진행" 금지.

```
[1] Phase 1 시작: `Dependency.cmake` 에 NRD `v4.13.3` ExternalProject_Add 추가
[2] `CMakeLists.txt` 에 NRD include / link 경로 연결
[3] NRI 없이 DXBC embed 경로로 `src/nrd_denoiser.{h,cpp}` 스캐폴딩 시작
```

담당: Codex.
Claude 는 이 P0 diff 를 리뷰하거나 Phase 1 착수 전 슬롯/수명 매핑을 점검.

---

## 3. 교차 세션 노트 (cross-session notes)

> 한 툴이 추가·변경한 심볼 중 **다른 툴이 모를 수 있는 것**만 기록한다.
> 추가·제거·리네임 모두 기록. 커밋이 머지되면 해당 줄을 제거해도 좋다.

### 새로 생긴 것

- `shader/Composite.hlsl` — `diffuse * albedo + specular + emissive` 합성 CS 패스
- `GlobalUniforms.prevViewProj` / `currViewProj` — motion vector 계산용 view-proj 행렬
- `Context` 화면 리소스: G-buffer 7종 + `m_compositeTexture`

### 이름이 바뀐 것

- (아직 없음)

### 제거된 것

- (아직 없음)

### 보류·미결 결정

- **NRI 도입 여부**: 도입하지 **않음**. DXBC 바이트코드를 직접 임베드 후 `ID3D11Device::CreateComputeShader` 로 생성. (AGENTS.md 참조)
- **첫 디노이저**: `REBLUR_DIFFUSE_SPECULAR`. SIGMA · ReLAX 는 Phase 3 옵션.
- **ViewZ 부호**: `+Z` 전진(linearized depth, 양수). (AGENTS.md 좌표계 섹션 참조)
- **Motion vector 표현**: "이전 프레임 위치 − 현재 프레임 위치" **픽셀 단위** (NRD `isMotionVectorInWorldSpace=false`).
- **행렬 업로드 결정**: HLSL `row_major` 유지. C++ (`glm` column-major) 에서 `glm::transpose(prev/currViewProj)` 후 상수버퍼 업로드.

---

## 4. 열린 질문 (open questions)

> 결정되지 않은 채 두 툴이 각각 다르게 해석할 위험이 있는 항목.

1. Denoise 입력 포맷 `R16G16B16A16_FLOAT` 고정 여부 → 일단 고정, 품질 이슈 발견 시 `R32` 로 승격.
2. 리사이즈 시 NRD `AccumulationMode::CLEAR_AND_RESTART` 호출 타이밍 → `OnResize` 훅에 연결.
3. 현재 빌드는 Codex 셸의 `Path` / `PATH` 충돌을 피하기 위해 clean-environment 우회로 검증함. 일반 빌드 경로 정리는 별도 환경 이슈.

---

## 5. 세션 로그 (session log)

> 새 항목을 **위**에 추가한다 (역시간순). 형식:
> `YYYY-MM-DD HH:MM  |  <tool>  |  <phase>  |  <요약 1줄>`

```
2026-04-17 20:02  |  Codex       |  P0-1  |  G-buffer 7종, prev/curr viewProj, motion vector, Composite 3-pass 연결 후 clean-env 빌드 성공
2026-04-17        |  Claude Code |  P0-0  |  STATUS.md / AGENTS.md 초안 작성
```

---

## 6. 갱신 규칙 (중요)

1. **세션 시작**: STATUS.md → AGENTS.md 순서로 읽는다. "다음 구체적 행동"과 어긋나면 중지하고 사용자에게 물어본다.
2. **세션 종료**: 작업 요약을 세션 로그에 추가하고, "다음 구체적 행동"을 새로 덮어쓴다. "마지막 커밋" 표를 최신화한다.
3. **충돌**: 같은 섹션을 두 툴이 동시에 편집하지 않는다. STATUS 갱신은 직전 커밋을 기준으로 병합한다.
4. **체크박스**: 커밋이 `main` 에 머지된 뒤에만 체크한다. 로컬 커밋 단계에서는 체크하지 않는다.

