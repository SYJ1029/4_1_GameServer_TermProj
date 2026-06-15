#include "server.h"
#include "db.h"

void error_display(const wchar_t* msg, int err_no)
{
	char buf[512];
	FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, err_no, 0, buf, sizeof(buf), NULL);
	char narrow[256] = {};
	wcstombs_s(nullptr, narrow, msg, sizeof(narrow) - 1);
	std::cout << narrow << " === error " << buf;
}

bool is_pc(int id)  { return id < NPC_ID_START; }
bool is_npc(int id) { return id >= NPC_ID_START; }

std::shared_ptr<CObject> get_object(int id)
{
	auto iter = clients.find(id);
	if (iter == clients.end()) return nullptr;
	return iter->second.load();
}

void send_login_fail(SOCKET client, const char* message)
{
	S2C_LoginResult packet;
	packet.size = sizeof(packet);
	packet.type = S2C_LOGIN_RESULT;
	packet.success = false;
	strcpy_s(packet.message, message);
	WSABUF wsa_buf;
	wsa_buf.buf = reinterpret_cast<char*>(&packet);
	wsa_buf.len = packet.size;
	WSASend(client, &wsa_buf, 1, 0, 0, nullptr, nullptr);
}

int get_new_player_id()
{
	for (;;) {
		int id = player_index++;
		if (id >= NPC_ID_START) return -1;

		auto iter = clients.find(id);
		if (iter == clients.end()) return id;

		std::shared_ptr<CObject> old = iter->second.load();
		if (nullptr == old || old->m_state == CS_FREE || old->m_state == CS_LOGOUT)
			return id;
	}
}

void disconnect(int key)
{
	std::shared_ptr<CObject> obj = get_object(key);
	if (nullptr == obj || obj->m_id >= NPC_ID_START) return;

	SESSION* cl = to_player(obj);

	if (cl->m_state == CS_PLAYING) {
		int quest_kill[QUEST_COUNT];
		for (int i = 0; i < QUEST_COUNT; ++i)
			quest_kill[i] = cl->m_quests[i].kill_count;
		db_push_save(key, cl->m_username, cl->m_x, cl->m_y,
		             cl->m_hp, cl->m_level, cl->m_xp,
		             cl->m_inventory, quest_kill);
	}

	cl->m_state = CS_LOGOUT;
	sector_manager.remove_object_from_sector(key, cl->m_x, cl->m_y);

	std::unordered_set<int> visible;
	{
		std::lock_guard<std::mutex> lock(cl->m_visible_mutex);
		visible = cl->m_visible_objects;
		cl->m_visible_objects.clear();
	}

	for (int object_id : visible) {
		if (!is_pc(object_id)) continue;
		std::shared_ptr<CObject> other_obj = get_object(object_id);
		if (nullptr != other_obj)
			to_player(other_obj)->send_remove_object(key);
	}

	if (cl->m_client != INVALID_SOCKET) {
		closesocket(cl->m_client);
		cl->m_client = INVALID_SOCKET;
	}
	clients[key].store(nullptr);
}

void update_player_view(int player_id)
{
	std::shared_ptr<CObject> player_obj = get_object(player_id);
	if (nullptr == player_obj) return;
	SESSION* player = to_player(player_obj);
	if (!player->can_send()) return;

	std::unordered_set<int> old_view;
	{
		std::lock_guard<std::mutex> lock(player->m_visible_mutex);
		old_view = player->m_visible_objects;
	}

	std::unordered_set<int> new_view;
	for (int object_id : sector_manager.get_objects_in_adjacent_sectors(player->m_x, player->m_y)) {
		if (object_id == player_id) continue;

		std::shared_ptr<CObject> obj = get_object(object_id);
		if (nullptr == obj) continue;
		if (obj->m_state != CS_PLAYING) continue;
		if (player->can_see(obj->m_x, obj->m_y))
			new_view.insert(object_id);
	}

	player->send_move_object(player_id);

	for (int object_id : new_view) {
		std::shared_ptr<CObject> obj = get_object(object_id);
		if (nullptr == obj) continue;

		if (old_view.count(object_id) == 0) {
			player->send_add_object(object_id);
			if (is_pc(object_id))
				to_player(obj)->send_add_object(player_id);
			else
				to_npc(obj)->wake_up();
		}
		else if (is_pc(object_id)) {
			to_player(obj)->send_move_object(player_id);
		}
	}

	for (int object_id : old_view) {
		if (new_view.count(object_id) != 0) continue;

		player->send_remove_object(object_id);
		if (is_pc(object_id)) {
			std::shared_ptr<CObject> other_obj = get_object(object_id);
			if (nullptr != other_obj)
				to_player(other_obj)->send_remove_object(player_id);
		}
	}
}

