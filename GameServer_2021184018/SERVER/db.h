#pragma once
#include "globals.h"
#include <concurrent_queue.h>

enum DB_TYPE { DB_LOGIN, DB_SAVE };

struct DB_EVENT {
    DB_TYPE type;
    int     session_id;
    char    login_id[MAX_NAME_LEN];
    short   x, y, hp;
    int     level, exp;
};

extern concurrency::concurrent_queue<DB_EVENT> db_queue;

void db_thread();
void db_push_save(int session_id, const char* login_id,
                  short x, short y, short hp, int level, int exp);
