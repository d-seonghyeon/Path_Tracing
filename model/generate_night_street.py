"""
Night Street Scene Generator
=============================
가로등이 있는 야경 거리 .obj + .mtl 자동 생성

씬 구성:
  - 도로 (Road)              : 어두운 아스팔트
  - 인도 (Sidewalk) x2       : 양쪽 보도블럭
  - 건물 4채                  : 양쪽에 2채씩, 높이 다양
  - 창문 (켜진/꺼진 혼합)      : emissive로 실내 조명 표현
  - 가로등 기둥 x6            : 인도 위에 일정 간격 배치
  - 가로등 헤드               : emissive (노란빛)
  - 지면                      : 건물 하단 기반

좌표 단위: 1u ≈ 1m
Y축 = 거리 방향 (앞뒤)
X축 = 폭 방향 (좌우)
Z축 = 높이
"""

import os, math

OUTPUT_DIR = "./model"
OBJ_FILE   = "night_street.obj"
MTL_FILE   = "night_street.mtl"

# ─────────────────────────────────────────
# 재질  (Ke = emissive → GpuMaterial.emissive)
# ─────────────────────────────────────────
MATERIALS = {
    # 이름              albedo(Kd)            spec(Ks)  rough  emissive(Ke)
    "road":         ("0.18 0.18 0.18", "0.0", "0.95", "0.00 0.00 0.00"),
    "sidewalk":     ("0.45 0.43 0.40", "0.0", "0.90", "0.00 0.00 0.00"),
    "ground":       ("0.30 0.28 0.26", "0.0", "1.00", "0.00 0.00 0.00"),
    # 건물 벽
    "wall_brick":   ("0.72 0.46 0.32", "0.0", "0.88", "0.00 0.00 0.00"),
    "wall_concrete":("0.52 0.54 0.56", "0.0", "0.82", "0.00 0.00 0.00"),
    "wall_dark":    ("0.28 0.30 0.33", "0.1", "0.65", "0.00 0.00 0.00"),
    "wall_beige":   ("0.78 0.72 0.60", "0.0", "0.85", "0.00 0.00 0.00"),
    "roof":         ("0.22 0.22 0.24", "0.0", "0.95", "0.00 0.00 0.00"),
    # 창문
    "win_on_warm":  ("1.00 0.88 0.55", "0.0", "0.05", "2.00 1.60 0.80"),  # 따뜻한 실내등
    "win_on_cool":  ("0.70 0.85 1.00", "0.0", "0.05", "1.20 1.50 2.00"),  # 차가운 형광등
    "win_off":      ("0.12 0.16 0.22", "0.8", "0.02", "0.00 0.00 0.00"),  # 꺼진 창문
    # 가로등
    "lamp_pole":    ("0.35 0.35 0.38", "0.3", "0.40", "0.00 0.00 0.00"),  # 금속 기둥
    "lamp_head":    ("0.90 0.85 0.50", "0.2", "0.20", "3.50 2.80 1.20"),  # 강한 발광
    "lamp_glass":   ("1.00 0.95 0.70", "0.5", "0.05", "2.50 2.00 0.90"),  # 등 유리
}


