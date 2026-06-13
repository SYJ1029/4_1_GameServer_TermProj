#pragma once

constexpr short PORT         = 3500;
constexpr int WORLD_WIDTH    = 2000;
constexpr int WORLD_HEIGHT   = 2000;
constexpr int MAX_PLAYERS    = 10000;
constexpr int NPC_ID_START   = MAX_PLAYERS;
constexpr int MAX_NPCS       = 200000;
constexpr int MAX_NAME_LEN   = 20;
constexpr int MAX_CHAT_LEN   = 128;

enum PACKET_TYPE {
    C2S_LOGIN, C2S_MOVE, C2S_CHAT, C2S_ATTACK,
    S2C_LOGIN_RESULT, S2C_AVATAR_INFO,
    S2C_ADD_PLAYER, S2C_REMOVE_PLAYER, S2C_MOVE_PLAYER,
    S2C_CHAT, S2C_STAT_INFO, S2C_DAMAGE_INFO
};

enum DIRECTION { UP, DOWN, LEFT, RIGHT };

// NPC 타입 (클라이언트/프로토콜용)
enum NPC_KIND : unsigned char { NPC_PC = 0, NPC_PEACE = 1, NPC_AGRO = 2 };

#pragma pack(push, 1)

// ── C2S ──────────────────────────────────────────────────────────
struct C2S_Login {
    unsigned char size;
    PACKET_TYPE   type;
    char          username[MAX_NAME_LEN];
};

struct C2S_Move {
    unsigned char size;
    PACKET_TYPE   type;
    DIRECTION     dir;
    int           move_time;
};

struct C2S_Chat {
    unsigned char size;
    PACKET_TYPE   type;
    char          msg[MAX_CHAT_LEN];
};

struct C2S_Attack {
    unsigned char size;
    PACKET_TYPE   type;
};

// ── S2C ──────────────────────────────────────────────────────────
struct S2C_LoginResult {
    unsigned char size;
    PACKET_TYPE   type;
    bool          success;
    char          message[50];
};

struct S2C_AvatarInfo {
    unsigned char size;
    PACKET_TYPE   type;
    int           playerId;
    short         x;
    short         y;
    short         hp;
    short         max_hp;
    int           level;
    int           exp;
    int           exp_next;
};

struct S2C_AddPlayer {
    unsigned char size;
    PACKET_TYPE   type;
    int           playerId;
    char          username[MAX_NAME_LEN];
    short         x;
    short         y;
    short         hp;
    short         max_hp;
    NPC_KIND      npc_type;   // 0=player, 1=peace, 2=agro
};

struct S2C_RemovePlayer {
    unsigned char size;
    PACKET_TYPE   type;
    int           playerId;
};

struct S2C_MovePlayer {
    unsigned char size;
    PACKET_TYPE   type;
    int           playerId;
    short         x;
    short         y;
    int           move_time;
};

struct S2C_Chat {
    unsigned char size;
    PACKET_TYPE   type;
    int           sender_id;
    char          sender_name[MAX_NAME_LEN];
    char          msg[MAX_CHAT_LEN];
};

struct S2C_StatInfo {
    unsigned char size;
    PACKET_TYPE   type;
    int           object_id;
    short         hp;
    short         max_hp;
    int           level;    // NPC는 0
    int           exp;      // NPC는 0
    int           exp_next; // NPC는 0
};

struct S2C_DamageInfo {
    unsigned char size;
    PACKET_TYPE   type;
    int           attacker_id;
    int           target_id;
    short         damage;
    short         target_hp;
};

#pragma pack(pop)
