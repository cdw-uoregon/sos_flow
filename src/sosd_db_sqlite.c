/*
 *  sosd_db_sqlite.c
 *
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sqlite3.h"

#include "sos.h"
#include "sos_debug.h"
#include "sos_types.h"
#include "sos_target.h"
#include "sosd_db_sqlite.h"
#include "sosa.h"

#define SOSD_DB_PUBS_TABLE_NAME "tblPubs"
#define SOSD_DB_DATA_TABLE_NAME "tblData"
#define SOSD_DB_VALS_TABLE_NAME "tblVals"
#define SOSD_DB_ENUM_TABLE_NAME "tblEnums"
#define SOSD_DB_SOSD_TABLE_NAME "tblSOSDConfig"

#define SOSD_DB_VIEW_COMBINED_NAME  "viewCombined"

void SOSD_db_insert_enum(const char *type, const char **var_strings, int max_index);

sqlite3      *database;
sqlite3_stmt *stmt_insert_pub;
sqlite3_stmt *stmt_insert_data;
sqlite3_stmt *stmt_insert_val;
sqlite3_stmt *stmt_insert_enum;
sqlite3_stmt *stmt_insert_sosd;
sqlite3_stmt *stmt_update_pub_frame;
sqlite3_stmt *stmt_update_data_frame;

#if (SOS_CONFIG_DB_ENUM_STRINGS > 0)
    #define __ENUM_DB_TYPE " STRING "
    #define __ENUM_C_TYPE const char*
    #define __ENUM_VAL(__source, __enum_handle) \
        SOS_ENUM_STR( __source, __enum_handle )
    #define __BIND_ENUM(__statement, __position, __variable) \
        CALL_SQLITE( bind_text(__statement, __position, __variable, 1 + strlen(__variable), SQLITE_STATIC))
#else
    #define __ENUM_DB_TYPE " INTEGER "
    #define __ENUM_C_TYPE int
    #define __ENUM_VAL(__source, __enum_handle) \
        __source
    #define __BIND_ENUM(__statement, __position, __variable) \
        CALL_SQLITE(bind_int(__statement, __position, __variable))
#endif

char *sql_create_table_pubs = ""                                        \
    "CREATE TABLE IF NOT EXISTS " SOSD_DB_PUBS_TABLE_NAME " ( "         \
    " row_id "          " INTEGER PRIMARY KEY, "                        \
    " guid "            " UNSIGNED BIG INT, "                           \
    " title "           " TEXT, "                                       \
    " process_id "      " INTEGER, "                                    \
    " thread_id "       " INTEGER, "                                    \
    " comm_rank "       " INTEGER, "                                    \
    " node_id "         " TEXT, "                                       \
    " prog_name "       " TEXT, "                                       \
    " prog_ver "        " TEXT, "                                       \
    " meta_channel "    " INTEGER, "                                    \
    " meta_nature "     __ENUM_DB_TYPE ", "                             \
    " meta_layer "      __ENUM_DB_TYPE ", "                             \
    " meta_pri_hint "   __ENUM_DB_TYPE ", "                             \
    " meta_scope_hint " __ENUM_DB_TYPE ", "                             \
    " meta_retain_hint "__ENUM_DB_TYPE ", "                             \
    " pragma "          " TEXT, "                                       \
    " latest_frame "    " INTEGER); ";

char *sql_create_table_data = ""                                        \
    "CREATE TABLE IF NOT EXISTS " SOSD_DB_DATA_TABLE_NAME " ( "         \
    " row_id "          " INTEGER PRIMARY KEY, "                        \
    " pub_guid "        " UNSIGNED BIG INT, "                           \
    " guid "            " UNSIGNED BIG INT, "                           \
    " name "            " TEXT, "                                       \
    " val_type "        __ENUM_DB_TYPE ", "                             \
    " meta_freq "       __ENUM_DB_TYPE ", "                             \
    " meta_class "      __ENUM_DB_TYPE ", "                             \
    " meta_pattern "    __ENUM_DB_TYPE ", "                             \
    " meta_compare "    __ENUM_DB_TYPE ", "                             \
    " latest_frame "    " INTEGER);";

char *sql_create_table_vals = ""                                        \
    "CREATE TABLE IF NOT EXISTS " SOSD_DB_VALS_TABLE_NAME " ( "         \
    " guid "            " UNSIGNED BIG INT, "                           \
    " val "             " TEXT, "                                       \
    " frame "           " INTEGER, "                                    \
    " meta_semantic "          __ENUM_DB_TYPE ", "                      \
    " meta_relation_id "" UNSIGNED BIG INT, "                           \
    " time_pack "       " DOUBLE, "                                     \
    " time_send "       " DOUBLE, "                                     \
    " time_recv "       " DOUBLE, "                                     \
    " time_pack_int "   " UNSIGNED BIG INT); ";


char *sql_create_table_enum = ""                                        \
    "CREATE TABLE IF NOT EXISTS " SOSD_DB_ENUM_TABLE_NAME " ( "         \
    " row_id "          " INTEGER PRIMARY KEY, "                        \
    " type "            " TEXT, "                                       \
    " text "            " TEXT, "                                       \
    " enum_val "        " INTEGER); ";

char *sql_create_table_sosd_config = ""                                 \
    "CREATE TABLE IF NOT EXISTS " SOSD_DB_SOSD_TABLE_NAME " ( "         \
    " row_id "          " INTEGER PRIMARY KEY, "                        \
    " daemon_id "       " TEXT, "                                       \
    " key "             " TEXT, "                                       \
    " value "           " TEXT); ";


char *sql_create_view_combined = ""                                     \
    "CREATE VIEW IF NOT EXISTS " SOSD_DB_VIEW_COMBINED_NAME " AS "      \
    "   SELECT "                                                        \
    "       tblPubs.process_id  AS process_id, "                        \
    "       tblPubs.node_id     AS node_id, "                           \
    "       tblPubs.title       AS pub_title, "                             \
    "       tblPubs.guid        AS pub_guid, "                              \
    "       tblPubs.comm_rank   AS comm_rank, "                             \
    "       tblPubs.prog_name   AS prog_name, "                             \
    "       tblVals.time_pack   AS time_pack, "                             \
    "       tblVals.time_recv   AS time_recv, "                             \
    "       tblVals.frame       AS frame, "                                 \
    "       tblData.name        AS value_name, "                            \
    "       tblData.guid        AS value_guid, "                            \
    "       tblData.val_type    AS value_type, "                            \
    "       tblVals.val         AS value "                                  \
    "   FROM "                                                              \
    "       tblPubs "                                                       \
    "       LEFT OUTER JOIN tblData ON tblPubs.guid   = tblData.pub_guid "  \
    "       LEFT OUTER JOIN tblVals ON tblData.guid   = tblVals.guid "      \
    "   ; "                                                                 \
    "";


char *sql_create_index_tblvals = "CREATE INDEX tblVals_GUID ON tblVals(guid,frame);";
char *sql_create_index_tbldata = "CREATE INDEX tblData_GUID ON tblData(pub_guid,guid,name);";
char *sql_create_index_tblpubs = "CREATE INDEX tblPubs_GUID ON tblPubs(prog_name,comm_rank);";

char *sql_create_index_tbldata_frame = "CREATE INDEX tblData_FRAME ON tblData(guid,latest_frame);";
char *sql_create_index_tblpubs_frame = "CREATE INDEX tblPubs_FRAME ON tblPubs(guid,latest_frame);";


const char *sql_insert_pub = ""                                         \
    "INSERT INTO " SOSD_DB_PUBS_TABLE_NAME " ("                         \
    " guid,"                                                            \
    " title,"                                                           \
    " process_id,"                                                      \
    " thread_id,"                                                       \
    " comm_rank,"                                                       \
    " node_id,"                                                         \
    " prog_name,"                                                       \
    " prog_ver,"                                                        \
    " meta_channel,"                                                    \
    " meta_nature,"                                                     \
    " meta_layer,"                                                      \
    " meta_pri_hint,"                                                   \
    " meta_scope_hint,"                                                 \
    " meta_retain_hint,"                                                \
    " pragma, "                                                         \
    " latest_frame "                                                    \
    ") VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?); ";

const char *sql_insert_data = ""                                        \
    "INSERT INTO " SOSD_DB_DATA_TABLE_NAME " ("                         \
    " pub_guid,"                                                        \
    " guid,"                                                            \
    " name,"                                                            \
    " val_type,"                                                        \
    " meta_freq,"                                                       \
    " meta_class,"                                                      \
    " meta_pattern,"                                                    \
    " meta_compare, "                                                   \
    " latest_frame "                                                    \
    ") VALUES (?,?,?,?,?,?,?,?,?); ";

const char *sql_insert_val = ""                                         \
    "INSERT INTO " SOSD_DB_VALS_TABLE_NAME " ("                         \
    " guid,"                                                            \
    " val, "                                                            \
    " frame, "                                                          \
    " meta_semantic, "                                                  \
    " meta_relation_id, "                                               \
    " time_pack,"                                                       \
    " time_send,"                                                       \
    " time_recv, "                                                      \
    " time_pack_int "                                                   \
    ") VALUES (?,?,?,?,?,?,?,?,?); ";

const char *sql_update_pub_frame = ""                                   \
    "UPDATE " SOSD_DB_PUBS_TABLE_NAME " "                               \
    " SET latest_frame = ? "                                            \
    " WHERE guid = ? ; ";

const char *sql_update_data_frame = ""                                  \
    "UPDATE " SOSD_DB_DATA_TABLE_NAME " "                               \
    " SET latest_frame = ? "                                            \
    " WHERE guid = ? ; ";



const char *sql_insert_enum = ""                \
    "INSERT INTO " SOSD_DB_ENUM_TABLE_NAME " (" \
    " type,"                                    \
    " text,"                                    \
    " enum_val "                                \
    ") VALUES (?,?,?); ";

const char *sql_insert_sosd_config = ""         \
    "INSERT INTO " SOSD_DB_SOSD_TABLE_NAME " (" \
    " daemon_id, "                              \
    " key, "                                    \
    " value "                                   \
    ") VALUES (?,?,?); ";

char *sql_cmd_begin_transaction    = "BEGIN DEFERRED TRANSACTION;";
char *sql_cmd_commit_transaction   = "COMMIT TRANSACTION;";


void SOSD_db_init_database() {
    SOS_SET_CONTEXT(SOSD.sos_context, "SOSD_db_init_database");
    int     retval;
    int     flags;

    if (SOS->config.options->db_disabled) {
        return;
    }

    dlog(1, "Opening database...\n");

    SOSD.db.ready = 0;
    SOSD.db.file  = (char *) malloc(SOS_DEFAULT_STRING_LEN);
    memset(SOSD.db.file, '\0', SOS_DEFAULT_STRING_LEN);

    SOSD.db.lock  = (pthread_mutex_t *) malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init( SOSD.db.lock, NULL );
    pthread_mutex_lock( SOSD.db.lock );

    if (SOS->role == SOS_ROLE_LISTENER) { 
        SOSD.db.frame_note_pub_table = qhashtbl(128);
        SOSD.db.frame_note_val_table = qhashtbl(4096);
    } else {
        //Aggregators get bigger hashtables.
        SOSD.db.frame_note_pub_table = qhashtbl(1280);
        SOSD.db.frame_note_val_table = qhashtbl(409600);
    }
    SOSD.db.frame_note_pub_list_head = NULL;
    SOSD.db.frame_note_val_list_head = NULL;

    flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;

    #if (SOSD_CLOUD_SYNC > 0)
    snprintf(SOSD.db.file, SOS_DEFAULT_STRING_LEN, "%s/%s.%05d.db", SOSD.daemon.work_dir, SOSD.daemon.name, SOS->config.comm_rank);
    #else
    snprintf(SOSD.db.file, SOS_DEFAULT_STRING_LEN, "%s/%s.local.db", SOSD.daemon.work_dir, SOSD.daemon.name);
    #endif

    // Does the user want an in memory database?
    if (SOS->config.options->db_in_memory_only) {
        snprintf(SOSD.db.file, SOS_DEFAULT_STRING_LEN, ":memory:");
    }

    if (SOS_file_exists(SOSD.db.file)) {
        fprintf(stderr, "NOTICE: Attaching to existing database file: %s\n",
                SOSD.db.file);
    }

    /*
     *   "unix-none"     =no locking (NOP's)
     *   "unix-excl"     =single-access only
     *   "unix-dotfile"  =uses a file as the lock.
     */
    retval = sqlite3_open_v2(SOSD.db.file, &database, flags, "unix-none");
    if( retval ){
        dlog(0, "ERROR!  Can't open database: %s   (%s)\n", SOSD.db.file, sqlite3_errmsg(database));
        sqlite3_close(database);
        exit(EXIT_FAILURE);
    } else {
        dlog(1, "Successfully opened database.\n");
    }

    sqlite3_exec(database, "PRAGMA synchronous   = OFF;",      NULL, NULL, NULL); // OFF = Let the OS handle flushes.
    sqlite3_exec(database, "PRAGMA cache_size    = 31250;",    NULL, NULL, NULL); // x 2048 def. page size = 64MB cache
    sqlite3_exec(database, "PRAGMA cache_spill   = FALSE;",    NULL, NULL, NULL); // Spilling goes exclusive, it's wasteful.
    sqlite3_exec(database, "PRAGMA temp_store    = MEMORY;",   NULL, NULL, NULL); // If we crash, we crash.
  //sqlite3_exec(database, "PRAGMA journal_mode  = DELETE;",   NULL, NULL, NULL); // Default
  //sqlite3_exec(database, "PRAGMA encoding      = UTF-8;",    NULL, NULL, NULL); // Trim bloat if possible, unicode is rare.
    sqlite3_exec(database, "PRAGMA journal_mode  = OFF;",   NULL, NULL, NULL); // ...ditto.  Speed prevents crashes.
  //sqlite3_exec(database, "PRAGMA journal_mode  = WAL;",      NULL, NULL, NULL); // This is the fastest file-based journal option.

    SOS_pipe_init(SOS, &SOSD.db.snap_queue, sizeof(SOS_val_snap *));

    if (SOSD.db.snap_queue->elem_count == 0) {
      SOSD.db.snap_queue->sync_pending = 0;
    }

    SOSD_db_create_tables();

    sqlite3_exec(database, sql_create_index_tblvals, NULL, NULL, NULL);
    sqlite3_exec(database, sql_create_index_tbldata, NULL, NULL, NULL);
    sqlite3_exec(database, sql_create_index_tblpubs, NULL, NULL, NULL);

    sqlite3_exec(database, sql_create_index_tbldata_frame, NULL, NULL, NULL);
    sqlite3_exec(database, sql_create_index_tblpubs_frame, NULL, NULL, NULL);


    dlog(2, "Preparing transactions...\n");

    dlog(2, "  --> \"%.50s...\"\n", sql_insert_pub);
    retval = sqlite3_prepare_v2(database, sql_insert_pub,
            strlen(sql_insert_pub) + 1, &stmt_insert_pub, NULL);
    if (retval) { dlog(2, "  ... error (%d) was returned.\n", retval); }

    dlog(2, "  --> \"%.50s...\"\n", sql_insert_data);
    retval = sqlite3_prepare_v2(database, sql_insert_data,
            strlen(sql_insert_data) + 1, &stmt_insert_data, NULL);
    if (retval) { dlog(2, "  ... error (%d) was returned.\n", retval); }

    dlog(2, "  --> \"%.50s...\"\n", sql_insert_val);
    retval = sqlite3_prepare_v2(database, sql_insert_val,
            strlen(sql_insert_val) + 1, &stmt_insert_val, NULL);
    if (retval) { dlog(2, "  ... error (%d) was returned.\n", retval); }

    dlog(2, "  --> \"%.50s...\"\n", sql_insert_enum);
    retval = sqlite3_prepare_v2(database, sql_insert_enum,
            strlen(sql_insert_enum) + 1, &stmt_insert_enum, NULL);
    if (retval) { dlog(2, "  ... error (%d) was returned.\n", retval); }

    dlog(2, "  --> \"%.50s...\"\n", sql_insert_sosd_config);
    retval = sqlite3_prepare_v2(database, sql_insert_sosd_config,
            strlen(sql_insert_sosd_config) + 1, &stmt_insert_sosd, NULL);
    if (retval) { dlog(2, "  ... error (%d) was returned.\n", retval); }

    dlog(2, "  --> \"%.50s...\"\n", sql_update_pub_frame);
    retval = sqlite3_prepare_v2(database, sql_update_pub_frame,
            strlen(sql_update_pub_frame) + 1, &stmt_update_pub_frame, NULL);
    if (retval) { dlog(2, "  ... error (%d) was returned.\n", retval); }

    dlog(2, "  --> \"%.50s...\"\n", sql_update_data_frame);
    retval = sqlite3_prepare_v2(database, sql_update_data_frame,
            strlen(sql_update_data_frame) + 1, &stmt_update_data_frame, NULL);
    if (retval) { dlog(2, "  ... error (%d) was returned.\n", retval); }

    SOSD.db.ready = 1;
    pthread_mutex_unlock(SOSD.db.lock);

    SOSD_db_transaction_begin();
    dlog(2, "  Inserting the enumeration table...\n");

    SOSD_db_insert_enum("ROLE",          SOS_ROLE_str,          SOS_ROLE___MAX          );
    SOSD_db_insert_enum("STATUS",        SOS_STATUS_str,        SOS_STATUS___MAX        );
    SOSD_db_insert_enum("MSG_TYPE",      SOS_MSG_TYPE_str,      SOS_MSG_TYPE___MAX      );
    SOSD_db_insert_enum("FEEDBACK",      SOS_FEEDBACK_TYPE_str, SOS_FEEDBACK_TYPE___MAX );
    SOSD_db_insert_enum("PRI",           SOS_PRI_str,           SOS_PRI___MAX           );
    SOSD_db_insert_enum("VAL_TYPE",      SOS_VAL_TYPE_str,      SOS_VAL_TYPE___MAX      );
    SOSD_db_insert_enum("VAL_STATE",     SOS_VAL_STATE_str,     SOS_VAL_STATE___MAX     );
    SOSD_db_insert_enum("VAL_SYNC",      SOS_VAL_SYNC_str,      SOS_VAL_SYNC___MAX      );
    SOSD_db_insert_enum("VAL_FREQ",      SOS_VAL_FREQ_str,      SOS_VAL_FREQ___MAX      );
    SOSD_db_insert_enum("VAL_SEMANTIC",  SOS_VAL_SEMANTIC_str,  SOS_VAL_SEMANTIC___MAX  );
    SOSD_db_insert_enum("VAL_PATTERN",   SOS_VAL_PATTERN_str,   SOS_VAL_PATTERN___MAX   );
    SOSD_db_insert_enum("VAL_COMPARE",   SOS_VAL_COMPARE_str,   SOS_VAL_COMPARE___MAX   );
    SOSD_db_insert_enum("VAL_CLASS",     SOS_VAL_CLASS_str,     SOS_VAL_CLASS___MAX     );
    SOSD_db_insert_enum("SCOPE",         SOS_SCOPE_str,         SOS_SCOPE___MAX         );
    SOSD_db_insert_enum("LAYER",         SOS_LAYER_str,         SOS_LAYER___MAX         );
    SOSD_db_insert_enum("NATURE",        SOS_NATURE_str,        SOS_NATURE___MAX        );
    SOSD_db_insert_enum("RETAIN",        SOS_RETAIN_str,        SOS_RETAIN___MAX        );

    SOSD_db_transaction_commit();

    dlog(2, "  ... done.\n");

    return;
}


