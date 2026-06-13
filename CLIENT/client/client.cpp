// SIMPLEST MMORPG - Client
// Graphics : SFML 2.x  (NuGet: SFML by SFML Team)
// Network  : Winsock2  (raw, same as server)
//
// SFML setup: Project > Manage NuGet Packages > install "SFML"
// Linker    : sfml-graphics, sfml-window, sfml-system, sfml-main, ws2_32

#define WIN32_LEAN_AND_MEAN
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

// ── 오브젝트 ──────────────────────────────────────────────────
struct ObjInfo {
    int   id;
    char  name[MAX_NAME_LEN];
    short x, y;
    bool  is_npc;
};

// ── 전역 게임 상태 ────────────────────────────────────────────
static SOCKET            g_sock      = INVALID_SOCKET;
static std::atomic<bool> g_running   { false };
static int               g_my_id     = -1;
static short             g_my_x      = 0, g_my_y = 0;
static int               g_move_time = 0;

static std::unordered_map<int, ObjInfo> g_objs;
static std::mutex g_objs_lock;

static std::deque<std::string> g_msgs;
static std::mutex              g_msgs_lock;
constexpr int MAX_MSGS = 5;

// ── 앱 상태 ───────────────────────────────────────────────────
enum class AppState { LOGIN, PLAYING };
static AppState    g_state      = AppState::LOGIN;
static std::string g_input_ip   = "127.0.0.1";
static std::string g_input_name;
static int         g_focus      = 1;   // 0=IP, 1=name

// ── 렌더 상수 ─────────────────────────────────────────────────
constexpr int   VSIZE    = 15;         // 시야 15x15 타일
constexpr int   TILE_PX  = 36;         // 타일당 픽셀 (정수)
constexpr float TILE_F   = 36.f;       // 타일당 픽셀 (SFML용 float)
constexpr int   WIN_W    = VSIZE * TILE_PX;   // 540
constexpr int   UI_H     = 120;
constexpr int   WIN_H    = WIN_W + UI_H;       // 660
constexpr float GAME_VP_H = (float)(WIN_H - UI_H) / WIN_H;  // 뷰포트 비율

// ── 유틸 ──────────────────────────────────────────────────────
static void push_msg(const std::string& s)
{
    std::lock_guard<std::mutex> lk(g_msgs_lock);
    g_msgs.push_back(s);
    if ((int)g_msgs.size() > MAX_MSGS)
        g_msgs.pop_front();
}

// ── 패킷 송신 ─────────────────────────────────────────────────
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

// ── 패킷 처리 ─────────────────────────────────────────────────
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
        }
        break;
    }
    case S2C_AVATAR_INFO:
    {
        auto* pkt = reinterpret_cast<S2C_AvatarInfo*>(p);
        g_my_id = pkt->playerId;
        g_my_x  = pkt->x;
        g_my_y  = pkt->y;
        break;
    }
    case S2C_ADD_PLAYER:
    {
        auto* pkt = reinterpret_cast<S2C_AddPlayer*>(p);
        ObjInfo o{};
        o.id     = pkt->playerId;
        o.x      = pkt->x;
        o.y      = pkt->y;
        o.is_npc = (pkt->playerId >= NPC_ID_START);
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
    }
}

// ── 수신 스레드 ───────────────────────────────────────────────
static char s_login_name[MAX_NAME_LEN];

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

// ── 서버 연결 ─────────────────────────────────────────────────
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

// ── 텍스트 그림자 helper ──────────────────────────────────────
static void draw_text_shadowed(sf::RenderWindow& win, sf::Text& text,
                                sf::Vector2f pos, sf::Color color)
{
    text.setFillColor(sf::Color(0, 0, 0, 160));
    text.setPosition(pos + sf::Vector2f(1.f, 1.f));
    win.draw(text);
    text.setFillColor(color);
    text.setPosition(pos);
    win.draw(text);
}

// ── 로그인 화면 ───────────────────────────────────────────────
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

    sf::Text hint("[Tab] 필드 전환    [Enter] 접속", font, 13);
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

