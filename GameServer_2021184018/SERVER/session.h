#pragma once
#include "Object.h"

// DB 연동 시 player_quests 테이블 행 하나와 1:1 대응
struct QuestProgress {
    QUEST_STATE state      = Q_ACTIVE;  // 현재 auto-accept; 나중에 Q_NONE으로 변경
    int         kill_count = 0;
};

class SESSION : public CObject {
public:
    SOCKET    m_client;
    EXP_OVER  m_recv_over;
    int       m_prev_recv;
    int       m_xp;
    int           m_potion_count;
    QuestProgress m_quests[QUEST_COUNT];   // [QUEST_ID_AGRO], [QUEST_ID_BOSS]
    system_clock::time_point m_last_skill_time;
    std::unordered_set<int> m_visible_objects;
    std::mutex              m_visible_mutex;

    int exp_for_next_level() const { return 100 * (1 << (m_level - 1)); }

    SESSION()
        : m_client(INVALID_SOCKET), m_prev_recv(0), m_xp(0), m_potion_count(0),
          m_last_skill_time(system_clock::now() - seconds(10))
    {
        m_state = CS_FREE;
        m_recv_over.m_iotype = IO_RECV;
    }

    SESSION(SOCKET s, int id)
        : m_client(s), m_prev_recv(0), m_xp(0), m_potion_count(0),
          m_last_skill_time(system_clock::now() - seconds(10))
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

    void do_send(int num_bytes, char* data)
    {
        if (!can_send()) return;
        EXP_OVER* over = new EXP_OVER(IO_SEND);
        over->m_wsa.len = num_bytes;
        memcpy(over->m_buff, data, num_bytes);
        WSASend(m_client, &over->m_wsa, 1, 0, 0, &over->m_over, nullptr);
    }

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
    void send_quest_update(int quest_id);
    void send_all_quest_states();

    bool process_packet(unsigned char* p);
    void do_move(DIRECTION dir);
};
