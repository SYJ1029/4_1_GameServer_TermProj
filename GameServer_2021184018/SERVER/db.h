#pragma once
#include "globals.h"
#include <concurrent_queue.h>

enum DB_TYPE { DB_LOGIN, DB_SAVE };

struct DB_RESULT {
    bool  success = false;
    short x       = PC_SPAWN_X;
    short y       = PC_SPAWN_Y;
    short hp      = PC_MAX_HP;
    int   level   = 1;
    int   exp     = 0;
    int   inventory[ITEM_SLOT_COUNT] = {};   // 슬롯 1-6 → 인덱스 0-5
    int   quest_kill[QUEST_COUNT]    = {};   // 퀘스트별 킬카운트
};

struct DB_EVENT {
    DB_TYPE type;
    int     session_id;
    char    login_id[MAX_NAME_LEN];
    short   x, y, hp;
    int     level, exp;
    int     inventory[ITEM_SLOT_COUNT];
    int     quest_kill[QUEST_COUNT];
};

extern concurrency::concurrent_queue<DB_EVENT> db_queue;

void db_thread();
void db_push_save(int session_id, const char* login_id,
                  short x, short y, short hp, int level, int exp,
                  const int inventory[ITEM_SLOT_COUNT],
                  const int quest_kill[QUEST_COUNT]);