void SOSD_db_close_database() {
    SOS_SET_CONTEXT(SOSD.sos_context, "SOSD_db_close_database");

    dlog(2, "Closing database.   (%s)\n", SOSD.db.file);
    //pthread_mutex_lock( SOSD.db.lock );
    dlog(2, "  ... finalizing statements.\n");
    SOSD.db.ready = -1;
    CALL_SQLITE (finalize(stmt_insert_pub));
    CALL_SQLITE (finalize(stmt_insert_data));
    CALL_SQLITE (finalize(stmt_insert_val));
    CALL_SQLITE (finalize(stmt_insert_enum));
    CALL_SQLITE (finalize(stmt_insert_sosd));
    dlog(2, "  ... closing database file.\n");
    sqlite3_close(database);
    dlog(2, "  ... destroying the mutex.\n");
    pthread_mutex_destroy(SOSD.db.lock);
    free(SOSD.db.lock);
    free(SOSD.db.file);
    dlog(2, "  ... done.\n");

    return;
}


int SOSD_db_export_to_file(char *dest_filename) {
    SOS_SET_CONTEXT(SOSD.sos_context, "SOSD_db_export_to_file");

    sqlite3        *dest_db;     // Database connection opened on zFilename 
    sqlite3_backup *backup_obj;  // Backup object used to copy data 
   
    bool verbose = SOS->config.options->db_export_verbose;

    int rc;                      
    rc = sqlite3_open(dest_filename, &dest_db); 

    if (rc == SQLITE_OK) {
        backup_obj = sqlite3_backup_init(dest_db, "main", database, "main");
        if (backup_obj && verbose) {
            dlog(1, "Exporting w/VERBOSE option enabled.\n");
            // Report progress during export.
            int remaining = 0;
            int pagecount = 0;
            float pct_done  = 0.0;

            double time_start;
            double time_now;

            SOS_TIME(time_start);

            do {
				// dump 64k worth at a time
                rc = sqlite3_backup_step(backup_obj, 65536);

                remaining = sqlite3_backup_remaining(backup_obj);
                pagecount = sqlite3_backup_pagecount(backup_obj);
                if ((pagecount != remaining) && (pagecount != 0)) {
                    pct_done  = 100.0 * (float) (((float) pagecount - (float)remaining) / (float)pagecount);
                } else {
                    pct_done  = 0.0;
                }

                SOS_TIME(time_now);

                printf("%s --> Exported %3.1f%% in %.2lf seconds.  (%d of %d pages)\n",
                        dest_filename, pct_done, (time_now - time_start),
                        (pagecount - remaining), pagecount);
                fflush(stdout);

            } while (rc == SQLITE_OK || rc == SQLITE_BUSY || rc == SQLITE_LOCKED);

            (void)sqlite3_backup_finish(backup_obj);

        } else if (backup_obj) {
            // Export the entire database in one silent operation.
            dlog(1, "Exporting in ONE operation, silently.\n");
            (void)sqlite3_backup_step(backup_obj, -1);
            (void)sqlite3_backup_finish(backup_obj);
        }
        rc = sqlite3_errcode(dest_db);
    }
    (void)sqlite3_close(dest_db);

    return rc;
}