// ── 게임 화면 ─────────────────────────────────────────────────
static void draw_game(sf::RenderWindow& win, sf::Font& font)
{
    // ── 게임 뷰 (월드 좌표 = 타일 단위) ──────────────────────
    sf::View game_view(sf::FloatRect(
        g_my_x - VSIZE / 2.f,
        g_my_y - VSIZE / 2.f,
        (float)VSIZE,
        (float)VSIZE
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
            bool out_of_bounds = wx < 0 || wx >= WORLD_WIDTH || wy < 0 || wy >= WORLD_HEIGHT;
            tile.setFillColor(out_of_bounds ? sf::Color(8, 8, 12) : sf::Color(28, 28, 36));
            tile.setPosition((float)wx, (float)wy);
            win.draw(tile);
        }
    }

    // 오브젝트 스냅샷 (lock 최소화)
    struct Snap { ObjInfo info; };
    std::vector<Snap> snaps;
    {
        std::lock_guard<std::mutex> lk(g_objs_lock);
        snaps.reserve(g_objs.size());
        for (auto& [id, o] : g_objs)
            snaps.push_back({ o });
    }

    // 오브젝트 사각형 (월드 뷰)
    sf::RectangleShape obj_rect(sf::Vector2f(0.84f, 0.84f));
    for (auto& s : snaps) {
        sf::Color c = s.info.is_npc ? sf::Color(200, 55, 55) : sf::Color(55, 185, 80);
        obj_rect.setFillColor(c);
        obj_rect.setPosition(s.info.x + 0.08f, s.info.y + 0.08f);
        win.draw(obj_rect);
    }

    // 내 캐릭터 (월드 뷰)
    obj_rect.setFillColor(sf::Color(70, 115, 255));
    obj_rect.setPosition(g_my_x + 0.08f, g_my_y + 0.08f);
    win.draw(obj_rect);

    // ── 픽셀 뷰로 전환해서 텍스트 레이블 ────────────────────
    win.setView(win.getDefaultView());

    sf::Text lbl("", font, 11);
    for (auto& s : snaps) {
        sf::Vector2i sp = win.mapCoordsToPixel(
            sf::Vector2f(s.info.x + 0.5f, s.info.y + 0.5f), game_view);
        if (sp.x < 0 || sp.x > WIN_W || sp.y < 0 || sp.y > WIN_H - UI_H) continue;
        lbl.setString(s.info.name);
        sf::Color c = s.info.is_npc ? sf::Color(255, 140, 140) : sf::Color(140, 230, 140);
        draw_text_shadowed(win, lbl,
            sf::Vector2f(sp.x - lbl.getLocalBounds().width / 2.f, (float)sp.y - 10.f), c);
    }

    // 내 이름
    lbl.setString(s_login_name);
    sf::Vector2i my_sp = win.mapCoordsToPixel(
        sf::Vector2f(g_my_x + 0.5f, g_my_y + 0.5f), game_view);
    draw_text_shadowed(win, lbl,
        sf::Vector2f(my_sp.x - lbl.getLocalBounds().width / 2.f, (float)my_sp.y - 10.f),
        sf::Color(180, 200, 255));

    // ── UI 패널 ───────────────────────────────────────────────
    sf::RectangleShape ui_bg(sf::Vector2f((float)WIN_W, (float)UI_H));
    ui_bg.setPosition(0.f, (float)(WIN_H - UI_H));
    ui_bg.setFillColor(sf::Color(12, 12, 22));
    win.draw(ui_bg);

    sf::RectangleShape sep(sf::Vector2f((float)WIN_W, 1.f));
    sep.setPosition(0.f, (float)(WIN_H - UI_H));
    sep.setFillColor(sf::Color(60, 60, 90));
    win.draw(sep);

    // 상태 표시
    char status_buf[128];
    sprintf_s(status_buf, "ID: %-6d  X: %-5d  Y: %-5d       [Arrow] 이동", g_my_id, g_my_x, g_my_y);
    sf::Text status_text(status_buf, font, 13);
    status_text.setFillColor(sf::Color(150, 155, 180));
    status_text.setPosition(10.f, (float)(WIN_H - UI_H) + 6.f);
    win.draw(status_text);

    // 메시지 로그
    std::lock_guard<std::mutex> lk(g_msgs_lock);
    float my2 = (float)(WIN_H - UI_H) + 28.f;
    sf::Text msg_text("", font, 13);
    for (auto& m : g_msgs) {
        msg_text.setString(m);
        msg_text.setFillColor(sf::Color(120, 200, 120));
        msg_text.setPosition(10.f, my2);
        win.draw(msg_text);
        my2 += 17.f;
    }
}

// ── main ──────────────────────────────────────────────────────
int main()
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    sf::Font font;
    if (!font.loadFromFile("C:/Windows/Fonts/consola.ttf"))
        font.loadFromFile("C:/Windows/Fonts/malgun.ttf");   // 한글 폰트 fallback

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
                    if (c == '\b') {
                        if (!field.empty()) field.pop_back();
                    } else if (c == '\t') {
                        g_focus = 1 - g_focus;
                    } else if (c >= 32 && c < 127 && (int)field.size() < MAX_NAME_LEN - 1) {
                        field += c;
                    }
                }
                if (event.type == sf::Event::KeyPressed &&
                    event.key.code == sf::Keyboard::Enter)
                {
                    if (!g_input_name.empty()) {
                        if (!do_connect(g_input_ip, g_input_name))
                            push_msg("서버 연결 실패.");
                    }
                }
            }
            else  // PLAYING
            {
                if (event.type == sf::Event::KeyPressed) {
                    switch (event.key.code) {
                    case sf::Keyboard::Up:    send_move(UP);    g_move_time += 500; break;
                    case sf::Keyboard::Down:  send_move(DOWN);  g_move_time += 500; break;
                    case sf::Keyboard::Left:  send_move(LEFT);  g_move_time += 500; break;
                    case sf::Keyboard::Right: send_move(RIGHT); g_move_time += 500; break;
                    default: break;
                    }
                }
            }
        }

        window.clear(sf::Color(18, 18, 28));

        if (g_state == AppState::LOGIN)
            draw_login(window, font);
        else
            draw_game(window, font);

        window.display();
    }

    if (g_sock != INVALID_SOCKET) closesocket(g_sock);
    WSACleanup();
    return 0;
}
