#include "server.h"
#include "lua_manager.h"
#include "db.h"

void SESSION::do_send(int num_bytes, const char* data)
{
    if (!can_send() || num_bytes <= 0 || num_bytes > BUF_SIZE) return;

    bool start_send = false;
    {
        std::lock_guard<std::mutex> lock(m_send_mutex);
        if (!can_send()) return;

        PendingSend pending;
        pending.size = num_bytes;
        memcpy(pending.data.data(), data, num_bytes);
        m_send_queue.push_back(std::move(pending));

        if (!m_send_pending) {
            m_send_pending = true;
            start_send = true;
        }
    }

    if (start_send)
        start_next_send();
}

void SESSION::start_next_send()
{
    EXP_OVER* over = new EXP_OVER(IO_SEND);
    over->m_send_storage.reserve(SEND_BATCH_SIZE);

    {
        std::lock_guard<std::mutex> lock(m_send_mutex);
        if (!can_send() || m_send_queue.empty()) {
            m_send_pending = false;
            delete over;
            return;
        }

        size_t batch_size = 0;
        while (!m_send_queue.empty()) {
            const PendingSend& pending = m_send_queue.front();
            if (batch_size != 0 &&
                batch_size + static_cast<size_t>(pending.size) > SEND_BATCH_SIZE)
                break;

            size_t old_size = over->m_send_storage.size();
            over->m_send_storage.resize(old_size + pending.size);
            memcpy(over->m_send_storage.data() + old_size,
                   pending.data.data(), pending.size);
            batch_size += pending.size;
            m_send_queue.pop_front();
        }
    }

    over->m_wsa.buf = over->m_send_storage.data();
    over->m_wsa.len = static_cast<ULONG>(over->m_send_storage.size());

    if (!post_send(over))
        fail_send(over);
}

bool SESSION::post_send(EXP_OVER* over)
{
    ZeroMemory(&over->m_over, sizeof(over->m_over));
    int ret = WSASend(m_client, &over->m_wsa, 1, nullptr, 0,
                      &over->m_over, nullptr);
    return ret == 0 || WSAGetLastError() == WSA_IO_PENDING;
}

void SESSION::fail_send(EXP_OVER* over)
{
    delete over;
    {
        std::lock_guard<std::mutex> lock(m_send_mutex);
        m_send_queue.clear();
        m_send_pending = false;
    }
    disconnect(m_id);
}

void SESSION::on_send_complete(EXP_OVER* over, DWORD num_bytes)
{
    if (num_bytes == 0 || num_bytes > over->m_wsa.len) {
        fail_send(over);
        return;
    }

    if (num_bytes < over->m_wsa.len) {
        over->m_wsa.buf += num_bytes;
        over->m_wsa.len -= num_bytes;
        if (!post_send(over))
            fail_send(over);
        return;
    }

    delete over;
    start_next_send();
}

// ── 월드 아이템 ───────────────────────────────────────────────────
struct WorldItem {
    int       id;
    short     x, y;
    ITEM_TYPE item_type;
    std::atomic<bool> active { true };
};
static std::atomic<int> g_next_item_id { 0 };
static tbb::concurrent_unordered_map<int, std::shared_ptr<WorldItem>> g_world_items;

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
    packet.dir = obj->m_dir;
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
    packet.dir       = obj->m_dir;
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

void SESSION::send_item_appear(int item_id, short x, short y, ITEM_TYPE item_type)
{
    if (!can_send()) return;
    S2C_ItemAppear pkt;
    pkt.size      = sizeof(pkt);
    pkt.type      = S2C_ITEM_APPEAR;
    pkt.item_id   = item_id;
    pkt.x         = x;
    pkt.y         = y;
    pkt.item_type = item_type;
    do_send(pkt.size, reinterpret_cast<char*>(&pkt));
}

void SESSION::send_item_remove(int item_id)
{
    if (!can_send()) return;
    S2C_ItemRemove pkt;
    pkt.size    = sizeof(pkt);
    pkt.type    = S2C_ITEM_REMOVE;
    pkt.item_id = item_id;
    do_send(pkt.size, reinterpret_cast<char*>(&pkt));
}