void SOSD_db_transaction_begin() {
    SOS_SET_CONTEXT(SOSD.sos_context, "SOSD_db_transaction_begin");
    int   rc;
    char *err = NULL;

    if (SOSD.db.ready == -1) {
        dlog(0, "ERROR: Attempted to begin a transaction during daemon shutdown.\n");
        return;
    }

    pthread_mutex_lock(SOSD.db.lock);

    dlog(6, " > > > > > > > > > > > > > > > > > > > > > > > > > > > > \n");
    dlog(6, ">>>>>>>>>> >> >> >> BEGIN TRANSACTION >> >> >> >>>>>>>>>>\n");
    dlog(6, " > > > > > > > > > > > > > > > > > > > > > > > > > > > > \n");
    rc = sqlite3_exec(database, sql_cmd_begin_transaction, NULL, NULL, &err);
    if (rc) { dlog(2, "##### ERROR ##### : (%d)\n", rc); }

    return;
}


void SOSD_db_transaction_commit() {
    SOS_SET_CONTEXT(SOSD.sos_context, "SOSD_db_transaction_commit");
    int   rc;
    char *err = NULL;

    dlog(6, " < < < < < < < < < < < < < < < < < < < < < < < < < < < <  \n");
    dlog(6, "<<<<<<<<<< << << << COMMIT TRANSACTION << << << <<<<<<<<<<\n");
    dlog(6, " < < < < < < < < < < < < < < < < < < < < < < < < < < < <  \n");
    rc = sqlite3_exec(database, sql_cmd_commit_transaction, NULL, NULL, &err);
    if (rc) { dlog(2, "##### ERROR ##### : (%d)\n", rc); }

    pthread_mutex_unlock(SOSD.db.lock);

    return;
}



