#include "server.h"
#include "lua_manager.h"
#include "astar.h"

void broadcast_npc_state(int npc_id, CNPC* npc)
{
    for (int id : sector_manager.get_objects_in_adjacent_sectors(npc->m_x, npc->m_y)) {
        if (!is_pc(id)) continue;
        auto obj = get_object(id);
        if (obj) to_player(obj)->send_stat_info(npc_id, npc->m_hp, npc->m_max_hp,
                                                 0, 0, 0, npc->m_move_state);
    }
}

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
    // Agro/Boss: 주변 플레이어 감지 시 즉시 CHASE 전환
    if (m_npc_type == NPC_AGRO_TYPE || m_npc_type == NPC_BOSS_TYPE) {
        int detect = (m_npc_type == NPC_BOSS_TYPE) ? BOSS_DETECT_RANGE : AGRO_DETECT_RANGE;
        int min_dist = INT_MAX;
        for (int id : sector_manager.get_objects_in_adjacent_sectors(m_x, m_y)) {
            if (!is_pc(id)) continue;
            auto obj = get_object(id);
            if (!obj || obj->m_state != CS_PLAYING) continue;
            SESSION* player = to_player(obj);
            if (!player->can_send()) continue;
            int dx = std::abs(m_x - obj->m_x);
            int dy = std::abs(m_y - obj->m_y);
            if (dx > detect || dy > detect) continue;
            int dist = dx + dy;
            if (dist < min_dist) { min_dist = dist; m_target_id = id; }
        }
        if (m_target_id != -1) {
            m_move_state = NPC_STATE_CHASE;
            broadcast_npc_state(m_id, this);
            do_chase_move();
            return;
        }
    }

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
        std::abs(ny - m_origin_y) <= ROAM_RANGE &&
        !is_obstacle(nx, ny)) {
        m_x = nx;
        m_y = ny;
    } else if (!is_obstacle(m_x, m_y)) {
        // 범위 복귀 이동
        short rx = m_x, ry = m_y;
        if      (m_x < m_origin_x) ++rx;
        else if (m_x > m_origin_x) --rx;
        else if (m_y < m_origin_y) ++ry;
        else if (m_y > m_origin_y) --ry;
        if (!is_obstacle(rx, ry)) { m_x = rx; m_y = ry; }
    }

    update_viewers(old_x, old_y);
}

