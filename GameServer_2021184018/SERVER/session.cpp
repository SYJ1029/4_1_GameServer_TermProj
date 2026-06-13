#include "server.h"

void SESSION::send_add_object(int object_id)
{
	if (!can_send()) return;

	std::shared_ptr<CObject> obj = get_object(object_id);
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

	std::shared_ptr<CObject> obj = get_object(object_id);
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
