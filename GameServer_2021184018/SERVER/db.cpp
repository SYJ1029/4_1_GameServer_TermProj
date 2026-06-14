#include "db.h"
#include "server.h"
#include <sqlext.h>
#pragma comment(lib, "odbc32.lib")

concurrency::concurrent_queue<DB_EVENT> db_queue;

static void db_error(SQLSMALLINT handle_type, SQLHANDLE handle)
{
    SQLSMALLINT rec = 0;
    SQLINTEGER  native;
    SQLWCHAR    state[SQL_SQLSTATE_SIZE + 1];
    SQLWCHAR    msg[1000];
    char        state_n[SQL_SQLSTATE_SIZE + 1];
    char        msg_n[1000];
    while (SQLGetDiagRecW(handle_type, handle, ++rec, state, &native,
                          msg, 1000, NULL) == SQL_SUCCESS) {
        wcstombs_s(nullptr, state_n, state, SQL_SQLSTATE_SIZE);
        wcstombs_s(nullptr, msg_n,   msg,   999);
        std::cerr << "[DB] [" << state_n << "] " << msg_n << "\n";
    }
}

static bool sql_ok(SQLRETURN r)
{
    return r == SQL_SUCCESS || r == SQL_SUCCESS_WITH_INFO;
}

static bool connect_db(SQLHENV& henv, SQLHDBC& hdbc)
{
    if (!sql_ok(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv)))
        return false;
    SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);

    if (!sql_ok(SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc)))
        return false;
    SQLSetConnectAttr(hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);

    SQLRETURN ret = SQLConnectW(hdbc,
        (SQLWCHAR*)L"SYJ_TERM_2021184018", SQL_NTS,
        NULL, 0, NULL, 0);
    if (!sql_ok(ret)) {
        db_error(SQL_HANDLE_DBC, hdbc);
        return false;
    }
    std::cout << "[DB] Connected to 2021184018_TermProjServer\n";
    return true;
}

// ── 로그인: x/y/hp/level/exp/inv1~6 로드 ────────────────────────────
static bool db_login_or_create(SQLHDBC hdbc, const char* login_id, DB_RESULT& out)
{
    SQLHSTMT hstmt;
    if (!sql_ok(SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt))) return false;

    SQLWCHAR query[128];
    swprintf_s(query, L"EXEC dbo.sp_LoginOrCreate @login_id=N'%S'", login_id);
    if (!sql_ok(SQLExecDirectW(hstmt, query, SQL_NTS))) {
        db_error(SQL_HANDLE_STMT, hstmt);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return false;
    }

    SQLSMALLINT x, y, hp;
    SQLINTEGER  level, exp;
    SQLINTEGER  inv[ITEM_SLOT_COUNT] = {};
    SQLLEN cb[11] = {};

    SQLBindCol(hstmt, 1,  SQL_C_SSHORT, &x,     0, &cb[0]);
    SQLBindCol(hstmt, 2,  SQL_C_SSHORT, &y,     0, &cb[1]);
    SQLBindCol(hstmt, 3,  SQL_C_SSHORT, &hp,    0, &cb[2]);
    SQLBindCol(hstmt, 4,  SQL_C_LONG,   &level, 0, &cb[3]);
    SQLBindCol(hstmt, 5,  SQL_C_LONG,   &exp,   0, &cb[4]);
    for (int i = 0; i < ITEM_SLOT_COUNT; ++i)
        SQLBindCol(hstmt, (SQLUSMALLINT)(6 + i), SQL_C_LONG, &inv[i], 0, &cb[5 + i]);

    if (sql_ok(SQLFetch(hstmt))) {
        out.success = true;
        out.x = x; out.y = y; out.hp = hp;
        out.level = level; out.exp = exp;
        for (int i = 0; i < ITEM_SLOT_COUNT; ++i)
            out.inventory[i] = (int)inv[i];
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return out.success;
}

// ── 퀘스트 로드 (sp_LoadPlayerExtra) ────────────────────────────────
static void db_load_quests(SQLHDBC hdbc, const char* login_id, DB_RESULT& out)
{
    SQLHSTMT hstmt;
    if (!sql_ok(SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt))) return;

    SQLWCHAR query[128];
    swprintf_s(query, L"EXEC dbo.sp_LoadPlayerExtra @login_id=N'%S'", login_id);
    if (!sql_ok(SQLExecDirectW(hstmt, query, SQL_NTS))) {
        db_error(SQL_HANDLE_STMT, hstmt);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return;
    }

    SQLINTEGER quest_id = 0, kill_count = 0;
    SQLLEN cb1 = 0, cb2 = 0;
    SQLBindCol(hstmt, 1, SQL_C_LONG, &quest_id,   0, &cb1);
    SQLBindCol(hstmt, 2, SQL_C_LONG, &kill_count, 0, &cb2);
    while (sql_ok(SQLFetch(hstmt))) {
        if (quest_id >= 0 && quest_id < QUEST_COUNT)
            out.quest_kill[quest_id] = (int)kill_count;
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
}

