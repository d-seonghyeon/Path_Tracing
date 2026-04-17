# STATUS — NRD Integration

> 이 문서는 Codex / Claude Code 두 툴이 **같은 상태를 공유**하기 위한 단일 소스(single source of truth)다.
> 세션을 시작할 때 맨 먼저 읽고, **세션을 끝낼 때 반드시 갱신**한다.
> 커밋·푸시 후에는 "마지막 커밋" 칸을 최신 해시로 바꾼다.

---

## 0. 현재 단계

- **활성 단계**: `Phase 0 — 시작 전`
- **상세 서브 단계**: `P0-0 · 공용 문서(STATUS / AGENTS) 작성`
- **차단(block) 상태**: 없음
- **브랜치**: `main` (NRD 작업 시작 시 `feature/nrd-phase0`로 분기 예정)

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
| Hash | `(채워지지 않음 — 첫 NRD 커밋 후 갱신)` |
| Author | — |
| Date | — |
| Scope | — |
| 요약 | — |

> 매 세션 종료 시 `git log -1 --pretty=format:"%h %an %ad %s"` 결과를 여기에 붙여 넣는다.

---

## 2. 다음 구체적 행동 (next concrete action)

**지금 해야 할 한 가지만 적는다.** 애매한 "계속 진행" 금지.

```
[1] feature/nrd-phase0 브랜치 생성
[2] PathTracer.hlsl 의 g_accum 누적 로직을 "per-frame overwrite" 로 바꾸고
    diffuse / specular 를 분리한 float4×2 출력으로 전환
[3] 동일 변경에 맞춰 Tonemap.hlsl 에서 /(frameCount+1) 제거, 입력은 Composite 결과로 교체
```

담당: Claude Code.
Codex 는 이 커밋이 푸시될 때까지 대기 (또는 Phase 1 dep_nrd 패치 사전 조사).

---

## 3. 교차 세션 노트 (cross-session notes)

> 한 툴이 추가·변경한 심볼 중 **다른 툴이 모를 수 있는 것**만 기록한다.
> 추가·제거·리네임 모두 기록. 커밋이 머지되면 해당 줄을 제거해도 좋다.

### 새로 생긴 것

- (아직 없음)

### 이름이 바뀐 것

- (아직 없음)

### 제거된 것

- (아직 없음)

### 보류·미결 결정

- **NRI 도입 여부**: 도입하지 **않음**. DXBC 바이트코드를 직접 임베드 후 `ID3D11Device::CreateComputeShader` 로 생성. (AGENTS.md 참조)
- **첫 디노이저**: `REBLUR_DIFFUSE_SPECULAR`. SIGMA · ReLAX 는 Phase 3 옵션.
- **ViewZ 부호**: `+Z` 전진(linearized depth, 양수). (AGENTS.md 좌표계 섹션 참조)
- **Motion vector 표현**: "이전 프레임 위치 − 현재 프레임 위치" **픽셀 단위** (NRD `isMotionVectorInWorldSpace=false`).

---

## 4. 열린 질문 (open questions)

> 결정되지 않은 채 두 툴이 각각 다르게 해석할 위험이 있는 항목.

1. Composite 를 별도 CS 패스로 둘지, PathTracer 의 끝에 같이 처리할지 → **별도 CS 로 결정**(이유: denoise 이후 합성 위치가 명확해야 함).
2. Denoise 입력 포맷 `R16G16B16A16_FLOAT` 고정 여부 → 일단 고정, 품질 이슈 발견 시 `R32` 로 승격.
3. 리사이즈 시 NRD `AccumulationMode::CLEAR_AND_RESTART` 호출 타이밍 → `OnResize` 훅에 연결.

---

## 5. 세션 로그 (session log)

> 새 항목을 **위**에 추가한다 (역시간순). 형식:
> `YYYY-MM-DD HH:MM  |  <tool>  |  <phase>  |  <요약 1줄>`

```
2026-04-17        |  Claude Code |  P0-0  |  STATUS.md / AGENTS.md 초안 작성
```

---

## 6. 갱신 규칙 (중요)

1. **세션 시작**: STATUS.md → AGENTS.md 순서로 읽는다. "다음 구체적 행동"과 어긋나면 중지하고 사용자에게 물어본다.
2. **세션 종료**: 작업 요약을 세션 로그에 추가하고, "다음 구체적 행동"을 새로 덮어쓴다. "마지막 커밋" 표를 최신화한다.
3. **충돌**: 같은 섹션을 두 툴이 동시에 편집하지 않는다. STATUS 갱신은 직전 커밋을 기준으로 병합한다.
4. **체크박스**: 커밋이 `main` 에 머지된 뒤에만 체크한다. 로컬 커밋 단계에서는 체크하지 않는다.
