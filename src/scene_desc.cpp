#include "scene_desc.h"

// -------------------------------------------------------
// MakeCityScene — 씬 데이터 정의 (지오메트리 생성 없음)
// 새 구조물을 추가하려면 이 함수만 수정하면 됩니다.
// -------------------------------------------------------
SceneDesc MakeCityScene() {
    SceneDesc desc;

    // ---- 재질 정의 ----
    GpuMaterial matRoad{};
    matRoad.albedo    = glm::vec3(0.07f, 0.06f, 0.05f);
    matRoad.roughness = 1.0f;

    GpuMaterial matSide{};
    matSide.albedo    = glm::vec3(0.65f, 0.62f, 0.58f);
    matSide.roughness = 0.90f;

    GpuMaterial matBrick{};
    matBrick.albedo    = glm::vec3(0.65f, 0.30f, 0.20f);
    matBrick.roughness = 0.85f;

    GpuMaterial matConcrete{};
    matConcrete.albedo    = glm::vec3(0.55f, 0.55f, 0.55f);
    matConcrete.roughness = 0.90f;

    GpuMaterial matDarkBld{};
    matDarkBld.albedo    = glm::vec3(0.10f, 0.10f, 0.12f);
    matDarkBld.roughness = 0.70f;

    GpuMaterial matBeige{};
    matBeige.albedo    = glm::vec3(0.88f, 0.80f, 0.62f);
    matBeige.roughness = 0.88f;

    GpuMaterial matPuddle{};
    matPuddle.albedo    = glm::vec3(0.03f, 0.04f, 0.06f);
    matPuddle.roughness = 0.02f;
    matPuddle.metallic  = 0.0f;

    GpuMaterial matWinWarm{};
    matWinWarm.albedo    = glm::vec3(1.0f, 0.9f, 0.6f);
    matWinWarm.roughness = 1.0f;
    matWinWarm.emissive  = glm::vec3(6.0f, 4.5f, 1.8f);

    GpuMaterial matWinCool1{};
    matWinCool1.albedo    = glm::vec3(0.7f, 0.85f, 1.0f);
    matWinCool1.roughness = 1.0f;
    matWinCool1.emissive  = glm::vec3(2.5f, 3.0f, 5.5f);

    GpuMaterial matWinCool2{};
    matWinCool2.albedo    = glm::vec3(0.8f, 0.9f, 1.0f);
    matWinCool2.roughness = 1.0f;
    matWinCool2.emissive  = glm::vec3(3.5f, 4.5f, 6.0f);

    GpuMaterial matWinDark{};
    matWinDark.albedo    = glm::vec3(0.04f, 0.04f, 0.05f);
    matWinDark.roughness = 0.05f;
    matWinDark.metallic  = 0.9f;

    GpuMaterial matPole{};
    matPole.albedo    = glm::vec3(0.18f, 0.18f, 0.20f);
    matPole.roughness = 0.50f;
    matPole.metallic  = 0.80f;

    GpuMaterial matHead{};
    matHead.albedo    = glm::vec3(1.0f, 0.95f, 0.70f);
    matHead.roughness = 1.0f;
    matHead.emissive  = glm::vec3(18.0f, 14.0f, 6.5f);

    GpuMaterial matGlass{};
    matGlass.albedo    = glm::vec3(0.95f, 0.90f, 0.75f);
    matGlass.roughness = 0.20f;
    matGlass.emissive  = glm::vec3(12.0f, 9.5f, 4.5f);

    // 나무 재질
    GpuMaterial matTrunk{};
    matTrunk.albedo    = glm::vec3(0.30f, 0.18f, 0.08f);  // 어두운 갈색
    matTrunk.roughness = 0.95f;

    GpuMaterial matLeafLow{};
    matLeafLow.albedo    = glm::vec3(0.10f, 0.28f, 0.08f);  // 짙은 초록
    matLeafLow.roughness = 0.90f;

    GpuMaterial matLeafHigh{};
    matLeafHigh.albedo    = glm::vec3(0.15f, 0.38f, 0.10f);  // 밝은 초록
    matLeafHigh.roughness = 0.85f;

    // -------------------------------------------------------
    // 도로 (인도는 제거 — Scene.hlsli의 체커 plane이 인도 영역을 담당)
    //
    // 선택지 B 적용:
    //   sidewalk 박스 2개를 제거해서 Scene.hlsli의 y=0 체커 plane이
    //   인도 영역에 그대로 노출되도록 함. 도로 박스는 그대로 둬서
    //   검정 도로 vs 체커 인도의 대비가 명확해지고, NRD가 평면 위
    //   고주파 디테일을 보존하는지 시각적으로 검증 가능.
    // -------------------------------------------------------
    desc.boxes.push_back({{-4.0f,-0.05f,-10.0f}, { 4.0f, 0.00f, 35.0f}, matRoad});

    // -------------------------------------------------------
    // 물웅덩이 (도로면보다 0.5mm 위)
    // -------------------------------------------------------
    desc.quads.push_back({
        {-3.0f, 0.0005f,  2.0f},
        { 3.0f, 0.0005f,  2.0f},
        { 3.0f, 0.0005f, 16.0f},
        {-3.0f, 0.0005f, 16.0f},
        {0.0f, 1.0f, 0.0f},
        matPuddle
    });

    // -------------------------------------------------------
    // 나무 (웅덩이 안, x=0 z=8 중심)
    // 새 구조물 추가는 여기처럼 desc.boxes.push_back() 만 하면 됩니다.
    // FlattenScene()과 BVH 빌드는 BuildSceneBuffers()가 자동으로 처리합니다.
    // -------------------------------------------------------
    desc.boxes.push_back({{-0.10f, 0.0f, 7.90f}, { 0.10f, 1.2f,  8.10f}, matTrunk});   // 기둥
    desc.boxes.push_back({{-0.55f, 0.9f, 7.45f}, { 0.55f, 2.0f,  8.55f}, matLeafLow}); // 하단 수관
    desc.boxes.push_back({{-0.30f, 1.8f, 7.70f}, { 0.30f, 3.0f,  8.30f}, matLeafHigh});// 상단 수관

    // -------------------------------------------------------
    // 건물 4채
    // -------------------------------------------------------
    desc.boxes.push_back({{-13.0f, 0.0f,  0.0f}, {-6.0f, 21.0f, 12.0f}, matBrick});    // A: 7층
    desc.boxes.push_back({{-13.0f, 0.0f, 14.0f}, {-6.0f, 24.0f, 26.0f}, matConcrete}); // B: 8층
    desc.boxes.push_back({{  6.0f, 0.0f,  0.0f}, {13.0f, 15.0f, 12.0f}, matDarkBld});  // C: 5층
    desc.boxes.push_back({{  6.0f, 0.0f, 14.0f}, {13.0f, 27.0f, 26.0f}, matBeige});    // D: 9층

    // -------------------------------------------------------
    // 창문
    // -------------------------------------------------------
    struct BWin { float faceX, nX, zS, zE; int floors; };
    const BWin bwins[] = {
        {-6.0f,  1.0f,  0.0f, 12.0f, 7},  // A
        {-6.0f,  1.0f, 14.0f, 26.0f, 8},  // B
        { 6.0f, -1.0f,  0.0f, 12.0f, 5},  // C
        { 6.0f, -1.0f, 14.0f, 26.0f, 9},  // D
    };
    // 패턴: 0=warm, 1=cool1, 2=cool2, 3=dark
    const int PAT[] = {0,0,3, 0,2,0, 3,0,1, 0,1,0, 3,0,0, 0,2,3, 1,0,0, 0,3,1, 0,0,2};
    const int   WPF  = 3;     // 층당 창문 수
    const float WH   = 1.4f;  // 창 높이
    const float WZ   = 1.5f;  // 창 폭 (Z축)
    const float FH   = 3.0f;  // 층 높이

    int pi = 0;
    for (const auto& bw : bwins) {
        float zStep = (bw.zE - bw.zS) / WPF;
        float fx    = bw.faceX + bw.nX * 0.01f;
        glm::vec3 n(bw.nX, 0.0f, 0.0f);

        for (int fl = 0; fl < bw.floors; ++fl) {
            float yB = fl * FH + 0.8f;
            float yT = yB + WH;
            for (int w = 0; w < WPF; ++w) {
                float zC = bw.zS + (w + 0.5f) * zStep;
                float z0 = zC - WZ * 0.5f;
                float z1 = zC + WZ * 0.5f;

                GpuMaterial mat;
                switch (PAT[pi++ % 9]) {
                    case 0:  mat = matWinWarm;  break;
                    case 1:  mat = matWinCool1; break;
                    case 2:  mat = matWinCool2; break;
                    default: mat = matWinDark;  break;
                }
                desc.quads.push_back({
                    {fx, yB, z0}, {fx, yB, z1},
                    {fx, yT, z1}, {fx, yT, z0},
                    n, mat
                });
            }
        }
    }

    // -------------------------------------------------------
    // 가로등 5쌍 (기둥/팔/헤드/유리 박스 + NEE 광원)
    // -------------------------------------------------------
    const float SL_Z[]  = {2.0f, 8.0f, 14.0f, 20.0f, 26.0f};
    const float sides[] = {-1.0f, 1.0f};

    for (float z : SL_Z) {
        for (float s : sides) {
            float px  = s * 5.0f;
            float arm = px - s * 1.8f;
            float ax0 = glm::min(px, arm);
            float ax1 = glm::max(px, arm);

            // 기둥
            desc.boxes.push_back({{px-0.10f, 0.0f, z-0.10f}, {px+0.10f, 3.5f, z+0.10f}, matPole});
            // 수평 팔
            desc.boxes.push_back({{ax0, 3.40f, z-0.05f}, {ax1, 3.55f, z+0.05f}, matPole});
            // 헤드
            desc.boxes.push_back({{arm-0.30f, 3.30f, z-0.18f}, {arm+0.30f, 3.50f, z+0.18f}, matHead});
            // 유리
            desc.boxes.push_back({{arm-0.25f, 3.10f, z-0.15f}, {arm+0.25f, 3.30f, z+0.15f}, matGlass});

            // NEE 광원 (2단계에서 GPU 버퍼로 연결)
            // 기존:
            // desc.lights.push_back({
            //     {arm, 3.3f, z},
            //     0.5f,
            //     {3.5f, 2.7f, 1.3f}
            // });

            // 변경:
            desc.lights.push_back(
                LightDesc::MakeSphere({arm, 3.3f, z}, 0.5f, {18.0f, 14.0f, 6.5f})
            );
        }
    }

    return desc;
}