void CNPC::do_chase_move()
{
    int detect_range = (m_npc_type == NPC_BOSS_TYPE) ? BOSS_DETECT_RANGE : AGRO_DETECT_RANGE;

    // 타겟 유효성 확인
    if (m_target_id != -1) {
        auto tobj = get_object(m_target_id);
        if (!tobj || !is_pc(m_target_id) || tobj->m_state != CS_PLAYING) {
            m_target_id = -1;
        } else {
            int dx = std::abs(m_x - tobj->m_x);
            int dy = std::abs(m_y - tobj->m_y);
            if (dx > detect_range || dy > detect_range)
                m_target_id = -1;
        }
    }

    // Agro/Boss: 주변 스캔으로 새 타겟 탐색, Peace: 공격받은 대상만 추적
    if (m_target_id == -1 && m_npc_type != NPC_PEACE_TYPE) {
        int min_dist = INT_MAX;
        for (int id : sector_manager.get_objects_in_adjacent_sectors(m_x, m_y)) {
            if (!is_pc(id)) continue;
            auto obj = get_object(id);
            if (!obj || obj->m_state != CS_PLAYING) continue;
            SESSION* player = to_player(obj);
            if (!player->can_send()) continue;
            int dx = std::abs(m_x - obj->m_x);
            int dy = std::abs(m_y - obj->m_y);
            if (dx > detect_range || dy > detect_range) continue;
            int dist = dx + dy;
            if (dist < min_dist) { min_dist = dist; m_target_id = id; }
        }
    }

    // 타겟 없음 → 원래 상태로 복귀
    if (m_target_id == -1) {
        m_move_state = (m_npc_type == NPC_PEACE_TYPE) ? NPC_STATE_IDLE : NPC_STATE_ROAMING;
        broadcast_npc_state(m_id, this);
        if (m_npc_type != NPC_PEACE_TYPE) do_roaming_move();
        return;
    }

    auto tobj = get_object(m_target_id);
    if (!tobj) {
        m_target_id  = -1;
        m_move_state = (m_npc_type == NPC_PEACE_TYPE) ? NPC_STATE_IDLE : NPC_STATE_ROAMING;
        broadcast_npc_state(m_id, this);
        return;
    }

    short tx = tobj->m_x, ty = tobj->m_y;
    int dx = std::abs(m_x - tx);
    int dy = std::abs(m_y - ty);

    // ── 보스 전용 공격 ──────────────────────────────────────────────
    if (m_npc_type == NPC_BOSS_TYPE && dx <= BOSS_ATTACK_RANGE && dy <= BOSS_ATTACK_RANGE) {
        int phase = get_boss_phase();

        if (phase == 3) {
            // Phase 3: 광역 공격 — BOSS_AREA_RANGE 내 모든 플레이어
            short dmg = BOSS_ATTACK_DMG_P3;  // 개별 플레이어 def boost는 아래에서 적용
            for (int id : sector_manager.get_objects_in_adjacent_sectors(m_x, m_y)) {
                if (!is_pc(id)) continue;
                auto pobj = get_object(id);
                if (!pobj || pobj->m_state != CS_PLAYING) continue;
                SESSION* pl = to_player(pobj);
                if (!pl->can_send()) continue;
                int pdx = std::abs(m_x - pl->m_x);
                int pdy = std::abs(m_y - pl->m_y);
                if (pdx > BOSS_AREA_RANGE || pdy > BOSS_AREA_RANGE) continue;

                short actual_dmg = (system_clock::now() < pl->m_def_boost_until)
                                   ? dmg / 2 : dmg;
                pl->m_hp -= actual_dmg;
                short rem = pl->m_hp;
                for (int vid : sector_manager.get_objects_in_adjacent_sectors(m_x, m_y)) {
                    if (!is_pc(vid)) continue;
                    auto vobj = get_object(vid);
                    if (vobj) to_player(vobj)->send_damage_info(m_id, id, actual_dmg, rem);
                }
                if (rem <= 0) {
                    pl->m_xp = pl->m_xp / 2;
                    pl->m_hp = pl->m_max_hp;
                    short opx = pl->m_x, opy = pl->m_y;
                    pl->m_x = PC_SPAWN_X;
                    pl->m_y = PC_SPAWN_Y;
                    sector_manager.update_object_sector(id, opx, opy, pl->m_x, pl->m_y);
                    pl->send_avatar_info();
                    pl->send_stat_info(id, pl->m_hp, pl->m_max_hp,
                                       pl->m_level, pl->m_xp, pl->exp_for_next_level());
                    update_player_view(id);
                    if (id == m_target_id) m_target_id = -1;
                } else {
                    pl->send_stat_info(id, rem, pl->m_max_hp,
                                       pl->m_level, pl->m_xp, pl->exp_for_next_level());
                }
            }
        } else {
            // Phase 1/2: 단일 타겟 공격
            SESSION* player = to_player(tobj);
            if (!player->can_send()) { m_target_id = -1; return; }

            short base_dmg = (phase == 2) ? BOSS_ATTACK_DMG_P2 : BOSS_ATTACK_DMG_P1;
            short dmg = (system_clock::now() < player->m_def_boost_until)
                        ? base_dmg / 2 : base_dmg;
            player->m_hp -= dmg;
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
            } else {
                player->send_stat_info(m_target_id, remaining, player->m_max_hp,
                                       player->m_level, player->m_xp,
                                       player->exp_for_next_level());
            }
        }
        return;
    }

    // ── Agro/Peace 기존 공격 (ATTACK_RANGE=1) ──────────────────────
    if (m_npc_type != NPC_BOSS_TYPE && dx <= ATTACK_RANGE && dy <= ATTACK_RANGE) {
        SESSION* player = to_player(tobj);
        if (!player->can_send()) { m_target_id = -1; return; }

        short dmg = (system_clock::now() < player->m_def_boost_until)
                    ? NPC_ATTACK_DMG / 2 : NPC_ATTACK_DMG;
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
            m_target_id  = -1;
            m_move_state = (m_npc_type == NPC_AGRO_TYPE) ? NPC_STATE_ROAMING : NPC_STATE_IDLE;
            broadcast_npc_state(m_id, this);
        } else {
            player->send_stat_info(m_target_id, remaining, player->m_max_hp,
                                   player->m_level, player->m_xp,
                                   player->exp_for_next_level());
        }
        return;
    }

    // A*로 다음 스텝 계산
    short old_x = m_x, old_y = m_y;
    auto [nx, ny] = astar_next_step(m_x, m_y, tx, ty);
    if (nx != m_x || ny != m_y) { m_x = nx; m_y = ny; }

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
    case NPC_STATE_IDLE:
        npc->m_active_npc = false;  // 다음 wake_up() 허용
        return;
    case NPC_STATE_ROAMING: npc->do_roaming_move(); break;
    case NPC_STATE_CHASE:   npc->do_chase_move();   break;
    }

    bool has_nearby = false;
    int cooltime = MOVE_COOL_TIME;

    if (npc->m_npc_type == NPC_BOSS_TYPE) {
        // 보스: 페이즈 기반 쿨타임
        int phase = npc->get_boss_phase();
        cooltime  = (phase >= 2) ? BOSS_MOVE_P2 : MOVE_COOL_TIME;
        if (npc->m_target_id != -1) {
            auto tobj = get_object(npc->m_target_id);
            if (tobj && tobj->m_state == CS_PLAYING &&
                std::abs(npc->m_x - tobj->m_x) <= BOSS_DETECT_RANGE &&
                std::abs(npc->m_y - tobj->m_y) <= BOSS_DETECT_RANGE) {
                has_nearby = true;
                int adx = std::abs(npc->m_x - tobj->m_x);
                int ady = std::abs(npc->m_y - tobj->m_y);
                if (adx <= BOSS_ATTACK_RANGE && ady <= BOSS_ATTACK_RANGE)
                    cooltime = (phase == 3) ? 600 : (phase == 2) ? 750 : ATTACK_COOL_TIME;
            }
        }
        if (!has_nearby) {
            for (int id : sector_manager.get_objects_in_adjacent_sectors(npc->m_x, npc->m_y)) {
                if (!is_pc(id)) continue;
                auto pobj = get_object(id);
                if (!pobj || !to_player(pobj)->can_send()) continue;
                has_nearby = true;
                break;
            }
        }
    } else {
        int deact_range = VIEW_RANGE;
        for (int id : sector_manager.get_objects_in_adjacent_sectors(npc->m_x, npc->m_y)) {
            if (!is_pc(id)) continue;
            auto pobj = get_object(id);
            if (!pobj) continue;
            SESSION* player = to_player(pobj);
            if (!player->can_send()) continue;
            int pdx = std::abs(npc->m_x - player->m_x);
            int pdy = std::abs(npc->m_y - player->m_y);
            if (pdx > deact_range || pdy > deact_range) continue;
            has_nearby = true;
            if (npc->m_npc_type == NPC_AGRO_TYPE && npc->m_target_id != -1) {
                if (pdx <= ATTACK_RANGE && pdy <= ATTACK_RANGE)
                    cooltime = ATTACK_COOL_TIME;
            }
            break;
        }
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
    npc->m_move_state = (npc->m_npc_type == NPC_PEACE_TYPE) ? NPC_STATE_IDLE : NPC_STATE_ROAMING;
    if (npc->m_npc_type == NPC_BOSS_TYPE) {
        // 보스: 고정 스폰 위치로 완전 회복 부활
        npc->m_x = npc->m_origin_x;
        npc->m_y = npc->m_origin_y;
    } else {
        // 일반 NPC: 원래 스폰 위치 ±80 반경 내 비장애물 위치에 부활
        constexpr int RESPAWN_RANGE = 80;
        short rx = npc->m_origin_x, ry = npc->m_origin_y;
        for (int tries = 0; tries < 100; ++tries) {
            short cx = npc->m_origin_x + (short)(rand() % (RESPAWN_RANGE * 2 + 1) - RESPAWN_RANGE);
            short cy = npc->m_origin_y + (short)(rand() % (RESPAWN_RANGE * 2 + 1) - RESPAWN_RANGE);
            cx = (short)std::max(1, std::min((int)cx, WORLD_WIDTH  - 1));
            cy = (short)std::max(1, std::min((int)cy, WORLD_HEIGHT - 1));
            if (!is_obstacle(cx, cy)) { rx = cx; ry = cy; break; }
        }
        npc->m_x = rx;
        npc->m_y = ry;
        // m_origin_x/y 유지 — 로밍 범위의 기준점을 초기 스폰 위치로 고정
    }
    sector_manager.add_object_to_sector(npc_id, npc->m_x, npc->m_y);
    // wake_up은 플레이어가 근처에 왔을 때 자동으로 호출됨
}

