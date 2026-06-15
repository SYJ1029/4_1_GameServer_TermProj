// SIMPLEST MMORPG - Client
// Graphics : SFML 2.x  (NuGet: SFML by SFML Team)
// Network  : Winsock2  (raw, same as server)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <SFML/Graphics.hpp>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <deque>
#include <string>
#include <cstdio>

#pragma comment(lib, "ws2_32.lib")

#include "../../COMMON/PROTOCOL/protocol_2026.h"

// ── 오브젝트 ──────────────────────────────────────────────────────
struct ObjInfo {
    int           id;
    char          name[MAX_NAME_LEN];
    short         x, y;
    short         hp, max_hp;
    NPC_KIND      npc_type;   // NPC_PC=0, NPC_PEACE=1, NPC_AGRO=2
    NPC_STATE     npc_state;  // NPC_STATE_IDLE/ROAMING/CHASE
};

// ── 전역 상태 ─────────────────────────────────────────────────────
static SOCKET            g_sock     = INVALID_SOCKET;
static std::atomic<bool> g_running  { false };
static int               g_my_id    = -1;
static short             g_my_x     = 0, g_my_y = 0;
static short             g_my_hp    = 100, g_my_max_hp = 100;
static int               g_my_level  = 1;
static int               g_my_xp     = 0;
static int               g_my_exp_next = 100;
static int               g_move_time = 0;

static std::unordered_map<int, ObjInfo> g_objs;
static std::mutex g_objs_lock;

struct ItemInfo { int id; short x, y; ITEM_TYPE item_type; };
static std::unordered_map<int, ItemInfo> g_world_items_cl;
static std::mutex g_items_lock;
static int g_inventory[ITEM_SLOT_COUNT] = {};  // 슬롯 1-6 → 인덱스 0-5

static std::deque<std::string> g_msgs;
static std::mutex              g_msgs_lock;
constexpr int MAX_MSGS    = 6;
constexpr int MSG_LINE_H  = 15;   // 메시지 한 줄 높이 (px)

// ── 채팅 상태 ─────────────────────────────────────────────────────
static bool        g_chat_mode  = false;
static std::string g_chat_input;

// ── 스킬 쿨타임 ───────────────────────────────────────────────────
constexpr int SKILL_CD_MS = 3000;
static std::chrono::steady_clock::time_point g_last_skill_time =
    std::chrono::steady_clock::now() - std::chrono::seconds(10);

// ── 퀘스트 ────────────────────────────────────────────────────────
struct QuestClient {
    const char* name;
    const char* desc;
    const char* reward_str;
    int         current;
    int         target;
    QUEST_STATE state;
};
static QuestClient g_quests[2] = {
    { "Agro Slayer",  "Agro NPC를 처치하라",  "보상: 500XP + 공격력 강화[4]",  0, 10, Q_ACTIVE },
    { "Boss Hunter",  "보스 몬스터를 처치하라", "보상: 3000XP + 방어력 강화[5]", 0,  3, Q_ACTIVE }
};
static bool g_quest_panel_open = false;
static bool g_map_open         = false;

// ── 공격 이펙트 ───────────────────────────────────────────────────
enum class EffectType { NONE, ATTACK, SKILL };
struct VisualEffect {
    EffectType type  = EffectType::NONE;
    std::chrono::steady_clock::time_point start;
};
static VisualEffect g_effect;

// ── 스프라이트 텍스처 (CC0 · Kenney Tiny Dungeon) ─────────────────
struct GameTextures {
    sf::Texture floor;
    sf::Texture wall;         // 소형 돌 장애물 (max dim <= 2)
    sf::Texture castle_wall;  // 성벽 장애물 (벽돌 패턴)
    sf::Texture player_me, player_other;
    sf::Texture npc_peace, npc_agro;
    sf::Texture npc_boss, npc_boss_p3;
    sf::Texture item_potion;
    bool ok = false;
} static g_tex;
constexpr float SPR_S = 1.0f / 16.0f;   // 16px 스프라이트 → 1 월드 단위

// ── 장애물 ────────────────────────────────────────────────────────
struct ObstacleRect { short x, y, w, h; };
static std::vector<ObstacleRect> g_obstacles;

static void load_obstacles_bin(const char* path)
{
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) return;

    char magic[4];
    fread(magic, 1, 4, f);
    if (magic[0]!='O'||magic[1]!='B'||magic[2]!='S'||magic[3]!='1') { fclose(f); return; }

    int32_t n = 0;
    fread(&n, 4, 1, f);
    g_obstacles.resize(n);
    for (int i = 0; i < n; ++i) {
        int16_t vals[4];
        fread(vals, 2, 4, f);
        g_obstacles[i] = { vals[0], vals[1], vals[2], vals[3] };
    }
    fclose(f);
}

// ── 앱 상태 ───────────────────────────────────────────────────────
enum class AppState { LOGIN, PLAYING };
static AppState    g_state      = AppState::LOGIN;
static std::string g_input_ip   = "127.0.0.1";
static std::string g_input_name;
static int         g_focus      = 1;  // 0=IP, 1=name

// ── 렌더 상수 ─────────────────────────────────────────────────────
constexpr int   VSIZE   = 15;
constexpr int   TILE_PX = 36;
constexpr float TILE_F  = 36.f;
constexpr int   WIN_W   = VSIZE * TILE_PX;   // 540
constexpr int   UI_H    = 170;               // HP바(17) + 힌트(17) + 메시지 6줄(90) + 채팅창(26) + 여유
constexpr int   WIN_H   = WIN_W + UI_H;       // 710
constexpr float GAME_VP_H = (float)(WIN_H - UI_H) / WIN_H;

// ── 유틸 ──────────────────────────────────────────────────────────
static void push_msg(const std::string& s)
{
    std::lock_guard<std::mutex> lk(g_msgs_lock);
    g_msgs.push_back(s);
    if ((int)g_msgs.size() > MAX_MSGS) g_msgs.pop_front();
}

// ── 패킷 송신 ─────────────────────────────────────────────────────
static void net_send(void* data, int sz)
{
    if (g_sock != INVALID_SOCKET)
        ::send(g_sock, reinterpret_cast<char*>(data), sz, 0);
}

static void send_login(const char* name)
{
    C2S_Login p{};
    p.size = sizeof(p);
    p.type = C2S_LOGIN;
    strncpy_s(p.username, name, MAX_NAME_LEN - 1);
    net_send(&p, p.size);
}

static void send_move(DIRECTION dir)
{
    C2S_Move p{};
    p.size      = sizeof(p);
    p.type      = C2S_MOVE;
    p.dir       = dir;
    p.move_time = g_move_time;
    net_send(&p, p.size);
}

static void send_attack()
{
    C2S_Attack p{};
    p.size = sizeof(p);
    p.type = C2S_ATTACK;
    net_send(&p, p.size);
}

static void send_skill()
{
    C2S_Skill p{};
    p.size = sizeof(p);
    p.type = C2S_SKILL;
    net_send(&p, p.size);
}

static void send_use_item(ITEM_TYPE item_type)
{
    C2S_UseItem p{};
    p.size      = sizeof(p);
    p.type      = C2S_USE_ITEM;
    p.item_type = item_type;
    net_send(&p, p.size);
}

static void send_chat(const std::string& msg)
{
    if (msg.empty()) return;
    C2S_Chat p{};
    p.size = sizeof(p);
    p.type = C2S_CHAT;
    strncpy_s(p.msg, msg.c_str(), MAX_CHAT_LEN - 1);
    net_send(&p, p.size);
}

// ── 패킷 처리 ─────────────────────────────────────────────────────
static char s_login_name[MAX_NAME_LEN];

