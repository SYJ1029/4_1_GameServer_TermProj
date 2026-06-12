#include "server.h"

void SESSION::send_add_object(int object_id)
{
	if (!can_send()) return;

	std::shared_ptr<SESSION> obj = get_session(object_id);
	if (nullptr == obj) return;
	if (obj->m_state != CS_PLAYING) return;

	{
		std::lock_guard<std::mutex> lock(m_visible_mutex);
		if (m_visible_objects.count(object_id) > 0) return;
		m_visible_objects.insert(object_id);
	}

	S2C_AddPlayer packet;
	packet.size = sizeof(packet);
	packet.type = S2C_ADD_PLAYER;
	packet.playerId = object_id;
	strcpy_s(packet.username, obj->m_username);
	packet.x = obj->m_x;
	packet.y = obj->m_y;
	do_send(packet.size, reinterpret_cast<char*>(&packet));
}

void SESSION::send_remove_object(int object_id)
{
	if (!can_send()) return;

	{
		std::lock_guard<std::mutex> lock(m_visible_mutex);
		if (m_visible_objects.count(object_id) == 0) return;
		m_visible_objects.erase(object_id);
	}

	S2C_RemovePlayer packet;
	packet.size = sizeof(packet);
	packet.type = S2C_REMOVE_PLAYER;
	packet.playerId = object_id;
	do_send(packet.size, reinterpret_cast<char*>(&packet));
}

void SESSION::send_move_object(int object_id)
{
	if (!can_send()) return;

	std::shared_ptr<SESSION> obj = get_session(object_id);
	if (nullptr == obj) return;
	if (obj->m_state != CS_PLAYING) return;

	S2C_MovePlayer packet;
	packet.size = sizeof(packet);
	packet.type = S2C_MOVE_PLAYER;
	packet.playerId = object_id;
	packet.x = obj->m_x;
	packet.y = obj->m_y;
	packet.move_time = obj->m_move_time;
	do_send(packet.size, reinterpret_cast<char*>(&packet));
}

bool SESSION::process_packet(unsigned char* p)
{
	PACKET_TYPE type = *reinterpret_cast<PACKET_TYPE*>(&p[1]);
	switch (type) {
	case C2S_LOGIN:
	{
		C2S_Login* packet = reinterpret_cast<C2S_Login*>(p);
		strncpy_s(m_username, packet->username, MAX_NAME_LEN);
		m_state = CS_PLAYING;
		sector_manager.add_object_to_sector(m_id, m_x, m_y);
		send_avatar_info();
		update_player_view(m_id);
		break;
	}
	case C2S_MOVE:
	{
		C2S_Move* packet = reinterpret_cast<C2S_Move*>(p);
		m_move_time = packet->move_time;
		do_move(packet->dir);
		break;
	}
	default:
		cout << "Unknown packet type received from player[" << m_id << "].\n";
		return false;
	}
	return true;
}

void SESSION::do_move(DIRECTION dir)
{
	short old_x = m_x;
	short old_y = m_y;

	switch (dir) {
	case UP:    if (m_y > 0) --m_y; break;
	case DOWN:  if (m_y < WORLD_HEIGHT - 1) ++m_y; break;
	case LEFT:  if (m_x > 0) --m_x; break;
	case RIGHT: if (m_x < WORLD_WIDTH - 1) ++m_x; break;
	}

	sector_manager.update_object_sector(m_id, old_x, old_y, m_x, m_y);
	update_player_view(m_id);
}

void SESSION::do_random_move()
{
	short old_x = m_x;
	short old_y = m_y;

	std::unordered_set<int> old_viewers;
	for (int object_id : sector_manager.get_objects_in_adjacent_sectors(old_x, old_y)) {
		if (!is_pc(object_id)) continue;

		std::shared_ptr<SESSION> player = get_session(object_id);
		if (nullptr == player || !player->can_send()) continue;
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

		std::shared_ptr<SESSION> player = get_session(object_id);
		if (nullptr == player || !player->can_send()) continue;
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

		std::shared_ptr<SESSION> player = get_session(player_id);
		if (nullptr != player)
			player->send_remove_object(m_id);
	}
}

void SESSION::wake_up()
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
