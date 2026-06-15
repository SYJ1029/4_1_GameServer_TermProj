-- SIMPLEST MMORPG - Map Script
-- obstacles : {x, y, w, h}
-- npc_groups: {x, y, range, count, npc_type, level, name}

obstacles = {}

local function rect(x, y, w, h)
    table.insert(obstacles, {x=x, y=y, w=w, h=h, t=0})
end

-- 타입 지정 장애물 (t=1: 나무, t=2: 물)
local function tree(x, y)
    if x > 4 and x < 1995 and y > 4 and y < 1995 then
        table.insert(obstacles, {x=x, y=y, w=1, h=1, t=1})
    end
end

local function water(x, y, w, h)
    table.insert(obstacles, {x=x, y=y, w=w, h=h, t=2})
end

-- castle(cx, cy, size, t, gate)
-- size = interior NxN empty space; walls placed OUTSIDE; 6-tile gate in center of each wall
local function castle(cx, cy, size, t, gate)
    gate = gate or 6
    local h   = math.floor(size / 2)
    local seg = math.floor((size - gate) / 2)
    local mid = math.floor(gate / 2)
    -- top wall
    rect(cx-h,     cy-h-t, seg, t)
    rect(cx+mid,   cy-h-t, seg, t)
    -- bottom wall
    rect(cx-h,     cy+h,   seg, t)
    rect(cx+mid,   cy+h,   seg, t)
    -- left wall
    rect(cx-h-t,   cy-h,   t,   seg)
    rect(cx-h-t,   cy+mid, t,   seg)
    -- right wall
    rect(cx+h,     cy-h,   t,   seg)
    rect(cx+h,     cy+mid, t,   seg)
end

-- 1×1 pillar grid for small procedural castle interiors
-- Creates a staggered grid of single-tile pillars (w=1, h=1)
local function decorate_small_castle(cx, cy, sz)
    local margin = 4
    local h      = math.floor(sz / 2) - margin
    local step   = 12
    local row    = 0
    for dy = -h + 2, h - 2, step do
        local shift = (row % 2 == 1) and math.floor(step / 2) or 0
        for dx = -h + 2, h - 2, step do
            local rx = dx + shift
            if math.abs(rx) <= h - 2 then
                rect(cx + rx, cy + dy, 1, 1)
            end
        end
        row = row + 1
    end
end

-- Alternating horizontal baffles inside named zone interiors.
-- Enemies must zigzag → A* pathfinding is clearly visible.
-- Baffles alternate anchoring: odd rows from left, even rows from right.
-- A gap on the opposite side lets entities pass through one at a time.
local function decorate_zone(cx, cy, sz, margin, rows)
    local h = math.floor(sz / 2) - margin
    if h < 10 then return end
    local seg     = math.floor(h * 4 / 3)
    local dy_step = math.floor(h * 2 / (rows + 1))
    for r = 1, rows do
        local ry = -h + r * dy_step
        if math.abs(ry) >= 20 then
            if r % 2 == 1 then
                rect(cx - h, cy + ry, seg, 1)
            else
                rect(cx + h - seg, cy + ry, seg, 1)
            end
        end
    end
end

-- ── Map border ────────────────────────────────────────────────────
rect(0, 0, 2000, 2)
rect(0, 1998, 2000, 2)
rect(0, 0, 2, 2000)
rect(1998, 0, 2, 2000)

-- ── Named NPC Zones (thick walls + baffle decorations) ────────────
castle(1000, 1000, 400, 8);  decorate_zone(1000, 1000, 400, 20, 6)  -- Boss
castle(400,  450,  240, 6);  decorate_zone(400,  450,  240, 15, 4)  -- Orc   NW
castle(1600, 450,  240, 6);  decorate_zone(1600, 450,  240, 15, 4)  -- Orc   NE
castle(400,  1550, 240, 6);  decorate_zone(400,  1550, 240, 15, 4)  -- Goblin SW
castle(1600, 1550, 240, 6);  decorate_zone(1600, 1550, 240, 15, 4)  -- Goblin SE
castle(400,  1000, 200, 6);  decorate_zone(400,  1000, 200, 12, 4)  -- Ogre  W
castle(1600, 1000, 200, 6);  decorate_zone(1600, 1000, 200, 12, 4)  -- Ogre  E
castle(650,  700,  160, 5);  decorate_zone(650,  700,  160, 10, 3)  -- Knight NW
castle(1350, 700,  160, 5);  decorate_zone(1350, 700,  160, 10, 3)  -- Knight NE
castle(650,  1300, 160, 5);  decorate_zone(650,  1300, 160, 10, 3)  -- Knight SW
castle(1350, 1300, 160, 5);  decorate_zone(1350, 1300, 160, 10, 3)  -- Knight SE
castle(750,  900,  120, 5);  decorate_zone(750,  900,  120,  8, 3)  -- Dragon NW
castle(1250, 900,  120, 5);  decorate_zone(1250, 900,  120,  8, 3)  -- Dragon NE
castle(750,  1100, 120, 5);  decorate_zone(750,  1100, 120,  8, 3)  -- Dragon SW
castle(1250, 1100, 120, 5);  decorate_zone(1250, 1100, 120,  8, 3)  -- Dragon SE