void worker_thread()
{
	for (;;) {
		DWORD num_bytes;
		ULONG_PTR long_key;
		LPOVERLAPPED over;
		BOOL ret = GetQueuedCompletionStatus(g_iocp, &num_bytes, &long_key, &over, INFINITE);
		int key = static_cast<int>(long_key);
		EXP_OVER* exp_over = reinterpret_cast<EXP_OVER*>(over);

		if (TRUE != ret) {
			error_display(L"GQCS Error: ", WSAGetLastError());
			if (key >= 0) disconnect(key);
			continue;
		}

		switch (exp_over->m_iotype) {
		case IO_ACCEPT:
		{
			int my_id = get_new_player_id();
			if (-1 == my_id) {
				send_login_fail(exp_over->m_client_socket, "Server is full.");
				closesocket(exp_over->m_client_socket);
			}
			else {
				CreateIoCompletionPort((HANDLE)exp_over->m_client_socket, g_iocp, my_id, 0);
				std::shared_ptr<SESSION> new_pl = std::make_shared<SESSION>(exp_over->m_client_socket, my_id);
				clients[my_id] = new_pl;
				new_pl->do_recv();
			}

			exp_over->m_client_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
			AcceptEx(g_server, exp_over->m_client_socket, &exp_over->m_buff, 0,
				sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
				NULL, &exp_over->m_over);
			break;
		}
		case IO_RECV:
		{
			if (0 == num_bytes) {
				disconnect(key);
				break;
			}

			std::shared_ptr<CObject> obj = get_object(key);
			if (nullptr == obj) break;
			SESSION* cl = to_player(obj);

			unsigned char* p = reinterpret_cast<unsigned char*>(exp_over->m_buff);
			int data_size = num_bytes + cl->m_prev_recv;
			while (data_size > 0) {
				int packet_size = p[0];
				if (packet_size > data_size) break;
				if (!cl->process_packet(p)) {
					disconnect(key);
					break;
				}
				p += packet_size;
				data_size -= packet_size;
			}

			if (data_size > 0) {
				memmove(cl->m_recv_over.m_buff, p, data_size);
				cl->m_prev_recv = data_size;
			}
			else {
				cl->m_prev_recv = 0;
			}
			cl->do_recv();
			break;
		}
		case IO_SEND:
			delete exp_over;
			break;
		case IO_NPC_MOVE:
			delete exp_over;
			process_npc_move(key);
			break;
		case IO_HP_REGEN:
			delete exp_over;
			process_hp_regen(key);
			break;
		case IO_NPC_RESPAWN:
			delete exp_over;
			process_npc_respawn(key);
			break;
		case IO_DB_LOGIN:
		{
			auto obj = get_object(key);
			if (!obj || to_player(obj)->m_state != CS_DB_WAIT) {
				delete exp_over;
				break;
			}
			SESSION* cl = to_player(obj);
			if (!exp_over->m_db_result.success) {
				cl->m_state = CS_LOGOUT;
				delete exp_over;
				break;
			}
			cl->m_x      = exp_over->m_db_result.x;
			cl->m_y      = exp_over->m_db_result.y;
			cl->m_level  = exp_over->m_db_result.level;
			cl->m_max_hp = calc_max_hp(cl->m_level);
			cl->m_hp     = std::min(exp_over->m_db_result.hp, (short)cl->m_max_hp);
			cl->m_xp     = exp_over->m_db_result.exp;
			for (int i = 0; i < ITEM_SLOT_COUNT; ++i)
				cl->m_inventory[i] = exp_over->m_db_result.inventory[i];
			for (int i = 0; i < QUEST_COUNT; ++i)
				cl->m_quests[i].kill_count = exp_over->m_db_result.quest_kill[i];
			cl->m_state = CS_PLAYING;
			sector_manager.add_object_to_sector(key, cl->m_x, cl->m_y);
			cl->send_login_success();
			cl->send_avatar_info();
			cl->send_all_inventory_items();
			cl->send_all_world_items();
			cl->send_all_quest_states();
			update_player_view(key);
			event_type ev;
			ev.obj_id      = key;
			ev.event_id    = EVENT_HP_REGEN;
			ev.wakeup_time = system_clock::now() + milliseconds(HP_REGEN_TIME);
			timer_queue.push(ev);
			delete exp_over;
			break;
		}
		default:
			cout << "Unknown IO type.\n";
			break;
		}
	}
}

void process_hp_regen(int player_id)
{
	auto obj = get_object(player_id);
	if (!obj || !is_pc(player_id)) return;
	SESSION* player = to_player(obj);
	if (!player->can_send() || player->m_state != CS_PLAYING) return;

	if (player->m_hp < player->m_max_hp) {
		short regen = std::max<short>(1, player->m_max_hp / 10);
		player->m_hp = std::min(player->m_max_hp,
		                        static_cast<short>(player->m_hp + regen));
		player->send_stat_info(player_id, player->m_hp, player->m_max_hp,
		                       player->m_level, player->m_xp,
		                       player->exp_for_next_level());
	}

	event_type ev;
	ev.obj_id      = player_id;
	ev.event_id    = EVENT_HP_REGEN;
	ev.wakeup_time = system_clock::now() + milliseconds(HP_REGEN_TIME);
	timer_queue.push(ev);
}

void timer_thread()
{
	for (;;) {
		event_type ev;
		if (timer_queue.try_pop(ev)) {
			auto now = system_clock::now();
			if (ev.wakeup_time <= now) {
				if (ev.event_id == EVENT_NPC_MOVE) {
					EXP_OVER* over = new EXP_OVER(IO_NPC_MOVE);
					PostQueuedCompletionStatus(g_iocp, 1, ev.obj_id, &over->m_over);
				}
				else if (ev.event_id == EVENT_HP_REGEN) {
					EXP_OVER* over = new EXP_OVER(IO_HP_REGEN);
					PostQueuedCompletionStatus(g_iocp, 1, ev.obj_id, &over->m_over);
				}
				else if (ev.event_id == EVENT_NPC_RESPAWN) {
					EXP_OVER* over = new EXP_OVER(IO_NPC_RESPAWN);
					PostQueuedCompletionStatus(g_iocp, 1, ev.obj_id, &over->m_over);
				}
			}
			else {
				timer_queue.push(ev);
				this_thread::sleep_for(milliseconds(1));
			}
		}
		else {
			this_thread::sleep_for(milliseconds(1));
		}
	}
}
