#pragma once
#include "sector.h"
#include "session.h"

// Global variable declarations (defined in GameServer.cpp)
extern concurrency::concurrent_priority_queue<event_type> timer_queue;
extern tbb::concurrent_unordered_map<int, std::atomic<std::shared_ptr<SESSION>>> clients;
extern SectorManager sector_manager;
extern SOCKET g_server;
extern HANDLE g_iocp;
extern std::atomic<int> player_index;

std::shared_ptr<SESSION> get_session(int id);

void disconnect(int key);
void update_player_view(int player_id);
void send_login_fail(SOCKET client, const char* message);
int get_new_player_id();
void process_npc_move(int npc_id);
void worker_thread();
void timer_thread();
void InitializeNPC();
