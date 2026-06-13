-- SIMPLEST MMORPG - Map Script
-- obstacles : {x, y, w, h}  (top-left origin, tile units)
-- npc_groups: {x, y, range, count, npc_type, level, name}

obstacles = {
    -- Map border
    {x=0,    y=0,    w=2000, h=2},
    {x=0,    y=1998, w=2000, h=2},
    {x=0,    y=0,    w=2,    h=2000},
    {x=1998, y=0,    w=2,    h=2000},

    -- Central castle walls
    {x=800,  y=800,  w=400,  h=10},
    {x=800,  y=800,  w=10,   h=400},
    {x=1190, y=800,  w=10,   h=400},
    {x=800,  y=1190, w=400,  h=10},

    -- Northwest corridors
    {x=500,  y=400,  w=100,  h=10},
    {x=500,  y=400,  w=10,   h=200},
    {x=500,  y=600,  w=100,  h=10},

    -- Northeast corridors
    {x=1400, y=400,  w=100,  h=10},
    {x=1490, y=400,  w=10,   h=200},
    {x=1400, y=600,  w=100,  h=10},

    -- Southwest corridors
    {x=500,  y=1400, w=100,  h=10},
    {x=500,  y=1590, w=100,  h=10},
    {x=590,  y=1400, w=10,   h=200},

    -- Southeast corridors
    {x=1400, y=1400, w=100,  h=10},
    {x=1400, y=1590, w=100,  h=10},
    {x=1400, y=1400, w=10,   h=200},

    -- Spawn near test wall (remove after confirming render)
    {x=990,  y=110,  w=20,   h=3},
}

-- NPC spawn zones
-- npc_type: 1=Peace, 2=Agro
-- Total: ~200000 (100k Peace + 100k Agro)
-- Layout: Peace covers outer ring, Agro fills mid/center, difficulty scales inward

npc_groups = {
    ------------------------------------------------------------
    -- PEACE NPCs  (level 1-3, outer ring)  total ~100000
    ------------------------------------------------------------
    -- North strip
    {x=200,  y=180,  range=150, count=5000, npc_type=1, level=1, name="Rabbit"},
    {x=600,  y=180,  range=150, count=5000, npc_type=1, level=1, name="Rabbit"},
    {x=1000, y=180,  range=150, count=5000, npc_type=1, level=1, name="Rabbit"},
    {x=1400, y=180,  range=150, count=5000, npc_type=1, level=1, name="Rabbit"},
    {x=1800, y=180,  range=150, count=5000, npc_type=1, level=1, name="Rabbit"},

    -- South strip
    {x=200,  y=1820, range=150, count=4000, npc_type=1, level=2, name="Troll"},
    {x=600,  y=1820, range=150, count=4000, npc_type=1, level=2, name="Troll"},
    {x=1000, y=1820, range=150, count=4000, npc_type=1, level=2, name="Troll"},
    {x=1400, y=1820, range=150, count=4000, npc_type=1, level=2, name="Troll"},
    {x=1800, y=1820, range=150, count=4000, npc_type=1, level=2, name="Troll"},

    -- West strip
    {x=180,  y=500,  range=120, count=4000, npc_type=1, level=1, name="Sheep"},
    {x=180,  y=800,  range=120, count=4000, npc_type=1, level=2, name="Sheep"},
    {x=180,  y=1100, range=120, count=4000, npc_type=1, level=2, name="Golem"},
    {x=180,  y=1400, range=120, count=4000, npc_type=1, level=3, name="Golem"},

    -- East strip
    {x=1820, y=500,  range=120, count=4000, npc_type=1, level=1, name="Sheep"},
    {x=1820, y=800,  range=120, count=4000, npc_type=1, level=2, name="Sheep"},
    {x=1820, y=1100, range=120, count=4000, npc_type=1, level=2, name="Golem"},
    {x=1820, y=1400, range=120, count=4000, npc_type=1, level=3, name="Golem"},

    -- Corner pockets
    {x=350,  y=350,  range=100, count=3000, npc_type=1, level=1, name="Bunny"},
    {x=1650, y=350,  range=100, count=3000, npc_type=1, level=1, name="Bunny"},
    {x=350,  y=1650, range=100, count=3000, npc_type=1, level=2, name="Bunny"},
    {x=1650, y=1650, range=100, count=3000, npc_type=1, level=2, name="Bunny"},

    ------------------------------------------------------------
    -- AGRO NPCs  (level 2-6, mid to center)  total ~100000
    ------------------------------------------------------------
    -- Mid-north band (lv2)
    {x=400,  y=450,  range=120, count=5000, npc_type=2, level=2, name="Orc"},
    {x=750,  y=500,  range=100, count=5000, npc_type=2, level=2, name="Orc"},
    {x=1250, y=500,  range=100, count=5000, npc_type=2, level=2, name="Orc"},
    {x=1600, y=450,  range=120, count=5000, npc_type=2, level=2, name="Orc"},

    -- Mid-south band (lv3)
    {x=400,  y=1550, range=120, count=5000, npc_type=2, level=3, name="Goblin"},
    {x=750,  y=1500, range=100, count=5000, npc_type=2, level=3, name="Goblin"},
    {x=1250, y=1500, range=100, count=5000, npc_type=2, level=3, name="Goblin"},
    {x=1600, y=1550, range=120, count=5000, npc_type=2, level=3, name="Goblin"},

    -- Mid-west band (lv3)
    {x=400,  y=750,  range=100, count=4000, npc_type=2, level=3, name="Ogre"},
    {x=400,  y=1000, range=100, count=4000, npc_type=2, level=3, name="Ogre"},
    {x=400,  y=1250, range=100, count=4000, npc_type=2, level=3, name="Ogre"},

    -- Mid-east band (lv3)
    {x=1600, y=750,  range=100, count=4000, npc_type=2, level=3, name="Ogre"},
    {x=1600, y=1000, range=100, count=4000, npc_type=2, level=3, name="Ogre"},
    {x=1600, y=1250, range=100, count=4000, npc_type=2, level=3, name="Ogre"},

    -- Inner ring (lv4)
    {x=650,  y=700,  range=80,  count=3000, npc_type=2, level=4, name="Knight"},
    {x=1350, y=700,  range=80,  count=3000, npc_type=2, level=4, name="Knight"},
    {x=650,  y=1300, range=80,  count=3000, npc_type=2, level=4, name="Knight"},
    {x=1350, y=1300, range=80,  count=3000, npc_type=2, level=4, name="Knight"},

    -- Castle approach (lv5)
    {x=750,  y=900,  range=60,  count=3000, npc_type=2, level=5, name="Dragon"},
    {x=1250, y=900,  range=60,  count=3000, npc_type=2, level=5, name="Dragon"},
    {x=750,  y=1100, range=60,  count=3000, npc_type=2, level=5, name="Dragon"},
    {x=1250, y=1100, range=60,  count=3000, npc_type=2, level=5, name="Dragon"},

    -- Castle interior (lv6 boss)
    {x=1000, y=1000, range=80,  count=4000, npc_type=2, level=6, name="Boss"},
}
