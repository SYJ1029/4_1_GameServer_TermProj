#pragma once
#include "sector.h"
#include "NPC.h"
#include "session.h"

extern concurrency::concurrent_priority_queue<event_type> timer_queue;
extern tbb::concurrent_unordered_map<int, std::atomic<std::shared_ptr<CObject>>> clients;
extern SectorManager sector_manager;
extern SOCKET g_server;
extern HANDLE g_iocp;
extern std::atomic<int> player_index;

std::shared_ptr<CObject> get_object(int id);

// reinterpret_cast helpers - call only after is_pc / is_npc check
inline SESSION* to_player(const std::shared_ptr<CObject>& obj)
{
	return reinterpret_cast<SESSION*>(obj.get());
}
inline CNPC* to_npc(const std::shared_ptr<CObject>& obj)
{
	return reinterpret_cast<CNPC*>(obj.get());
}

void disconnect(int key);
void update_player_view(int player_id);
void send_login_fail(SOCKET client, const char* message);
int get_new_player_id();
void process_npc_move(int npc_id);
void worker_thread();
void timer_thread();
void InitializeNPC();
