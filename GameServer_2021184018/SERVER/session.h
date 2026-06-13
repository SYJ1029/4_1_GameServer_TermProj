#pragma once
#include "Object.h"

class SESSION : public CObject {
public:
	SOCKET m_client;
	EXP_OVER m_recv_over;
	int m_prev_recv;
	std::unordered_set<int> m_visible_objects;
	std::mutex m_visible_mutex;

	SESSION()
		: m_client(INVALID_SOCKET), m_prev_recv(0)
	{
		m_state = CS_FREE;
		m_recv_over.m_iotype = IO_RECV;
	}

	SESSION(SOCKET s, int id)
		: m_client(s), m_prev_recv(0)
	{
		m_id = id;
		m_state = CS_CONNECT;
		m_recv_over.m_iotype = IO_RECV;
		m_x = rand() % WORLD_WIDTH;
		m_y = rand() % WORLD_HEIGHT;
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
};