class ObjBuilder:
    def __init__(self):
        self.verts  = []   # (x,y,z)
        self.norms  = []   # (nx,ny,nz)
        self.faces  = []   # (mat, [(vi,ni)...])

    # ── 기본 quad (삼각형 2개) ──────────────────
    def quad(self, v0,v1,v2,v3, n, mat):
        bv = len(self.verts)
        bn = len(self.norms)
        for v in [v0,v1,v2,v3]:
            self.verts.append(v)
        self.norms.append(n)
        ni = bn + 1
        for tri in [(0,1,2),(0,2,3)]:
            self.faces.append((mat, [(bv+t+1, ni) for t in tri]))

    # ── 박스 (cx,cy = 수평 중심, cz = 하단) ────
    def box(self, cx,cy,cz, w,d,h, mat_wall, mat_roof=None):
        mr = mat_roof or mat_wall
        hx,hd = w/2, d/2
        # front(-Y), back(+Y), left(-X), right(+X), top
        self.quad((cx-hx,cy-hd,cz),   (cx+hx,cy-hd,cz),
                  (cx+hx,cy-hd,cz+h), (cx-hx,cy-hd,cz+h), (0,-1,0), mat_wall)
        self.quad((cx+hx,cy+hd,cz),   (cx-hx,cy+hd,cz),
                  (cx-hx,cy+hd,cz+h), (cx+hx,cy+hd,cz+h), (0,1,0),  mat_wall)
        self.quad((cx-hx,cy+hd,cz),   (cx-hx,cy-hd,cz),
                  (cx-hx,cy-hd,cz+h), (cx-hx,cy+hd,cz+h), (-1,0,0), mat_wall)
        self.quad((cx+hx,cy-hd,cz),   (cx+hx,cy+hd,cz),
                  (cx+hx,cy+hd,cz+h), (cx+hx,cy-hd,cz+h), (1,0,0),  mat_wall)
        self.quad((cx-hx,cy-hd,cz+h), (cx+hx,cy-hd,cz+h),
                  (cx+hx,cy+hd,cz+h), (cx-hx,cy+hd,cz+h), (0,0,1),  mr)

    # ── 수평 평면 ───────────────────────────────
    def plane(self, cx,cy,z, w,d, mat):
        hx,hd = w/2,d/2
        self.quad((cx-hx,cy-hd,z),(cx+hx,cy-hd,z),
                  (cx+hx,cy+hd,z),(cx-hx,cy+hd,z),(0,0,1),mat)

    # ── 창문 격자 (건물 정면 -Y 방향) ──────────
    def windows(self, cx, front_y, base_z, bldg_w,
                floors, cols, floor_h=3.0,
                win_w=1.0, win_h=1.4, first_z=0.8, mat="win_on_warm"):
        step = bldg_w / cols
        y = front_y - 0.05          # 살짝 돌출
        for fl in range(floors):
            z0 = base_z + first_z + fl * floor_h
            for c in range(cols):
                x0 = cx - bldg_w/2 + step*(c+0.5)
                self.quad(
                    (x0-win_w/2, y, z0),   (x0+win_w/2, y, z0),
                    (x0+win_w/2, y, z0+win_h),(x0-win_w/2,y, z0+win_h),
                    (0,-1,0), mat)

    # ── 가로등 (기둥 + 팔 + 헤드) ──────────────
    def lamp(self, x, y, base_z=0.0):
        """
        가로등 구조:
          기둥(pole): 지름 0.2, 높이 5.5
          수평 팔(arm): 도로 쪽으로 뻗음, 길이 1.2
          헤드 박스: 0.6 x 0.3 x 0.25
          유리 면: 헤드 하단 (발광)
        """
        PH   = 5.5    # 기둥 높이
        PR   = 0.10   # 기둥 반지름
        ARM  = 1.2    # 팔 길이 (도로 방향 +X or -X)
        # 기둥 — 얇은 박스로 근사
        self.box(x, y, base_z, PR*2, PR*2, PH, "lamp_pole")
        # 팔 — 가로등이 도로 쪽을 향하도록
        arm_sign = 1.0 if x < 0 else -1.0   # 왼쪽 등 → 오른쪽(+X), 오른쪽 등 → 왼쪽(-X)
        ax = x + arm_sign * ARM/2
        ay = y
        az = base_z + PH - 0.15
        self.box(ax, ay, az, ARM, PR*2, PR*1.5, "lamp_pole")
        # 헤드
        hx = x + arm_sign * ARM
        self.box(hx, ay, az - 0.05, 0.60, 0.30, 0.25, "lamp_head")
        # 유리 (하단 발광면)
        self.quad(
            (hx-0.30, ay-0.15, az-0.05),
            (hx+0.30, ay-0.15, az-0.05),
            (hx+0.30, ay+0.15, az-0.05),
            (hx-0.30, ay+0.15, az-0.05),
            (0,0,-1), "lamp_glass")

    # ── 출력 ────────────────────────────────────
    def write(self, obj_path, mtl_name):
        with open(obj_path,"w") as f:
            f.write(f"# Night Street Scene\nmtllib {mtl_name}\n\n")
            for x,y,z in self.verts:
                f.write(f"v {x:.5f} {y:.5f} {z:.5f}\n")
            f.write("\n")
            for nx,ny,nz in self.norms:
                f.write(f"vn {nx:.5f} {ny:.5f} {nz:.5f}\n")
            f.write("\n")
            cur = None
            for mat,face in self.faces:
                if mat != cur:
                    f.write(f"\nusemtl {mat}\n"); cur = mat
                vs = " ".join(f"{vi}//{ni}" for vi,ni in face)
                f.write(f"f {vs}\n")
        v = len(self.verts); t = len(self.faces)
        print(f"[OK] {obj_path}  ({v} verts, {t} tris)")


def write_mtl(path):
    with open(path,"w") as f:
        f.write("# Night Street Materials\n\n")
        for name,(kd,ks,rough,ke) in MATERIALS.items():
            f.write(f"newmtl {name}\n")
            f.write(f"Kd {kd}\nKs {ks} {ks} {ks}\n")
            f.write(f"Ns {float(rough)*100:.1f}\nKe {ke}\nd 1.0\n\n")
    print(f"[OK] {path}")