void SOSD_db_handle_sosa_query(SOSD_db_task *task) {
    SOS_SET_CONTEXT(SOSD.sos_context, "SOSD_db_handle_sosa_query");

    SOSD_query_handle *query = (SOSD_query_handle *) task->ref;
    char *sosa_query = query->query_sql;

    dlog(6, "Processing query task...\n");
    dlog(6, "   ...reply_guid: %" SOS_GUID_FMT "\n",
            query->reply_to_guid);
    dlog(6, "   ...reply_host: \"%s\"\n",
            query->reply_host);
    dlog(6, "   ...reply_port: \"%d\"\n",
            query->reply_port);
    dlog(6, "   ...query_sql: \"%s\"\n",
            query->query_sql);
    dlog(6, "   ...query_guid: %" SOS_GUID_FMT "\n",
            query->query_guid);

    bool OK_to_execute = true;

    dlog(4, "Building result set...\n");
    SOSA_results *results = NULL;
    SOSA_results_init(SOS, &results);

    double query_start_time = 0.0;
    double query_stop_time  = 0.0;

    sqlite3_stmt *sosa_statement = NULL;
    int rc = 0;
    char *err = NULL;

    if (sosa_query == NULL) {
        dlog(1, "WARNING: Empty (NULL) query submitted."
                " Doing nothing and returning.\n");
        OK_to_execute = false;
    } else {

        dlog(6, "Flushing the database.  (BEFORE query)\n");
        rc = sqlite3_exec(database, sql_cmd_commit_transaction, NULL, NULL, &err);
        rc = sqlite3_exec(database, sql_cmd_begin_transaction, NULL, NULL, &err);

        SOS_TIME(query_start_time);

        rc = sqlite3_prepare_v2(database, sosa_query,
                strlen(sosa_query) + 1, &sosa_statement, NULL);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "ERROR: Invalid query sent to daemon from %"
                    SOS_GUID_FMT "\n", query->reply_to_guid);
            OK_to_execute = false;
        }
    }

    if (OK_to_execute) {
        int col = 0;
        int col_incoming = sqlite3_column_count( sosa_statement );

        dlog(7, "   ... col_incoming == %d\n", col_incoming);
        results->col_count = col_incoming;

        SOSA_results_grow_to(results, col_incoming, results->row_max);
        for (col = 0; col < col_incoming; col++) {
            dlog(7, "   ... results->col_names[%d] == \"%s\"\n", col,
                    sqlite3_column_name(sosa_statement, col));
            SOSA_results_put_name(results, col, sqlite3_column_name(sosa_statement, col));
        }

        int row_incoming = 0;
        const char *val  = NULL;

        while( sqlite3_step( sosa_statement ) == SQLITE_ROW ) {
            dlog(7, "   ... results->data[%d][ | | | ... | ]\n", row_incoming);
            SOSA_results_grow_to(results, col_incoming, row_incoming);
            for (col = 0; col < col_incoming; col++) {
                val = (const char *) sqlite3_column_text(sosa_statement, col);
                SOSA_results_put(results, col, row_incoming, val);
            }//for:col
            row_incoming++;
            results->row_count = row_incoming;
        }//while:rows

        sqlite3_finalize(sosa_statement);

        SOS_TIME(query_stop_time);
        results->exec_duration = query_stop_time - query_start_time;

        dlog(6, "Flushing the database.  (AFTER query)\n");
        rc = sqlite3_exec(database, sql_cmd_commit_transaction, NULL, NULL, &err);
        rc = sqlite3_exec(database, sql_cmd_begin_transaction, NULL, NULL, &err);
    } //OK_to_execute

    // Even if we didn't execute the query, we need to send back the empty
    // results in case there is a client that is blocking waiting on them.
    SOSA_results_label(results, query->query_guid, query->query_sql);
    query->results = results;

    // Enqueue the results to send back to the client...
    SOSD_feedback_task *feedback = calloc(1, sizeof(SOSD_feedback_task));
    feedback->type = SOS_FEEDBACK_TYPE_QUERY;
    feedback->ref = query;
    pthread_mutex_lock(SOSD.sync.feedback.queue->sync_lock);
    pipe_push(SOSD.sync.feedback.queue->intake, (void *) &feedback, 1);
    SOSD.sync.feedback.queue->elem_count++;
    pthread_mutex_unlock(SOSD.sync.feedback.queue->sync_lock);

    return;
}





