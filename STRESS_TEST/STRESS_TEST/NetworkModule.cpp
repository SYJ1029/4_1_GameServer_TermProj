#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <WinSock2.h>
#include <winsock.h>
#include <Windows.h>
#include <iostream>
#include <thread>
#include <vector>
#include <unordered_set>
#include <mutex>
#include <atomic>
#include <chrono>
#include <queue>
#include <array>
#include <memory>

using namespace std;
using namespace chrono;

extern HWND		hWnd;

const static int MAX_TEST = 10000;
const static int MAX_CLIENTS = MAX_TEST * 2;
const static int INVALID_ID = -1;
const static int MAX_PACKET_SIZE = 255;
const static int MAX_BUFF_SIZE = 255;

#pragma comment (lib, "ws2_32.lib")

#include "..\..\COMMON\PROTOCOL\protocol_2026.h"

HANDLE g_hiocp;

enum OPTYPE { OP_SEND, OP_RECV, OP_DO_MOVE };

high_resolution_clock::time_point last_connect_time;

struct OverlappedEx {
	WSAOVERLAPPED over;
	WSABUF wsabuf;
	unsigned char IOCP_buf[MAX_BUFF_SIZE];
	OPTYPE event_type;
	int event_target;
};

struct CLIENT {
	int id;
	int x;
	int y;
	DIRECTION dir;
	atomic_bool connected;

	SOCKET client_socket;
	OverlappedEx recv_over;
	unsigned char packet_buf[MAX_PACKET_SIZE];
	int prev_packet_data;
	int curr_packet_size;
	high_resolution_clock::time_point last_move_time;
	high_resolution_clock::time_point last_atk_time;
	high_resolution_clock::time_point last_ranged_time;
};

array<int, MAX_CLIENTS> client_map;
array<CLIENT, MAX_CLIENTS> g_clients;
atomic_int num_connections;
atomic_int client_to_close;
atomic_int active_clients;

int global_delay;  // ms 단위, 1000이 넘으면 클라이언트 수 감소

vector <thread*> worker_threads;
thread test_thread;

float point_cloud[MAX_TEST * 2];

struct ALIEN {
	int id;
	int x, y;
	int visible_count;
};

void error_display(const char* msg, int err_no)
{
	WCHAR* lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, err_no,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	std::cout << msg;
	std::wcout << L"Error: " << lpMsgBuf << std::endl;
	LocalFree(lpMsgBuf);
}

void DisconnectClient(int ci)
{
	bool status = true;
	if (true == atomic_compare_exchange_strong(&g_clients[ci].connected, &status, false)) {
		closesocket(g_clients[ci].client_socket);
		active_clients--;
	}
}

void SendPacket(int cl, void* packet)
{
	int psize = reinterpret_cast<unsigned char*>(packet)[0];
	OverlappedEx* over = new OverlappedEx;
	over->event_type = OP_SEND;
	memcpy(over->IOCP_buf, packet, psize);
	ZeroMemory(&over->over, sizeof(over->over));
	over->wsabuf.buf = reinterpret_cast<CHAR*>(over->IOCP_buf);
	over->wsabuf.len = psize;
	int ret = WSASend(g_clients[cl].client_socket, &over->wsabuf, 1, NULL, 0,
		&over->over, NULL);
	if (0 != ret) {
		int err_no = WSAGetLastError();
		if (WSA_IO_PENDING != err_no)
			error_display("Error in SendPacket:", err_no);
	}
}

void ProcessPacket(int ci, unsigned char packet[])
{
	PACKET_TYPE type = *reinterpret_cast<PACKET_TYPE*>(&packet[1]);
	switch (type) {
	case S2C_LOGIN_RESULT:
	{
		S2C_LoginResult* p = reinterpret_cast<S2C_LoginResult*>(packet);
		if (!p->success) {
			DisconnectClient(ci);
		}
		// 성공 시: 서버가 이어서 S2C_AVATAR_INFO를 전송하므로 대기
		break;
	}
	case S2C_MOVE_PLAYER:
	{
		S2C_MovePlayer* move_packet = reinterpret_cast<S2C_MovePlayer*>(packet);
		if (move_packet->playerId < MAX_CLIENTS) {
			int my_id = client_map[move_packet->playerId];
			if (-1 != my_id) {
				g_clients[my_id].x   = move_packet->x;
				g_clients[my_id].y   = move_packet->y;
				g_clients[my_id].dir = move_packet->dir;
			}
			if (ci == my_id) {
				if (0 != move_packet->move_time) {
					auto d_ms = duration_cast<milliseconds>(high_resolution_clock::now().time_since_epoch()).count()
						- static_cast<long long>(move_packet->move_time);
					if (global_delay < d_ms) global_delay++;
					else if (global_delay > d_ms) global_delay--;
				}
			}
		}
		break;
	}
	case S2C_ADD_PLAYER: break;
	case S2C_REMOVE_PLAYER: break;
	case S2C_AVATAR_INFO:
	{
		g_clients[ci].connected = true;
		active_clients++;
		S2C_AvatarInfo* login_packet = reinterpret_cast<S2C_AvatarInfo*>(packet);
		int my_id = ci;
		client_map[login_packet->playerId] = my_id;
		g_clients[my_id].id  = login_packet->playerId;
		g_clients[my_id].x   = login_packet->x;
		g_clients[my_id].y   = login_packet->y;
		g_clients[my_id].dir = login_packet->dir;
		break;
	}
	case S2C_CHAT:        break;
	case S2C_STAT_INFO:   break;
	case S2C_DAMAGE_INFO: break;
	case S2C_ITEM_APPEAR: break;
	case S2C_ITEM_REMOVE: break;
	case S2C_ITEM_ADD:    break;
	case S2C_QUEST_UPDATE: break;
	case S2C_PROJECTILE:  break;
	default: break;
	}
}

