#pragma once
#include "lua.hpp"
#include <vector>
#include <string>

// NPC 스폰 존 (Lua에서 읽어온 데이터)
struct NpcSpawnGroup {
    int   x, y;          // 존 중심
    int   range;         // 스폰 반경
    int   count;         // 이 존에서 생성할 NPC 수
    int   npc_type;      // 1=Peace, 2=Agro
    int   level;
    char  name[32];      // 이름 접두사
};

// 장애물 직사각형
// obs_type: 0=벽/바위, 1=나무, 2=물
struct ObstacleRect {
    int x, y, w, h;
    int obs_type = 0;
};

extern lua_State*                  g_lua;
extern std::vector<bool>           g_obstacle_map;   // [y * WORLD_WIDTH + x]
extern std::vector<NpcSpawnGroup>  g_npc_groups;
extern std::vector<ObstacleRect>   g_obstacle_rects; // 원본 rect 목록 (bin 덤프용)

bool init_lua(const char* script_path);
void close_lua();
bool write_obstacle_bin(const char* path);

inline bool is_obstacle(int x, int y)
{
    if (x < 0 || x >= 2000 || y < 0 || y >= 2000) return true;
    return g_obstacle_map[y * 2000 + x];
}