void SOSD_db_insert_pub( SOS_pub *pub ) {
    SOS_SET_CONTEXT(SOSD.sos_context, "SOSD_db_insert_pub");
    int i;

    if (pub->announced == 1) {
        dlog(5, "Skipping database insertion, this pub is already marked as announced.\n");

        SOSD_countof(db_insert_announce_nop++);
        return;
    }

    SOSD_countof(db_insert_announce++);

    pthread_mutex_lock( pub->lock );

    dlog(5, "Inserting pub(%" SOS_GUID_FMT ")->data into database(%s).\n",
            pub->guid, SOSD.db.file);

    //NOTE: SQLite3 behaves strangely unless you pass it variables stored on the stack.
    SOS_guid       guid              = pub->guid;
    char          *title             = pub->title;
    int            process_id        = pub->process_id;
    int            thread_id         = pub->thread_id;
    int            comm_rank         = pub->comm_rank;
    char          *node_id           = pub->node_id;
    char          *prog_name         = pub->prog_name;
    char          *prog_ver          = pub->prog_ver;
    int            meta_channel      = pub->meta.channel;
    __ENUM_C_TYPE  meta_nature       = __ENUM_VAL(pub->meta.nature, SOS_NATURE);
    __ENUM_C_TYPE  meta_layer        = __ENUM_VAL(pub->meta.layer, SOS_LAYER);
    __ENUM_C_TYPE  meta_pri_hint     = __ENUM_VAL(pub->meta.pri_hint, SOS_PRI);
    __ENUM_C_TYPE  meta_scope_hint   = __ENUM_VAL(pub->meta.scope_hint, SOS_SCOPE);
    __ENUM_C_TYPE  meta_retain_hint  = __ENUM_VAL(pub->meta.retain_hint, SOS_RETAIN);
    unsigned char *pragma            = pub->pragma_msg;
    int            pragma_len        = pub->pragma_len;
    unsigned char  pragma_empty[2];    memset(pragma_empty, '\0', 2);
    int            latest_frame      = 0;

    dlog(5, "  ... binding values into the statement\n");
    dlog(6, "     ... pragma_len = %d\n", pragma_len);
    dlog(6, "     ... pragma     = \"%s\"\n", pragma);

    CALL_SQLITE (bind_int64  (stmt_insert_pub, 1,  guid         ));
    CALL_SQLITE (bind_text   (stmt_insert_pub, 2,  title, -1 , SQLITE_STATIC  ));
    CALL_SQLITE (bind_int    (stmt_insert_pub, 3,  process_id   ));
    CALL_SQLITE (bind_int    (stmt_insert_pub, 4,  thread_id    ));
    CALL_SQLITE (bind_int    (stmt_insert_pub, 5,  comm_rank    ));
    CALL_SQLITE (bind_text   (stmt_insert_pub, 6,  node_id, -1 , SQLITE_STATIC  ));
    CALL_SQLITE (bind_text   (stmt_insert_pub, 7,  prog_name, -1 , SQLITE_STATIC  ));
    CALL_SQLITE (bind_text   (stmt_insert_pub, 8,  prog_ver, -1 , SQLITE_STATIC  ));
    CALL_SQLITE (bind_int    (stmt_insert_pub, 9,  meta_channel ));
    __BIND_ENUM (stmt_insert_pub, 10, meta_nature  );
    __BIND_ENUM (stmt_insert_pub, 11, meta_layer   );
    __BIND_ENUM (stmt_insert_pub, 12, meta_pri_hint);
    __BIND_ENUM (stmt_insert_pub, 13, meta_scope_hint );
    __BIND_ENUM (stmt_insert_pub, 14, meta_retain_hint );
    if (pragma_len > 0) {
        // Only bind the pragma if there actually is something to insert...
        CALL_SQLITE (bind_text (stmt_insert_pub, 15,
                    (char const *) pragma, pub->pragma_len, SQLITE_STATIC  ));
    } else {
        CALL_SQLITE (bind_text (stmt_insert_pub, 15,
                    (char const *) pragma_empty, 0, SQLITE_STATIC  ));
    }
    CALL_SQLITE (bind_int    (stmt_insert_pub, 16, latest_frame ));

    dlog(5, "  ... executing the query\n");

    // Execute the query.
    CALL_SQLITE_EXPECT (step (stmt_insert_pub), DONE);

    dlog(5, "  ... success!  resetting the statement.\n");

    CALL_SQLITE (reset (stmt_insert_pub));
    CALL_SQLITE (clear_bindings (stmt_insert_pub));

    pthread_mutex_unlock( pub->lock );

    dlog(5, "  ... done.  returning to loop.\n");
    return;
}