-- ── Exclusion zones (enlarged to keep procedural castles away from named zones) ──
local no_castle = {
    {1000, 1000, 280*280},   -- Boss         (was 228)
    {400,  450,  185*185},   -- Orc   NW     (was 140)
    {1600, 450,  185*185},   -- Orc   NE
    {400,  1550, 185*185},   -- Goblin SW
    {1600, 1550, 185*185},   -- Goblin SE
    {400,  1000, 160*160},   -- Ogre  W      (was 120)
    {1600, 1000, 160*160},   -- Ogre  E
    {650,  700,  135*135},   -- Knight NW    (was 100)
    {1350, 700,  135*135},   -- Knight NE
    {650,  1300, 135*135},   -- Knight SW
    {1350, 1300, 135*135},   -- Knight SE
    {750,  900,  115*115},   -- Dragon NW    (was 80)
    {1250, 900,  115*115},   -- Dragon NE
    {750,  1100, 115*115},   -- Dragon SW
    {1250, 1100, 115*115},   -- Dragon SE
    {1000, 100,  170*170},   -- Player spawn corridor
}

local function is_excluded(x, y)
    for _, z in ipairs(no_castle) do
        local dx, dy = x - z[1], y - z[2]
        if dx*dx + dy*dy < z[3] then return true end
    end
    return false
end

-- ── Procedural grid: 1-tile-wall castles with pillar interiors ────
-- step=140 → 13×13=169 candidate positions; skip excluded zones
local placed = 0
for gi = 0, 12 do
    for gj = 0, 12 do
        local gx = 140 + gi * 140
        local gy = 140 + gj * 140
        if not is_excluded(gx, gy) then
            local sz = ((gi + gj) % 2 == 0) and 60 or 80
            castle(gx, gy, sz, 1)           -- 1-tile-thick wall
            decorate_small_castle(gx, gy, sz)
            placed = placed + 1
        end
    end
end

-- ── Scattered rock obstacles (min one dimension ≥ 2 to avoid 1×1) ─
-- Pure 1×1 size is reserved for interior pillar obstacles
math.randomseed(31415)
for _ = 1, 600 do
    local rx = math.random(30, 1970)
    local ry = math.random(30, 1970)
    if not is_excluded(rx, ry) then
        local rw = math.random(1, 2)
        local rh = math.random(1, 2)
        if rw == 1 and rh == 1 then rw = 2 end  -- guarantee ≥ 2 tiles total
        rect(rx, ry, rw, rh)
    end
end