static void handle_packet(unsigned char* p)
{
    PACKET_TYPE type = *reinterpret_cast<PACKET_TYPE*>(&p[1]);
    switch (type)
    {
    case S2C_LOGIN_RESULT:
    {
        auto* pkt = reinterpret_cast<S2C_LoginResult*>(p);
        if (pkt->success) {
            push_msg("Login OK.");
            g_state = AppState::PLAYING;
        } else {
            push_msg(std::string("Login failed: ") + pkt->message);
            g_running = false;
            closesocket(g_sock);
            g_sock  = INVALID_SOCKET;
            g_state = AppState::LOGIN;
        }
        break;
    }
    case S2C_AVATAR_INFO:
    {
        auto* pkt = reinterpret_cast<S2C_AvatarInfo*>(p);
        g_my_id       = pkt->playerId;
        g_my_x        = pkt->x;
        g_my_y        = pkt->y;
        g_my_hp       = pkt->hp;
        g_my_max_hp   = pkt->max_hp;
        g_my_level    = pkt->level;
        g_my_xp       = pkt->exp;
        g_my_exp_next = pkt->exp_next;
        break;
    }
    case S2C_ADD_PLAYER:
    {
        auto* pkt = reinterpret_cast<S2C_AddPlayer*>(p);
        ObjInfo o{};
        o.id        = pkt->playerId;
        o.x         = pkt->x;
        o.y         = pkt->y;
        o.hp        = pkt->hp;
        o.max_hp    = pkt->max_hp;
        o.npc_type  = pkt->npc_type;
        o.npc_state = pkt->npc_state;
        strncpy_s(o.name, pkt->username, MAX_NAME_LEN - 1);
        std::lock_guard<std::mutex> lk(g_objs_lock);
        g_objs[o.id] = o;
        break;
    }
    case S2C_REMOVE_PLAYER:
    {
        auto* pkt = reinterpret_cast<S2C_RemovePlayer*>(p);
        std::lock_guard<std::mutex> lk(g_objs_lock);
        g_objs.erase(pkt->playerId);
        break;
    }
    case S2C_MOVE_PLAYER:
    {
        auto* pkt = reinterpret_cast<S2C_MovePlayer*>(p);
        if (pkt->playerId == g_my_id) {
            g_my_x = pkt->x;
            g_my_y = pkt->y;
        } else {
            std::lock_guard<std::mutex> lk(g_objs_lock);
            auto it = g_objs.find(pkt->playerId);
            if (it != g_objs.end()) {
                it->second.x = pkt->x;
                it->second.y = pkt->y;
            }
        }
        break;
    }
    case S2C_CHAT:
    {
        auto* pkt = reinterpret_cast<S2C_Chat*>(p);
        pkt->sender_name[MAX_NAME_LEN - 1] = 0;
        pkt->msg[MAX_CHAT_LEN - 1]         = 0;
        push_msg(std::string("[") + pkt->sender_name + "] " + pkt->msg);
        break;
    }
    case S2C_STAT_INFO:
    {
        auto* pkt = reinterpret_cast<S2C_StatInfo*>(p);
        if (pkt->object_id == g_my_id) {
            g_my_hp       = pkt->hp;
            g_my_max_hp   = pkt->max_hp;
            if (pkt->level > 0) {
                g_my_level    = pkt->level;
                g_my_xp       = pkt->exp;
                g_my_exp_next = pkt->exp_next;
            }
        } else {
            std::lock_guard<std::mutex> lk(g_objs_lock);
            auto it = g_objs.find(pkt->object_id);
            if (it != g_objs.end()) {
                it->second.hp        = pkt->hp;
                it->second.max_hp    = pkt->max_hp;
                it->second.npc_state = pkt->npc_state;
            }
        }
        break;
    }
    case S2C_ITEM_APPEAR:
    {
        auto* pkt = reinterpret_cast<S2C_ItemAppear*>(p);
        ItemInfo info { pkt->item_id, pkt->x, pkt->y, pkt->item_type };
        std::lock_guard<std::mutex> lk(g_items_lock);
        g_world_items_cl[info.id] = info;
        break;
    }
    case S2C_ITEM_REMOVE:
    {
        auto* pkt = reinterpret_cast<S2C_ItemRemove*>(p);
        std::lock_guard<std::mutex> lk(g_items_lock);
        g_world_items_cl.erase(pkt->item_id);
        break;
    }
    case S2C_ITEM_ADD:
    {
        auto* pkt = reinterpret_cast<S2C_ItemAdd*>(p);
        int idx = (int)pkt->item_type - 1;
        if (idx >= 0 && idx < ITEM_SLOT_COUNT)
            g_inventory[idx] = pkt->count;
        break;
    }
    case S2C_QUEST_UPDATE:
    {
        auto* pkt = reinterpret_cast<S2C_QuestUpdate*>(p);
        if (pkt->quest_id < 2) {
            g_quests[pkt->quest_id].current = pkt->current;
            g_quests[pkt->quest_id].target  = pkt->target;
            g_quests[pkt->quest_id].state   = pkt->state;
        }
        break;
    }
    case S2C_DAMAGE_INFO:
    {
        auto* pkt = reinterpret_cast<S2C_DamageInfo*>(p);

        // g_objs에서 대상 HP 갱신
        {
            std::lock_guard<std::mutex> lk(g_objs_lock);
            auto it = g_objs.find(pkt->target_id);
            if (it != g_objs.end()) it->second.hp = pkt->target_hp;
        }

        // 전투 메시지 생성
        bool atk_is_me  = (pkt->attacker_id == g_my_id);
        bool tgt_is_me  = (pkt->target_id   == g_my_id);
        bool atk_is_npc = (pkt->attacker_id >= NPC_ID_START);
        bool tgt_is_npc = (pkt->target_id   >= NPC_ID_START);

        char buf[128];
        if (atk_is_me)
            sprintf_s(buf, "You hit NPC(%d) for %d! HP:%d",
                pkt->target_id - NPC_ID_START, pkt->damage, pkt->target_hp);
        else if (tgt_is_me)
            sprintf_s(buf, "NPC(%d) hit You for %d! HP:%d/%d",
                pkt->attacker_id - NPC_ID_START, pkt->damage, pkt->target_hp, g_my_max_hp);
        else if (atk_is_npc && !tgt_is_npc)
            sprintf_s(buf, "NPC hit PC#%d for %d (HP:%d)",
                pkt->target_id, pkt->damage, pkt->target_hp);
        else
            sprintf_s(buf, "PC#%d hit NPC(%d) for %d (HP:%d)",
                pkt->attacker_id, pkt->target_id - NPC_ID_START, pkt->damage, pkt->target_hp);

        push_msg(buf);
        break;
    }
    }
}

// ── 수신 스레드 ───────────────────────────────────────────────────
static void recv_thread_fn()
{
    send_login(s_login_name);

    char buf[4096];
    int  stored = 0;

    while (g_running) {
        int n = ::recv(g_sock, buf + stored, (int)sizeof(buf) - stored, 0);
        if (n <= 0) {
            push_msg("Disconnected from server.");
            g_running = false;
            break;
        }
        stored += n;
        while (stored > 0) {
            unsigned char pkt_sz = (unsigned char)buf[0];
            if (pkt_sz == 0 || stored < (int)pkt_sz) break;
            handle_packet(reinterpret_cast<unsigned char*>(buf));
            memmove(buf, buf + pkt_sz, stored - pkt_sz);
            stored -= pkt_sz;
        }
    }
}

