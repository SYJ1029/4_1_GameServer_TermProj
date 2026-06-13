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
    packet.size     = sizeof(packet);
    packet.type     = S2C_ADD_PLAYER;
    packet.playerId = object_id;
    strcpy_s(packet.username, obj->m_username);
    packet.x        = obj->m_x;
    packet.y        = obj->m_y;
    packet.hp       = obj->m_hp;
    packet.max_hp   = obj->m_max_hp;
    if (is_npc(object_id)) {
        CNPC* npc       = to_npc(obj);
        packet.npc_type  = (NPC_KIND)npc->m_npc_type;
        packet.npc_state = npc->m_move_state;
    } else {
        packet.npc_type  = NPC_PC;
        packet.npc_state = NPC_STATE_IDLE;
    }
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
    packet.size     = sizeof(packet);
    packet.type     = S2C_REMOVE_PLAYER;
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
    packet.size      = sizeof(packet);
    packet.type      = S2C_MOVE_PLAYER;
    packet.playerId  = object_id;
    packet.x         = obj->m_x;
    packet.y         = obj->m_y;
    packet.move_time = obj->m_move_time;
    do_send(packet.size, reinterpret_cast<char*>(&packet));
}

void SESSION::send_chat(int sender_id, const char* sender_name, const char* msg)
{
    if (!can_send()) return;

    S2C_Chat packet;
    packet.size      = sizeof(packet);
    packet.type      = S2C_CHAT;
    packet.sender_id = sender_id;
    strncpy_s(packet.sender_name, sender_name, MAX_NAME_LEN - 1);
    strncpy_s(packet.msg, msg, MAX_CHAT_LEN - 1);
    do_send(packet.size, reinterpret_cast<char*>(&packet));
}

void SESSION::send_stat_info(int object_id, short hp, short max_hp,
                              int level, int exp, int exp_next, NPC_STATE npc_state)
{
    if (!can_send()) return;

    S2C_StatInfo packet;
    packet.size      = sizeof(packet);
    packet.type      = S2C_STAT_INFO;
    packet.object_id = object_id;
    packet.hp        = hp;
    packet.max_hp    = max_hp;
    packet.level     = level;
    packet.exp       = exp;
    packet.exp_next  = exp_next;
    packet.npc_state = npc_state;
    do_send(packet.size, reinterpret_cast<char*>(&packet));
}

void SESSION::send_damage_info(int attacker_id, int target_id, short damage, short target_hp)
{
    if (!can_send()) return;

    S2C_DamageInfo packet;
    packet.size        = sizeof(packet);
    packet.type        = S2C_DAMAGE_INFO;
    packet.attacker_id = attacker_id;
    packet.target_id   = target_id;
    packet.damage      = damage;
    packet.target_hp   = target_hp;
    do_send(packet.size, reinterpret_cast<char*>(&packet));
}

static void broadcast_damage(short ax, short ay, int attacker_id, int target_id,
                              short dmg, short target_hp)
{
    for (int id : sector_manager.get_objects_in_adjacent_sectors(ax, ay)) {
        if (!is_pc(id)) continue;
        auto obj = get_object(id);
        if (obj) to_player(obj)->send_damage_info(attacker_id, target_id, dmg, target_hp);
    }
}

// NPC 사망 처리 (섹터 제거 + 30초 리스폰 예약)
static void npc_die(int npc_id, CNPC* npc)
{
    short old_x = npc->m_x, old_y = npc->m_y;

    for (int vid : sector_manager.get_objects_in_adjacent_sectors(old_x, old_y)) {
        if (!is_pc(vid)) continue;
        auto vobj = get_object(vid);
        if (vobj) to_player(vobj)->send_remove_object(npc_id);
    }

    sector_manager.remove_object_from_sector(npc_id, old_x, old_y);
    npc->m_active_npc = false;
    npc->m_target_id  = -1;

    event_type ev;
    ev.obj_id      = npc_id;
    ev.event_id    = EVENT_NPC_RESPAWN;
    ev.wakeup_time = system_clock::now() + milliseconds(NPC_RESPAWN_TIME);
    timer_queue.push(ev);
}

// 플레이어 사망 처리 (EXP 50% 감소 + 스폰 위치 귀환)
static void player_die(int player_id, SESSION* player)
{
    player->m_xp    = player->m_xp / 2;
    player->m_hp    = player->m_max_hp;
    short old_x = player->m_x, old_y = player->m_y;
    player->m_x = PC_SPAWN_X;
    player->m_y = PC_SPAWN_Y;
    sector_manager.update_object_sector(player_id, old_x, old_y, player->m_x, player->m_y);
    player->send_avatar_info();
    player->send_stat_info(player_id, player->m_hp, player->m_max_hp,
                           player->m_level, player->m_xp, player->exp_for_next_level());
    update_player_view(player_id);
}