-- ── 나무 숲 (type=1) ─────────────────────────────────────────────
local tree_clusters = {
    -- 모서리 대형 숲 (4곳)
    {cx=110,  cy=220,  r=75, n=65},   -- 북서 코너
    {cx=1890, cy=220,  r=75, n=65},   -- 북동 코너
    {cx=110,  cy=1780, r=75, n=65},   -- 남서 코너
    {cx=1890, cy=1780, r=75, n=65},   -- 남동 코너

    -- 서쪽 외곽 숲 줄기
    {cx=75,   cy=560,  r=55, n=45},   -- 서 (북서 호수 옆)
    {cx=75,   cy=820,  r=55, n=42},   -- 서 (중북)
    {cx=75,   cy=1000, r=50, n=38},   -- 서 (중간)
    {cx=75,   cy=1180, r=55, n=42},   -- 서 (중남)
    {cx=75,   cy=1400, r=55, n=45},   -- 서 (남서 호수 옆)

    -- 동쪽 외곽 숲 줄기
    {cx=1925, cy=560,  r=55, n=45},
    {cx=1925, cy=820,  r=55, n=42},
    {cx=1925, cy=1000, r=50, n=38},
    {cx=1925, cy=1180, r=55, n=42},
    {cx=1925, cy=1400, r=55, n=45},

    -- 북부 내측 숲 (플레이어 스폰~Orc 구간)
    {cx=600,  cy=255,  r=65, n=50},   -- 북 좌
    {cx=1400, cy=255,  r=65, n=50},   -- 북 우
    {cx=1000, cy=220,  r=50, n=35},   -- 북 중 (스폰 양옆 좁음)

    -- 남부 내측 숲 (Goblin 아래)
    {cx=600,  cy=1745, r=65, n=50},
    {cx=1400, cy=1745, r=65, n=50},
    {cx=1000, cy=1780, r=50, n=35},

    -- 명명 존 사이 빈 공간 숲 (대각선)
    {cx=500,  cy=655,  r=45, n=32},   -- Orc NW ~ Knight NW 사이
    {cx=1500, cy=655,  r=45, n=32},   -- Orc NE ~ Knight NE 사이
    {cx=500,  cy=1345, r=45, n=32},   -- Goblin SW ~ Knight SW 사이
    {cx=1500, cy=1345, r=45, n=32},   -- Goblin SE ~ Knight SE 사이

    -- 서쪽 복도 (Orc W ~ Ogre W 사이)
    {cx=265,  cy=730,  r=48, n=35},
    {cx=265,  cy=1265, r=48, n=35},

    -- 동쪽 복도
    {cx=1735, cy=730,  r=48, n=35},
    {cx=1735, cy=1265, r=48, n=35},
}

math.randomseed(31415 + 9265)
for _, cl in ipairs(tree_clusters) do
    local placed_t = 0
    local tries_t  = 0
    while placed_t < cl.n and tries_t < cl.n * 14 do
        tries_t = tries_t + 1
        local tx = cl.cx + math.random(-cl.r, cl.r)
        local ty = cl.cy + math.random(-cl.r, cl.r)
        if not is_excluded(tx, ty) then
            tree(tx, ty)
            placed_t = placed_t + 1
        end
    end
end

-- ── 호수 / 연못 (type=2) ─────────────────────────────────────────
-- 외곽 대형 호수 (명명 존 바깥)
water(182,  550, 14, 10)   -- 북서 호수
water(1786, 550, 14, 10)   -- 북동 호수
water(182,  1392, 14, 10)  -- 남서 호수
water(1786, 1392, 14, 10)  -- 남동 호수

-- 서쪽 복도 연못
water(195,  980,  10,  7)  -- 서 중간 연못
water(195,  740,  8,   5)  -- 서 북부 연못
water(195,  1220, 8,   5)  -- 서 남부 연못

-- 동쪽 복도 연못
water(1787, 980,  10,  7)
water(1787, 740,  8,   5)
water(1787, 1220, 8,   5)

-- 북부 연못 군 (스폰 아래 ~ Orc 구간)
water(936,  306, 20,  7)   -- 중앙 북부 (기존 확장)
water(612,  298, 12,  5)   -- 좌 북부
water(1360, 298, 12,  5)   -- 우 북부

-- 남부 연못 군 (Goblin 아래)
water(936,  1658, 20,  7)
water(612,  1668, 12,  5)
water(1360, 1668, 12,  5)

-- 내측 소형 연못 (명명 존 사이 공간)
water(482,  648,  8,   5)  -- Orc NW ~ Knight NW 사이
water(1502, 648,  8,   5)  -- Orc NE ~ Knight NE 사이
water(482,  1338, 8,   5)  -- Goblin SW ~ Knight SW 사이
water(1502, 1338, 8,   5)  -- Goblin SE ~ Knight SE 사이

-- 남북 강줄기 (서쪽 끝, 2타일 폭)
water(30,  370,  3, 130)   -- 서 북부 수직 수로
water(30,  1490, 3, 130)   -- 서 남부 수직 수로
water(1959, 370,  3, 130)  -- 동 북부 수직 수로
water(1959, 1490, 3, 130)  -- 동 남부 수직 수로

