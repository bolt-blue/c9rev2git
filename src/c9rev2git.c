/* Ref: https://www.tutorialspoint.com/sqlite/sqlite_c_cpp.htm */

#include <unistd.h>     // getopt

#include <errno.h>
#include <stdio.h>      // printf, fprintf
#include <string.h>     // strncpy

#include <sys/stat.h>   // open, mkdir
#include <sys/types.h>  // open, mkdir
#include <fcntl.h>      // open

#include <git2.h>

#include <sqlite3.h>

/* ========================================================================== */

#define BYTE char

#define KILOBYTE(v) ((v) * 1024LL)
#define MEGABYTE(v) (KILOBYTE(v) * 1024L)

#ifdef DEBUG
    #define ASSERT(exp) \
        if (!(exp)) \
        { \
            fprintf(stderr, "[ASSERT] %s:%d:%s\n", __FILE__, __LINE__, __func__); \
            exit(2); \
        }
#else
    #define ASSERT(exp)
#endif

#define false 0
#define true 1

/* ========================================================================== */

enum OPTS
{
    // Must be powers of two
    QUIET = 0x1
};

typedef struct mem_pool
{
    BYTE *base;
    BYTE *top;
    BYTE *cur;
} mem_pool_t;

typedef struct rev {
    int id;
    char *op;
} rev_t;

typedef struct doc {
    int id;
    int rev_num;
    int rev_cnt;
    char *save_path;
    rev_t *revisions;
} doc_t;

/* ========================================================================== */

// Global memory for entire Process
mem_pool_t MEM;
mem_pool_t STRUCT_POOL;
mem_pool_t STRING_POOL;

doc_t *DOC_LIST;
rev_t *REV_LIST;

unsigned int DOC_CNT;
unsigned int REV_CNT;

/* ========================================================================== */

void print_usage()
{
    // TODO : Have `make` insert the binary name before compilation
    fprintf(stderr, "Usage: ./c9rev2git [-q] [-o output-dir] database.db\n");
}

void git2_exit_with_error(int error)
{
    // ref: https://libgit2.org/docs/guides/101-samples/
    const git_error *e = git_error_last();
    fprintf(stderr, "[Error %d/%d] %s\n", error, e->klass, e->message);
    exit(error);
}

/* ========================================================================== */

BYTE * mem_push(mem_pool_t *pool, unsigned int sz)
{
    // Used during DEBUG. Can be used to tune total required memory
    ASSERT(pool->base + sz <= pool->top);

    BYTE *ret = pool->cur;

    pool->cur += sz;

    return ret;
}

int mem_alloc(mem_pool_t *pool, unsigned long capacity)
{
    pool->base = malloc(capacity);
    if (!pool->base)
    {
        return 1;
    }

    pool->top = pool->base + capacity;
    pool->cur = pool->base;

    return 0;
}

int mem_sub_alloc(mem_pool_t *parent, mem_pool_t *child, unsigned long capacity)
{
    ASSERT(parent->base);
    ASSERT(parent->cur + capacity <= parent->top);

    child->base = mem_push(parent, capacity);
    child->top = child->base + capacity;
    child->cur = child->base;

    return 0;
}

void mem_free(mem_pool_t *pool)
{
    free(pool->base);
}

/* ========================================================================== */

/*
 * Process each file revision
 */
static int process_rev_cb(void *unused, int col_cnt, char **col_data, char **col_names)
{
    // TODO

    return 0;
}

