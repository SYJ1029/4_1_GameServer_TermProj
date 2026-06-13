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

static bool db_login_or_create(SQLHDBC hdbc, const char* login_id, DB_RESULT& out)
{
    SQLHSTMT hstmt;
    if (!sql_ok(SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt))) return false;

    SQLWCHAR query[128];
    swprintf_s(query, L"EXEC dbo.sp_LoginOrCreate @login_id = N'%S'", login_id);
    if (!sql_ok(SQLExecDirectW(hstmt, query, SQL_NTS))) {
        db_error(SQL_HANDLE_STMT, hstmt);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return false;
    }

    SQLSMALLINT x, y, hp;
    SQLINTEGER  level, exp;
    SQLLEN cbx, cby, cbhp, cblevel, cbexp;
    SQLBindCol(hstmt, 1, SQL_C_SSHORT, &x,     0, &cbx);
    SQLBindCol(hstmt, 2, SQL_C_SSHORT, &y,     0, &cby);
    SQLBindCol(hstmt, 3, SQL_C_SSHORT, &hp,    0, &cbhp);
    SQLBindCol(hstmt, 4, SQL_C_LONG,   &level, 0, &cblevel);
    SQLBindCol(hstmt, 5, SQL_C_LONG,   &exp,   0, &cbexp);

    if (sql_ok(SQLFetch(hstmt))) {
        out.success = true;
        out.x = x; out.y = y; out.hp = hp;
        out.level = level; out.exp = exp;
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return out.success;
}

static void db_save_player(SQLHDBC hdbc, const char* login_id,
                            short x, short y, short hp, int level, int exp)
{
    SQLHSTMT hstmt;
    if (!sql_ok(SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt))) return;

    SQLWCHAR query[200];
    swprintf_s(query,
        L"EXEC dbo.sp_SavePlayer @login_id=N'%S',@x=%d,@y=%d,@hp=%d,@level=%d,@exp=%d",
        login_id, x, y, hp, level, exp);

    if (!sql_ok(SQLExecDirectW(hstmt, query, SQL_NTS)))
        db_error(SQL_HANDLE_STMT, hstmt);

    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
}

void db_push_save(int session_id, const char* login_id,
                  short x, short y, short hp, int level, int exp)
{
    if (strncmp(login_id, "bot_", 4) == 0) return;
    DB_EVENT ev;
    ev.type       = DB_SAVE;
    ev.session_id = session_id;
    strncpy_s(ev.login_id, login_id, MAX_NAME_LEN - 1);
    ev.x = x; ev.y = y; ev.hp = hp; ev.level = level; ev.exp = exp;
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
            if (connected)
                db_login_or_create(hdbc, ev.login_id, db_over->m_db_result);
            PostQueuedCompletionStatus(g_iocp, 1,
                                       (ULONG_PTR)ev.session_id, &db_over->m_over);
        }
        else if (ev.type == DB_SAVE && connected) {
            db_save_player(hdbc, ev.login_id, ev.x, ev.y, ev.hp, ev.level, ev.exp);
        }
    }
}
