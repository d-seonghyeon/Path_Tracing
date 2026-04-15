// 공통 구조체 정의
struct Ray {
    float3 origin;
    float3 direction;
};

struct HitRecord {
    float t;
    float3 p;
    float3 normal;
    float3 bary; // 삼각형일 경우 무게 중심 좌표
};

// 1. 구(Sphere) 교차 검사 - 노트 5p (7.8.2) 기반
// 말씀하신 대로 '반지름보다 멀면' 바로 리턴하는 효율적 구조입니다. [cite: 52, 55]
bool IntersectSphere(Ray ray, float3 center, float radius, out HitRecord rec) {

    rec = (HitRecord)0;

    float3 v = ray.origin - center;
    float b = dot(v, ray.direction);
    float c = dot(v, v) - (radius * radius);

    // 조기 탈출: 구가 레이 뒤에 있고 시작점이 구 외부라면 [cite: 52, 55]
    if (c > 0.0f && b > 0.0f) return false;

    float discriminant = b * b - c;
    if (discriminant < 0.0f) return false; // 레이가 비껴감 [cite: 51, 52]

    // 가장 가까운 t 계산
    float t = -b - sqrt(discriminant);
    
    // t가 음수면 구 내부에서 시작한 것이므로 먼 쪽 t 계산 [cite: 108]
    if (t < 0.0f) t = -b + sqrt(discriminant);

    rec.t = t;
    rec.p = ray.origin + t * ray.direction;
    rec.normal = normalize(rec.p - center);
    return true;
}

// 2. 삼각형(Triangle) 교차 검사 - 노트 8~9p 기반
// 수치적 안정성을 위해 작은 오차(EPSILON)를 체크합니다. [cite: 93, 106, 114]
bool IntersectTriangle(Ray ray, float3 v0, float3 v1, float3 v2, out HitRecord rec) {

    rec = (HitRecord)0;

    float3 edge1 = v1 - v0;
    float3 edge2 = v2 - v0;
    float3 normal = cross(edge1, edge2); // 노트 8p 법선 계산 [cite: 110]

    // 1단계: det 계산 (Möller-Trumbore 표준 방식)
    float3 pvec = cross(ray.direction, edge2);
    float det = dot(edge1, pvec);
    
    if (abs(det) < 1e-6f) return false; // 평행할 때 UNSTABLE 처리 
    float invDet = 1.0f / det;
    
    // 2단계: u 파라미터
    float3 tvec = ray.origin - v0;
    float u = dot(tvec, pvec) * invDet;
    if (u < 0.0f || u > 1.0f) return false;
    
    // 3단계: v 파라미터
    float3 qvec = cross(tvec, edge1);
    float v = dot(ray.direction, qvec) * invDet;
    if (v < 0.0f || u + v > 1.0f) return false;
    
    // 4단계: t 파라미터
    float t = dot(edge2, qvec) * invDet;
    if (t < 0.0001f) return false;

    // 5단계: 결과 저장
    rec.t = t;
    rec.p = ray.origin + t * ray.direction;
    rec.normal = normalize(normal);
    rec.bary = float3(1.0f - u - v, u, v); // 무게 중심 좌표
    return true;
}