/*
 * Process each target file
 *   - Create any directory tree as required
 *   - Save copy of original file to repo, for further processing
 *
 * Arg Reference:
 *   data      : Data provided in the 4th argument of `sqlite3_exec()`
 *   col_cnt   : The number of columns in row
 *   col_data  : An array of strings representing fields in the row
 *   col_names : An array of strings representing column names
 *
 * Expects:
 *   data to be a file descriptor for the repository directory
 *   col_data[0] to be 'id'
 *   col_data[1] to be 'path'
 *   col_data[2] to be 'contents'
 *   col_data[3] to be 'content_len'
 *   col_data[4] to be 'rev_num'
*/
static int process_doc_cb(void *repo_fd, int col_cnt, char **col_data, char **col_names)
{
    // TODO : Confirm each column by name, rather than assuming ?
    // WARNING : `col_data` will contain NULL pointers where there is no value stored
    char *doc_id    = col_data[0];
    char *path      = col_data[1];
    char *contents  = col_data[2];
    int content_len = atoi(col_data[3]);
    int rev_num     = atoi(col_data[4]);

    // TODO : Test and/or get feedback on this assumption
    // Assuming 512 is long enough to account for most reasonable directory tree depth
    char dir_path[512];

    int cnt = 0;
    int dir_len = 0;
    int filename_len = 0;

    for (; path[cnt] != '\0'; cnt++)
    {
        // Create any necessary directories as we find them
        if (!(path[cnt] == '/'))
        {
            continue;
        }

        strncpy(dir_path, path, cnt);

        // Always remember the null terminator
        dir_path[cnt] = '\0';

        // Update total directory path length
        dir_len = cnt;

        if (mkdirat(*(int *)repo_fd, dir_path, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) == -1)
        {
            if (errno == EEXIST)
            {
                // Don't worry if the directory already exists
                continue;
            }

            // Trigger a sqlite abort
            fprintf(stderr, "[Error] Failed to create directory '%s'. Aborting...\n", dir_path);

            // TODO : Implement "clean up" on failure, in main(), and remove this
            fprintf(stderr, "[WARNING] This may leave file and/or directory artefacts.\n");

            return 1;
        }
    }

    // This will inherently account for the null terminator
    filename_len = cnt - dir_len;

    // Remember to account for null terminator
    char filename[filename_len + 1];
    strncpy(filename, path + dir_len + 1, filename_len);

    //printf("[DEBUG] dir: %-40s | filename: %-30s | doc_id: %5s\n", dir_path, filename, doc_id);

    // Thanks to the SQL query, we can guarantee the file paths are in
    // ascending document id order - which means they can be accessed
    // directly by index later when using DOC_LIST

    // Append doc directly to STRUCT_POOL
    // They will be contiguous, and managed elsewhere
    doc_t *doc = (doc_t *)mem_push(&STRUCT_POOL, sizeof(doc_t));
    doc->id = atoi(doc_id);
    doc->rev_num = rev_num;
    doc->rev_cnt = 0;

    // Save paths will also be contiguous in STRING_POOL
    // (can therefore also be directly - null pointer separated)
    doc->save_path = mem_push(&STRING_POOL, cnt + 1);

    // Revisions will get set later
    doc->revisions = NULL;

    // Store a copy of the relative save path
    strncpy(doc->save_path, path, cnt + 1);

    DOC_CNT++;

    // Save out document in it's "final" state.
    // Working later with revisions will initially process backwards from that state.
    int save_fd = openat(*(int *)repo_fd, path, O_WRONLY | O_CREAT,
                                                S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    if (write(save_fd, contents, content_len) == -1)
    {
        fprintf(stderr, "[Error] Failed to write out %s\n", path);

        close(save_fd);
        return -1;
    }

    close(save_fd);

    return 0;
}

/* ========================================================================== */

/*
 * Return codes:
 *   0 - Success
 *   1 - Usage error
 *   2 - mkdir error
 *   3 - sqlite3 error
 */
int main(int argc, char **argv)
{
    if (argc < 2)
    {
        print_usage();
        return 1;
    }

    int flags, opt;
    char *repo_dir = "repo";

    // Get command line args
    while ((opt = getopt(argc, argv, "qo:")) != -1)
    {
        switch (opt)
        {
            case 'q':
                // quiet - prevent output to stdout
                flags |= QUIET;
                break;
            case 'o':
                // Alter the output directory name
                repo_dir = optarg;
                break;
            default: /* '?' */
                print_usage();
                return 1;
        }
    }

    // Make sure a 'database path' has been passed
    if (optind >= argc)
    {
        print_usage();
        return 1;
    }

    // Initialise main memory
    mem_alloc(&MEM, MEGABYTE(1));

    // Initialise two sub-pools
    mem_sub_alloc(&MEM, &STRUCT_POOL, KILOBYTE(512));
    mem_sub_alloc(&MEM, &STRING_POOL, KILOBYTE(512));

    char *filepath = argv[optind];
    sqlite3 *db;

    if ((flags & QUIET) == 0)
    {
        fprintf(stdout, "Opening database: %s\n", filepath);
    }

    if (sqlite3_open(filepath, &db) != SQLITE_OK)
    {
        // TODO : Utilise sqlite3_errmsg() [ref: https://www.sqlite.org/c3ref/errcode.html]
        fprintf(stderr, "Failed to open %s : %s\n", filepath, sqlite3_errmsg(db));
        return 2;
    }

    // Create working directory with permissions 755
    if (mkdir(repo_dir, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) == -1)
    {
        switch (errno)
        {
            // TODO : Add more specific cases ? [see: `man 2 mkdir`]
            case EEXIST:
                fprintf(stderr, "[Error %d] Directory already exists. Exiting.\n", errno);
                break;
            default:
                fprintf(stderr, "[Error %d] Failed to create working directory. (Ref: errno-base.h) Exiting\n", errno);
        }

        return 2;
    }

    // Store the repo file descriptor
    int repo_fd = open(repo_dir, O_DIRECTORY | O_RDONLY);

    // Intitialise git2 library
    git_libgit2_init();

    // Set up git repo
    git_repository *repo = NULL;

    if ((flags & QUIET) == 0)
    {
        fprintf(stdout, "Initialising git repo...\n");
    }

    // Temp variable for the result value of various function calls to follow
    int res;

    res = git_repository_init(&repo, repo_dir, false);
    if (res < 0)
    {
        git2_exit_with_error(res);
    }

    if ((flags & QUIET) == 0)
    {
        fprintf(stdout, "Importing document data...\n");
    }

    char *sql_err = NULL;

    // DOC_LIST array will be stored contiguously in STRUCT_POOL
    // and populated by the following sqlite3_exec()
    DOC_LIST = (doc_t *)STRUCT_POOL.cur;

    // Query to select and store all relevent document data
    char *file_query = "SELECT id, path, contents, length(contents) AS content_len, revNum AS rev_num FROM Documents ORDER BY id ASC";

    // Process each target file in database
    res = sqlite3_exec(db, file_query, process_doc_cb, &repo_fd, &sql_err);
    if (res != SQLITE_OK)
    {
        fprintf(stderr, "Failed to retrieve target filenames from database\n");
        fprintf(stderr, "[Error: SQL] %s\n", sql_err);

        goto CLEANUP;

        return 3;
    }

    if ((flags & QUIET) == 0)
    {
        fprintf(stdout, "Importing revision data...\n");
    }

    // REV_LIST array will be stored contiguously in STRUCT_POOL
    // and populated by the following sqlite3_exec()
    // TODO : Implement better memory alignment
    REV_LIST = (rev_t *)STRUCT_POOL.cur;

    // Query to select and store all relevent revision data
    char *rev_query = "SELECT id, document_id, operation, length(operation) AS op_len FROM Revisions ORDER BY document_id ASC, id ASC";

    // Process all revisions in database
    res = sqlite3_exec(db, rev_query, process_rev_cb, 0, &sql_err);
    if (res != SQLITE_OK)
    {
        fprintf(stderr, "Failed to retrieve revisions from database\n");
        fprintf(stderr, "[Error: SQL] %s\n", sql_err);

        goto CLEANUP;

        return 3;
    }

    // TODO : Process each revision for the each target file
    // - Update the file
    // - Create a new `git commit`
    //
    // - For each document:
    //   - Open read write
    //   - For each revision:
    //     ~ Add, Delete or Retain as necessary
    //     ~ Save the changes to disk
    //     ~ Run `git add`
    //     ~ Run `git commit` with basic message

CLEANUP:

    if ((flags & QUIET) == 0)
    {
        fprintf(stdout, "Cleaning up memory...\n");
    }

    sqlite3_free(sql_err);
    sqlite3_close(db);

    // Free repo initialsed by libgit2
    git_repository_free(repo);

    // Clean up libgit2 state (not strictly necessary)
    git_libgit2_shutdown();

    close(repo_fd);

    mem_free(&MEM);

    return 0;
}
