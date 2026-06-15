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
constexpr int SEND_BATCH_SIZE = 16 * 1024;
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

// 보스 수치
constexpr int   BOSS_COUNT          = 15;
constexpr int   BOSS_LEVEL          = 20;
constexpr short BOSS_MAX_HP         = 500;
constexpr short BOSS_ATTACK_DMG_P1  = 20;   // Phase 1 (HP > 66%)
constexpr short BOSS_ATTACK_DMG_P2  = 30;   // Phase 2 (HP 33~66%)
constexpr short BOSS_ATTACK_DMG_P3  = 45;   // Phase 3 (HP < 33%, 광역)
constexpr int   BOSS_DETECT_RANGE   = 15;
constexpr int   BOSS_ATTACK_RANGE   = 2;
constexpr int   BOSS_AREA_RANGE     = 3;    // Phase 3 광역 공격 반경
constexpr int   BOSS_MOVE_P2        = 250;  // Phase 2/3 이동 인터벌(ms)
constexpr int   BOSS_RESPAWN_TIME   = 120000; // 보스 부활 2분

// 전투 수치
constexpr int   ATTACK_RANGE      = 1;   // 공격 가능 거리 (체비쇼프)
constexpr int   AGRO_DETECT_RANGE = 10;  // 어그로 감지 거리
constexpr short PC_MAX_HP         = 100;
constexpr short NPC_PEACE_MAX_HP  = 30;
constexpr short NPC_AGRO_MAX_HP   = 80;
constexpr short PC_ATTACK_DMG     = 15;
constexpr short PC_SKILL_DMG      = 30;   // 3x3 광역기
constexpr int   SKILL_COOL_TIME   = 3000; // 3초
constexpr short NPC_ATTACK_DMG    = 8;

// 레벨 비례 스탯 (DB 로드 시 / 레벨업 시 적용)
inline short calc_max_hp(int lv)    { return PC_MAX_HP    + (short)((lv - 1) * 10); }
inline short calc_atk_dmg(int lv)   { return PC_ATTACK_DMG + (short)((lv - 1) * 2); }
inline short calc_skill_dmg(int lv) { return PC_SKILL_DMG  + (short)((lv - 1) * 3); }
constexpr short HP_POTION_RESTORE    = 30;   // HP 포션 회복량
constexpr short HP_HI_POTION_RESTORE = 60;   // 대형 HP 포션 회복량

// 퀘스트 수치 (DB 연동 시 quest_definitions 테이블로 분리 예정)
// QUEST_COUNT / QUEST_ID_* 는 protocol_2026.h 에 정의
constexpr int QUEST_AGRO_TARGET       = 10;
constexpr int QUEST_BOSS_TARGET       = 3;
constexpr int QUEST_AGRO_REWARD_XP    = 500;
constexpr int QUEST_BOSS_REWARD_XP    = 3000;

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
    int   inventory[ITEM_SLOT_COUNT] = {};
    int   quest_kill[QUEST_COUNT]    = {};
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
	std::vector<char> m_send_storage;
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
