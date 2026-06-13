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

-- ── Map border ────────────────────────────────────────────────────
rect(0, 0, 2000, 2)
rect(0, 1998, 2000, 2)
rect(0, 0, 2, 2000)
rect(1998, 0, 2, 2000)

-- ── NPC Zone enclosures (walls wrap OUTSIDE each zone interior) ───
castle(1000, 1000, 400, 8)   -- Boss  (400x400 interior)
castle(400,  450,  240, 6)   -- Orc   NW
castle(1600, 450,  240, 6)   -- Orc   NE
castle(400,  1550, 240, 6)   -- Goblin SW
castle(1600, 1550, 240, 6)   -- Goblin SE
castle(400,  1000, 200, 6)   -- Ogre  W
castle(1600, 1000, 200, 6)   -- Ogre  E
castle(650,  700,  160, 5)   -- Knight NW
castle(1350, 700,  160, 5)   -- Knight NE
castle(650,  1300, 160, 5)   -- Knight SW
castle(1350, 1300, 160, 5)   -- Knight SE
castle(750,  900,  120, 5)   -- Dragon NW
castle(1250, 900,  120, 5)   -- Dragon NE
castle(750,  1100, 120, 5)   -- Dragon SW
castle(1250, 1100, 120, 5)   -- Dragon SE

-- ── Procedural grid: 100+ scattered castles ───────────────────────
-- Grid step=140 → 13×13=169 candidate positions.
-- Only exclude NPC zone interiors (so no castle-in-castle).
-- Peace/Agro NPC spawn positions are handled individually by InitializeNPC()
-- obstacle-check loop, so no need to clear entire strips here.

-- Each zone: {cx, cy, radius_sq}  (castle center must NOT fall inside)
-- Radius = interior_half + wall_thickness + small_buffer
local no_castle = {
    -- Boss (interior 400×400, wall=8) → r = 200+8+20 = 228
    {1000, 1000, 228*228},
    -- Orc (interior 240×240, wall=6) → r = 120+6+14 = 140
    {400,  450,  140*140},
    {1600, 450,  140*140},
    -- Goblin
    {400,  1550, 140*140},
    {1600, 1550, 140*140},
    -- Ogre (interior 200×200, wall=6) → r = 100+6+14 = 120
    {400,  1000, 120*120},
    {1600, 1000, 120*120},
    -- Knight (interior 160×160, wall=5) → r = 80+5+15 = 100
    {650,  700,  100*100},
    {1350, 700,  100*100},
    {650,  1300, 100*100},
    {1350, 1300, 100*100},
    -- Dragon (interior 120×120, wall=5) → r = 60+5+15 = 80
    {750,  900,   80*80},
    {1250, 900,   80*80},
    {750,  1100,  80*80},
    {1250, 1100,  80*80},
    -- Player spawn: keep a safe corridor north of map center
    {1000, 100,  130*130},
}

local function is_excluded(x, y)
    for _, z in ipairs(no_castle) do
        local dx, dy = x - z[1], y - z[2]
        if dx*dx + dy*dy < z[3] then return true end
    end
    return false
end

-- step=140: gx/gy ∈ {140, 280, 420, ..., 1820} (13 values each)
-- Checkerboard: even cell index → size 60, odd → size 80
local placed = 0
for gi = 0, 12 do
    for gj = 0, 12 do
        local gx = 140 + gi * 140
        local gy = 140 + gj * 140
        if not is_excluded(gx, gy) then
            local sz = ((gi + gj) % 2 == 0) and 60 or 80
            castle(gx, gy, sz, 5)
            placed = placed + 1
        end
    end
end
-- placed count is printed by C++ (g_obstacle_rects.size())

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