//tblData : Data definitions / metadata that comes with a SOS_publish() call.
void SOSD_db_insert_data( SOS_pub *pub ) {
    SOS_SET_CONTEXT(SOSD.sos_context, "SOSD_db_insert_data");
    int i;
    int inserted_count = 0;

    pthread_mutex_lock( pub->lock );
    pub->sync_pending = 0;

    dlog(5, "Inserting pub(%" SOS_GUID_FMT ")->data into database(%s).\n",
            pub->guid, SOSD.db.file);

    inserted_count = 0;
    for (i = 0; i < pub->elem_count; i++) {

        if (pub->data[i]->sync != SOS_VAL_SYNC_RENEW) {
            dlog(6, "Skipping pub->data[%d]->sync == %s\n",
                    i, SOS_ENUM_STR(pub->data[i]->sync, SOS_VAL_SYNC));
            continue;
        } else {
            inserted_count++;
        }


        //NOTE: SQLite3 behaves strangely unless you pass it variables stored on the stack.
        SOS_guid      pub_guid      = pub->guid;
        SOS_guid      guid          = pub->data[i]->guid;
        const char   *name          = pub->data[i]->name;
        int           latest_frame  = 0;
        char         *val;
        __ENUM_C_TYPE val_type      = __ENUM_VAL( pub->data[i]->type, SOS_VAL_TYPE );
        __ENUM_C_TYPE meta_freq     = __ENUM_VAL( pub->data[i]->meta.freq, SOS_VAL_FREQ );
        __ENUM_C_TYPE meta_class    = __ENUM_VAL( pub->data[i]->meta.classifier, SOS_VAL_CLASS );
        __ENUM_C_TYPE meta_pattern  = __ENUM_VAL( pub->data[i]->meta.pattern, SOS_VAL_PATTERN );
        __ENUM_C_TYPE meta_compare  = __ENUM_VAL( pub->data[i]->meta.compare, SOS_VAL_COMPARE );

        char          val_num_as_str[SOS_DEFAULT_STRING_LEN];
        memset( val_num_as_str, '\0', SOS_DEFAULT_STRING_LEN);

        switch (pub->data[i]->type) {
        case SOS_VAL_TYPE_INT:
            val = val_num_as_str;
            snprintf(val, SOS_DEFAULT_STRING_LEN, "%d",  pub->data[i]->val.i_val);
            break;
        case SOS_VAL_TYPE_LONG:
            val = val_num_as_str;
            snprintf(val, SOS_DEFAULT_STRING_LEN, "%ld", pub->data[i]->val.l_val);
            break;
        case SOS_VAL_TYPE_DOUBLE:
            val = val_num_as_str;
            snprintf(val, SOS_DEFAULT_STRING_LEN, "%lf", pub->data[i]->val.d_val);
            break;
        case SOS_VAL_TYPE_STRING:
            val = pub->data[i]->val.c_val;
            break;
        default:
            dlog(5, "ERROR: Attempting to insert an invalid"
                    " data type.  pub[%s]->data[%d]->type == %d  (Skipping...)\n",
                    pub->title, i, pub->data[i]->type);
            continue;
        }

        CALL_SQLITE (bind_int64  (stmt_insert_data, 1,  pub_guid     ));
        CALL_SQLITE (bind_int64  (stmt_insert_data, 2,  guid         ));
        CALL_SQLITE (bind_text   (stmt_insert_data, 3,  name, -1 , SQLITE_STATIC     ));
        __BIND_ENUM (stmt_insert_data, 4,  val_type     );
        __BIND_ENUM (stmt_insert_data, 5,  meta_freq    );
        __BIND_ENUM (stmt_insert_data, 6,  meta_class   );
        __BIND_ENUM (stmt_insert_data, 7,  meta_pattern );
        __BIND_ENUM (stmt_insert_data, 8,  meta_compare );
        
        CALL_SQLITE (bind_int    (stmt_insert_data, 9,  latest_frame));

        dlog(5, "  ... executing insert query   pub->data[%d].(%s)\n", i, pub->data[i]->name);

        CALL_SQLITE_EXPECT (step (stmt_insert_data), DONE);

        dlog(6, "  ... success!  resetting the statement.\n");
        CALL_SQLITE (reset(stmt_insert_data));
        CALL_SQLITE (clear_bindings (stmt_insert_data));

        pub->data[i]->sync = SOS_VAL_SYNC_LOCAL;
    }

    dlog(5, "  ... done.  returning to loop.\n");

    if (inserted_count > 0) {
        SOSD_countof(db_insert_publish++);
    } else {
        SOSD_countof(db_insert_publish_nop++);
    }

    pthread_mutex_unlock( pub->lock );

    return;
}


