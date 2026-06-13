#pragma once
#include <iostream>
#include <WS2tcpip.h>
#include <array>
#include <MSWSock.h>
#include <thread>
#include <vector>
#include <mutex>
#include <unordered_set>
#include <chrono>
#include <atomic>
#include <memory>
#include <concurrent_priority_queue.h>
#include <tbb/concurrent_unordered_map.h>
#include "../../COMMON/PROTOCOL/protocol_2026.h"

#pragma comment(lib, "MSWSock.lib")
#pragma comment(lib, "WS2_32.lib")

using namespace std;
using namespace std::chrono;

constexpr int BUF_SIZE = 200;
constexpr int VIEW_RANGE = 5;
constexpr int MOVE_COOL_TIME = 1000;
constexpr int EVENT_NPC_MOVE = 1;

constexpr int SECTOR_SIZE = VIEW_RANGE * 2 + 1;
constexpr int MAX_SECTORS_X = (WORLD_WIDTH + SECTOR_SIZE - 1) / SECTOR_SIZE;
constexpr int MAX_SECTORS_Y = (WORLD_HEIGHT + SECTOR_SIZE - 1) / SECTOR_SIZE;

constexpr int SECTOR_X(int x) { return x / SECTOR_SIZE; }
constexpr int SECTOR_Y(int y) { return y / SECTOR_SIZE; }
constexpr int SECTOR_ID(int sx, int sy) { return sy * MAX_SECTORS_X + sx; }

enum IOType { IO_SEND, IO_RECV, IO_ACCEPT, IO_NPC_MOVE };
enum CL_STATE { CS_FREE, CS_CONNECT, CS_PLAYING, CS_LOGOUT };

struct event_type {
	int obj_id;
	system_clock::time_point wakeup_time;
	int event_id;

	constexpr bool operator<(const event_type& other) const
	{
		return wakeup_time > other.wakeup_time;
	}
};

class EXP_OVER {
public:
	WSAOVERLAPPED m_over;
	IOType m_iotype;
	WSABUF m_wsa;
	SOCKET m_client_socket;
	char m_buff[BUF_SIZE];

	EXP_OVER() : m_iotype(IO_RECV), m_client_socket(INVALID_SOCKET)
	{
		ZeroMemory(&m_over, sizeof(m_over));
		m_wsa.buf = m_buff;
		m_wsa.len = BUF_SIZE;
	}

	EXP_OVER(IOType iot) : m_iotype(iot), m_client_socket(INVALID_SOCKET)
	{
		ZeroMemory(&m_over, sizeof(m_over));
		m_wsa.buf = m_buff;
		m_wsa.len = BUF_SIZE;
	}
};

void error_display(const wchar_t* msg, int err_no);
bool is_pc(int id);
bool is_npc(int id);
