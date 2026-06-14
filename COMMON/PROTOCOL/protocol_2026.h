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
    C2S_LOGIN, C2S_MOVE, C2S_CHAT, C2S_ATTACK, C2S_SKILL, C2S_USE_ITEM,
    S2C_LOGIN_RESULT, S2C_AVATAR_INFO,
    S2C_ADD_PLAYER, S2C_REMOVE_PLAYER, S2C_MOVE_PLAYER,
    S2C_CHAT, S2C_STAT_INFO, S2C_DAMAGE_INFO,
    S2C_ITEM_APPEAR, S2C_ITEM_REMOVE, S2C_ITEM_ADD
};

enum ITEM_TYPE : unsigned char { ITEM_HP_POTION = 1 };

enum DIRECTION { UP, DOWN, LEFT, RIGHT };

// NPC 타입 (클라이언트/프로토콜용)
enum NPC_KIND  : unsigned char { NPC_PC = 0, NPC_PEACE = 1, NPC_AGRO = 2, NPC_BOSS = 3 };

// NPC 이동 상태 (서버 내부 + 프로토콜 공유)
enum NPC_STATE : unsigned char { NPC_STATE_IDLE = 0, NPC_STATE_ROAMING = 1, NPC_STATE_CHASE = 2 };

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

struct C2S_Skill {
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
    NPC_KIND      npc_type;    // 0=player, 1=peace, 2=agro
    NPC_STATE     npc_state;   // IDLE/ROAMING/CHASE
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
    int           level;      // NPC는 0
    int           exp;        // NPC는 0
    int           exp_next;   // NPC는 0
    NPC_STATE     npc_state;  // 플레이어는 NPC_STATE_IDLE
};

struct S2C_DamageInfo {
    unsigned char size;
    PACKET_TYPE   type;
    int           attacker_id;
    int           target_id;
    short         damage;
    short         target_hp;
};

struct C2S_UseItem {
    unsigned char size;
    PACKET_TYPE   type;
    ITEM_TYPE     item_type;
};

struct S2C_ItemAppear {
    unsigned char size;
    PACKET_TYPE   type;
    int           item_id;
    short         x, y;
    ITEM_TYPE     item_type;
};

struct S2C_ItemRemove {
    unsigned char size;
    PACKET_TYPE   type;
    int           item_id;
};

struct S2C_ItemAdd {
    unsigned char size;
    PACKET_TYPE   type;
    ITEM_TYPE     item_type;
    int           count;   // 현재 보유량
};

#pragma pack(pop)
