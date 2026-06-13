#include "server.h"

void CNPC::update_viewers(short old_x, short old_y)
{
    std::unordered_set<int> old_viewers;
    for (int id : sector_manager.get_objects_in_adjacent_sectors(old_x, old_y)) {
        if (!is_pc(id)) continue;
        auto obj = get_object(id);
        if (!obj) continue;
        SESSION* player = to_player(obj);
        if (player->can_send() && player->can_see(old_x, old_y))
            old_viewers.insert(id);
    }

    sector_manager.update_object_sector(m_id, old_x, old_y, m_x, m_y);
    m_last_npc_move_time = system_clock::now();

    std::unordered_set<int> new_viewers;
    for (int id : sector_manager.get_objects_in_adjacent_sectors(m_x, m_y)) {
        if (!is_pc(id)) continue;
        auto obj = get_object(id);
        if (!obj) continue;
        SESSION* player = to_player(obj);
        if (!player->can_send() || !player->can_see(m_x, m_y)) continue;
        new_viewers.insert(id);

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

    for (int pid : old_viewers) {
        if (new_viewers.count(pid)) continue;
        auto obj = get_object(pid);
        if (obj) to_player(obj)->send_remove_object(m_id);
    }
}

void CNPC::do_roaming_move()
{
    short old_x = m_x, old_y = m_y;
    constexpr int ROAM_RANGE = 20;

    short nx = m_x, ny = m_y;
    switch (rand() % 4) {
    case 0: ny = m_y - 1; break;
    case 1: ny = m_y + 1; break;
    case 2: nx = m_x - 1; break;
    case 3: nx = m_x + 1; break;
    }

    nx = static_cast<short>(std::max(0, std::min((int)nx, WORLD_WIDTH  - 1)));
    ny = static_cast<short>(std::max(0, std::min((int)ny, WORLD_HEIGHT - 1)));

    if (std::abs(nx - m_origin_x) <= ROAM_RANGE &&
        std::abs(ny - m_origin_y) <= ROAM_RANGE) {
        m_x = nx;
        m_y = ny;
    } else {
        if      (m_x < m_origin_x) ++m_x;
        else if (m_x > m_origin_x) --m_x;
        else if (m_y < m_origin_y) ++m_y;
        else if (m_y > m_origin_y) --m_y;
    }

    update_viewers(old_x, old_y);
}

void CNPC::do_chase_move()
{
    // 타겟 유효성 확인
    if (m_target_id != -1) {
        auto tobj = get_object(m_target_id);
        if (!tobj || !is_pc(m_target_id) || tobj->m_state != CS_PLAYING) {
            m_target_id = -1;
        } else {
            int dx = std::abs(m_x - tobj->m_x);
            int dy = std::abs(m_y - tobj->m_y);
            if (dx > AGRO_DETECT_RANGE || dy > AGRO_DETECT_RANGE)
                m_target_id = -1;
        }
    }

    // Agro만 주변 스캔으로 새 타겟 탐색, Peace는 공격받은 대상만 추적
    if (m_target_id == -1 && m_npc_type == NPC_AGRO_TYPE) {
        int min_dist = INT_MAX;
        for (int id : sector_manager.get_objects_in_adjacent_sectors(m_x, m_y)) {
            if (!is_pc(id)) continue;
            auto obj = get_object(id);
            if (!obj || obj->m_state != CS_PLAYING) continue;
            SESSION* player = to_player(obj);
            if (!player->can_send()) continue;
            int dx = std::abs(m_x - obj->m_x);
            int dy = std::abs(m_y - obj->m_y);
            if (dx > AGRO_DETECT_RANGE || dy > AGRO_DETECT_RANGE) continue;
            int dist = dx + dy;
            if (dist < min_dist) { min_dist = dist; m_target_id = id; }
        }
    }

    // 타겟 없음 → 원래 상태로 복귀
    if (m_target_id == -1) {
        if (m_npc_type == NPC_AGRO_TYPE) {
            m_move_state = NPC_ROAMING;
            do_roaming_move();
        } else {
            m_move_state = NPC_IDLE;
        }
        return;
    }

    auto tobj = get_object(m_target_id);
    if (!tobj) {
        m_target_id = -1;
        m_move_state = (m_npc_type == NPC_AGRO_TYPE) ? NPC_ROAMING : NPC_IDLE;
        return;
    }

    short tx = tobj->m_x, ty = tobj->m_y;
    int dx = std::abs(m_x - tx);
    int dy = std::abs(m_y - ty);

    // 인접: 공격
    if (dx <= ATTACK_RANGE && dy <= ATTACK_RANGE) {
        SESSION* player = to_player(tobj);
        if (!player->can_send()) { m_target_id = -1; return; }

        short dmg       = NPC_ATTACK_DMG;
        player->m_hp   -= dmg;
        short remaining = player->m_hp;

        for (int id : sector_manager.get_objects_in_adjacent_sectors(m_x, m_y)) {
            if (!is_pc(id)) continue;
            auto obj = get_object(id);
            if (obj) to_player(obj)->send_damage_info(m_id, m_target_id, dmg, remaining);
        }

        if (remaining <= 0) {
            player->m_xp = player->m_xp / 2;
            player->m_hp = player->m_max_hp;
            short old_px = player->m_x, old_py = player->m_y;
            player->m_x = PC_SPAWN_X;
            player->m_y = PC_SPAWN_Y;
            sector_manager.update_object_sector(m_target_id, old_px, old_py,
                                                player->m_x, player->m_y);
            player->send_avatar_info();
            player->send_stat_info(m_target_id, player->m_hp, player->m_max_hp,
                                   player->m_level, player->m_xp,
                                   player->exp_for_next_level());
            update_player_view(m_target_id);
            m_target_id = -1;
            m_move_state = (m_npc_type == NPC_AGRO_TYPE) ? NPC_ROAMING : NPC_IDLE;
        } else {
            player->send_stat_info(m_target_id, remaining, player->m_max_hp,
                                   player->m_level, player->m_xp,
                                   player->exp_for_next_level());
        }
        return;
    }

    // 타겟 방향으로 이동
    short old_x = m_x, old_y = m_y;

    if (dx >= dy) {
        if (tx > m_x && m_x < WORLD_WIDTH - 1)  ++m_x;
        else if (tx < m_x && m_x > 0)            --m_x;
    } else {
        if (ty > m_y && m_y < WORLD_HEIGHT - 1)  ++m_y;
        else if (ty < m_y && m_y > 0)            --m_y;
    }

    update_viewers(old_x, old_y);
}

void CNPC::wake_up()
{
    bool expected = false;
    if (!m_active_npc.compare_exchange_strong(expected, true)) return;

    event_type ev;
    ev.obj_id      = m_id;
    ev.event_id    = EVENT_NPC_MOVE;
    ev.wakeup_time = system_clock::now() + milliseconds(MOVE_COOL_TIME);
    timer_queue.push(ev);
}

void process_npc_move(int npc_id)
{
    std::shared_ptr<CObject> obj = get_object(npc_id);
    if (!obj || obj->m_id < NPC_ID_START || obj->m_state != CS_PLAYING) return;

    CNPC* npc = to_npc(obj);
    if (npc->m_hp <= 0) return;

    switch (npc->m_move_state) {
    case NPC_IDLE:    return;
    case NPC_ROAMING: npc->do_roaming_move(); break;
    case NPC_CHASE:   npc->do_chase_move();   break;
    }

    bool has_nearby = false;
    int cooltime = MOVE_COOL_TIME;

    for (int id : sector_manager.get_objects_in_adjacent_sectors(npc->m_x, npc->m_y)) {
        if (!is_pc(id)) continue;
        auto pobj = get_object(id);
        if (!pobj) continue;
        SESSION* player = to_player(pobj);
        if (!player->can_send() || !player->can_see(npc->m_x, npc->m_y)) continue;
        has_nearby = true;
        if (npc->m_npc_type == NPC_AGRO_TYPE && npc->m_target_id != -1) {
            int adx = std::abs(npc->m_x - player->m_x);
            int ady = std::abs(npc->m_y - player->m_y);
            if (adx <= ATTACK_RANGE && ady <= ATTACK_RANGE)
                cooltime = ATTACK_COOL_TIME;
        }
        break;
    }

    if (has_nearby) {
        event_type ev;
        ev.obj_id      = npc_id;
        ev.event_id    = EVENT_NPC_MOVE;
        ev.wakeup_time = system_clock::now() + milliseconds(cooltime);
        timer_queue.push(ev);
    } else {
        npc->m_active_npc = false;
        if (npc->m_npc_type == NPC_AGRO_TYPE) npc->m_target_id = -1;
    }
}

void process_npc_respawn(int npc_id)
{
    std::shared_ptr<CObject> obj = get_object(npc_id);
    if (!obj || !is_npc(npc_id)) return;

    CNPC* npc    = to_npc(obj);
    npc->m_hp         = npc->m_max_hp;
    npc->m_target_id  = -1;
    npc->m_move_state = (npc->m_npc_type == NPC_AGRO_TYPE) ? NPC_ROAMING : NPC_IDLE;
    npc->m_x          = static_cast<short>(rand() % WORLD_WIDTH);
    npc->m_y          = static_cast<short>(rand() % WORLD_HEIGHT);
    npc->m_origin_x   = npc->m_x;
    npc->m_origin_y   = npc->m_y;
    sector_manager.add_object_to_sector(npc_id, npc->m_x, npc->m_y);
    // wake_up은 플레이어가 근처에 왔을 때 자동으로 호출됨
}

void InitializeNPC()
{
    cout << "NPC initialize begin.\n";
    for (int i = NPC_ID_START; i < NPC_ID_START + MAX_NPCS; ++i) {
        std::shared_ptr<CNPC> npc = std::make_shared<CNPC>();
        npc->m_id = i;
        npc->m_x  = static_cast<short>(rand() % WORLD_WIDTH);
        npc->m_y  = static_cast<short>(rand() % WORLD_HEIGHT);
        npc->m_origin_x = npc->m_x;
        npc->m_origin_y = npc->m_y;
        sprintf_s(npc->m_username, "NPC%d", i - NPC_ID_START);

        if ((i % 2) == 0) {
            npc->m_npc_type  = NPC_PEACE_TYPE;
            npc->m_move_state = NPC_IDLE;
            npc->m_level     = NPC_PEACE_LEVEL;
            npc->m_hp = npc->m_max_hp = NPC_PEACE_MAX_HP;
        } else {
            npc->m_npc_type  = NPC_AGRO_TYPE;
            npc->m_move_state = NPC_ROAMING;
            npc->m_level     = NPC_AGRO_LEVEL;
            npc->m_hp = npc->m_max_hp = NPC_AGRO_MAX_HP;
        }

        clients[i] = npc;
        sector_manager.add_object_to_sector(i, npc->m_x, npc->m_y);
    }
    cout << "NPC initialize end.\n";
}