void SESSION::send_item_add(ITEM_TYPE item_type, int count)
{
    if (!can_send()) return;
    S2C_ItemAdd pkt;
    pkt.size      = sizeof(pkt);
    pkt.type      = S2C_ITEM_ADD;
    pkt.item_type = item_type;
    pkt.count     = count;
    do_send(pkt.size, reinterpret_cast<char*>(&pkt));
}

void SESSION::send_all_world_items()
{
    for (auto& [iid, witem] : g_world_items) {
        if (!witem || !witem->active) continue;
        send_item_appear(iid, witem->x, witem->y, witem->item_type);
    }
}

void SESSION::send_all_inventory_items()
{
    for (int i = 0; i < ITEM_SLOT_COUNT; ++i) {
        if (m_inventory[i] > 0)
            send_item_add((ITEM_TYPE)(i + 1), m_inventory[i]);
    }
}

void SESSION::send_quest_update(int quest_id)
{
    if (!can_send() || quest_id < 0 || quest_id >= QUEST_COUNT) return;
    static const int targets[QUEST_COUNT] = { QUEST_AGRO_TARGET, QUEST_BOSS_TARGET };
    auto& q = m_quests[quest_id];
    S2C_QuestUpdate pkt;
    pkt.size     = sizeof(pkt);
    pkt.type     = S2C_QUEST_UPDATE;
    pkt.quest_id = (unsigned char)quest_id;
    pkt.state    = q.state;
    pkt.current  = q.kill_count;
    pkt.target   = targets[quest_id];
    do_send(pkt.size, reinterpret_cast<char*>(&pkt));
}