-- ── NPC spawn zones ───────────────────────────────────────────────
npc_groups = {
    ------------------------------------------------------------
    -- PEACE NPCs  (level 1-3, outer ring)  total ~100000
    ------------------------------------------------------------
    {x=200,  y=180,  range=150, count=5000, npc_type=1, level=1, name="Rabbit"},
    {x=600,  y=180,  range=150, count=5000, npc_type=1, level=1, name="Rabbit"},
    {x=1000, y=180,  range=150, count=5000, npc_type=1, level=1, name="Rabbit"},
    {x=1400, y=180,  range=150, count=5000, npc_type=1, level=1, name="Rabbit"},
    {x=1800, y=180,  range=150, count=5000, npc_type=1, level=1, name="Rabbit"},

    {x=200,  y=1820, range=150, count=4000, npc_type=1, level=2, name="Troll"},
    {x=600,  y=1820, range=150, count=4000, npc_type=1, level=2, name="Troll"},
    {x=1000, y=1820, range=150, count=4000, npc_type=1, level=2, name="Troll"},
    {x=1400, y=1820, range=150, count=4000, npc_type=1, level=2, name="Troll"},
    {x=1800, y=1820, range=150, count=4000, npc_type=1, level=2, name="Troll"},

    {x=180,  y=500,  range=120, count=4000, npc_type=1, level=1, name="Sheep"},
    {x=180,  y=800,  range=120, count=4000, npc_type=1, level=2, name="Sheep"},
    {x=180,  y=1100, range=120, count=4000, npc_type=1, level=2, name="Golem"},
    {x=180,  y=1400, range=120, count=4000, npc_type=1, level=3, name="Golem"},

    {x=1820, y=500,  range=120, count=4000, npc_type=1, level=1, name="Sheep"},
    {x=1820, y=800,  range=120, count=4000, npc_type=1, level=2, name="Sheep"},
    {x=1820, y=1100, range=120, count=4000, npc_type=1, level=2, name="Golem"},
    {x=1820, y=1400, range=120, count=4000, npc_type=1, level=3, name="Golem"},

    {x=350,  y=350,  range=100, count=3000, npc_type=1, level=1, name="Bunny"},
    {x=1650, y=350,  range=100, count=3000, npc_type=1, level=1, name="Bunny"},
    {x=350,  y=1650, range=100, count=3000, npc_type=1, level=2, name="Bunny"},
    {x=1650, y=1650, range=100, count=3000, npc_type=1, level=2, name="Bunny"},

    ------------------------------------------------------------
    -- AGRO NPCs  (level 2-6, mid to center)  total ~100000
    ------------------------------------------------------------
    {x=400,  y=450,  range=120, count=5000, npc_type=2, level=2, name="Orc"},
    {x=750,  y=500,  range=100, count=5000, npc_type=2, level=2, name="Orc"},
    {x=1250, y=500,  range=100, count=5000, npc_type=2, level=2, name="Orc"},
    {x=1600, y=450,  range=120, count=5000, npc_type=2, level=2, name="Orc"},

    {x=400,  y=1550, range=120, count=5000, npc_type=2, level=3, name="Goblin"},
    {x=750,  y=1500, range=100, count=5000, npc_type=2, level=3, name="Goblin"},
    {x=1250, y=1500, range=100, count=5000, npc_type=2, level=3, name="Goblin"},
    {x=1600, y=1550, range=120, count=5000, npc_type=2, level=3, name="Goblin"},

    {x=400,  y=750,  range=100, count=4000, npc_type=2, level=3, name="Ogre"},
    {x=400,  y=1000, range=100, count=4000, npc_type=2, level=3, name="Ogre"},
    {x=400,  y=1250, range=100, count=4000, npc_type=2, level=3, name="Ogre"},

    {x=1600, y=750,  range=100, count=4000, npc_type=2, level=3, name="Ogre"},
    {x=1600, y=1000, range=100, count=4000, npc_type=2, level=3, name="Ogre"},
    {x=1600, y=1250, range=100, count=4000, npc_type=2, level=3, name="Ogre"},

    {x=650,  y=700,  range=80,  count=3000, npc_type=2, level=4, name="Knight"},
    {x=1350, y=700,  range=80,  count=3000, npc_type=2, level=4, name="Knight"},
    {x=650,  y=1300, range=80,  count=3000, npc_type=2, level=4, name="Knight"},
    {x=1350, y=1300, range=80,  count=3000, npc_type=2, level=4, name="Knight"},

    {x=750,  y=900,  range=60,  count=3000, npc_type=2, level=5, name="Dragon"},
    {x=1250, y=900,  range=60,  count=3000, npc_type=2, level=5, name="Dragon"},
    {x=750,  y=1100, range=60,  count=3000, npc_type=2, level=5, name="Dragon"},
    {x=1250, y=1100, range=60,  count=3000, npc_type=2, level=5, name="Dragon"},

    {x=1000, y=1000, range=80,  count=4000, npc_type=2, level=6, name="Boss"},
}
