#pragma once
#include "globals.h"

class SESSION {
public:
	SOCKET m_client;
	int m_id;
	CL_STATE m_state;
	EXP_OVER m_recv_over;
	int m_prev_recv;
	char m_username[MAX_NAME_LEN];
	short m_x, m_y;
	int m_move_time;
	bool m_is_npc;
	std::atomic<bool> m_active_npc;
	system_clock::time_point m_last_npc_move_time;
	std::unordered_set<int> m_visible_objects;
	std::mutex m_visible_mutex;

	SESSION()
		: m_client(INVALID_SOCKET), m_id(-1), m_state(CS_FREE), m_prev_recv(0),
		  m_x(0), m_y(0), m_move_time(0), m_is_npc(false), m_active_npc(false)
	{
		m_username[0] = 0;
		m_recv_over.m_iotype = IO_RECV;
		m_last_npc_move_time = system_clock::now();
	}

	SESSION(SOCKET s, int id)
		: m_client(s), m_id(id), m_state(CS_CONNECT), m_prev_recv(0),
		  m_move_time(0), m_is_npc(false), m_active_npc(false)
	{
		m_recv_over.m_iotype = IO_RECV;
		m_x = rand() % WORLD_WIDTH;
		m_y = rand() % WORLD_HEIGHT;
		m_username[0] = 0;
		m_last_npc_move_time = system_clock::now();
	}

	~SESSION()
	{
		if (m_client != INVALID_SOCKET)
			closesocket(m_client);
	}

	bool can_see(short x, short y) const
	{
		return abs(m_x - x) <= VIEW_RANGE && abs(m_y - y) <= VIEW_RANGE;
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
		packet.size = sizeof(packet);
		packet.type = S2C_LOGIN_RESULT;
		packet.success = true;
		strcpy_s(packet.message, "Login successful.");
		do_send(packet.size, reinterpret_cast<char*>(&packet));
	}

	void send_avatar_info()
	{
		S2C_AvatarInfo packet;
		packet.size = sizeof(packet);
		packet.type = S2C_AVATAR_INFO;
		packet.playerId = m_id;
		packet.x = m_x;
		packet.y = m_y;
		do_send(packet.size, reinterpret_cast<char*>(&packet));
	}

	void send_add_object(int object_id);
	void send_remove_object(int object_id);
	void send_move_object(int object_id);
	bool process_packet(unsigned char* p);
	void do_move(DIRECTION dir);
	void do_random_move();
	void wake_up();
};
