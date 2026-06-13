#include "server.h"

void CNPC::do_random_move()
{
	short old_x = m_x;
	short old_y = m_y;

	std::unordered_set<int> old_viewers;
	for (int object_id : sector_manager.get_objects_in_adjacent_sectors(old_x, old_y)) {
		if (!is_pc(object_id)) continue;

		std::shared_ptr<CObject> obj = get_object(object_id);
		if (nullptr == obj) continue;
		SESSION* player = to_player(obj);
		if (!player->can_send()) continue;
		if (player->can_see(old_x, old_y))
			old_viewers.insert(object_id);
	}

	switch (rand() % 4) {
	case 0: if (m_y > 0) --m_y; break;
	case 1: if (m_y < WORLD_HEIGHT - 1) ++m_y; break;
	case 2: if (m_x > 0) --m_x; break;
	case 3: if (m_x < WORLD_WIDTH - 1) ++m_x; break;
	}

	sector_manager.update_object_sector(m_id, old_x, old_y, m_x, m_y);
	m_last_npc_move_time = system_clock::now();

	std::unordered_set<int> new_viewers;
	for (int object_id : sector_manager.get_objects_in_adjacent_sectors(m_x, m_y)) {
		if (!is_pc(object_id)) continue;

		std::shared_ptr<CObject> obj = get_object(object_id);
		if (nullptr == obj) continue;
		SESSION* player = to_player(obj);
		if (!player->can_send()) continue;
		if (!player->can_see(m_x, m_y)) continue;
		new_viewers.insert(object_id);

		bool already_visible = false;
		{
			std::lock_guard<std::mutex> lock(player->m_visible_mutex);
			already_visible = player->m_visible_objects.count(m_id) > 0;
		}

		if (already_visible)
			player->send_move_object(m_id);
		else
			player->send_add_object(m_id);
	}

	for (int player_id : old_viewers) {
		if (new_viewers.count(player_id) != 0) continue;

		std::shared_ptr<CObject> obj = get_object(player_id);
		if (nullptr != obj)
			to_player(obj)->send_remove_object(m_id);
	}
}

void CNPC::wake_up()
{
	bool expected = false;
	if (!m_active_npc.compare_exchange_strong(expected, true))
		return;

	event_type ev;
	ev.obj_id = m_id;
	ev.event_id = EVENT_NPC_MOVE;
	ev.wakeup_time = system_clock::now() + milliseconds(MOVE_COOL_TIME);
	timer_queue.push(ev);
}

void process_npc_move(int npc_id)
{
	std::shared_ptr<CObject> obj = get_object(npc_id);
	if (nullptr == obj || obj->m_id < NPC_ID_START || obj->m_state != CS_PLAYING) return;

	CNPC* npc = to_npc(obj);
	npc->do_random_move();

	bool has_nearby_player = false;
	for (int object_id : sector_manager.get_objects_in_adjacent_sectors(npc->m_x, npc->m_y)) {
		if (!is_pc(object_id)) continue;
		std::shared_ptr<CObject> player_obj = get_object(object_id);
		if (nullptr == player_obj) continue;
		SESSION* player = to_player(player_obj);
		if (player->can_send() && player->can_see(npc->m_x, npc->m_y)) {
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
		std::shared_ptr<CNPC> npc = std::make_shared<CNPC>();
		npc->m_id = i;
		npc->m_x = rand() % WORLD_WIDTH;
		npc->m_y = rand() % WORLD_HEIGHT;
		sprintf_s(npc->m_username, "NPC%d", i);
		clients[i] = npc;
		sector_manager.add_object_to_sector(i, npc->m_x, npc->m_y);
	}
	cout << "NPC initialize end.\n";
}
