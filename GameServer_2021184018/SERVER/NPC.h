#pragma once
#include "Object.h"

enum NPC_TYPE { NPC_PEACE_TYPE = 1, NPC_AGRO_TYPE = 2 };

class CNPC : public CObject {
public:
    std::atomic<bool>          m_active_npc;
    system_clock::time_point   m_last_npc_move_time;
    NPC_TYPE                   m_npc_type;
    int                        m_target_id;

    CNPC()
        : m_active_npc(false),
          m_npc_type(NPC_PEACE_TYPE),
          m_target_id(-1)
    {
        m_state = CS_PLAYING;
        m_last_npc_move_time = system_clock::now();
    }

    void do_peace_move();
    void do_agro_move();
    void wake_up();

private:
    void update_viewers(short old_x, short old_y);
};