// ── 서버 연결 ─────────────────────────────────────────────────────
static bool do_connect(const std::string& ip, const std::string& name)
{
    g_sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (g_sock == INVALID_SOCKET) return false;

    SOCKADDR_IN addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(PORT);
    if (InetPtonA(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        closesocket(g_sock); g_sock = INVALID_SOCKET; return false;
    }
    if (::connect(g_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closesocket(g_sock); g_sock = INVALID_SOCKET; return false;
    }

    strncpy_s(s_login_name, name.c_str(), MAX_NAME_LEN - 1);
    g_running = true;
    std::thread(recv_thread_fn).detach();
    return true;
}

// ── 텍스트 그림자 ──────────────────────────────────────────────────
static void draw_text_shadowed(sf::RenderWindow& win, sf::Text& text,
                                sf::Vector2f pos, sf::Color color)
{
    text.setFillColor(sf::Color(0, 0, 0, 180));
    text.setPosition(pos + sf::Vector2f(1.f, 1.f));
    win.draw(text);
    text.setFillColor(color);
    text.setPosition(pos);
    win.draw(text);
}

// HP 바 (월드 좌표계)
static void draw_hp_bar(sf::RenderWindow& win, float wx, float wy,
                         short hp, short max_hp)
{
    if (max_hp <= 0) return;
    constexpr float W = 0.8f, H = 0.08f;
    float ratio = std::max(0.f, (float)hp / max_hp);

    sf::RectangleShape bg({ W, H });
    bg.setFillColor(sf::Color(80, 0, 0));
    bg.setPosition(wx + 0.1f, wy - 0.18f);
    win.draw(bg);

    if (ratio > 0.f) {
        sf::RectangleShape bar({ W * ratio, H });
        bar.setFillColor(ratio > 0.5f ? sf::Color(0, 200, 50)
                        : ratio > 0.25f ? sf::Color(220, 180, 0)
                        : sf::Color(220, 40, 40));
        bar.setPosition(wx + 0.1f, wy - 0.18f);
        win.draw(bar);
    }
}

// ── 로그인 화면 ───────────────────────────────────────────────────
static void draw_login(sf::RenderWindow& win, sf::Font& font)
{
    win.clear(sf::Color(18, 18, 28));

    sf::Text title("SIMPLEST MMORPG", font, 30);
    title.setFillColor(sf::Color(180, 200, 255));
    title.setStyle(sf::Text::Bold);
    title.setPosition(WIN_W / 2.f - title.getLocalBounds().width / 2.f, 150.f);
    win.draw(title);

    auto draw_field = [&](const std::string& label, const std::string& value,
                          float y, bool focused)
    {
        sf::Text lbl(label, font, 15);
        lbl.setFillColor(sf::Color(150, 150, 160));
        lbl.setPosition(115.f, y + 4.f);
        win.draw(lbl);

        sf::RectangleShape box(sf::Vector2f(220.f, 28.f));
        box.setPosition(225.f, y);
        box.setFillColor(sf::Color(35, 35, 50));
        box.setOutlineColor(focused ? sf::Color(100, 140, 255) : sf::Color(70, 70, 90));
        box.setOutlineThickness(1.5f);
        win.draw(box);

        sf::Text val(value + (focused ? "|" : ""), font, 15);
        val.setFillColor(sf::Color(220, 220, 230));
        val.setPosition(231.f, y + 4.f);
        win.draw(val);
    };

    draw_field("Server IP :", g_input_ip,   240.f, g_focus == 0);
    draw_field("Username  :", g_input_name, 285.f, g_focus == 1);

    std::string hint_str = "[Tab] switch field    [Enter] connect";
    sf::Text hint(hint_str, font, 13);
    hint.setFillColor(sf::Color(90, 90, 100));
    hint.setPosition(WIN_W / 2.f - hint.getLocalBounds().width / 2.f, 340.f);
    win.draw(hint);

    std::lock_guard<std::mutex> lk(g_msgs_lock);
    float my = 390.f;
    for (auto& m : g_msgs) {
        sf::Text mt(m, font, 13);
        mt.setFillColor(sf::Color(210, 90, 90));
        mt.setPosition(WIN_W / 2.f - mt.getLocalBounds().width / 2.f, my);
        win.draw(mt);
        my += 18.f;
    }
}

// ── 게임 화면 ─────────────────────────────────────────────────────
static void draw_game(sf::RenderWindow& win, sf::Font& font)
{
    // 게임 뷰 (월드 좌표 = 타일 단위)
    sf::View game_view(sf::FloatRect(
        g_my_x - VSIZE / 2.f,
        g_my_y - VSIZE / 2.f,
        (float)VSIZE, (float)VSIZE
    ));
    game_view.setViewport(sf::FloatRect(0.f, 0.f, 1.f, GAME_VP_H));
    win.setView(game_view);

    // 배경 바닥 — 인-바운드 영역 한 장의 반복 텍스처로 렌더링
    {
        int fx0 = std::max(0,            g_my_x - VSIZE / 2 - 1);
        int fy0 = std::max(0,            g_my_y - VSIZE / 2 - 1);
        int fx1 = std::min(WORLD_WIDTH,  g_my_x + VSIZE / 2 + VSIZE);
        int fy1 = std::min(WORLD_HEIGHT, g_my_y + VSIZE / 2 + VSIZE);
        int fw = fx1 - fx0, fh = fy1 - fy0;
        if (fw > 0 && fh > 0) {
            sf::RectangleShape fr(sf::Vector2f((float)fw, (float)fh));
            if (g_tex.ok) {
                fr.setTexture(&g_tex.floor);
                fr.setTextureRect(sf::IntRect(0, 0, fw * 16, fh * 16));
            } else {
                fr.setFillColor(sf::Color(28, 28, 36));
            }
            fr.setPosition((float)fx0, (float)fy0);
            win.draw(fr);
        }
    }

    // 장애물 렌더링 — max(w,h)<=2: 돌(rock), 그 외: 성벽(castle_wall)
    {
        float vx0 = g_my_x - VSIZE / 2.f, vy0 = g_my_y - VSIZE / 2.f;
        float vx1 = vx0 + VSIZE,           vy1 = vy0 + VSIZE;
        sf::RectangleShape wall;
        for (auto& r : g_obstacles) {
            if (r.x + r.w < vx0 || r.x > vx1) continue;
            if (r.y + r.h < vy0 || r.y > vy1) continue;
            wall.setSize(sf::Vector2f((float)r.w, (float)r.h));
            bool is_rock = (std::max(r.w, r.h) <= 2);
            if (g_tex.ok) {
                sf::Texture& tex = is_rock ? g_tex.wall : g_tex.castle_wall;
                wall.setTexture(&tex);
                wall.setTextureRect(sf::IntRect(0, 0, r.w * 16, r.h * 16));
            } else {
                wall.setFillColor(is_rock ? sf::Color(80, 65, 50)
                                          : sf::Color(90, 85, 110));
                wall.setOutlineColor(sf::Color(50, 40, 30));
                wall.setOutlineThickness(0.04f);
            }
            wall.setPosition((float)r.x, (float)r.y);
            win.draw(wall);
        }
    }

    // 아이템 렌더링 (오브젝트 아래 레이어)
    {
        std::vector<ItemInfo> item_snaps;
        {
            std::lock_guard<std::mutex> lk(g_items_lock);
            item_snaps.reserve(g_world_items_cl.size());
            for (auto& [id, item] : g_world_items_cl) item_snaps.push_back(item);
        }
        static const sf::Color item_tints[ITEM_SLOT_COUNT+1] = {
            sf::Color(255,220, 30),  // 0=none
            sf::Color(255, 80, 80),  // 1=HP 포션
            sf::Color(255,150, 40),  // 2=대형 HP 포션
            sf::Color(255,220,  0),  // 3=엘릭서
            sf::Color( 80,200,255),  // 4=공격력 강화
            sf::Color(120,255,120),  // 5=방어력 강화
            sf::Color(100,180,255),  // 6=이동속도
        };
        float vx0 = g_my_x - VSIZE / 2.f, vy0 = g_my_y - VSIZE / 2.f;
        float vx1 = vx0 + VSIZE,           vy1 = vy0 + VSIZE;
        for (auto& item : item_snaps) {
            if (item.x < vx0 || item.x > vx1 || item.y < vy0 || item.y > vy1) continue;
            int ci = (int)item.item_type;
            sf::Color tint = (ci >= 1 && ci <= ITEM_SLOT_COUNT) ? item_tints[ci] : item_tints[0];
            if (g_tex.ok) {
                // 발광 후광 (아이템 강조)
                sf::CircleShape glow(0.45f);
                glow.setFillColor(sf::Color(tint.r/3, tint.g/3, tint.b/3, 140));
                glow.setPosition(item.x + 0.05f, item.y + 0.05f);
                win.draw(glow);
                // 포션 스프라이트
                sf::Sprite spr(g_tex.item_potion);
                spr.setScale(SPR_S, SPR_S);
                spr.setColor(tint);
                spr.setPosition((float)item.x, (float)item.y);
                win.draw(spr);
            } else {
                sf::CircleShape item_circle(0.24f);
                item_circle.setOutlineThickness(0.06f);
                item_circle.setFillColor(tint);
                item_circle.setOutlineColor(sf::Color(tint.r/2, tint.g/2, tint.b/2));
                item_circle.setPosition(item.x + 0.26f, item.y + 0.26f);
                win.draw(item_circle);
            }
        }
    }

    // 오브젝트 스냅샷 (lock 최소화)
    struct Snap { ObjInfo info; };
    std::vector<Snap> snaps;
    {
        std::lock_guard<std::mutex> lk(g_objs_lock);
        snaps.reserve(g_objs.size());
        for (auto& [id, o] : g_objs) snaps.push_back({ o });
    }

    // 스프라이트 드로우 헬퍼
    auto draw_spr = [&](sf::Texture& tex, float wx, float wy,
                        sf::Color tint = sf::Color::White) {
        sf::Sprite spr(tex);
        spr.setScale(SPR_S, SPR_S);
        spr.setColor(tint);
        spr.setPosition(wx, wy);
        win.draw(spr);
    };
    // 폴백용 사각형 드로우 헬퍼
    auto draw_rect = [&](float wx, float wy, sf::Color c) {
        sf::RectangleShape r(sf::Vector2f(0.84f, 0.84f));
        r.setFillColor(c);
        r.setPosition(wx + 0.08f, wy + 0.08f);
        win.draw(r);
    };

    for (auto& s : snaps) {
        float wx = (float)s.info.x, wy = (float)s.info.y;
        float ratio = (s.info.max_hp > 0)
                    ? std::max(0.f, (float)s.info.hp / s.info.max_hp) : 0.f;

        if (s.info.npc_type == NPC_BOSS) {
            // 페이즈별 틴트
            sf::Color tint = (ratio > 0.66f) ? sf::Color(255, 255, 180)
                           : (ratio > 0.33f) ? sf::Color(255, 160,  60)
                                             : sf::Color(255,  80,  80);
            sf::Texture& boss_tex = (ratio <= 0.33f) ? g_tex.npc_boss_p3 : g_tex.npc_boss;

            // 보스 후광 (2×2 크기에 맞게 확대)
            float glow_r = 1.2f;
            sf::CircleShape glow(glow_r);
            glow.setFillColor(sf::Color(tint.r/4, tint.g/4, tint.b/4, 140));
            glow.setOutlineColor(sf::Color(tint.r, tint.g, tint.b, 200));
            glow.setOutlineThickness(0.08f);
            glow.setPosition(wx + 0.5f - glow_r, wy + 0.5f - glow_r);
            win.draw(glow);

            // 보스 스프라이트 2×2 타일
            if (g_tex.ok) {
                sf::Sprite spr(boss_tex);
                spr.setScale(SPR_S * 2, SPR_S * 2);
                spr.setColor(tint);
                spr.setPosition(wx - 0.5f, wy - 0.5f);
                win.draw(spr);
            } else {
                sf::CircleShape bc(0.9f);
                bc.setFillColor(tint);
                bc.setOutlineColor(sf::Color(255, 230, 80));
                bc.setOutlineThickness(0.10f);
                bc.setPosition(wx - 0.4f, wy - 0.4f);
                win.draw(bc);
            }

            // 보스 HP바 (2타일 폭)
            {
                constexpr float BW = 1.8f, BH = 0.10f;
                float bx0 = wx - 0.5f + 0.1f;
                float by0 = wy - 0.5f - 0.18f;
                sf::RectangleShape bg({ BW, BH });
                bg.setFillColor(sf::Color(80, 0, 0));
                bg.setPosition(bx0, by0);
                win.draw(bg);
                if (ratio > 0.f) {
                    sf::RectangleShape bar({ BW * ratio, BH });
                    bar.setFillColor(ratio > 0.5f ? sf::Color(0, 200, 50)
                                   : ratio > 0.25f ? sf::Color(220, 180, 0)
                                   : sf::Color(220, 40, 40));
                    bar.setPosition(bx0, by0);
                    win.draw(bar);
                }
            }
            continue;
        }

        if (s.info.npc_type == NPC_PC) {
            if (g_tex.ok) draw_spr(g_tex.player_other, wx, wy);
            else          draw_rect(wx, wy, sf::Color(55, 185, 80));
            draw_hp_bar(win, wx, wy, s.info.hp, s.info.max_hp);
        } else if (s.info.npc_type == NPC_AGRO) {
            sf::Color tint = (s.info.npc_state == NPC_STATE_CHASE)
                           ? sf::Color(255, 120, 120)   // 추격: 붉은 틴트
                           : sf::Color::White;
            if (g_tex.ok) draw_spr(g_tex.npc_agro, wx, wy, tint);
            else          draw_rect(wx, wy, (s.info.npc_state == NPC_STATE_CHASE)
                                            ? sf::Color(255, 40, 40) : sf::Color(200, 100, 30));
            draw_hp_bar(win, wx, wy, s.info.hp, s.info.max_hp);
        } else { // PEACE
            if (g_tex.ok) draw_spr(g_tex.npc_peace, wx, wy);
            else          draw_rect(wx, wy, sf::Color(120, 140, 90));
            draw_hp_bar(win, wx, wy, s.info.hp, s.info.max_hp);
        }
    }

    // 내 캐릭터: 파란 기사 (선택 링 추가)
    {
        sf::CircleShape sel(0.52f);
        sel.setFillColor(sf::Color::Transparent);
        sel.setOutlineColor(sf::Color(100, 160, 255, 200));
        sel.setOutlineThickness(0.06f);
        sel.setPosition(g_my_x + 0.5f - 0.52f, g_my_y + 0.5f - 0.52f);
        win.draw(sel);
    }
    if (g_tex.ok) draw_spr(g_tex.player_me, (float)g_my_x, (float)g_my_y);
    else          draw_rect((float)g_my_x, (float)g_my_y, sf::Color(70, 115, 255));
    draw_hp_bar(win, (float)g_my_x, (float)g_my_y, g_my_hp, g_my_max_hp);

    // ── 공격/스킬 이펙트 ──────────────────────────────────────────
    if (g_effect.type != EffectType::NONE) {
        using ms = std::chrono::milliseconds;
        int eff_ms = (int)std::chrono::duration_cast<ms>(
            std::chrono::steady_clock::now() - g_effect.start).count();
        int dur = (g_effect.type == EffectType::SKILL) ? 350 : 220;

        if (eff_ms < dur) {
            float t = 1.f - (float)eff_ms / dur;   // 1→0 페이드
            sf::Uint8 a = (sf::Uint8)(255 * t);

            if (g_effect.type == EffectType::ATTACK) {
                // 4방향 플래시 타일
                sf::RectangleShape flash(sf::Vector2f(0.92f, 0.92f));
                flash.setFillColor(sf::Color(255, 220, 60, (sf::Uint8)(160 * t)));
                flash.setOutlineColor(sf::Color(255, 120, 0, a));
                flash.setOutlineThickness(0.05f);
                const int dirs[4][2] = {{0,-1},{0,1},{-1,0},{1,0}};
                for (auto& d : dirs) {
                    flash.setPosition(g_my_x + d[0] + 0.04f, g_my_y + d[1] + 0.04f);
                    win.draw(flash);
                }
                // 십자 슬래시 선 (가로 / 세로)
                sf::RectangleShape slash(sf::Vector2f(3.1f, 0.10f));
                slash.setFillColor(sf::Color(255, 255, 220, a));
                slash.setOrigin(1.55f, 0.05f);
                slash.setPosition(g_my_x + 0.5f, g_my_y + 0.5f);
                win.draw(slash);
                slash.setSize(sf::Vector2f(0.10f, 3.1f));
                slash.setOrigin(0.05f, 1.55f);
                win.draw(slash);

            } else { // SKILL
                float expand = (float)eff_ms / dur;   // 0→1 팽창

                // 바닥 보라 플래시 (3x3)
                sf::RectangleShape area(sf::Vector2f(3.f, 3.f));
                area.setFillColor(sf::Color(120, 30, 220, (sf::Uint8)(80 * t)));
                area.setPosition(g_my_x - 1.f, g_my_y - 1.f);
                win.draw(area);

                // 팽창하는 외곽 링 (크게)
                float r1 = 0.4f + expand * 2.2f;
                sf::CircleShape ring1(r1);
                ring1.setFillColor(sf::Color::Transparent);
                ring1.setOutlineColor(sf::Color(200, 80, 255, a));
                ring1.setOutlineThickness(0.12f);
                ring1.setPosition(g_my_x + 0.5f - r1, g_my_y + 0.5f - r1);
                win.draw(ring1);

                // 빠르게 팽창하는 내부 링 (작게)
                float r2 = 0.2f + expand * 1.4f;
                sf::CircleShape ring2(r2);
                ring2.setFillColor(sf::Color::Transparent);
                ring2.setOutlineColor(sf::Color(240, 160, 255, (sf::Uint8)(a * 0.7f)));
                ring2.setOutlineThickness(0.08f);
                ring2.setPosition(g_my_x + 0.5f - r2, g_my_y + 0.5f - r2);
                win.draw(ring2);

                // 중앙 섬광
                sf::CircleShape flash(0.3f * t);
                flash.setFillColor(sf::Color(220, 160, 255, (sf::Uint8)(200 * t)));
                flash.setPosition(g_my_x + 0.5f - 0.3f * t, g_my_y + 0.5f - 0.3f * t);
                win.draw(flash);
            }
        } else {
            g_effect.type = EffectType::NONE;
        }
    }

    // 픽셀 뷰로 전환
    win.setView(win.getDefaultView());

    // ── 인벤토리 (좌상단 오버레이) ──────────────────────────────────
    {
        static const sf::Color slot_col[ITEM_SLOT_COUNT] = {
            sf::Color(180, 50,  50),  // 1 HP 포션
            sf::Color(200,110,  30),  // 2 대형 HP 포션
            sf::Color(190,160,  10),  // 3 엘릭서
            sf::Color( 30,190,190),   // 4 공격력 강화
            sf::Color(130, 40,200),   // 5 방어력 강화
            sf::Color( 40,130,230),   // 6 이동속도 증가
        };
        static const char* slot_label[ITEM_SLOT_COUNT] = {
            "HP", "Hi", "Elx", "ATK", "DEF", "SPD"
        };
        constexpr float SW = 72.f, SH = 22.f, SG = 3.f;
        constexpr float INV_X = 5.f, INV_Y = 5.f;

        // 반투명 배경
        float bg_w = 3.f * (SW + SG) - SG + 4.f;
        float bg_h = 2.f * (SH + 3.f) - 3.f + 4.f;
        sf::RectangleShape inv_bg({ bg_w, bg_h });
        inv_bg.setFillColor(sf::Color(8, 8, 20, 180));
        inv_bg.setPosition(INV_X - 2.f, INV_Y - 2.f);
        win.draw(inv_bg);

        sf::Text st("", font, 9);
        for (int s = 0; s < ITEM_SLOT_COUNT; ++s) {
            int row = s / 3, col = s % 3;
            float sx = INV_X + col * (SW + SG);
            float sy = INV_Y + row * (SH + 3.f);

            int cnt = g_inventory[s];
            sf::Color base = slot_col[s];
            sf::Color fill = (cnt > 0)
                ? sf::Color(base.r, base.g, base.b, 200)
                : sf::Color(base.r/5, base.g/5, base.b/5, 160);

            sf::RectangleShape slot({ SW, SH });
            slot.setFillColor(fill);
            slot.setOutlineColor(sf::Color(140, 140, 170, 160));
            slot.setOutlineThickness(0.8f);
            slot.setPosition(sx, sy);
            win.draw(slot);

            char lbuf[12];
            sprintf_s(lbuf, "[%d]%s", s + 1, slot_label[s]);
            st.setString(lbuf);
            st.setFillColor(cnt > 0 ? sf::Color(255,255,255) : sf::Color(110,110,110));
            st.setPosition(sx + 2.f, sy + 2.f);
            win.draw(st);

            char cbuf[8];
            sprintf_s(cbuf, "x%d", cnt);
            st.setString(cbuf);
            st.setFillColor(cnt > 0 ? sf::Color(255,240,80) : sf::Color(70,70,70));
            float tw = st.getLocalBounds().width;
            st.setPosition(sx + SW - tw - 3.f, sy + 11.f);
            win.draw(st);
        }
    }

    sf::Text lbl("", font, 11);
    for (auto& s : snaps) {
        sf::Vector2i sp = win.mapCoordsToPixel(
            sf::Vector2f(s.info.x + 0.5f, s.info.y + 0.5f), game_view);
        if (sp.x < 0 || sp.x > WIN_W || sp.y < 0 || sp.y > WIN_H - UI_H) continue;

        sf::Color tc;
        if (s.info.npc_type == NPC_BOSS) {
            tc = sf::Color(255, 215, 0);    // 보스 — 금색
        } else if (s.info.npc_type == NPC_PC) {
            tc = sf::Color(140, 230, 140);
        } else if (s.info.npc_state == NPC_STATE_CHASE) {
            tc = sf::Color(255, 80, 80);
        } else if (s.info.npc_type == NPC_AGRO) {
            tc = sf::Color(240, 170, 80);
        } else {
            tc = sf::Color(180, 200, 140);
        }
        lbl.setString(s.info.name);
        draw_text_shadowed(win, lbl,
            sf::Vector2f(sp.x - lbl.getLocalBounds().width / 2.f, (float)sp.y - 14.f), tc);
    }

    // ── 퀘스트 트래커 (우상단 항상 표시) ────────────────────────────
    {
        constexpr float QW = 152.f;
        constexpr float QX = WIN_W - QW - 6.f;
        constexpr float QY = 6.f;
        constexpr float ROW_H = 40.f;
        float qh = 18.f + QUEST_COUNT * ROW_H;

        sf::RectangleShape qbg({ QW, qh });
        qbg.setFillColor(sf::Color(8, 8, 20, 210));
        qbg.setOutlineColor(sf::Color(70, 70, 110));
        qbg.setOutlineThickness(1.f);
        qbg.setPosition(QX, QY);
        win.draw(qbg);

        sf::Text qhdr("[Q] Quest Tracker", font, 10);
        qhdr.setFillColor(sf::Color(160, 160, 210));
        qhdr.setStyle(sf::Text::Bold);
        qhdr.setPosition(QX + 5.f, QY + 3.f);
        win.draw(qhdr);

        const sf::Color q_colors[2] = { sf::Color(240, 170, 60), sf::Color(255, 215, 0) };
        float row_y = QY + 18.f;
        for (int qi = 0; qi < 2; ++qi) {
            auto& q = g_quests[qi];
            float ratio = (q.target > 0) ? std::min(1.f, (float)q.current / q.target) : 0.f;

            sf::Text qname(q.name, font, 10);
            qname.setFillColor(q_colors[qi]);
            qname.setPosition(QX + 5.f, row_y);
            win.draw(qname);

            // 진행 바 배경
            constexpr float BW = QW - 10.f;
            sf::RectangleShape bar_bg({ BW, 9.f });
            bar_bg.setFillColor(sf::Color(25, 25, 45));
            bar_bg.setPosition(QX + 5.f, row_y + 13.f);
            win.draw(bar_bg);

            if (ratio > 0.f) {
                sf::RectangleShape bar({ BW * ratio, 9.f });
                bar.setFillColor(ratio >= 1.f ? sf::Color(60, 220, 60) : q_colors[qi]);
                bar.setPosition(QX + 5.f, row_y + 13.f);
                win.draw(bar);
            }

            char cnt[16];
            sprintf_s(cnt, "%d/%d", q.current, q.target);
            sf::Text qcnt(cnt, font, 9);
            qcnt.setFillColor(sf::Color(190, 190, 190));
            qcnt.setPosition(QX + QW - 5.f - qcnt.getLocalBounds().width, row_y + 13.f);
            win.draw(qcnt);

            row_y += ROW_H;
        }
    }

    // ── 퀘스트 패널 (Q 토글) ─────────────────────────────────────
    if (g_quest_panel_open) {
        constexpr float PW = 240.f, PX = (WIN_W - PW) / 2.f, PY = 30.f;
        constexpr float PROW_H = 62.f;
        float ph = 28.f + QUEST_COUNT * PROW_H + 10.f;

        // 반투명 배경
        sf::RectangleShape pbg({ PW, ph });
        pbg.setFillColor(sf::Color(8, 8, 22, 230));
        pbg.setOutlineColor(sf::Color(100, 100, 160));
        pbg.setOutlineThickness(1.5f);
        pbg.setPosition(PX, PY);
        win.draw(pbg);

        sf::Text phdr("  Quest Log", font, 13);
        phdr.setFillColor(sf::Color(200, 210, 255));
        phdr.setStyle(sf::Text::Bold);
        phdr.setPosition(PX + 8.f, PY + 6.f);
        win.draw(phdr);

        // 구분선
        sf::RectangleShape sep({ PW - 16.f, 1.f });
        sep.setFillColor(sf::Color(70, 70, 110));
        sep.setPosition(PX + 8.f, PY + 24.f);
        win.draw(sep);

        const sf::Color p_colors[2] = { sf::Color(240, 170, 60), sf::Color(255, 215, 0) };
        float py = PY + 30.f;
        for (int qi = 0; qi < 2; ++qi) {
            auto& q = g_quests[qi];
            float ratio = (q.target > 0) ? std::min(1.f, (float)q.current / q.target) : 0.f;

            // 이름
            sf::Text pname(q.name, font, 12);
            pname.setFillColor(p_colors[qi]);
            pname.setStyle(sf::Text::Bold);
            pname.setPosition(PX + 10.f, py);
            win.draw(pname);

            // 설명
            sf::Text pdesc(q.desc, font, 10);
            pdesc.setFillColor(sf::Color(160, 165, 180));
            pdesc.setPosition(PX + 10.f, py + 14.f);
            win.draw(pdesc);

            // 진행 바
            constexpr float PBW = PW - 20.f;
            sf::RectangleShape pbar_bg({ PBW, 10.f });
            pbar_bg.setFillColor(sf::Color(25, 25, 45));
            pbar_bg.setPosition(PX + 10.f, py + 28.f);
            win.draw(pbar_bg);

            if (ratio > 0.f) {
                sf::RectangleShape pbar({ PBW * ratio, 10.f });
                pbar.setFillColor(ratio >= 1.f ? sf::Color(60, 220, 60) : p_colors[qi]);
                pbar.setPosition(PX + 10.f, py + 28.f);
                win.draw(pbar);
            }

            // 카운트 + 보상
            char pcnt[32];
            sprintf_s(pcnt, "%d / %d     %s", q.current, q.target, q.reward_str);
            sf::Text pcnt_txt(pcnt, font, 10);
            pcnt_txt.setFillColor(sf::Color(180, 185, 200));
            pcnt_txt.setPosition(PX + 10.f, py + 41.f);
            win.draw(pcnt_txt);

            // 퀘스트 간 구분선
            if (qi < QUEST_COUNT - 1) {
                sf::RectangleShape qsep({ PW - 16.f, 1.f });
                qsep.setFillColor(sf::Color(50, 50, 80));
                qsep.setPosition(PX + 8.f, py + PROW_H - 2.f);
                win.draw(qsep);
            }
            py += PROW_H;
        }
    }

    // 내 이름
    lbl.setString(s_login_name);
    sf::Vector2i my_sp = win.mapCoordsToPixel(
        sf::Vector2f(g_my_x + 0.5f, g_my_y + 0.5f), game_view);
    draw_text_shadowed(win, lbl,
        sf::Vector2f(my_sp.x - lbl.getLocalBounds().width / 2.f, (float)my_sp.y - 14.f),
        sf::Color(180, 200, 255));

    // ── UI 패널 ──────────────────────────────────────────────────
    constexpr float CHAT_BOX_H = 22.f;
    constexpr float CHAT_BOX_B = 8.f;   // 패널 하단 여백
    float ui_top  = (float)(WIN_H - UI_H);
    float chat_y  = (float)WIN_H - CHAT_BOX_H - CHAT_BOX_B;   // 채팅창 Y (패널 하단 고정)
    float msg_end = chat_y - 4.f;                               // 메시지가 올라올 수 있는 최하단
    float msg_start = ui_top + 34.f;                            // 메시지 최상단 (HP바/힌트 아래)

    sf::RectangleShape ui_bg(sf::Vector2f((float)WIN_W, (float)UI_H));
    ui_bg.setPosition(0.f, ui_top);
    ui_bg.setFillColor(sf::Color(12, 12, 22));
    win.draw(ui_bg);

    sf::RectangleShape sep(sf::Vector2f((float)WIN_W, 1.f));
    sep.setPosition(0.f, ui_top);
    sep.setFillColor(sf::Color(60, 60, 90));
    win.draw(sep);

    // HP 바
    {
        float hpRatio = (g_my_max_hp > 0) ? (float)g_my_hp / g_my_max_hp : 0.f;
        hpRatio = std::max(0.f, hpRatio);
        constexpr float BAR_W = 160.f;

        sf::RectangleShape hpBg({ BAR_W, 11.f });
        hpBg.setFillColor(sf::Color(80, 0, 0));
        hpBg.setPosition(10.f, ui_top + 5.f);
        win.draw(hpBg);

        if (hpRatio > 0.f) {
            sf::RectangleShape hpBar({ BAR_W * hpRatio, 11.f });
            hpBar.setFillColor(hpRatio > 0.5f ? sf::Color(0, 200, 50)
                             : hpRatio > 0.25f ? sf::Color(220, 180, 0)
                             : sf::Color(220, 40, 40));
            hpBar.setPosition(10.f, ui_top + 5.f);
            win.draw(hpBar);
        }

        char hp_buf[32];
        sprintf_s(hp_buf, "HP %d/%d", g_my_hp, g_my_max_hp);
        sf::Text hp_text(hp_buf, font, 11);
        hp_text.setFillColor(sf::Color(210, 215, 225));
        hp_text.setPosition(12.f, ui_top + 4.f);
        win.draw(hp_text);
    }

    // EXP 바 (HP 바 오른쪽)
    {
        float expRatio = (g_my_exp_next > 0) ? std::min(1.f, (float)g_my_xp / g_my_exp_next) : 0.f;
        constexpr float EXP_X = 180.f;
        constexpr float EXP_W = 150.f;

        sf::RectangleShape expBg({ EXP_W, 11.f });
        expBg.setFillColor(sf::Color(20, 20, 60));
        expBg.setPosition(EXP_X, ui_top + 5.f);
        win.draw(expBg);

        if (expRatio > 0.f) {
            sf::RectangleShape expBar({ EXP_W * expRatio, 11.f });
            expBar.setFillColor(sf::Color(80, 120, 240));
            expBar.setPosition(EXP_X, ui_top + 5.f);
            win.draw(expBar);
        }

        char exp_buf[48];
        sprintf_s(exp_buf, "Lv.%d  XP %d/%d", g_my_level, g_my_xp, g_my_exp_next);
        sf::Text exp_text(exp_buf, font, 11);
        exp_text.setFillColor(sf::Color(180, 190, 255));
        exp_text.setPosition(EXP_X + 2.f, ui_top + 4.f);
        win.draw(exp_text);
    }

    // 조작 힌트
    char hint_buf[128];
    sprintf_s(hint_buf, "ID:%-4d X:%-4d Y:%-4d  [Arrow]Move [A]Atk [S]Skill [Q]Quest [M]Map [T]Chat",
              g_my_id, g_my_x, g_my_y);
    sf::Text hint_text(hint_buf, font, 11);
    hint_text.setFillColor(sf::Color(100, 105, 130));
    hint_text.setPosition(10.f, ui_top + 20.f);
    win.draw(hint_text);

    // 스킬 쿨타임 바
    {
        auto now_sk = std::chrono::steady_clock::now();
        int elapsed_sk = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
            now_sk - g_last_skill_time).count();
        float sk_ratio = std::min(1.f, (float)elapsed_sk / SKILL_CD_MS);

        constexpr float SK_X = 346.f, SK_W = 186.f;
        sf::RectangleShape skBg({ SK_W, 11.f });
        skBg.setFillColor(sf::Color(30, 20, 50));
        skBg.setPosition(SK_X, ui_top + 5.f);
        win.draw(skBg);

        if (sk_ratio > 0.f) {
            sf::RectangleShape skBar({ SK_W * sk_ratio, 11.f });
            skBar.setFillColor(sk_ratio >= 1.f ? sf::Color(120, 60, 220) : sf::Color(70, 40, 130));
            skBar.setPosition(SK_X, ui_top + 5.f);
            win.draw(skBar);
        }

        char sk_buf[32];
        if (sk_ratio >= 1.f)
            sprintf_s(sk_buf, "Skill READY");
        else
            sprintf_s(sk_buf, "Skill %.1fs", (SKILL_CD_MS - elapsed_sk) / 1000.f);
        sf::Text sk_text(sk_buf, font, 11);
        sk_text.setFillColor(sk_ratio >= 1.f ? sf::Color(200, 160, 255) : sf::Color(120, 100, 160));
        sk_text.setPosition(SK_X + 2.f, ui_top + 4.f);
        win.draw(sk_text);
    }

    // 메시지 로그 (msg_start 부터 아래로, msg_end 넘으면 클리핑)
    {
        std::lock_guard<std::mutex> lk(g_msgs_lock);
        sf::Text msg_text("", font, 12);
        float my = msg_start;
        for (auto& m : g_msgs) {
            if (my + MSG_LINE_H > msg_end) break;   // 채팅창 위에서 짤림 방지
            msg_text.setString(sf::String::fromUtf8(m.begin(), m.end()));
            sf::Color mc = (m.size() > 0 && m[0] == '[') ? sf::Color(210, 210, 255) :
                           (m.find("hit") != std::string::npos) ? sf::Color(255, 165, 60) :
                           sf::Color(100, 195, 100);
            msg_text.setFillColor(mc);
            msg_text.setPosition(10.f, my);
            win.draw(msg_text);
            my += (float)MSG_LINE_H;
        }
    }

    // 채팅 입력창 (하단 고정, 항상 테두리 표시 / 비활성 시 어둡게)
    {
        sf::RectangleShape chat_box(sf::Vector2f((float)WIN_W - 20.f, CHAT_BOX_H));
        chat_box.setPosition(10.f, chat_y);
        chat_box.setFillColor(sf::Color(22, 22, 38));
        chat_box.setOutlineColor(g_chat_mode ? sf::Color(100, 140, 255) : sf::Color(50, 50, 70));
        chat_box.setOutlineThickness(1.f);
        win.draw(chat_box);

        std::string chat_display = g_chat_mode ? (">" + g_chat_input + "|")
                                               : "[T] 채팅 입력";
        sf::Text chat_text(sf::String::fromUtf8(chat_display.begin(), chat_display.end()), font, 12);
        chat_text.setFillColor(g_chat_mode ? sf::Color(220, 220, 245) : sf::Color(70, 70, 90));
        chat_text.setPosition(14.f, chat_y + 4.f);
        win.draw(chat_text);
    }

    // ── M키 월드 지도 오버레이 ────────────────────────────────────────
    if (g_map_open) {
        constexpr float MM_PAD  = 18.f;
        constexpr float MM_SIZE = (float)WIN_W - MM_PAD * 2;   // 504 px
        constexpr float MM_X    = MM_PAD;
        constexpr float MM_Y    = MM_PAD;
        constexpr float MM_S    = MM_SIZE / (float)WORLD_WIDTH; // 504/2000 = 0.252

        // 반투명 배경 (게임 뷰 영역 전체)
        sf::RectangleShape mm_bg({ (float)WIN_W, (float)(WIN_H - UI_H) });
        mm_bg.setFillColor(sf::Color(0, 0, 0, 210));
        mm_bg.setPosition(0.f, 0.f);
        win.draw(mm_bg);

        // 맵 외곽선
        sf::RectangleShape mm_border({ MM_SIZE + 2.f, MM_SIZE + 2.f });
        mm_border.setFillColor(sf::Color(10, 10, 18));
        mm_border.setOutlineColor(sf::Color(90, 90, 140));
        mm_border.setOutlineThickness(1.5f);
        mm_border.setPosition(MM_X - 1.f, MM_Y - 1.f);
        win.draw(mm_border);

        // 장애물 (성벽만 표시, 소형 돌은 생략)
        for (auto& r : g_obstacles) {
            bool is_rock = (std::max(r.w, r.h) <= 2);
            float obs_w = r.w * MM_S;
            float obs_h = r.h * MM_S;
            if (is_rock) {
                if (obs_w < 1.5f) obs_w = 1.5f;
                if (obs_h < 1.5f) obs_h = 1.5f;
            }
            sf::RectangleShape obs({ obs_w, obs_h });
            obs.setFillColor(is_rock ? sf::Color(80, 65, 50, 200)
                                     : sf::Color(100, 95, 130, 240));
            obs.setPosition(MM_X + r.x * MM_S, MM_Y + r.y * MM_S);
            win.draw(obs);
        }

        // 다른 오브젝트
        {
            std::lock_guard<std::mutex> lk(g_objs_lock);
            for (auto& [id, o] : g_objs) {
                float dot_r;
                sf::Color dc;
                if (o.npc_type == NPC_PC) {
                    dc = sf::Color(80, 220, 80);   dot_r = 3.5f;
                } else if (o.npc_type == NPC_BOSS) {
                    dc = sf::Color(255, 215, 0);   dot_r = 5.f;
                } else {
                    continue;  // Peace/Agro는 미니맵 생략
                }
                sf::CircleShape dot(dot_r);
                dot.setFillColor(dc);
                dot.setPosition(MM_X + o.x * MM_S - dot_r, MM_Y + o.y * MM_S - dot_r);
                win.draw(dot);
            }
        }

        // 내 위치 (흰 테두리 파란 점)
        constexpr float MY_R = 5.f;
        sf::CircleShape my_dot(MY_R);
        my_dot.setFillColor(sf::Color(80, 140, 255));
        my_dot.setOutlineColor(sf::Color::White);
        my_dot.setOutlineThickness(1.5f);
        my_dot.setPosition(MM_X + g_my_x * MM_S - MY_R, MM_Y + g_my_y * MM_S - MY_R);
        win.draw(my_dot);

        // 범례 & 안내
        sf::Text mm_title("  World Map  [M] to close", font, 11);
        mm_title.setFillColor(sf::Color(170, 175, 220));
        mm_title.setPosition(MM_X, MM_Y + MM_SIZE + 4.f);
        win.draw(mm_title);

        // 범례 점들
        struct Legend { float r; sf::Color c; const char* label; };
        static const Legend legends[] = {
            { 5.f, sf::Color(80, 140, 255), "You" },
            { 3.5f, sf::Color(80, 220, 80), "Player" },
            { 5.f, sf::Color(255, 215, 0),  "Boss" },
        };
        float lx = MM_X + MM_SIZE - 130.f;
        float ly = MM_Y + MM_SIZE + 2.f;
        sf::Text leg("", font, 10);
        for (auto& l : legends) {
            sf::CircleShape ld(l.r * 0.6f);
            ld.setFillColor(l.c);
            ld.setPosition(lx, ly + 1.f);
            win.draw(ld);
            leg.setString(l.label);
            leg.setFillColor(sf::Color(190, 195, 210));
            leg.setPosition(lx + l.r * 1.4f, ly);
            win.draw(leg);
            lx += leg.getLocalBounds().width + l.r * 1.4f + 10.f;
        }
    }
}

// ── main ──────────────────────────────────────────────────────────
int main()
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    load_obstacles_bin("../../COMMON/Binaries/Map/obstacles.bin");

    sf::Font font;
    if (!font.loadFromFile("C:/Windows/Fonts/malgun.ttf"))
        font.loadFromFile("C:/Windows/Fonts/consola.ttf");

    sf::RenderWindow window(
        sf::VideoMode(WIN_W, WIN_H),
        "SIMPLEST MMORPG",
        sf::Style::Titlebar | sf::Style::Close
    );
    window.setFramerateLimit(60);

    // 스프라이트 텍스처 로드 (CC0 · Kenney Tiny Dungeon)
    {
        const std::string BASE = "assets/sprites/";
        g_tex.ok =
            g_tex.floor.loadFromFile(BASE + "floor.png")             &&
            g_tex.wall.loadFromFile(BASE + "wall.png")               &&
            g_tex.castle_wall.loadFromFile(BASE + "castle_wall.png") &&
            g_tex.player_me.loadFromFile(BASE + "player_me.png")     &&
            g_tex.player_other.loadFromFile(BASE + "player_other.png") &&
            g_tex.npc_peace.loadFromFile(BASE + "npc_peace.png")     &&
            g_tex.npc_agro.loadFromFile(BASE + "npc_agro.png")       &&
            g_tex.npc_boss.loadFromFile(BASE + "npc_boss.png")       &&
            g_tex.npc_boss_p3.loadFromFile(BASE + "npc_boss_p3.png") &&
            g_tex.item_potion.loadFromFile(BASE + "item_potion.png");
        // 픽셀아트 — 확대 시 최근접 필터 유지
        g_tex.floor.setSmooth(false);        g_tex.floor.setRepeated(true);
        g_tex.wall.setSmooth(false);         g_tex.wall.setRepeated(true);
        g_tex.castle_wall.setSmooth(false);  g_tex.castle_wall.setRepeated(true);
        g_tex.player_me.setSmooth(false);
        g_tex.player_other.setSmooth(false);
        g_tex.npc_peace.setSmooth(false);
        g_tex.npc_agro.setSmooth(false);
        g_tex.npc_boss.setSmooth(false);
        g_tex.npc_boss_p3.setSmooth(false);
        g_tex.item_potion.setSmooth(false);
    }

    while (window.isOpen())
    {
        sf::Event event;
        while (window.pollEvent(event))
        {
            if (event.type == sf::Event::Closed) {
                g_running = false;
                window.close();
                break;
            }

            if (g_state == AppState::LOGIN)
            {
                if (event.type == sf::Event::TextEntered) {
                    char c = (char)event.text.unicode;
                    std::string& field = (g_focus == 0) ? g_input_ip : g_input_name;
                    if (c == '\b') { if (!field.empty()) field.pop_back(); }
                    else if (c == '\t') { g_focus = 1 - g_focus; }
                    else if (c >= 32 && c < 127 && (int)field.size() < MAX_NAME_LEN - 1)
                        field += c;
                }
                if (event.type == sf::Event::KeyPressed &&
                    event.key.code == sf::Keyboard::Enter) {
                    if (!g_input_name.empty() && !do_connect(g_input_ip, g_input_name))
                        push_msg("Connection failed.");
                }
            }
            else  // PLAYING
            {
                if (g_chat_mode) {
                    // 채팅 모드 입력
                    if (event.type == sf::Event::TextEntered) {
                        char c = (char)event.text.unicode;
                        if (c == '\b') {
                            if (!g_chat_input.empty()) g_chat_input.pop_back();
                        } else if (c >= 32 && c < 127 && (int)g_chat_input.size() < MAX_CHAT_LEN - 1) {
                            g_chat_input += c;
                        }
                    }
                    if (event.type == sf::Event::KeyPressed) {
                        if (event.key.code == sf::Keyboard::Enter) {
                            send_chat(g_chat_input);
                            g_chat_input.clear();
                            g_chat_mode = false;
                        } else if (event.key.code == sf::Keyboard::Escape) {
                            g_chat_input.clear();
                            g_chat_mode = false;
                        }
                    }
                } else {
                    // 일반 게임 입력
                    if (event.type == sf::Event::KeyPressed) {
                        switch (event.key.code) {
                        case sf::Keyboard::Up:    send_move(UP);    g_move_time += 500; break;
                        case sf::Keyboard::Down:  send_move(DOWN);  g_move_time += 500; break;
                        case sf::Keyboard::Left:  send_move(LEFT);  g_move_time += 500; break;
                        case sf::Keyboard::Right: send_move(RIGHT); g_move_time += 500; break;
                        case sf::Keyboard::A:
                            send_attack();
                            g_effect = { EffectType::ATTACK, std::chrono::steady_clock::now() };
                            break;
                        case sf::Keyboard::S:
                        {
                            auto now = std::chrono::steady_clock::now();
                            int elapsed = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
                                now - g_last_skill_time).count();
                            if (elapsed >= SKILL_CD_MS) {
                                send_skill();
                                g_last_skill_time = now;
                                g_effect = { EffectType::SKILL, now };
                            }
                            break;
                        }
                        case sf::Keyboard::Num1:
                            if (g_inventory[0] > 0) send_use_item(ITEM_HP_POTION);  break;
                        case sf::Keyboard::Num2:
                            if (g_inventory[1] > 0) send_use_item(ITEM_HI_POTION);  break;
                        case sf::Keyboard::Num3:
                            if (g_inventory[2] > 0) send_use_item(ITEM_ELIXIR);     break;
                        case sf::Keyboard::Num4:
                            if (g_inventory[3] > 0) send_use_item(ITEM_ATK_BOOST);  break;
                        case sf::Keyboard::Num5:
                            if (g_inventory[4] > 0) send_use_item(ITEM_DEF_BOOST);  break;
                        case sf::Keyboard::Num6:
                            if (g_inventory[5] > 0) send_use_item(ITEM_SPD_BOOST);  break;
                        case sf::Keyboard::Q:
                            g_quest_panel_open = !g_quest_panel_open;
                            break;
                        case sf::Keyboard::M:
                            g_map_open = !g_map_open;
                            break;
                        case sf::Keyboard::T:
                            g_chat_mode = true;
                            g_chat_input.clear();
                            break;
                        default: break;
                        }
                    }
                }
            }
        }

        window.clear(sf::Color(18, 18, 28));
        if (g_state == AppState::LOGIN) draw_login(window, font);
        else                             draw_game(window, font);
        window.display();
    }

    if (g_sock != INVALID_SOCKET) closesocket(g_sock);
    WSACleanup();
    return 0;
}