void InitializeNPC()
{
    cout << "NPC initialize begin.\n";

    int npc_id = NPC_ID_START;
    int total   = 0;

    // 보스 NPC 배치 — 각 성채(Zone) 중심에 1마리씩 고정 스폰
    // 순서: Center, OrcNW, OrcNE, GoblinSW, GoblinSE,
    //       OgreW, OgreE, KnightNW, KnightNE, KnightSW, KnightSE,
    //       DragonNW, DragonNE, DragonSW, DragonSE
    static const short BOSS_SPAWN_X[BOSS_COUNT] = {
        1000, 400, 1600, 400, 1600,
         400, 1600, 650, 1350, 650, 1350,
         750, 1250,  750, 1250
    };
    static const short BOSS_SPAWN_Y[BOSS_COUNT] = {
        1000, 450,  450, 1550, 1550,
        1000, 1000, 700,  700, 1300, 1300,
         900,  900, 1100, 1100
    };
    for (int i = 0; i < BOSS_COUNT && total < MAX_NPCS; ++i, ++npc_id, ++total) {
        auto npc = std::make_shared<CNPC>();
        npc->m_id = npc_id;

        // 장애물 회피: 원하는 위치 주변 탐색
        short bx = BOSS_SPAWN_X[i], by = BOSS_SPAWN_Y[i];
        for (int tries = 0; is_obstacle(bx, by) && tries < 50; ++tries)
            bx = BOSS_SPAWN_X[i] + (short)(rand() % 21 - 10),
            by = BOSS_SPAWN_Y[i] + (short)(rand() % 21 - 10);

        npc->m_x = npc->m_origin_x = bx;
        npc->m_y = npc->m_origin_y = by;
        npc->m_npc_type   = NPC_BOSS_TYPE;
        npc->m_move_state = NPC_STATE_ROAMING;
        npc->m_level      = BOSS_LEVEL;
        npc->m_hp = npc->m_max_hp = BOSS_MAX_HP;
        sprintf_s(npc->m_username, "Boss_%d", i + 1);
        clients[npc_id] = npc;
        sector_manager.add_object_to_sector(npc_id, bx, by);
    }
    cout << BOSS_COUNT << " boss(es) spawned.\n";

    // Lua 스크립트에 스폰 그룹이 정의된 경우 해당 기준으로 배치
    if (!g_npc_groups.empty()) {
        for (auto& grp : g_npc_groups) {
            for (int k = 0; k < grp.count && total < MAX_NPCS; ++k, ++npc_id, ++total) {
                auto npc = std::make_shared<CNPC>();
                npc->m_id = npc_id;

                // 존 범위 내 장애물 없는 위치 탐색
                short px, py;
                int tries = 0;
                do {
                    int dx = (rand() % (grp.range * 2 + 1)) - grp.range;
                    int dy = (rand() % (grp.range * 2 + 1)) - grp.range;
                    px = static_cast<short>(std::max(0, std::min(grp.x + dx, WORLD_WIDTH  - 1)));
                    py = static_cast<short>(std::max(0, std::min(grp.y + dy, WORLD_HEIGHT - 1)));
                } while (is_obstacle(px, py) && ++tries < 100);

                npc->m_x = npc->m_origin_x = px;
                npc->m_y = npc->m_origin_y = py;

                sprintf_s(npc->m_username, "%s_%d", grp.name, total);

                if (grp.npc_type == 2) {
                    npc->m_npc_type   = NPC_AGRO_TYPE;
                    npc->m_move_state = NPC_STATE_ROAMING;
                    npc->m_hp = npc->m_max_hp = NPC_AGRO_MAX_HP;
                } else {
                    npc->m_npc_type   = NPC_PEACE_TYPE;
                    npc->m_move_state = NPC_STATE_IDLE;
                    npc->m_hp = npc->m_max_hp = NPC_PEACE_MAX_HP;
                }
                npc->m_level = grp.level;

                clients[npc_id] = npc;
                sector_manager.add_object_to_sector(npc_id, px, py);
            }
        }
    }

    // 스크립트 부족분 or 그룹 없을 때 랜덤으로 채움
    for (; total < MAX_NPCS; ++npc_id, ++total) {
        auto npc = std::make_shared<CNPC>();
        npc->m_id = npc_id;
        npc->m_x  = npc->m_origin_x = static_cast<short>(rand() % WORLD_WIDTH);
        npc->m_y  = npc->m_origin_y = static_cast<short>(rand() % WORLD_HEIGHT);
        sprintf_s(npc->m_username, "NPC_%d", total);

        if ((total % 2) == 0) {
            npc->m_npc_type   = NPC_PEACE_TYPE;
            npc->m_move_state = NPC_STATE_IDLE;
            npc->m_level      = NPC_PEACE_LEVEL;
            npc->m_hp = npc->m_max_hp = NPC_PEACE_MAX_HP;
        } else {
            npc->m_npc_type   = NPC_AGRO_TYPE;
            npc->m_move_state = NPC_STATE_ROAMING;
            npc->m_level      = NPC_AGRO_LEVEL;
            npc->m_hp = npc->m_max_hp = NPC_AGRO_MAX_HP;
        }

        clients[npc_id] = npc;
        sector_manager.add_object_to_sector(npc_id, npc->m_x, npc->m_y);
    }

    cout << "NPC initialize end. (" << total << " NPCs)\n";
}
