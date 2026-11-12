/* store_int.h - internals shared by store.c and store_sim.c.  Not part of the API. */
#ifndef STORE_INT_H
#define STORE_INT_H

#include <windows.h>

#include "store.h"
#include "sqlite3.h"

#define STORE_NSTMT 16

struct StoreSim;

struct StoreConn {
    Store *s;
    sqlite3 *db;
    sqlite3_stmt *st[STORE_NSTMT];
    bool isMain;
};

struct Store {
    char uri[160];
    StoreConn *main;
    SRWLOCK lock;                    /* readers: queries; writer: save */
    StoreInfo info;
    CRITICAL_SECTION simCs;          /* guards `sim` */
    struct StoreSim *sim;            /* the similarity cache (store_sim.c) or NULL */
    volatile LONG gen, savedGen;     /* the change counter, and its value when the file was loaded or last written */
};

double store_now(void);
sqlite3_stmt *store_stmt(StoreConn *c, int id, const char *sql);
void store_sim_free(Store *s);                        /* store_close */
void store_sim_font_added(Store *s, const char *pxfmHash);   /* after a save: add the font to the cache, if it exists */

void store_sim_fonts_removed(Store *s, const uint8_t (*hashes)[16], int n);   /* after a deletion */
#endif
