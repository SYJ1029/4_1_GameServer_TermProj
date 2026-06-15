#pragma once
#include "Object.h"
#include <deque>

// DB 연동 시 player_quests 테이블 행 하나와 1:1 대응
struct QuestProgress {
    QUEST_STATE state      = Q_ACTIVE;  // 현재 auto-accept; 나중에 Q_NONE으로 변경
    int         kill_count = 0;
};

class SESSION : public CObject {
    struct PendingSend {
        int size;
        std::array<char, BUF_SIZE> data;
    };

public:
    SOCKET    m_client;
    EXP_OVER  m_recv_over;
    int       m_prev_recv;
    int       m_xp;
    int           m_inventory[ITEM_SLOT_COUNT];   // 슬롯 1-6 → 인덱스 0-5
    QuestProgress m_quests[QUEST_COUNT];           // [QUEST_ID_AGRO], [QUEST_ID_BOSS]
    system_clock::time_point m_last_skill_time;
    system_clock::time_point m_last_ranged_atk_time;
    system_clock::time_point m_atk_boost_until;   // 공격력 강화 만료 시각
    system_clock::time_point m_def_boost_until;   // 방어력 강화 만료 시각
    system_clock::time_point m_spd_boost_until;   // 이동속도 강화 만료 시각
    std::unordered_set<int> m_visible_objects;
    std::mutex              m_visible_mutex;
    std::deque<PendingSend> m_send_queue;
    std::mutex              m_send_mutex;
    bool                    m_send_pending = false;

    int exp_for_next_level() const { return 100 * (1 << (m_level - 1)); }

    SESSION()
        : m_client(INVALID_SOCKET), m_prev_recv(0), m_xp(0), m_inventory{},
          m_last_skill_time(system_clock::now() - seconds(10)),
          m_last_ranged_atk_time(system_clock::now() - seconds(10))
    {
        m_state = CS_FREE;
        m_recv_over.m_iotype = IO_RECV;
    }

    SESSION(SOCKET s, int id)
        : m_client(s), m_prev_recv(0), m_xp(0), m_inventory{},
          m_last_skill_time(system_clock::now() - seconds(10)),
          m_last_ranged_atk_time(system_clock::now() - seconds(10))
    {
        m_id      = id;
        m_level   = 1;
        m_state   = CS_CONNECT;
        m_hp      = PC_MAX_HP;
        m_max_hp  = PC_MAX_HP;
        m_recv_over.m_iotype = IO_RECV;
        m_x = PC_SPAWN_X;
        m_y = PC_SPAWN_Y;
    }

    ~SESSION()
    {
        if (m_client != INVALID_SOCKET)
            closesocket(m_client);
    }

    bool can_send() const
    {
        return (m_id < MAX_PLAYERS) && m_client != INVALID_SOCKET;
    }

    void do_recv()
    {
        DWORD recv_flag = 0;
        memset(&m_recv_over.m_over, 0, sizeof(m_recv_over.m_over));
        m_recv_over.m_wsa.len = BUF_SIZE - m_prev_recv;
        m_recv_over.m_wsa.buf = m_recv_over.m_buff + m_prev_recv;
        WSARecv(m_client, &m_recv_over.m_wsa, 1, 0, &recv_flag, &m_recv_over.m_over, nullptr);
    }

    void do_send(int num_bytes, const char* data);
    void on_send_complete(EXP_OVER* over, DWORD num_bytes);

    void send_login_success()
    {
        S2C_LoginResult packet;
        packet.size    = sizeof(packet);
        packet.type    = S2C_LOGIN_RESULT;
        packet.success = true;
        strcpy_s(packet.message, "Login successful.");
        do_send(packet.size, reinterpret_cast<char*>(&packet));
    }

    void send_avatar_info()
    {
        S2C_AvatarInfo packet;
        packet.size     = sizeof(packet);
        packet.type     = S2C_AVATAR_INFO;
        packet.playerId = m_id;
        packet.x        = m_x;
        packet.y        = m_y;
        packet.hp       = m_hp;
        packet.max_hp   = m_max_hp;
        packet.level    = m_level;
        packet.exp      = m_xp;
        packet.exp_next = exp_for_next_level();
        packet.dir      = m_dir;
        do_send(packet.size, reinterpret_cast<char*>(&packet));
    }

    void send_add_object(int object_id);
    void send_remove_object(int object_id);
    void send_move_object(int object_id);
    void send_chat(int sender_id, const char* sender_name, const char* msg);
    void send_stat_info(int object_id, short hp, short max_hp, int level = 0, int exp = 0, int exp_next = 0, NPC_STATE npc_state = NPC_STATE_IDLE);
    void send_damage_info(int attacker_id, int target_id, short damage, short target_hp);
    void send_item_appear(int item_id, short x, short y, ITEM_TYPE item_type);
    void send_item_remove(int item_id);
    void send_item_add(ITEM_TYPE item_type, int count);
    void send_all_world_items();
    void send_all_inventory_items();
    void send_quest_update(int quest_id);
    void send_all_quest_states();

    bool process_packet(unsigned char* p);
    void do_move(DIRECTION dir);

private:
    void start_next_send();
    bool post_send(EXP_OVER* over);
    void fail_send(EXP_OVER* over);
};
