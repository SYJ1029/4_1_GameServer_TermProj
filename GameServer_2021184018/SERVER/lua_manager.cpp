#include "lua_manager.h"
#include "../../COMMON/PROTOCOL/protocol_2026.h"
#include <iostream>
#include <algorithm>

lua_State*                  g_lua          = nullptr;
std::vector<bool>           g_obstacle_map;
std::vector<NpcSpawnGroup>  g_npc_groups;
std::vector<ObstacleRect>   g_obstacle_rects;

// ── 장애물 테이블 파싱 ────────────────────────────────────────────
static void load_obstacles(lua_State* L)
{
    g_obstacle_map.assign((size_t)WORLD_WIDTH * WORLD_HEIGHT, false);

    lua_getglobal(L, "obstacles");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }

    int n = (int)lua_rawlen(L, -1);
    for (int i = 1; i <= n; ++i) {
        lua_rawgeti(L, -1, i);
        if (!lua_istable(L, -1)) { lua_pop(L, 1); continue; }

        auto get_int = [&](const char* key) {
            lua_getfield(L, -1, key);
            int v = (int)lua_tointeger(L, -1);
            lua_pop(L, 1);
            return v;
        };
        int ox = get_int("x"), oy = get_int("y");
        int ow = get_int("w"), oh = get_int("h");
        int ot = 0;
        lua_getfield(L, -1, "t");
        if (lua_isnumber(L, -1)) ot = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);

        g_obstacle_rects.push_back({ ox, oy, ow, oh, ot });

        for (int dy = 0; dy < oh; ++dy)
            for (int dx = 0; dx < ow; ++dx) {
                int tx = ox + dx, ty = oy + dy;
                if (tx >= 0 && tx < WORLD_WIDTH && ty >= 0 && ty < WORLD_HEIGHT)
                    g_obstacle_map[ty * WORLD_WIDTH + tx] = true;
            }

        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    std::cout << "[Lua] Obstacles loaded: " << g_obstacle_rects.size() << " rects\n";
}

// ── NPC 스폰 그룹 테이블 파싱 ────────────────────────────────────
static void load_npc_groups(lua_State* L)
{
    lua_getglobal(L, "npc_groups");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }

    int n = (int)lua_rawlen(L, -1);
    for (int i = 1; i <= n; ++i) {
        lua_rawgeti(L, -1, i);
        if (!lua_istable(L, -1)) { lua_pop(L, 1); continue; }

        NpcSpawnGroup g{};

        auto get_int = [&](const char* key) {
            lua_getfield(L, -1, key);
            int v = (int)lua_tointeger(L, -1);
            lua_pop(L, 1);
            return v;
        };
        auto get_str = [&](const char* key) {
            lua_getfield(L, -1, key);
            const char* s = lua_tostring(L, -1);
            if (s) strncpy_s(g.name, s, sizeof(g.name) - 1);
            lua_pop(L, 1);
        };

        g.x        = get_int("x");
        g.y        = get_int("y");
        g.range    = get_int("range");
        g.count    = get_int("count");
        g.npc_type = get_int("npc_type");
        g.level    = get_int("level");
        get_str("name");

        g_npc_groups.push_back(g);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    std::cout << "[Lua] NPC groups loaded: " << g_npc_groups.size() << " groups\n";
}

// ── 공개 함수 ─────────────────────────────────────────────────────
bool init_lua(const char* script_path)
{
    g_lua = luaL_newstate();
    luaL_openlibs(g_lua);

    if (luaL_dofile(g_lua, script_path) != LUA_OK) {
        std::cerr << "[Lua] Error: " << lua_tostring(g_lua, -1) << "\n";
        lua_close(g_lua);
        g_lua = nullptr;
        return false;
    }

    load_obstacles(g_lua);
    load_npc_groups(g_lua);
    return true;
}

void close_lua()
{
    if (g_lua) { lua_close(g_lua); g_lua = nullptr; }
}

bool write_obstacle_bin(const char* path)
{
    FILE* f = nullptr;
    if (fopen_s(&f, path, "wb") != 0 || !f) {
        std::cerr << "[Map] Failed to write obstacles.bin: " << path << "\n";
        return false;
    }

    const char magic[4] = { 'O','B','S','2' };
    fwrite(magic, 1, 4, f);

    int32_t n = (int32_t)g_obstacle_rects.size();
    fwrite(&n, 4, 1, f);

    for (auto& r : g_obstacle_rects) {
        int16_t vals[5] = {
            (int16_t)r.x, (int16_t)r.y,
            (int16_t)r.w, (int16_t)r.h,
            (int16_t)r.obs_type
        };
        fwrite(vals, 2, 5, f);
    }

    fclose(f);
    std::cout << "[Map] obstacles.bin saved (" << n << " rects) -> " << path << "\n";
    return true;
}