bool SESSION::process_packet(unsigned char* p)
{
    PACKET_TYPE type = *reinterpret_cast<PACKET_TYPE*>(&p[1]);
    switch (type)
    {
    case C2S_LOGIN:
    {
        C2S_Login* packet = reinterpret_cast<C2S_Login*>(p);
        strncpy_s(m_username, packet->username, MAX_NAME_LEN - 1);
        m_state = CS_PLAYING;
        sector_manager.add_object_to_sector(m_id, m_x, m_y);
        send_avatar_info();
        update_player_view(m_id);

        // HP 자동 회복 타이머 시작
        event_type ev;
        ev.obj_id      = m_id;
        ev.event_id    = EVENT_HP_REGEN;
        ev.wakeup_time = system_clock::now() + milliseconds(HP_REGEN_TIME);
        timer_queue.push(ev);
        break;
    }
    case C2S_MOVE:
    {
        C2S_Move* packet = reinterpret_cast<C2S_Move*>(p);
        m_move_time = packet->move_time;
        do_move(packet->dir);
        break;
    }
    case C2S_CHAT:
    {
        C2S_Chat* packet = reinterpret_cast<C2S_Chat*>(p);
        packet->msg[MAX_CHAT_LEN - 1] = 0;

        for (int id : sector_manager.get_objects_in_adjacent_sectors(m_x, m_y)) {
            if (!is_pc(id)) continue;
            auto obj = get_object(id);
            if (obj) to_player(obj)->send_chat(m_id, m_username, packet->msg);
        }
        break;
    }
    case C2S_ATTACK:
    {
        // 상하좌우 4방향 동시 공격
        const short offsets[4][2] = { {0,-1},{0,1},{-1,0},{1,0} };

        std::vector<int> visibles;
        {
            std::lock_guard<std::mutex> lock(m_visible_mutex);
            visibles.assign(m_visible_objects.begin(), m_visible_objects.end());
        }

        for (int obj_id : visibles) {
            if (!is_npc(obj_id)) continue;
            auto target_obj = get_object(obj_id);
            if (!target_obj || target_obj->m_state != CS_PLAYING || target_obj->m_hp <= 0)
                continue;

            // 4방향 중 하나인지 확인 (정확히 상/하/좌/우 1칸)
            int dx = target_obj->m_x - m_x;
            int dy = target_obj->m_y - m_y;
            bool is_cardinal = (dx == 0 && std::abs(dy) == 1)
                             || (dy == 0 && std::abs(dx) == 1);
            if (!is_cardinal) continue;

            CNPC* npc  = to_npc(target_obj);
            short dmg  = PC_ATTACK_DMG;
            npc->m_hp -= dmg;
            short rem  = npc->m_hp;

            broadcast_damage(m_x, m_y, m_id, obj_id, dmg, rem);

            if (rem <= 0) {
                // EXP 계산: 레벨^2 * 2, Agro=2배(로밍), Peace=1배(고정)
                int xp_gain = npc->m_level * npc->m_level * 2;
                if (npc->m_npc_type == NPC_AGRO_TYPE) xp_gain *= 2; // Agro = 로밍 2배

                npc_die(obj_id, npc);

                // EXP 지급 + 레벨업
                m_xp += xp_gain;
                while (m_xp >= exp_for_next_level())
                    m_xp -= exp_for_next_level(), ++m_level;

                // 전투 로그 메시지
                char sys_msg[MAX_CHAT_LEN];
                sprintf_s(sys_msg, "%s 처치! +%d XP (Lv.%d, XP:%d/%d)",
                          npc->m_username, xp_gain, m_level, m_xp, exp_for_next_level());
                send_chat(-1, "System", sys_msg);

                send_stat_info(m_id, m_hp, m_max_hp,
                               m_level, m_xp, exp_for_next_level());
            }
            else {
                // 피격 시 모든 NPC 타입이 CHASE로 전환 (Peace도 반격 추적)
                npc->m_target_id  = m_id;
                npc->m_move_state = NPC_STATE_CHASE;
                broadcast_npc_state(obj_id, npc);
                npc->wake_up();
            }
        }
        break;
    }
    default:
        cout << "Unknown packet type from player[" << m_id << "].\n";
        return false;
    }
    return true;
}

void SESSION::do_move(DIRECTION dir)
{
    short old_x = m_x, old_y = m_y;

    switch (dir) {
    case UP:    if (m_y > 0)               --m_y; break;
    case DOWN:  if (m_y < WORLD_HEIGHT - 1) ++m_y; break;
    case LEFT:  if (m_x > 0)               --m_x; break;
    case RIGHT: if (m_x < WORLD_WIDTH - 1)  ++m_x; break;
    }

    sector_manager.update_object_sector(m_id, old_x, old_y, m_x, m_y);
    update_player_view(m_id);
}
