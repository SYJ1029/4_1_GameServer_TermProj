#pragma once
#include "globals.h"
#include <concurrent_queue.h>

enum DB_TYPE { DB_LOGIN, DB_SAVE };

// DB_RESULT는 globals.h 에 정의 (EXP_OVER가 멤버로 사용)

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
