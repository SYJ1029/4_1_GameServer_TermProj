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

// ── 공격 이펙트 ───────────────────────────────────────────────────
enum class EffectType { NONE, ATTACK, SKILL };
struct VisualEffect {
    EffectType type  = EffectType::NONE;
    std::chrono::steady_clock::time_point start;
};
static VisualEffect g_effect;

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

    // 배경 타일
    sf::RectangleShape tile(sf::Vector2f(1.f, 1.f));
    tile.setOutlineThickness(0.025f);
    tile.setOutlineColor(sf::Color(48, 48, 58));
    for (int dy = -1; dy <= VSIZE; ++dy) {
        for (int dx = -1; dx <= VSIZE; ++dx) {
            int wx = g_my_x - VSIZE / 2 + dx;
            int wy = g_my_y - VSIZE / 2 + dy;
            bool oob = wx < 0 || wx >= WORLD_WIDTH || wy < 0 || wy >= WORLD_HEIGHT;
            tile.setFillColor(oob ? sf::Color(8, 8, 12) : sf::Color(28, 28, 36));
            tile.setPosition((float)wx, (float)wy);
            win.draw(tile);
        }
    }

    // 장애물 렌더링
    {
        float vx0 = g_my_x - VSIZE / 2.f, vy0 = g_my_y - VSIZE / 2.f;
        float vx1 = vx0 + VSIZE,           vy1 = vy0 + VSIZE;
        sf::RectangleShape wall;
        wall.setFillColor(sf::Color(90, 75, 60));
        wall.setOutlineColor(sf::Color(60, 50, 40));
        wall.setOutlineThickness(0.04f);
        for (auto& r : g_obstacles) {
            if (r.x + r.w < vx0 || r.x > vx1) continue;
            if (r.y + r.h < vy0 || r.y > vy1) continue;
            wall.setSize(sf::Vector2f((float)r.w, (float)r.h));
            wall.setPosition((float)r.x, (float)r.y);
            win.draw(wall);
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

    // 오브젝트 렌더링: 보스=원, 나머지=사각형
    sf::RectangleShape obj_rect(sf::Vector2f(0.84f, 0.84f));
    sf::CircleShape boss_circle(0.46f);
    boss_circle.setOutlineThickness(0.07f);
    boss_circle.setOutlineColor(sf::Color(255, 230, 80));

    for (auto& s : snaps) {
        if (s.info.npc_type == NPC_BOSS) {
            // 페이즈별 색상: 금색(P1) → 주황(P2) → 진홍(P3)
            float ratio = (s.info.max_hp > 0)
                        ? std::max(0.f, (float)s.info.hp / s.info.max_hp) : 0.f;
            sf::Color bc = (ratio > 0.66f) ? sf::Color(210, 160, 0)
                         : (ratio > 0.33f) ? sf::Color(200, 80,  0)
                                           : sf::Color(180, 0,   0);
            boss_circle.setFillColor(bc);
            boss_circle.setPosition(s.info.x + 0.08f, s.info.y + 0.08f);
            win.draw(boss_circle);
            draw_hp_bar(win, (float)s.info.x, (float)s.info.y, s.info.hp, s.info.max_hp);
            continue;
        }

        sf::Color c;
        if (s.info.npc_type == NPC_PC) {
            c = sf::Color(55, 185, 80);
        } else if (s.info.npc_state == NPC_STATE_CHASE) {
            c = sf::Color(255, 40, 40);   // 추격 중 — 밝은 빨강
        } else if (s.info.npc_type == NPC_AGRO) {
            c = sf::Color(200, 100, 30);  // Agro 로밍 — 주황
        } else {
            c = sf::Color(120, 140, 90);  // Peace 대기 — 올리브
        }
        obj_rect.setFillColor(c);
        obj_rect.setPosition(s.info.x + 0.08f, s.info.y + 0.08f);
        win.draw(obj_rect);

        if (s.info.npc_type != NPC_PC)
            draw_hp_bar(win, (float)s.info.x, (float)s.info.y, s.info.hp, s.info.max_hp);
    }

    // 내 캐릭터 + HP 바
    obj_rect.setFillColor(sf::Color(70, 115, 255));
    obj_rect.setPosition(g_my_x + 0.08f, g_my_y + 0.08f);
    win.draw(obj_rect);
    draw_hp_bar(win, (float)g_my_x, (float)g_my_y, g_my_hp, g_my_max_hp);

    // ── 공격/스킬 이펙트 (게임 뷰, 오브젝트 위에 오버레이) ──────
    if (g_effect.type != EffectType::NONE) {
        auto now_ef = std::chrono::steady_clock::now();
        int eff_ms  = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
            now_ef - g_effect.start).count();
        int duration = (g_effect.type == EffectType::SKILL) ? 200 : 150;

        if (eff_ms < duration) {
            float t     = 1.f - (float)eff_ms / duration;  // 1→0 페이드
            auto alpha  = (sf::Uint8)(220 * t);

            sf::RectangleShape eff_tile(sf::Vector2f(0.92f, 0.92f));

            if (g_effect.type == EffectType::ATTACK) {
                // 상하좌우 4칸 — 주황
                eff_tile.setFillColor(sf::Color(255, 140, 0, alpha));
                const int dirs[4][2] = {{0,-1},{0,1},{-1,0},{1,0}};
                for (auto& d : dirs) {
                    eff_tile.setPosition(g_my_x + d[0] + 0.04f, g_my_y + d[1] + 0.04f);
                    win.draw(eff_tile);
                }
            } else {
                // 3x3 전체 (자기 포함) — 보라
                eff_tile.setFillColor(sf::Color(160, 60, 255, alpha));
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        eff_tile.setPosition(g_my_x + dx + 0.04f, g_my_y + dy + 0.04f);
                        win.draw(eff_tile);
                    }
            }
        } else {
            g_effect.type = EffectType::NONE;
        }
    }

    // 픽셀 뷰로 전환해서 이름 레이블 출력
    win.setView(win.getDefaultView());

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
    float msg_start = ui_top + 36.f;                            // 메시지 최상단 (HP/힌트 아래)

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
    sprintf_s(hint_buf, "ID:%-5d  X:%-4d Y:%-4d    [Arrow]Move  [A]Attack  [S]Skill  [T]Chat",
              g_my_id, g_my_x, g_my_y);
    sf::Text hint_text(hint_buf, font, 11);
    hint_text.setFillColor(sf::Color(100, 105, 130));
    hint_text.setPosition(10.f, ui_top + 19.f);
    win.draw(hint_text);

    // 스킬 쿨타임 바
    {
        auto now_sk = std::chrono::steady_clock::now();
        int elapsed_sk = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
            now_sk - g_last_skill_time).count();
        float sk_ratio = std::min(1.f, (float)elapsed_sk / SKILL_CD_MS);

        constexpr float SK_X = 350.f, SK_W = 100.f;
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