void SESSION::send_all_quest_states()
{
    for (int i = 0; i < QUEST_COUNT; ++i) send_quest_update(i);
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

// ── 아이템 헬퍼 ──────────────────────────────────────────────────
static void spawn_item(short x, short y, ITEM_TYPE type)
{
    auto item       = std::make_shared<WorldItem>();
    item->id        = g_next_item_id++;
    item->x         = x;
    item->y         = y;
    item->item_type = type;
    g_world_items[item->id] = item;
    for (int id : sector_manager.get_objects_in_adjacent_sectors(x, y)) {
        if (!is_pc(id)) continue;
        auto obj = get_object(id);
        if (obj) to_player(obj)->send_item_appear(item->id, x, y, type);
    }
}

// 퀘스트 킬 카운트 증가 + 완료 처리 (C2S_ATTACK/C2S_SKILL 공용)
static void process_quest_kill(SESSION* player, int npc_type_int)
{
    static const int targets[QUEST_COUNT] = { QUEST_AGRO_TARGET, QUEST_BOSS_TARGET };
    static const int rewards[QUEST_COUNT] = { QUEST_AGRO_REWARD_XP, QUEST_BOSS_REWARD_XP };
    static const char* names[QUEST_COUNT] = { "Agro Slayer", "Boss Hunter" };

    int qid = -1;
    if      (npc_type_int == NPC_AGRO_TYPE) qid = QUEST_ID_AGRO;
    else if (npc_type_int == NPC_BOSS_TYPE) qid = QUEST_ID_BOSS;
    if (qid < 0) return;

    // 퀘스트 보상 아이템: Agro Slayer→공격력 강화, Boss Hunter→방어력 강화
    static const ITEM_TYPE reward_items[QUEST_COUNT] = { ITEM_ATK_BOOST, ITEM_DEF_BOOST };

    auto& q = player->m_quests[qid];
    if (q.state != Q_ACTIVE) return;

    q.kill_count++;
    if (q.kill_count >= targets[qid]) {
        q.kill_count = 0;  // 반복 가능 퀘스트

        // XP 즉시 지급 + 레벨업
        int xp = rewards[qid];
        player->m_xp += xp;
        while (player->m_xp >= player->exp_for_next_level())
            player->m_xp -= player->exp_for_next_level(), ++player->m_level;
        player->m_max_hp = calc_max_hp(player->m_level);
        player->m_hp     = player->m_max_hp;

        // 보상 아이템 인벤토리에 추가
        ITEM_TYPE ritem = reward_items[qid];
        int slot_idx    = (int)ritem - 1;
        player->m_inventory[slot_idx]++;
        player->send_item_add(ritem, player->m_inventory[slot_idx]);

        char sys[MAX_CHAT_LEN];
        static const char* rnames[QUEST_COUNT] = { "공격력 강화", "방어력 강화" };
        sprintf_s(sys, "[Quest] %s 완료! +%d XP (Lv.%d) + %s 획득!",
                  names[qid], xp, player->m_level, rnames[qid]);
        player->send_chat(-1, "System", sys);
        player->send_stat_info(player->m_id, player->m_hp, player->m_max_hp,
                               player->m_level, player->m_xp, player->exp_for_next_level());
    }
    player->send_quest_update(qid);
}

static void broadcast_item_remove(int item_id, short x, short y)
{
    for (int id : sector_manager.get_objects_in_adjacent_sectors(x, y)) {
        if (!is_pc(id)) continue;
        auto obj = get_object(id);
        if (obj) to_player(obj)->send_item_remove(item_id);
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

    // NPC 타입별 아이템 드롭 (슬롯 1-3만 세계 아이템으로 소환)
    {
        int r = rand() % 100;
        if (npc->m_npc_type == NPC_BOSS_TYPE) {
            if      (r < 20) spawn_item(old_x, old_y, ITEM_HP_POTION);
            else if (r < 40) spawn_item(old_x, old_y, ITEM_HI_POTION);
            else if (r < 70) spawn_item(old_x, old_y, ITEM_ELIXIR);
        } else if (npc->m_npc_type == NPC_AGRO_TYPE) {
            if      (r < 25) spawn_item(old_x, old_y, ITEM_HP_POTION);
            else if (r < 35) spawn_item(old_x, old_y, ITEM_HI_POTION);
        } else {
            if (r < 30) spawn_item(old_x, old_y, ITEM_HP_POTION);
        }
    }

    event_type ev;
    ev.obj_id      = npc_id;
    ev.event_id    = EVENT_NPC_RESPAWN;
    int respawn_ms = (npc->m_npc_type == NPC_BOSS_TYPE) ? BOSS_RESPAWN_TIME : NPC_RESPAWN_TIME;
    ev.wakeup_time = system_clock::now() + milliseconds(respawn_ms);
    timer_queue.push(ev);
}

// 플레이어 사망 처리 (EXP 50% 감소 + 스폰 위치 귀환)
static void player_die(int player_id, SESSION* player)
{
    player->m_xp = player->m_xp / 2;
    player->m_hp = player->m_max_hp;
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
        if (m_state != CS_CONNECT) break;
        C2S_Login* packet = reinterpret_cast<C2S_Login*>(p);
        strncpy_s(m_username, packet->username, MAX_NAME_LEN - 1);

        if (strncmp(m_username, "bot_", 4) == 0) {
            // 스트레스 테스트 봇: DB 스킵, 월드 전역 랜덤 위치에 분산 배치
            m_x = static_cast<short>(rand() % WORLD_WIDTH);
            m_y = static_cast<short>(rand() % WORLD_HEIGHT);
            m_state = CS_PLAYING;
            sector_manager.add_object_to_sector(m_id, m_x, m_y);
            send_login_success();
            send_avatar_info();
            send_all_world_items();
            send_all_quest_states();
            update_player_view(m_id);
            event_type regen_ev;
            regen_ev.obj_id      = m_id;
            regen_ev.event_id    = EVENT_HP_REGEN;
            regen_ev.wakeup_time = system_clock::now() + milliseconds(HP_REGEN_TIME);
            timer_queue.push(regen_ev);
            break;
        }

        m_state = CS_DB_WAIT;
        DB_EVENT ev;
        ev.type       = DB_LOGIN;
        ev.session_id = m_id;
        strncpy_s(ev.login_id, m_username, MAX_NAME_LEN - 1);
        db_queue.push(ev);
        break;
    }
    case C2S_MOVE:
    {
        C2S_Move* packet = reinterpret_cast<C2S_Move*>(p);
        m_move_time = packet->move_time;
        DIRECTION dir = packet->dir;
        m_dir = dir;  // 바라보는 방향 갱신
        do_move(dir);
        if (system_clock::now() < m_spd_boost_until) do_move(dir);  // 이동속도 버프: 2칸
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

            // 바라보는 방향 정면 3칸 호(arc) 공격
            int dx = target_obj->m_x - m_x;
            int dy = target_obj->m_y - m_y;
            bool in_arc = false;
            switch (m_dir) {
            case UP:    in_arc = (dy == -1 && std::abs(dx) <= 1); break;
            case DOWN:  in_arc = (dy ==  1 && std::abs(dx) <= 1); break;
            case LEFT:  in_arc = (dx == -1 && std::abs(dy) <= 1); break;
            case RIGHT: in_arc = (dx ==  1 && std::abs(dy) <= 1); break;
            }
            if (!in_arc) continue;

            CNPC* npc  = to_npc(target_obj);
            short base_atk = calc_atk_dmg(m_level);
            short dmg  = (system_clock::now() < m_atk_boost_until) ? base_atk * 2 : base_atk;
            npc->m_hp -= dmg;
            short rem  = npc->m_hp;

            broadcast_damage(m_x, m_y, m_id, obj_id, dmg, rem);

            if (rem <= 0) {
                // EXP 계산: 레벨^2 * 2, Agro=2배(로밍), Peace=1배(고정)
                int xp_gain = npc->m_level * npc->m_level * 2;
                if      (npc->m_npc_type == NPC_BOSS_TYPE) xp_gain *= 10; // 보스 = 10배
                else if (npc->m_npc_type == NPC_AGRO_TYPE) xp_gain *= 2;  // Agro = 2배

                int npc_type_snapshot = npc->m_npc_type;
                npc_die(obj_id, npc);

                // EXP 지급 + 레벨업
                m_xp += xp_gain;
                while (m_xp >= exp_for_next_level())
                    m_xp -= exp_for_next_level(), ++m_level;
                m_max_hp = calc_max_hp(m_level);
                m_hp     = m_max_hp;

                // 퀘스트 진행
                process_quest_kill(this, npc_type_snapshot);

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
    case C2S_SKILL:
    {
        if (m_state != CS_PLAYING) break;

        auto now = system_clock::now();
        if (duration_cast<milliseconds>(now - m_last_skill_time).count() < SKILL_COOL_TIME) break;
        m_last_skill_time = now;

        // 3x3 범위 (체비쇼프 거리 1) 내 모든 NPC 피격
        std::vector<int> visibles;
        {
            std::lock_guard<std::mutex> lock(m_visible_mutex);
            visibles.assign(m_visible_objects.begin(), m_visible_objects.end());
        }

        for (int obj_id : visibles) {
            if (!is_npc(obj_id)) continue;
            auto target_obj = get_object(obj_id);
            if (!target_obj || target_obj->m_state != CS_PLAYING || target_obj->m_hp <= 0) continue;

            int dx = std::abs(target_obj->m_x - m_x);
            int dy = std::abs(target_obj->m_y - m_y);
            if (dx > 1 || dy > 1) continue;

            CNPC* npc  = to_npc(target_obj);
            short base_skill = calc_skill_dmg(m_level);
            short dmg  = (system_clock::now() < m_atk_boost_until) ? base_skill * 2 : base_skill;
            npc->m_hp -= dmg;
            short rem  = npc->m_hp;

            broadcast_damage(m_x, m_y, m_id, obj_id, dmg, rem);

            if (rem <= 0) {
                int xp_gain = npc->m_level * npc->m_level * 2;
                if      (npc->m_npc_type == NPC_BOSS_TYPE) xp_gain *= 10;
                else if (npc->m_npc_type == NPC_AGRO_TYPE) xp_gain *= 2;

                int npc_type_snapshot = npc->m_npc_type;
                npc_die(obj_id, npc);

                m_xp += xp_gain;
                while (m_xp >= exp_for_next_level())
                    m_xp -= exp_for_next_level(), ++m_level;
                m_max_hp = calc_max_hp(m_level);
                m_hp     = m_max_hp;

                process_quest_kill(this, npc_type_snapshot);

                char sys_msg[MAX_CHAT_LEN];
                sprintf_s(sys_msg, "[Skill] %s 처치! +%d XP (Lv.%d, XP:%d/%d)",
                          npc->m_username, xp_gain, m_level, m_xp, exp_for_next_level());
                send_chat(-1, "System", sys_msg);
                send_stat_info(m_id, m_hp, m_max_hp, m_level, m_xp, exp_for_next_level());
            } else {
                npc->m_target_id  = m_id;
                npc->m_move_state = NPC_STATE_CHASE;
                broadcast_npc_state(obj_id, npc);
                npc->wake_up();
            }
        }
        break;
    }
    case C2S_USE_ITEM:
    {
        if (m_state != CS_PLAYING) break;
        auto* pkt = reinterpret_cast<C2S_UseItem*>(p);
        int idx   = (int)pkt->item_type - 1;
        if (idx < 0 || idx >= ITEM_SLOT_COUNT || m_inventory[idx] <= 0) break;

        char sys[MAX_CHAT_LEN] = "";
        bool used = false;

        if (pkt->item_type == ITEM_HP_POTION && m_hp < m_max_hp) {
            m_hp  = std::min(m_max_hp, (short)(m_hp + HP_POTION_RESTORE));
            sprintf_s(sys, "HP 포션 사용! HP %d/%d", m_hp, m_max_hp);
            used = true;
        } else if (pkt->item_type == ITEM_HI_POTION && m_hp < m_max_hp) {
            m_hp  = std::min(m_max_hp, (short)(m_hp + HP_HI_POTION_RESTORE));
            sprintf_s(sys, "대형 HP 포션 사용! HP %d/%d", m_hp, m_max_hp);
            used = true;
        } else if (pkt->item_type == ITEM_ELIXIR && m_hp < m_max_hp) {
            m_hp  = m_max_hp;
            sprintf_s(sys, "엘릭서 사용! HP 완전 회복 (%d/%d)", m_hp, m_max_hp);
            used = true;
        } else if (pkt->item_type == ITEM_ATK_BOOST) {
            m_atk_boost_until = system_clock::now() + seconds(30);
            sprintf_s(sys, "공격력 강화! 30초간 공격 데미지 2배");
            used = true;
        } else if (pkt->item_type == ITEM_DEF_BOOST) {
            m_def_boost_until = system_clock::now() + seconds(30);
            sprintf_s(sys, "방어력 강화! 30초간 피해 50%% 감소");
            used = true;
        } else if (pkt->item_type == ITEM_SPD_BOOST) {
            m_spd_boost_until = system_clock::now() + seconds(30);
            sprintf_s(sys, "이동속도 증가! 30초간 이동 2칸");
            used = true;
        }

        if (used) {
            m_inventory[idx]--;
            send_stat_info(m_id, m_hp, m_max_hp, m_level, m_xp, exp_for_next_level());
            send_item_add(pkt->item_type, m_inventory[idx]);
            if (sys[0]) send_chat(-1, "System", sys);
        }
        break;
    }
    case C2S_RANGED_ATTACK:
    {
        if (m_state != CS_PLAYING) break;
        auto now = system_clock::now();
        if (duration_cast<milliseconds>(now - m_last_ranged_atk_time).count() < RANGED_ATK_COOL_TIME) break;
        m_last_ranged_atk_time = now;

        int ddx = (m_dir == LEFT ? -1 : m_dir == RIGHT ? 1 : 0);
        int ddy = (m_dir == UP   ? -1 : m_dir == DOWN  ? 1 : 0);

        int   hit_id = -1;
        short travel = 0;
        for (short i = 1; i <= RANGED_ATK_RANGE; ++i) {
            short tx = static_cast<short>(m_x + ddx * i);
            short ty = static_cast<short>(m_y + ddy * i);
            if (tx < 0 || tx >= WORLD_WIDTH || ty < 0 || ty >= WORLD_HEIGHT) break;
            if (is_obstacle(tx, ty)) break;
            travel = i;
            for (int vid : sector_manager.get_objects_in_adjacent_sectors(tx, ty)) {
                if (!is_npc(vid)) continue;
                auto nobj = get_object(vid);
                if (!nobj || nobj->m_state != CS_PLAYING || nobj->m_hp <= 0) continue;
                if (nobj->m_x != tx || nobj->m_y != ty) continue;
                hit_id = vid;
                break;
            }
            if (hit_id != -1) break;
        }

        // 모든 인접 플레이어에게 투사체 패킷 브로드캐스트
        for (int id : sector_manager.get_objects_in_adjacent_sectors(m_x, m_y)) {
            if (!is_pc(id)) continue;
            auto obj = get_object(id);
            if (!obj) continue;
            S2C_Projectile pkt;
            pkt.size        = sizeof(pkt);
            pkt.type        = S2C_PROJECTILE;
            pkt.attacker_id = m_id;
            pkt.sx          = m_x;
            pkt.sy          = m_y;
            pkt.dir         = m_dir;
            pkt.range       = travel;
            pkt.hit_id      = hit_id;
            to_player(obj)->do_send(pkt.size, reinterpret_cast<char*>(&pkt));
        }

        if (hit_id != -1) {
            auto nobj = get_object(hit_id);
            if (nobj && nobj->m_hp > 0) {
                CNPC* npc      = to_npc(nobj);
                short base_dmg = calc_skill_dmg(m_level);
                short dmg      = (now < m_atk_boost_until) ? base_dmg * 2 : base_dmg;
                npc->m_hp -= dmg;
                short rem  = npc->m_hp;

                broadcast_damage(m_x, m_y, m_id, hit_id, dmg, rem);

                if (rem <= 0) {
                    int xp_gain = npc->m_level * npc->m_level * 2;
                    if      (npc->m_npc_type == NPC_BOSS_TYPE) xp_gain *= 10;
                    else if (npc->m_npc_type == NPC_AGRO_TYPE) xp_gain *= 2;

                    int npc_type_snapshot = npc->m_npc_type;
                    npc_die(hit_id, npc);

                    m_xp += xp_gain;
                    while (m_xp >= exp_for_next_level())
                        m_xp -= exp_for_next_level(), ++m_level;
                    m_max_hp = calc_max_hp(m_level);
                    m_hp     = m_max_hp;

                    process_quest_kill(this, npc_type_snapshot);

                    char sys_msg[MAX_CHAT_LEN];
                    sprintf_s(sys_msg, "[Ranged] %s 처치! +%d XP (Lv.%d, XP:%d/%d)",
                              npc->m_username, xp_gain, m_level, m_xp, exp_for_next_level());
                    send_chat(-1, "System", sys_msg);
                    send_stat_info(m_id, m_hp, m_max_hp, m_level, m_xp, exp_for_next_level());
                } else {
                    npc->m_target_id  = m_id;
                    npc->m_move_state = NPC_STATE_CHASE;
                    broadcast_npc_state(hit_id, npc);
                    npc->wake_up();
                }
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

    short nx = m_x, ny = m_y;
    switch (dir) {
    case UP:    if (ny > 0)               --ny; break;
    case DOWN:  if (ny < WORLD_HEIGHT - 1) ++ny; break;
    case LEFT:  if (nx > 0)               --nx; break;
    case RIGHT: if (nx < WORLD_WIDTH - 1)  ++nx; break;
    }
    if (!is_obstacle(nx, ny)) { m_x = nx; m_y = ny; }

    sector_manager.update_object_sector(m_id, old_x, old_y, m_x, m_y);
    update_player_view(m_id);

    // 같은 칸 아이템 자동 획득 (슬롯 1-3 월드 아이템)
    static const char* item_names[ITEM_SLOT_COUNT+1] = {
        "", "HP 포션", "대형 HP 포션", "엘릭서", "공격력 강화", "방어력 강화", "이동속도 증가"
    };
    for (auto& [iid, witem] : g_world_items) {
        if (!witem || !witem->active) continue;
        if (witem->x != m_x || witem->y != m_y) continue;
        bool expected = true;
        if (witem->active.compare_exchange_strong(expected, false)) {
            int slot_idx = (int)witem->item_type - 1;
            m_inventory[slot_idx]++;
            broadcast_item_remove(iid, witem->x, witem->y);
            send_item_add(witem->item_type, m_inventory[slot_idx]);
            char sys[MAX_CHAT_LEN];
            const char* nm = (slot_idx >= 0 && slot_idx < ITEM_SLOT_COUNT)
                             ? item_names[witem->item_type] : "아이템";
            sprintf_s(sys, "%s 획득! (보유: %d개) [%d]키로 사용",
                      nm, m_inventory[slot_idx], (int)witem->item_type);
            send_chat(-1, "System", sys);
        }
    }
}
