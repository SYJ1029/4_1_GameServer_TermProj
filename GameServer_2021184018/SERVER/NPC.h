#pragma once
#include "Object.h"

class CNPC : public CObject {
public:
	std::atomic<bool> m_active_npc;
	system_clock::time_point m_last_npc_move_time;

	CNPC() : m_active_npc(false)
	{
		m_state = CS_PLAYING;
		m_last_npc_move_time = system_clock::now();
	}

	void do_random_move();
	void wake_up();
};
