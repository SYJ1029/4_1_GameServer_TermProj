#pragma once
#include "globals.h"

class CObject {
public:
    int   m_id;
    char  m_username[MAX_NAME_LEN];
    short m_x, m_y;
    int   m_move_time;
    CL_STATE m_state;
    short m_hp;
    short m_max_hp;
    int   m_level;

    CObject()
        : m_id(-1), m_x(0), m_y(0), m_move_time(0),
          m_state(CS_FREE), m_hp(0), m_max_hp(0), m_level(1)
    {
        m_username[0] = 0;
    }

    bool can_see(short x, short y) const
    {
        return std::abs(m_x - x) <= VIEW_RANGE && std::abs(m_y - y) <= VIEW_RANGE;
    }
};