// ── 기본 플레이어 저장 (x/y/hp/level/exp/inv1~6) ────────────────────
static void db_save_player(SQLHDBC hdbc, const char* login_id,
                            short x, short y, short hp, int level, int exp,
                            const int inventory[ITEM_SLOT_COUNT])
{
    SQLHSTMT hstmt;
    if (!sql_ok(SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt))) return;

    SQLWCHAR query[300];
    swprintf_s(query,
        L"EXEC dbo.sp_SavePlayer @login_id=N'%S',"
        L"@x=%d,@y=%d,@hp=%d,@level=%d,@exp=%d,"
        L"@inv1=%d,@inv2=%d,@inv3=%d,@inv4=%d,@inv5=%d,@inv6=%d",
        login_id, x, y, hp, level, exp,
        inventory[0], inventory[1], inventory[2],
        inventory[3], inventory[4], inventory[5]);

    if (!sql_ok(SQLExecDirectW(hstmt, query, SQL_NTS)))
        db_error(SQL_HANDLE_STMT, hstmt);

    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
}

// ── 퀘스트 저장 (sp_SavePlayerExtra) ────────────────────────────────
static void db_save_quests(SQLHDBC hdbc, const char* login_id,
                            const int quest_kill[QUEST_COUNT])
{
    SQLHSTMT hstmt;
    if (!sql_ok(SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt))) return;

    SQLWCHAR query[150];
    swprintf_s(query,
        L"EXEC dbo.sp_SavePlayerExtra @login_id=N'%S',@q0_kill=%d,@q1_kill=%d",
        login_id, quest_kill[0], quest_kill[1]);

    if (!sql_ok(SQLExecDirectW(hstmt, query, SQL_NTS)))
        db_error(SQL_HANDLE_STMT, hstmt);

    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
}

// ── 퍼블릭 API ───────────────────────────────────────────────────────
void db_push_save(int session_id, const char* login_id,
                  short x, short y, short hp, int level, int exp,
                  const int inventory[ITEM_SLOT_COUNT],
                  const int quest_kill[QUEST_COUNT])
{
    if (strncmp(login_id, "bot_", 4) == 0) return;
    DB_EVENT ev;
    ev.type       = DB_SAVE;
    ev.session_id = session_id;
    strncpy_s(ev.login_id, login_id, MAX_NAME_LEN - 1);
    ev.x = x; ev.y = y; ev.hp = hp; ev.level = level; ev.exp = exp;
    for (int i = 0; i < ITEM_SLOT_COUNT; ++i) ev.inventory[i]  = inventory[i];
    for (int i = 0; i < QUEST_COUNT;     ++i) ev.quest_kill[i] = quest_kill[i];
    db_queue.push(ev);
}

void db_thread()
{
    SQLHENV henv = SQL_NULL_HENV;
    SQLHDBC hdbc = SQL_NULL_HDBC;
    bool connected = connect_db(henv, hdbc);

    for (;;) {
        DB_EVENT ev;
        if (!db_queue.try_pop(ev)) {
            this_thread::sleep_for(milliseconds(1));
            continue;
        }

        if (ev.type == DB_LOGIN) {
            EXP_OVER* db_over = new EXP_OVER(IO_DB_LOGIN);
            if (connected) {
                db_login_or_create(hdbc, ev.login_id, db_over->m_db_result);
                if (db_over->m_db_result.success)
                    db_load_quests(hdbc, ev.login_id, db_over->m_db_result);
            }
            PostQueuedCompletionStatus(g_iocp, 1,
                                       (ULONG_PTR)ev.session_id, &db_over->m_over);
        }
        else if (ev.type == DB_SAVE && connected) {
            db_save_player(hdbc, ev.login_id, ev.x, ev.y, ev.hp, ev.level, ev.exp,
                           ev.inventory);
            db_save_quests(hdbc, ev.login_id, ev.quest_kill);
        }
    }
}
