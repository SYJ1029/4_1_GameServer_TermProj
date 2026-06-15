-- SIMPLEST MMORPG - Map Script
-- obstacles : {x, y, w, h}
-- npc_groups: {x, y, range, count, npc_type, level, name}

obstacles = {}

local function rect(x, y, w, h)
    table.insert(obstacles, {x=x, y=y, w=w, h=h})
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
