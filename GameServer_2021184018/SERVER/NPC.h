#pragma once
#include "Object.h"

enum NPC_TYPE { NPC_PEACE_TYPE = 1, NPC_AGRO_TYPE = 2, NPC_BOSS_TYPE = 3 };
// NPC_STATE는 protocol_2026.h에서 공유 (NPC_STATE_IDLE/ROAMING/CHASE)

class CNPC : public CObject {
public:
    std::atomic<bool>          m_active_npc;
    system_clock::time_point   m_last_npc_move_time;
    NPC_TYPE                   m_npc_type;
    NPC_STATE                  m_move_state;
    int                        m_target_id;
    short                      m_origin_x, m_origin_y;

    CNPC()
        : m_active_npc(false),
          m_npc_type(NPC_PEACE_TYPE),
          m_move_state(NPC_STATE_IDLE),
          m_target_id(-1),
          m_origin_x(0), m_origin_y(0)
    {
        m_state = CS_PLAYING;
        m_last_npc_move_time = system_clock::now();
    }

    int get_boss_phase() const {
        if (m_max_hp <= 0) return 1;
        int pct = (m_hp * 100) / m_max_hp;
        return (pct > 66) ? 1 : (pct > 33) ? 2 : 3;
    }

    void do_roaming_move();
    void do_chase_move();
    void wake_up();

private:
    void update_viewers(short old_x, short old_y);
};
