/*
** winsqlite3.h - Declarations for the Windows system SQLite3 DLL (winsqlite3.dll)
**
** winsqlite3.dll ships with Windows 10 and exposes the standard SQLite3 C API.
** This header provides the necessary declarations to compile against it.
** The actual implementation is in %SystemRoot%\System32\winsqlite3.dll.
**
** SQLite is in the public domain. See https://www.sqlite.org/
*/
#ifndef WINSQLITE3_H
#define WINSQLITE3_H

#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque database and statement handles */
typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;
typedef struct sqlite3_context sqlite3_context;
typedef struct sqlite3_value sqlite3_value;
typedef long long int sqlite3_int64;
typedef unsigned long long int sqlite3_uint64;

/* Callback types */
typedef int (*sqlite3_callback)(void*, int, char**, char**);
typedef void (*sqlite3_destructor_type)(void*);

/* Special destructor values */
#define SQLITE_STATIC      ((sqlite3_destructor_type)0)
#define SQLITE_TRANSIENT   ((sqlite3_destructor_type)-1)

/* Result codes */
#define SQLITE_OK           0
#define SQLITE_ERROR        1
#define SQLITE_INTERNAL     2
#define SQLITE_PERM         3
#define SQLITE_ABORT        4
#define SQLITE_BUSY         5
#define SQLITE_LOCKED       6
#define SQLITE_NOMEM        7
#define SQLITE_READONLY     8
#define SQLITE_INTERRUPT    9
#define SQLITE_IOERR       10
#define SQLITE_CORRUPT     11
#define SQLITE_NOTFOUND    12
#define SQLITE_FULL        13
#define SQLITE_CANTOPEN    14
#define SQLITE_PROTOCOL    15
#define SQLITE_EMPTY       16
#define SQLITE_SCHEMA      17
#define SQLITE_TOOBIG      18
#define SQLITE_CONSTRAINT  19
#define SQLITE_MISMATCH    20
#define SQLITE_MISUSE      21
#define SQLITE_NOLFS       22
#define SQLITE_AUTH        23
#define SQLITE_FORMAT      24
#define SQLITE_RANGE       25
#define SQLITE_NOTADB      26
#define SQLITE_NOTICE      27
#define SQLITE_WARNING     28
#define SQLITE_ROW        100
#define SQLITE_DONE       101

/* Open flags */
#define SQLITE_OPEN_READONLY         0x00000001
#define SQLITE_OPEN_READWRITE        0x00000002
#define SQLITE_OPEN_CREATE           0x00000004
#define SQLITE_OPEN_DELETEONCLOSE    0x00000008
#define SQLITE_OPEN_EXCLUSIVE        0x00000010
#define SQLITE_OPEN_AUTOPROXY        0x00000020
#define SQLITE_OPEN_URI              0x00000040
#define SQLITE_OPEN_MEMORY           0x00000080
#define SQLITE_OPEN_MAIN_DB          0x00000100
#define SQLITE_OPEN_TEMP_DB          0x00000200
#define SQLITE_OPEN_TRANSIENT_DB     0x00000400
#define SQLITE_OPEN_MAIN_JOURNAL     0x00000800
#define SQLITE_OPEN_TEMP_JOURNAL     0x00001000
#define SQLITE_OPEN_SUBJOURNAL       0x00002000
#define SQLITE_OPEN_MASTER_JOURNAL   0x00004000
#define SQLITE_OPEN_NOMUTEX          0x00008000
#define SQLITE_OPEN_FULLMUTEX        0x00010000
#define SQLITE_OPEN_SHAREDCACHE      0x00020000
#define SQLITE_OPEN_PRIVATECACHE     0x00040000
#define SQLITE_OPEN_WAL              0x00080000
#define SQLITE_OPEN_NOFOLLOW         0x01000000

/* Column types */
#define SQLITE_INTEGER  1
#define SQLITE_FLOAT    2
#define SQLITE_BLOB     4
#define SQLITE_NULL     5
#ifdef SQLITE_TEXT
# undef SQLITE_TEXT
#endif
#define SQLITE_TEXT     3
#define SQLITE3_TEXT    3

/* Database connection */
int  sqlite3_open(const char *filename, sqlite3 **ppDb);
int  sqlite3_open_v2(const char *filename, sqlite3 **ppDb, int flags, const char *zVfs);
int  sqlite3_close(sqlite3 *db);
int  sqlite3_close_v2(sqlite3 *db);

/* Error info */
const char *sqlite3_errmsg(sqlite3 *db);
int         sqlite3_errcode(sqlite3 *db);

/* Execute SQL */
int sqlite3_exec(sqlite3 *db, const char *sql, sqlite3_callback callback, void *arg, char **errmsg);
void sqlite3_free(void *ptr);

/* Prepared statements */
int sqlite3_prepare(sqlite3 *db, const char *zSql, int nByte, sqlite3_stmt **ppStmt, const char **pzTail);
int sqlite3_prepare_v2(sqlite3 *db, const char *zSql, int nByte, sqlite3_stmt **ppStmt, const char **pzTail);
int sqlite3_step(sqlite3_stmt *pStmt);
int sqlite3_reset(sqlite3_stmt *pStmt);
int sqlite3_finalize(sqlite3_stmt *pStmt);
int sqlite3_clear_bindings(sqlite3_stmt *pStmt);

/* Binding */
int sqlite3_bind_blob(sqlite3_stmt *pStmt, int i, const void *zData, int nData, sqlite3_destructor_type xDel);
int sqlite3_bind_double(sqlite3_stmt *pStmt, int i, double rValue);
int sqlite3_bind_int(sqlite3_stmt *pStmt, int i, int iValue);
int sqlite3_bind_int64(sqlite3_stmt *pStmt, int i, sqlite3_int64 iValue);
int sqlite3_bind_null(sqlite3_stmt *pStmt, int i);
int sqlite3_bind_text(sqlite3_stmt *pStmt, int i, const char *zData, int nData, sqlite3_destructor_type xDel);
int sqlite3_bind_value(sqlite3_stmt *pStmt, int i, const sqlite3_value *pVal);
int sqlite3_bind_zeroblob(sqlite3_stmt *pStmt, int i, int n);

/* Column results */
const void *sqlite3_column_blob(sqlite3_stmt *pStmt, int iCol);
int         sqlite3_column_bytes(sqlite3_stmt *pStmt, int iCol);
double      sqlite3_column_double(sqlite3_stmt *pStmt, int iCol);
int         sqlite3_column_int(sqlite3_stmt *pStmt, int iCol);
sqlite3_int64 sqlite3_column_int64(sqlite3_stmt *pStmt, int iCol);
const unsigned char *sqlite3_column_text(sqlite3_stmt *pStmt, int iCol);
int         sqlite3_column_type(sqlite3_stmt *pStmt, int iCol);
int         sqlite3_column_count(sqlite3_stmt *pStmt);

/* Misc */
int         sqlite3_changes(sqlite3 *db);
sqlite3_int64 sqlite3_last_insert_rowid(sqlite3 *db);
int         sqlite3_bind_parameter_count(sqlite3_stmt *pStmt);
int         sqlite3_bind_parameter_index(sqlite3_stmt *pStmt, const char *zName);
const char *sqlite3_bind_parameter_name(sqlite3_stmt *pStmt, int i);
const char *sqlite3_libversion(void);
int         sqlite3_libversion_number(void);

#ifdef __cplusplus
}
#endif

#endif /* WINSQLITE3_H */