# ─────────────────────────────────────────
# 씬 배치
# ─────────────────────────────────────────
def build():
    b = ObjBuilder()

    ROAD_W   = 9.0    # 도로 폭
    SIDE_W   = 5.0    # 인도 폭
    SCENE_LEN= 60.0   # 거리 길이 (Y축)
    SCENE_W  = 50.0   # 전체 폭

    # ── 지면 ──────────────────────────────
    b.plane(0, 0, -0.02, SCENE_W, SCENE_LEN, "ground")

    # ── 도로 ──────────────────────────────
    b.plane(0, 0, 0.0,  ROAD_W, SCENE_LEN, "road")

    # ── 인도 (좌/우) ───────────────────────
    side_cx_L = -(ROAD_W/2 + SIDE_W/2)   # -9.5
    side_cx_R =  (ROAD_W/2 + SIDE_W/2)   #  9.5
    b.plane(side_cx_L, 0, 0.03, SIDE_W, SCENE_LEN, "sidewalk")
    b.plane(side_cx_R, 0, 0.03, SIDE_W, SCENE_LEN, "sidewalk")

    # ─────────────────────────────────────
    # 건물 배치  (cx, cy, width, depth, height, wall_mat, 층수, 창문 열)
    # 왼쪽: X = -(ROAD_W/2 + SIDE_W + 건물깊이/2 + 여백)
    # 오른쪽: X = +(ROAD_W/2 + SIDE_W + 건물깊이/2 + 여백)
    # ─────────────────────────────────────
    buildings = [
        # (cx,    cy,    w,   d,   h,  wall,          floors, cols, win_mat)
        (-18,   -12,  14,  10,  15, "wall_brick",    5,  5, "win_on_warm"),  # 왼쪽 앞 — 낮은 벽돌
        (-18,   +16,  14,  10,  27, "wall_dark",     9,  4, "win_on_cool"),  # 왼쪽 뒤 — 고층 어두운
        ( 18,   -10,  12,  10,  21, "wall_concrete", 7,  4, "win_on_warm"),  # 오른쪽 앞 — 콘크리트
        ( 18,   +18,  12,  10,  12, "wall_beige",    4,  4, "win_on_warm"),  # 오른쪽 뒤 — 베이지
    ]

    for cx,cy,w,d,h,wall,floors,cols,wmat in buildings:
        b.box(cx, cy, 0, w, d, h, wall, "roof")
        # 건물 정면 Y = cy - d/2
        front_y = cy - d/2
        floor_h = h / floors
        # 창문 전체 배치 (꺼진 창문 먼저 깔고 그 위에 켜진 창문 덮기)
        b.windows(cx, front_y, 0, w, floors, cols,
                  floor_h=floor_h, win_w=min(1.2, w/cols*0.65),
                  win_h=floor_h*0.5, mat="win_off")
        # 켜진 창문 — 무작위처럼 보이도록 홀수층만
        for fl_on in range(0, floors, 2):
            b.windows(cx, front_y, fl_on*floor_h, w, 1, cols,
                      floor_h=floor_h, win_w=min(1.2, w/cols*0.65),
                      win_h=floor_h*0.5, mat=wmat)

    # ─────────────────────────────────────
    # 가로등 — 인도 위에 Y축으로 일정 간격
    # 왼쪽 인도 X ≈ -9.5, 오른쪽 X ≈ +9.5
    # ─────────────────────────────────────
    lamp_y_positions = [-22, -10, 2, 14, 26]   # 5쌍 × 양쪽 = 10개

    for ly in lamp_y_positions:
        b.lamp(side_cx_L - 0.5, ly, base_z=0.03)   # 왼쪽 인도 안쪽
        b.lamp(side_cx_R + 0.5, ly, base_z=0.03)   # 오른쪽 인도 안쪽

    return b


# ─────────────────────────────────────────
if __name__ == "__main__":
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    obj_p = os.path.join(OUTPUT_DIR, OBJ_FILE)
    mtl_p = os.path.join(OUTPUT_DIR, MTL_FILE)

    print("야경 거리 씬 생성 중...")
    scene = build()
    scene.write(obj_p, MTL_FILE)
    write_mtl(mtl_p)

    print(f"""
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
완료! 파일 위치:
  {os.path.abspath(obj_p)}
  {os.path.abspath(mtl_p)}

적용 방법:
  1. 두 파일을 프로젝트 model/ 폴더에 복사
  2. context.cpp 경로 변경:
     m_model = Model::Load(device, "model/night_street.obj");

재질 구성 ({len(MATERIALS)}종):
  - road / sidewalk / ground      : 바닥
  - wall_brick/concrete/dark/beige: 건물 벽
  - win_on_warm / win_on_cool     : 켜진 창문 (emissive)
  - win_off                       : 꺼진 창문
  - lamp_pole / lamp_head         : 가로등 기둥/헤드
  - lamp_glass                    : 가로등 유리 (emissive)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━""")