void Worker_Thread()
{
	while (true) {
		DWORD io_size;
		unsigned long long ci;
		OverlappedEx* over;
		BOOL ret = GetQueuedCompletionStatus(g_hiocp, &io_size, &ci,
			reinterpret_cast<LPWSAOVERLAPPED*>(&over), INFINITE);
		int client_id = static_cast<int>(ci);
		if (FALSE == ret) {
			int err_no = WSAGetLastError();
			if (64 == err_no) DisconnectClient(client_id);
			else {
				DisconnectClient(client_id);
			}
			if (OP_SEND == over->event_type) delete over;
		}
		if (0 == io_size) {
			DisconnectClient(client_id);
			continue;
		}
		if (OP_RECV == over->event_type) {
			unsigned char* buf = g_clients[ci].recv_over.IOCP_buf;
			unsigned psize = g_clients[ci].curr_packet_size;
			unsigned pr_size = g_clients[ci].prev_packet_data;
			while (io_size > 0) {
				if (0 == psize) psize = buf[0];
				if (io_size + pr_size >= psize) {
					unsigned char packet[MAX_PACKET_SIZE];
					memcpy(packet, g_clients[ci].packet_buf, pr_size);
					memcpy(packet + pr_size, buf, psize - pr_size);
					ProcessPacket(static_cast<int>(ci), packet);
					io_size -= psize - pr_size;
					buf += psize - pr_size;
					psize = 0; pr_size = 0;
				}
				else {
					memcpy(g_clients[ci].packet_buf + pr_size, buf, io_size);
					pr_size += io_size;
					io_size = 0;
				}
			}
			g_clients[ci].curr_packet_size = psize;
			g_clients[ci].prev_packet_data = pr_size;
			DWORD recv_flag = 0;
			int ret = WSARecv(g_clients[ci].client_socket,
				&g_clients[ci].recv_over.wsabuf, 1,
				NULL, &recv_flag, &g_clients[ci].recv_over.over, NULL);
			if (SOCKET_ERROR == ret) {
				int err_no = WSAGetLastError();
				if (err_no != WSA_IO_PENDING)
				{
					DisconnectClient(client_id);
				}
			}
		}
		else if (OP_SEND == over->event_type) {
			if (io_size != over->wsabuf.len) {
				DisconnectClient(client_id);
			}
			delete over;
		}
		else if (OP_DO_MOVE == over->event_type) {
			delete over;
		}
		else {
			std::cout << "Unknown GQCS event!\n";
		}
	}
}

constexpr int DELAY_LIMIT = 100;
constexpr int DELAY_LIMIT2 = 150;
constexpr int ACCEPT_DELY = 10;