// NOTE: re_queue can be NULL, and snaps are then free()'ed.
void SOSD_db_insert_vals( SOS_pipe *queue, SOS_pipe *re_queue ) {
    SOS_SET_CONTEXT(SOSD.sos_context, "SOSD_db_insert_vals");
    SOS_val_snap **snap_list;
    int            snap_index;
    int            snap_count;
    int            count;

    qhashtbl_t      *note_table         = NULL;
    SOSD_frame_note *note               = NULL;
    char             note_guid_str[128] = {0};

    bool update_latest_frame_is_enabled = 
        SOS->config.options->db_update_frame;

    dlog(5, "Flushing SOSD.db.snap_queue into database...\n");
    pthread_mutex_lock( queue->sync_lock );
    snap_count = queue->elem_count;
    if (queue->elem_count < 1) {
        dlog(5, "  ... nothing in the queue, returning.\n");
        pthread_mutex_unlock( queue->sync_lock);
        SOSD_countof(db_insert_val_snaps_nop++);
        return;
    }

    SOSD_countof(db_insert_val_snaps++);

    snap_list = (SOS_val_snap **) malloc(snap_count * sizeof(SOS_val_snap *));
    dlog(5, "  ... grabbing %d snaps from the queue.\n", snap_count);
    count = pipe_pop_eager(queue->outlet, (void *) snap_list, snap_count);
    dlog(5, "      %d snaps were returned from the queue on request for %d.\n",
            count, snap_count);
    queue->elem_count -= count;
    snap_count = count;

    if (queue->elem_count == 0) {
      queue->sync_pending = 0;
    }
    dlog(5, "  ... [bbb] releasing queue->lock\n");
    pthread_mutex_unlock(queue->sync_lock);

    dlog(5, "  ... processing snaps extracted from the queue\n");

    int           elem;
    char         *val, *val_alloc;
    SOS_guid      guid;
    SOS_guid      pub_guid;
    double        time_pack;
    double        time_send;
    double        time_recv;
    int           frame;
    __ENUM_C_TYPE semantic;
    SOS_guid      relation_id;
    unsigned long int time_pack_int;

    SOS_val_type  val_type;
    int           val_insert_count = 0;

    val_alloc = (char *) malloc(SOS_DEFAULT_STRING_LEN);

    for (snap_index = 0; snap_index < snap_count ; snap_index++) {

        elem              = snap_list[snap_index]->elem;
        guid              = snap_list[snap_index]->guid;
        pub_guid          = snap_list[snap_index]->pub_guid;
        time_pack         = snap_list[snap_index]->time.pack;
        time_send         = snap_list[snap_index]->time.send;
        time_recv         = snap_list[snap_index]->time.recv;
        frame             = snap_list[snap_index]->frame;
        semantic          = __ENUM_VAL( snap_list[snap_index]->semantic, SOS_VAL_SEMANTIC );
        relation_id       = snap_list[snap_index]->relation_id;
        time_pack_int     = (unsigned long int) time_pack * 1000000;

        val_type = snap_list[snap_index]->type;
        SOS_TIME( time_recv );

        if (val_type != SOS_VAL_TYPE_STRING) {
            val = val_alloc;
            memset(val, '\0', SOS_DEFAULT_STRING_LEN);
        }


        switch (val_type) {
        case SOS_VAL_TYPE_INT:
            snprintf(val, SOS_DEFAULT_STRING_LEN, "%d",  snap_list[snap_index]->val.i_val);
            break;
        case SOS_VAL_TYPE_LONG:
            snprintf(val, SOS_DEFAULT_STRING_LEN, "%ld", snap_list[snap_index]->val.l_val);
            break;
        case SOS_VAL_TYPE_DOUBLE:
            snprintf(val, SOS_DEFAULT_STRING_LEN, "%.17lf", snap_list[snap_index]->val.d_val);
            break;
        case SOS_VAL_TYPE_STRING:
            val = snap_list[snap_index]->val.c_val;
            break;
        default:
            dlog(5, "     ... error: invalid value type.  (%d)\n", val_type);
            break;
        }

        dlog(5, "     ... (%d) binding values\n", val_insert_count);
        val_insert_count++;

        CALL_SQLITE (bind_int64  (stmt_insert_val, 1,  guid         ));
        if (val != NULL) {
            CALL_SQLITE (bind_text   (stmt_insert_val, 2,  val, -1 , SQLITE_STATIC ));
        } else {
            CALL_SQLITE (bind_text   (stmt_insert_val, 2,  "", 1, SQLITE_STATIC ));
        }
        CALL_SQLITE (bind_int    (stmt_insert_val, 3,  frame        ));
        __BIND_ENUM (stmt_insert_val, 4,  semantic     );
        CALL_SQLITE (bind_int64  (stmt_insert_val, 5,  relation_id  ));
        CALL_SQLITE (bind_double (stmt_insert_val, 6,  time_pack    ));
        CALL_SQLITE (bind_double (stmt_insert_val, 7,  time_send    ));
        CALL_SQLITE (bind_double (stmt_insert_val, 8,  time_recv    ));
        CALL_SQLITE (bind_int64  (stmt_insert_val, 9,  time_pack_int));

        dlog(5, "     ... executing the query\n");

        // Execute the query.
        CALL_SQLITE_EXPECT (step (stmt_insert_val), DONE);

        dlog(5, "     ... success!  resetting the statement.\n");
        CALL_SQLITE (reset (stmt_insert_val));
        CALL_SQLITE (clear_bindings (stmt_insert_val));

        if (update_latest_frame_is_enabled) {

            //----
            dlog(5, "     ... Tracking the latest_frame values...\n");
            // >>> tblPubs.latest_frame
            note_table = SOSD.db.frame_note_pub_table;
            snprintf(note_guid_str, 128, "%" SOS_GUID_FMT "", pub_guid);
            note = (SOSD_frame_note *)
                note_table->get(note_table, note_guid_str);
            if (note == NULL) {
                // Make a new note for this pub's latest frame.
                note = (SOSD_frame_note *) calloc(1, sizeof(SOSD_frame_note));
                note->guid      = guid;
                note->pub_guid  = pub_guid;
                note->frame     = frame;
                note->dirty     = true;
                note->next_note = SOSD.db.frame_note_pub_list_head;
                SOSD.db.frame_note_pub_list_head = note;
                note_table->put(note_table, note_guid_str, note); 
            } else {
                // Update the existing note's value.
                note->frame = frame;
                note->dirty = true;
            }
            // >>> tblData.latest_frame
            note_table = SOSD.db.frame_note_val_table;
            snprintf(note_guid_str, 128, "%" SOS_GUID_FMT "", guid);
            note = (SOSD_frame_note *)
                note_table->get(note_table, note_guid_str);
            if (note == NULL) {
                // Make a new note for this pub's latest frame.
                note = (SOSD_frame_note *) calloc(1, sizeof(SOSD_frame_note));
                note->guid      = guid;
                note->pub_guid  = pub_guid;
                note->frame     = frame;
                note->dirty     = true;
                note->next_note = SOSD.db.frame_note_val_list_head;
                SOSD.db.frame_note_val_list_head = note;
                note_table->put(note_table, note_guid_str, note); 
            } else {
                // Update the existing note's value.
                note->frame = frame;
                note->dirty = true;
            }

            //----

        }

        dlog(5, "     ... grabbing the next snap.\n");
    
    } //end:for

    dlog(5, "   ... All val snaps are updated in tblVals!\n");

    if (update_latest_frame_is_enabled) {
        // ----- Update the latest_frame fields...
        dlog(5, "   ... Updating latest_frame for all snaps...\n");
        double   latest_frame_time_start;
        double   latest_frame_time_stop;
        int      rc;
        char    *err = NULL;
        SOS_TIME(latest_frame_time_start);
        sqlite3_exec(database, sql_cmd_commit_transaction, NULL, NULL, &err);
        sqlite3_exec(database, sql_cmd_begin_transaction, NULL, NULL, &err);
        note = SOSD.db.frame_note_pub_list_head;
        while(note != NULL) {
            if (note->dirty != true) {
                note = note->next_note;
                continue;
            }
            frame      = note->frame;
            pub_guid   = note->pub_guid;
            // Update tblPubs.latest_frame
            CALL_SQLITE (bind_int    (stmt_update_pub_frame, 1, frame));
            CALL_SQLITE (bind_int64  (stmt_update_pub_frame, 2, pub_guid));
            CALL_SQLITE_EXPECT (step (stmt_update_pub_frame), DONE);
            // Clear the bindings
            CALL_SQLITE (reset (stmt_update_pub_frame));
            CALL_SQLITE (clear_bindings (stmt_update_pub_frame));
            // Move to the next note...
            note->dirty = false;
            note        = note->next_note;
        }

        dlog(5, "   ... Updating tblData.latest_frame for all snaps...\n");
        sqlite3_exec(database, sql_cmd_commit_transaction, NULL, NULL, &err);
        sqlite3_exec(database, sql_cmd_begin_transaction, NULL, NULL, &err);
        note = SOSD.db.frame_note_val_list_head;
        while(note != NULL) {
            if (note->dirty != true) {
                note = note->next_note;
                continue;
            }
            frame      = note->frame;
            guid       = note->guid;
            // Update tblData.latest_frame
            CALL_SQLITE (bind_int    (stmt_update_data_frame, 1, frame));
            CALL_SQLITE (bind_int64  (stmt_update_data_frame, 2, guid));
            CALL_SQLITE_EXPECT (step (stmt_update_data_frame), DONE);
            // Clear the bindings:
            CALL_SQLITE (reset (stmt_update_data_frame));
            CALL_SQLITE (clear_bindings (stmt_update_data_frame));
            // Move to the next note...
            note->dirty = false;
            note        = note->next_note;
        }
        sqlite3_exec(database, sql_cmd_commit_transaction, NULL, NULL, &err);
        sqlite3_exec(database, sql_cmd_begin_transaction, NULL, NULL, &err);
        SOS_TIME(latest_frame_time_stop);
        dlog(5, " >> updating latest_frame values cost %lf seconds.\n",
                (latest_frame_time_stop - latest_frame_time_start));
    }
    
    // ----- Done... re-queue or free the snaps now.

    if (re_queue == NULL) {
        // Roll through and free the snaps.
        for (snap_index = 0; snap_index < snap_count ; snap_index++) {
            switch(val_type) {
            case SOS_VAL_TYPE_STRING:
                // memory controlled by SQLite
                //free(snap_list[snap_index]->val.c_val);
                break;
            case SOS_VAL_TYPE_BYTES:  
                // memory controlled by SQLite
                //free(snap_list[snap_index]->val.bytes); 
                break;
            default: 
                break;
            }
            free(snap_list[snap_index]);
        }
    } else {
       // Inject this snap queue into the next one en masse.
       dlog(5, "Re-queue'ing this snap queue to send to the aggregator.\n");
       pthread_mutex_lock(re_queue->sync_lock);
       pipe_push(re_queue->intake, (void *) snap_list, snap_count);
       re_queue->elem_count += snap_count;
       pthread_mutex_unlock(re_queue->sync_lock);
       dlog(5, "Done re-queueing.\n");
    }

    free(snap_list);
    free(val_alloc);

    dlog(5, "  ... done.  returning to loop.\n");

    return;
}


