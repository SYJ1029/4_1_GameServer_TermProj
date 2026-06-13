#pragma once
#define NOMINMAX
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
#include <climits>
#include <algorithm>
#include <concurrent_priority_queue.h>
#include <tbb/concurrent_unordered_map.h>
#include "../../COMMON/PROTOCOL/protocol_2026.h"

#pragma comment(lib, "MSWSock.lib")
#pragma comment(lib, "WS2_32.lib")

using namespace std;
using namespace std::chrono;

constexpr int BUF_SIZE = 200;
constexpr int VIEW_RANGE = 5;
constexpr int MOVE_COOL_TIME    = 500;    // 0.5초/칸 (PDF 스펙)
constexpr int ATTACK_COOL_TIME  = 1000;   // 1초/회
constexpr int HP_REGEN_TIME     = 5000;   // 5초마다 10% 회복
constexpr int NPC_RESPAWN_TIME  = 30000;  // 30초 후 NPC 부활
constexpr int EVENT_NPC_MOVE    = 1;
constexpr int EVENT_HP_REGEN    = 2;
constexpr int EVENT_NPC_RESPAWN = 3;

// 스폰 위치 — 맵 중앙 북쪽
constexpr short PC_SPAWN_X = 1000;
constexpr short PC_SPAWN_Y = 100;

// NPC 기본 레벨
constexpr int NPC_PEACE_LEVEL = 1;
constexpr int NPC_AGRO_LEVEL  = 2;

// 전투 수치
constexpr int   ATTACK_RANGE      = 1;   // 공격 가능 거리 (체비쇼프)
constexpr int   AGRO_DETECT_RANGE = 10;  // 어그로 감지 거리
constexpr short PC_MAX_HP         = 100;
constexpr short NPC_PEACE_MAX_HP  = 30;
constexpr short NPC_AGRO_MAX_HP   = 80;
constexpr short PC_ATTACK_DMG     = 15;
constexpr short NPC_ATTACK_DMG    = 8;

constexpr int SECTOR_SIZE = VIEW_RANGE * 2 + 1;
constexpr int MAX_SECTORS_X = (WORLD_WIDTH + SECTOR_SIZE - 1) / SECTOR_SIZE;
constexpr int MAX_SECTORS_Y = (WORLD_HEIGHT + SECTOR_SIZE - 1) / SECTOR_SIZE;

constexpr int SECTOR_X(int x) { return x / SECTOR_SIZE; }
constexpr int SECTOR_Y(int y) { return y / SECTOR_SIZE; }
constexpr int SECTOR_ID(int sx, int sy) { return sy * MAX_SECTORS_X + sx; }

enum IOType { IO_SEND, IO_RECV, IO_ACCEPT, IO_NPC_MOVE, IO_HP_REGEN, IO_NPC_RESPAWN, IO_DB_LOGIN };
enum CL_STATE { CS_FREE, CS_CONNECT, CS_DB_WAIT, CS_PLAYING, CS_LOGOUT };

struct DB_RESULT {
    bool  success = false;
    short x       = PC_SPAWN_X;
    short y       = PC_SPAWN_Y;
    short hp      = PC_MAX_HP;
    int   level   = 1;
    int   exp     = 0;
};

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
	char      m_buff[BUF_SIZE];
	DB_RESULT m_db_result;

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