void Adjust_Number_Of_Client()
{
	static int delay_multiplier = 1;
	static int max_limit = MAXINT;
	static bool increasing = true;

	if (active_clients >= MAX_TEST) return;
	if (num_connections >= MAX_CLIENTS) return;

	auto duration = high_resolution_clock::now() - last_connect_time;
	if (ACCEPT_DELY * delay_multiplier > duration_cast<milliseconds>(duration).count()) return;

	int t_delay = global_delay;
	if (DELAY_LIMIT2 < t_delay) {
		if (true == increasing) {
			max_limit = active_clients;
			increasing = false;
		}
		if (100 > active_clients) return;
		if (ACCEPT_DELY * 10 > duration_cast<milliseconds>(duration).count()) return;
		last_connect_time = high_resolution_clock::now();
		DisconnectClient(client_to_close);
		client_to_close++;
		return;
	}
	else
		if (DELAY_LIMIT < t_delay) {
			delay_multiplier = 10;
			return;
		}
	if (max_limit - (max_limit / 20) < active_clients) return;

	increasing = true;
	last_connect_time = high_resolution_clock::now();
	int idx = num_connections;
	g_clients[idx].client_socket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);

	SOCKADDR_IN ServerAddr;
	ZeroMemory(&ServerAddr, sizeof(SOCKADDR_IN));
	ServerAddr.sin_family = AF_INET;
	ServerAddr.sin_port = htons(PORT);
	ServerAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

	int Result = WSAConnect(g_clients[idx].client_socket, (sockaddr*)&ServerAddr, sizeof(ServerAddr), NULL, NULL, NULL, NULL);
	if (0 != Result) {
		error_display("WSAConnect : ", GetLastError());
		goto fail_to_connect;
	}

	g_clients[idx].curr_packet_size = 0;
	g_clients[idx].prev_packet_data = 0;
	ZeroMemory(&g_clients[idx].recv_over, sizeof(g_clients[idx].recv_over));
	g_clients[idx].recv_over.event_type = OP_RECV;
	g_clients[idx].recv_over.wsabuf.buf =
		reinterpret_cast<CHAR*>(g_clients[idx].recv_over.IOCP_buf);
	g_clients[idx].recv_over.wsabuf.len = sizeof(g_clients[idx].recv_over.IOCP_buf);

	{
		DWORD recv_flag = 0;
		CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_clients[idx].client_socket), g_hiocp, idx, 0);

		int ret = WSARecv(g_clients[idx].client_socket, &g_clients[idx].recv_over.wsabuf, 1,
			NULL, &recv_flag, &g_clients[idx].recv_over.over, NULL);
		if (SOCKET_ERROR == ret) {
			int err_no = WSAGetLastError();
			if (err_no != WSA_IO_PENDING)
			{
				error_display("RECV ERROR", err_no);
				goto fail_to_connect;
			}
		}
	}

	// 접속 즉시 로그인 패킷 전송 (bot_ 접두사 → 서버가 DB 스킵하고 즉시 처리)
	{
		C2S_Login login_pkt;
		login_pkt.size = sizeof(C2S_Login);
		login_pkt.type = C2S_LOGIN;
		sprintf_s(login_pkt.username, "bot_%d", idx);
		SendPacket(idx, &login_pkt);
	}

	num_connections++;
fail_to_connect:
	return;
}

void Test_Thread()
{
	while (true) {
		Adjust_Number_Of_Client();

		auto now = high_resolution_clock::now();

		for (int i = 0; i < num_connections; ++i) {
			if (false == g_clients[i].connected) continue;

			// 이동 (1초마다)
			if (g_clients[i].last_move_time + 1s <= now) {
				g_clients[i].last_move_time = now;
				C2S_Move my_packet;
				my_packet.size = sizeof(my_packet);
				my_packet.type = C2S_MOVE;
				switch (rand() % 4) {
				case 0: my_packet.dir = UP;    break;
				case 1: my_packet.dir = DOWN;  break;
				case 2: my_packet.dir = LEFT;  break;
				case 3: my_packet.dir = RIGHT; break;
				}
				g_clients[i].dir = my_packet.dir;  // 로컬 방향 갱신
				my_packet.move_time = static_cast<int>(
					duration_cast<milliseconds>(now.time_since_epoch()).count());
				SendPacket(i, &my_packet);
			}

			// 방향성 근접 공격 (1.5초마다, 서버 쿨타임 1초)
			if (g_clients[i].last_atk_time + milliseconds(1500) <= now) {
				g_clients[i].last_atk_time = now;
				C2S_Attack atk_pkt;
				atk_pkt.size = sizeof(atk_pkt);
				atk_pkt.type = C2S_ATTACK;
				SendPacket(i, &atk_pkt);
			}

			// 원거리 공격 (2.5초마다, 서버 쿨타임 2초)
			if (g_clients[i].last_ranged_time + milliseconds(2500) <= now) {
				g_clients[i].last_ranged_time = now;
				C2S_RangedAttack ranged_pkt;
				ranged_pkt.size = sizeof(ranged_pkt);
				ranged_pkt.type = C2S_RANGED_ATTACK;
				SendPacket(i, &ranged_pkt);
			}
		}
	}
}

void InitializeNetwork()
{
	auto epoch = high_resolution_clock::now() - seconds(10);
	for (auto& cl : g_clients) {
		cl.connected       = false;
		cl.id              = INVALID_ID;
		cl.dir             = DOWN;
		cl.last_move_time  = epoch;
		cl.last_atk_time   = epoch;
		cl.last_ranged_time = epoch;
	}

	for (auto& cl : client_map) cl = -1;
	num_connections = 0;
	last_connect_time = high_resolution_clock::now();

	WSADATA	wsadata;
	WSAStartup(MAKEWORD(2, 2), &wsadata);

	g_hiocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, NULL, 0);

	for (int i = 0; i < 6; ++i)
		worker_threads.push_back(new std::thread{ Worker_Thread });

	test_thread = thread{ Test_Thread };
}

void ShutdownNetwork()
{
	test_thread.join();
	for (auto pth : worker_threads) {
		pth->join();
		delete pth;
	}
}

void GetPointCloud(int* size, float** points)
{
	int index = 0;
	for (int i = 0; i < num_connections; ++i)
		if (true == g_clients[i].connected) {
			point_cloud[index * 2] = static_cast<float>(g_clients[i].x);
			point_cloud[index * 2 + 1] = static_cast<float>(g_clients[i].y);
			index++;
		}

	*size = index;
	*points = point_cloud;
}