void SOSD_db_insert_enum(const char *var_type, const char **var_name, int var_max_index) {
    SOS_SET_CONTEXT(SOSD.sos_context, "SOSD_db_insert_enum");

    int i;
    const char *type = var_type;
    const char *name;

    for (i = 0; i < var_max_index; i++) {
        int pos = i;
        name = var_name[i];

        CALL_SQLITE (bind_text   (stmt_insert_enum, 1,  type,         -1 , SQLITE_STATIC     ));
        CALL_SQLITE (bind_text   (stmt_insert_enum, 2,  name,         -1 , SQLITE_STATIC     ));
        CALL_SQLITE (bind_int    (stmt_insert_enum, 3,  pos ));

        dlog(2, "  --> SOS_%s_string[%d] == \"%s\"\n", type, i, name);

        CALL_SQLITE_EXPECT (step (stmt_insert_enum), DONE);

        CALL_SQLITE (reset(stmt_insert_enum));
        CALL_SQLITE (clear_bindings (stmt_insert_enum));

    }

    return;
}



void SOSD_db_create_tables(void) {
    SOS_SET_CONTEXT(SOSD.sos_context, "SOSD_db_create_tables");
    int rc;
    char *err = NULL;

    dlog(1, "Creating tables in the database...\n");

    rc = sqlite3_exec(database, sql_create_table_pubs, NULL, NULL, &err);
    if( err != NULL ) {
        dlog(0, "ERROR!  Can't create " SOSD_DB_PUBS_TABLE_NAME
                " in the database!  (%s)\n", err);
        sqlite3_close(database);
        exit(EXIT_FAILURE);
    } else {
        dlog(1, "  ... Created: %s\n", SOSD_DB_PUBS_TABLE_NAME);
    }

    rc = sqlite3_exec(database, sql_create_table_data, NULL, NULL, &err);
    if( err != NULL ) {
        dlog(0, "ERROR!  Can't create " SOSD_DB_DATA_TABLE_NAME
                " in the database!  (%s)\n", err);
        sqlite3_close(database);
        exit(EXIT_FAILURE);
    } else {
        dlog(1, "  ... Created: %s\n", SOSD_DB_DATA_TABLE_NAME);
    }

    rc = sqlite3_exec(database, sql_create_table_vals, NULL, NULL, &err);
    if( err != NULL ) {
        dlog(0, "ERROR!  Can't create " SOSD_DB_VALS_TABLE_NAME
                " in the database!  (%s)\n", err);
        sqlite3_close(database);
        exit(EXIT_FAILURE);
    } else {
        dlog(1, "  ... Created: %s\n", SOSD_DB_VALS_TABLE_NAME);
    }

    rc = sqlite3_exec(database, sql_create_table_enum, NULL, NULL, &err);
    if ( err != NULL ) {
        dlog(0, "ERROR!  Can't create " SOSD_DB_ENUM_TABLE_NAME
                " in the database!  (%s)\n", err);
        sqlite3_close(database);
        exit(EXIT_FAILURE);
    } else {
        dlog(1, "  ... Created: %s\n", SOSD_DB_ENUM_TABLE_NAME);
    }

    rc = sqlite3_exec(database, sql_create_table_sosd_config, NULL, NULL, &err);
    if ( err != NULL ) {
        dlog(0, "ERROR!  Can't create " SOSD_DB_SOSD_TABLE_NAME
                " in the database!  (%s)\n", err);
        sqlite3_close(database);
        exit(EXIT_FAILURE);
    } else {
        dlog(1, "  ... Created: %s\n", SOSD_DB_SOSD_TABLE_NAME);
    }

    rc = sqlite3_exec(database, sql_create_view_combined, NULL, NULL, &err);
    if ( err != NULL ) {
        dlog(0, "ERROR!  Can't create " SOSD_DB_VIEW_COMBINED_NAME
                " in the database!  (%s)\n", err);
        sqlite3_close(database);
        exit(EXIT_FAILURE);
    } else {
        dlog(1, "  ... Created: %s\n", SOSD_DB_VIEW_COMBINED_NAME);
    }

    dlog(1, "  ... done.\n");
    return;

}

