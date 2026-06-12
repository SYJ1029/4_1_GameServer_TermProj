#include "server.h"

void process_npc_move(int npc_id)
{
	std::shared_ptr<SESSION> npc = get_session(npc_id);
	if (nullptr == npc || npc->m_id < NPC_ID_START || npc->m_state != CS_PLAYING) return;

	npc->do_random_move();

	bool has_nearby_player = false;
	for (int object_id : sector_manager.get_objects_in_adjacent_sectors(npc->m_x, npc->m_y)) {
		if (!is_pc(object_id)) continue;
		std::shared_ptr<SESSION> player = get_session(object_id);
		if (nullptr != player && player->can_send() && player->can_see(npc->m_x, npc->m_y)) {
			has_nearby_player = true;
			break;
		}
	}

	if (has_nearby_player) {
		event_type ev;
		ev.obj_id = npc_id;
		ev.event_id = EVENT_NPC_MOVE;
		ev.wakeup_time = system_clock::now() + milliseconds(MOVE_COOL_TIME);
		timer_queue.push(ev);
	}
	else {
		npc->m_active_npc = false;
	}
}

void InitializeNPC()
{
	cout << "NPC initialize begin.\n";
	for (int i = NPC_ID_START; i < NPC_ID_START + MAX_NPCS; ++i) {
		std::shared_ptr<SESSION> npc = std::make_shared<SESSION>();
		npc->m_id = i;
		npc->m_is_npc = true;
		npc->m_state = CS_PLAYING;
		npc->m_client = INVALID_SOCKET;
		npc->m_x = rand() % WORLD_WIDTH;
		npc->m_y = rand() % WORLD_HEIGHT;
		npc->m_move_time = 0;
		npc->m_active_npc = false;
		npc->m_last_npc_move_time = system_clock::now();
		sprintf_s(npc->m_username, "NPC%d", i);
		clients[i] = npc;
		sector_manager.add_object_to_sector(i, npc->m_x, npc->m_y);
	}
	cout << "NPC initialize end.\n";
}
