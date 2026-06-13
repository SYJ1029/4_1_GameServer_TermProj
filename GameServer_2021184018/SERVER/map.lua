-- SIMPLEST MMORPG - 맵 배치 스크립트
-- 장애물: {x, y, w, h} (좌상단 기준 직사각형, 단위: 타일)
-- NPC 스폰 존: {x, y, range, count, npc_type, level, name}

obstacles = {
    -- 맵 테두리 (바깥 경계)
    {x=0,    y=0,    w=2000, h=2},     -- 상단
    {x=0,    y=1998, w=2000, h=2},     -- 하단
    {x=0,    y=0,    w=2,    h=2000},  -- 좌측
    {x=1998, y=0,    w=2,    h=2000},  -- 우측

    -- 중앙 구역 장애물
    {x=500,  y=400,  w=100,  h=10},
    {x=500,  y=400,  w=10,   h=200},
    {x=500,  y=600,  w=100,  h=10},

    {x=1400, y=400,  w=100,  h=10},
    {x=1490, y=400,  w=10,   h=200},
    {x=1400, y=600,  w=100,  h=10},

    -- 맵 중앙 성벽
    {x=800,  y=800,  w=400,  h=10},
    {x=800,  y=800,  w=10,   h=400},
    {x=1190, y=800,  w=10,   h=400},
    {x=800,  y=1190, w=400,  h=10},

    -- 북쪽 통로
    {x=200,  y=300,  w=150,  h=8},
    {x=200,  y=500,  w=150,  h=8},

    -- 남쪽 통로
    {x=1650, y=1300, w=150,  h=8},
    {x=1650, y=1500, w=150,  h=8},
}

-- NPC 스폰 존
-- npc_type: 1=Peace, 2=Agro
-- count: 이 존에서 생성할 NPC 수 (총합 200000)
npc_groups = {
    -- Peace NPC 존 (총 100000마리)
    {x=300,  y=300,  range=250, count=25000, npc_type=1, level=1, name="Bunny"},
    {x=1700, y=300,  range=250, count=25000, npc_type=1, level=1, name="Sheep"},
    {x=300,  y=1700, range=250, count=25000, npc_type=1, level=2, name="Golem"},
    {x=1700, y=1700, range=250, count=25000, npc_type=1, level=2, name="Troll"},

    -- Agro NPC 존 (총 100000마리)
    {x=700,  y=700,  range=150, count=20000, npc_type=2, level=2, name="Orc"},
    {x=1300, y=700,  range=150, count=20000, npc_type=2, level=3, name="Goblin"},
    {x=700,  y=1300, range=150, count=20000, npc_type=2, level=3, name="Ogre"},
    {x=1300, y=1300, range=150, count=20000, npc_type=2, level=4, name="Dragon"},
    {x=1000, y=1000, range=100, count=20000, npc_type=2, level=5, name="Boss"},
}
