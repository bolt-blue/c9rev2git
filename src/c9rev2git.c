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

typedef struct mem_pool
{
    BYTE *base;
    BYTE *top;
    BYTE *cur;
} mem_pool_t;

typedef struct rev {
    int num;
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
mem_pool_t SCRATCH_POOL;

git_commit **HEAD;

doc_t *DOC_LIST;
rev_t *REV_LIST;

unsigned int DOC_CNT;
unsigned int REV_CNT;

// Boolean to limit prints to stdout
int QUIET = 0;

// Unit Separator
char US = 31;

/* ========================================================================== */

void print_usage()
{
    // TODO : Have `make` insert the binary name before compilation ?
    fprintf(stderr, "Usage: ./c9rev2git [-q] [-o output-dir] database.db\n");
}

void git2_exit_with_error(int error)
{
    // ref: https://libgit2.org/docs/guides/101-samples/
    const git_error *e = git_error_last();
    fprintf(stderr, "[ERROR %d/%d] %s\n", error, e->klass, e->message);
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

void mem_pop(BYTE **mem, mem_pool_t *pool, unsigned int sz)
{
    ASSERT(pool->cur - sz >= pool->base);

    pool->cur -= sz;
    *mem = NULL;
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
 * Replace quotes around instructions with a single Unit Separator char
 * Replace escaped characters
 *   TODO : anything other than '\n' and '\t' ?
 * Return parsed op len, including null terminator
 */
int parse_op(const char *op, char *parsed)
{
    int len = 0;

    // Can safely skip the first char - it is always '['
    while (*++op)
    {
        if (*op == '"' && *(op - 1) != '\\')
        {
            if (*(op + 1) == ',' || *(op + 1) == ']')
            {
                op++;
            }
            else {
                *parsed++ = US;
                len++;
            }
        }
        else if (*op == '\\')
        {
            switch (*(op + 1))
            {
                case '\\':
                    // Preserve escaped escape sequences
                    // TODO : Don't just allow for single-char escape sequences
                    *parsed++ = *++op;
                    *parsed++ = *++op;
                    len += 2;
                    break;
                case 'n':
                    *parsed++ = '\n';
                    len++;
                    op++;
                    break;
                case 't':
                    *parsed++ = '\t';
                    len++;
                    op++;
                    break;
                case '"':
                    *parsed++ = *++op;
                    len++;
                    break;
            }
        }
        else
        {
            *parsed++ = *op;
            len++;
        }
    }

    // Nul terminate parsed op
    *parsed = '\0';

    return ++len;
}

/*
 * sqlite3 Callback Reference:
 *   data      : Data provided in the 4th argument of `sqlite3_exec()`
 *   col_cnt   : The number of columns in row
 *   col_data  : An array of strings representing fields in the row
 *   col_names : An array of strings representing column names
 */

/*
 * Process each file revision
 *   - Store revision data in memory
 *
 * Expects:
 *   data to be null
 *   col_data[0] to be 'doc_id'
 *   col_data[1] to be 'rev_num'
 *   col_data[2] to be 'op'
 *   col_data[3] to be 'op_len'
 */
static int process_rev_cb(void *unused, int col_cnt, char **col_data, char **col_names)
{
    // WARNING : `col_data` will contain NULL pointers where there is no value stored
    int doc_id  = atoi(col_data[0]);
    int rev_num = atoi(col_data[1]);
    int op_len  = atoi(col_data[3]);
    char *op    = col_data[2];

    // Thanks to the SQL query, we can guarantee the revisions are in
    // ascending document id order, and per doc in ascending revision number.
    // Should give better memory access when working with a given document.

    // Skip "empty" revisions - generally the first for each document
    if (strcmp(op, "[]") == 0)
    {
        return 0;
    }

    // Append rev directly to STRUCT_POOL
    // They will be contiguous, and managed elsewhere
    rev_t *rev = (rev_t *)mem_push(&STRUCT_POOL, sizeof(rev_t));

    rev->num = rev_num;

    // Get some temp mem for the parsing the op
    char *parsed = mem_push(&SCRATCH_POOL, op_len);
    int p_len = parse_op(op, parsed);

    // Operation strings will also be contiguous in STRING_POOL
    // Can therefore also be accessed directly
    //   - whole operations are "null byte" separated
    //   - operation instructions are "unit separator" separated
    rev->op = mem_push(&STRING_POOL, p_len);
    strncpy(rev->op, parsed, p_len);

    // Remember to clean up temp mem usage
    mem_pop(&parsed, &SCRATCH_POOL, op_len);

    // Document id's are 1-indexed in the database
    doc_t *doc = DOC_LIST + doc_id - 1;

    // Point doc to the first revision
    if (!doc->revisions)
    {
        doc->revisions = rev;
    }
    doc->rev_cnt++;

    REV_CNT++;

    return 0;
}

/*
 * Process each target file
 *   - Create any directory tree as required
 *   - Store some document data in memory
 *   - Save copy of original file to repo, for further processing
 *
 * Expects:
 *   data to be a file descriptor for the repository directory
 *   col_data[0] to be 'id'
 *   col_data[1] to be 'path'
 *   col_data[2] to be 'contents'
 *   col_data[3] to be 'content_len'
 *   col_data[4] to be 'rev_num'
*/
static int prepare_doc_cb(void *repo_fd, int col_cnt, char **col_data, char **col_names)
{
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

                if (QUIET == 0)
                {
                    fprintf(stdout, "[mkdir] Skipping '%s'. Already exists\n", dir_path);
                }
                continue;
            }

            // Trigger a sqlite abort
            fprintf(stderr, "[ERROR] Failed to create directory '%s'. Aborting...\n", dir_path);

            // TODO : Implement "clean up" on failure, in main(), and remove this
            fprintf(stderr, "[WARNING] This may leave file and/or directory artefacts.\n");

            return 1;
        }
        else if (QUIET == 0)
        {
            fprintf(stdout, "[mkdir] Creating '%s'\n", dir_path);
        }

    }

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
    // (can therefore also be accessed directly - null pointer separated)
    doc->save_path = mem_push(&STRING_POOL, cnt + 1);

    // Revisions will get set later
    doc->revisions = NULL;

    // Store a copy of the relative save path
    strncpy(doc->save_path, path, cnt + 1);

    DOC_CNT++;

    // Save out document in it's "final" state.
    // Working later with revisions will initially process backwards
    // from that state, or wipe the doc and start fresh.
    int save_fd = openat(*(int *)repo_fd, path, O_WRONLY | O_CREAT,
                                                S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    if (write(save_fd, contents, content_len) == -1)
    {
        fprintf(stderr, "[ERROR] Failed to write out %s\n", path);

        close(save_fd);
        return -1;
    }

    close(save_fd);

    return 0;
}

/* ========================================================================== */

// TODO : Find a way to clean up the overt repetition in `git_initial_commit()`
//        and `add_and_commit()`

/*
 * Returns:
 *  0 : Success
 * <0 : Failure
 */
int git_initial_commit(git_repository *repo)
{
    // Get latest repo index
    git_index *idx;

    if (git_repository_index(&idx, repo) < 0)
    {
        fprintf(stderr, "[ERROR] Could not open repository index. Exiting...\n");
    }

    // Prepare commit
    git_oid tree_id, commit_id;
    git_tree *tree;

    if (git_index_write_tree(&tree_id, idx) < 0)
    {
        fprintf(stderr, "[ERROR] Unable to write initial tree from index\n");
        return -2;
    }

    if (git_index_write(idx) < 0)
    {
        fprintf(stderr, "[ERROR] Failed to write updated repo index\n");
        return -3;
    }

    if (git_tree_lookup(&tree, repo, &tree_id) < 0)
    {
        fprintf(stderr, "[ERROR] Could not look up initial tree\n");
        return -4;
    }

    // NOTE : Defaults to using global git config, but has a fallback
    // TODO : Allow sig to be set from command line
    git_signature *sig;
    if (git_signature_default(&sig, repo) < 0)
    {
        fprintf(stdout, "[INFO] It appears 'user.name' and 'user.email' are not set. Using 'c9rev2git' and 'bot@localhost'\n");

        if (git_signature_now(&sig, "c9rev2git", "bot@localhost") < 0)
        {
            fprintf(stderr, "[ERROR] Failed to set 'user.name' and 'user.email'. Exiting...\n");
            return -1;
        }
    }

    int error = git_commit_create(&commit_id, repo, "HEAD", sig, sig,
                                  NULL, "Initial commit", tree, 0, NULL);
    if (error < 0)
    {
        fprintf(stderr, "[ERROR] Failed to create initial commit\n");
        return -4;
    }

    // Dereference HEAD to a commit
    //git_commit_lookup(HEAD, repo, &commit_id);
    git_object *head_commit;
    error = git_revparse_single(&head_commit, repo, "HEAD^{commit}");
    *HEAD = (git_commit*)head_commit;

    // Cleanup
    git_tree_free(tree);
    git_signature_free(sig);
    git_index_free(idx);

    return 0;
}

/*
 * Returns:
 *  0 : Success
 * <0 : Failure
 */
int add_and_commit(git_repository *repo, char *path, int rev_num)
{
    // Get latest repo index
    git_index *idx;

    if (git_repository_index(&idx, repo) < 0)
    {
        fprintf(stderr, "[ERROR] Could not open repository index. Exiting...\n");
    }

    // Stage file
    if (git_index_add_bypath(idx, path) < 0)
    {
        fprintf(stderr, "[ERROR] Failed to add %s for new commit. Exiting...\n", path);
        return -1;
    }

    // Prepare commit
    git_oid tree_id, commit_id;
    git_tree *tree;

    if (git_index_write_tree(&tree_id, idx) < 0)
    {
        fprintf(stderr, "[ERROR] Unable to write initial tree from index\n");
        return -2;
    }

    if (git_index_write(idx) < 0)
    {
        fprintf(stderr, "[ERROR] Failed to write updated repo index\n");
        return -3;
    }

    if (git_tree_lookup(&tree, repo, &tree_id) < 0)
    {
        fprintf(stderr, "[ERROR] Could not look up initial tree\n");
        return -4;
    }

    // TODO : Allow sig to be set from command line
    // NOTE : Defaults to using global git config, but has a fallback
    git_signature *sig;
    if (git_signature_default(&sig, repo) < 0)
    {
        fprintf(stdout, "[INFO] It appears 'user.name' and 'user.email' are not set. Using 'c9rev2git' and 'bot@localhost'\n");

        if (git_signature_now(&sig, "c9rev2git", "bot@localhost") < 0)
        {
            fprintf(stderr, "[ERROR] Failed to set 'user.name' and 'user.email'. Exiting...\n");
            return -1;
        }
    }

    char commit_msg[255] = {0};
    sprintf(commit_msg, "./%s [rev: %d]", path, rev_num);

    int error = git_commit_create(&commit_id, repo, "HEAD", sig, sig,
                                  NULL, commit_msg, tree, 1, (const git_commit **)HEAD);
    if (error < 0)
    {
        fprintf(stderr, "[ERROR %d] Failed to create commit for %s\n", error, path);
        return -4;
    }

    // Dereference HEAD to a commit
    //git_commit_lookup(HEAD, repo, &commit_id);
    git_object *head_commit;
    error = git_revparse_single(&head_commit, repo, "HEAD^{commit}");
    *HEAD = (git_commit*)head_commit;

    // Cleanup
    git_tree_free(tree);
    git_signature_free(sig);
    git_index_free(idx);

    return 0;
}

/* ========================================================================== */

/*
 * Modify 'op' pointer to point at the next instruction char.
 * Returns: 1 for success, 0 for failure
 */
int next_op_code(char **op)
{
    while (**op)
    {
        if (**op == US)
        {
            // The next char is an instruction
            (*op)++;

            return 1;
        }
        (*op)++;
    }

    return 0;
}

/*
 * Returns retain value
 */
int get_retain_val(const char *val)
{
    const char *cur = val;
    int len = 0;

    while (*cur)
    {
        if (*cur == US)
        {
            break;
        }
        cur++;
        len++;
    }

    char val_dup[len + 1];
    strncpy(val_dup, val, len);
    val_dup[len] = '\0';

    return atoi(val_dup);
}

/*
 * Returns character count for instruction
 */
int get_instruction_len(const char *cur)
{
    int len = 0;

    while (*cur)
    {
        if (*cur == US)
        {
            return len;
        }
        cur++;
        len++;
    }

    return len;
}

int reset_check(char *op)
{
    // If instruction is an insertion ('i'), with no preceding or trailing retain ('r'),
    // then we know the revision process began with an empty document
    char *cur = op;
    while(next_op_code(&cur))
    {
        if (*cur == 'r' || *cur == 'd')
        {
            // The revisions do not start from a "clean slate"
            // and require full processing
            return false;
        }
    }
    return true;
}

/*
 * Process each revision from last to first, with inverted operations
 */
int revert_doc(int repo_fd, doc_t *doc)
{
#ifdef DEBUG
    // Save a backup of the original
    char bak_name[255] = {0};
    sprintf(bak_name, "%s.bak", doc->save_path);
    int bak_fd = openat(repo_fd, bak_name, O_CREAT | O_WRONLY | O_TRUNC,
                                           S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    if (QUIET == 0)
    {
        fprintf(stdout, "[DEBUG] Saving backup as '%s'...\n", bak_name);
    }
#endif

    for (int i = doc->rev_cnt -1; i >= 0; i--)
    {
        // Read a copy of doc into memory
        int read_fd = openat(repo_fd, doc->save_path, O_RDONLY);
        if (read_fd == -1)
        {
            fprintf(stderr, "[ERROR %d] Failed to open document for copying!\n", errno);
            return false;
        }

        struct stat rs;
        if (fstat(read_fd, &rs) == -1)
        {
            fprintf(stderr, "Failed to retrieve document file stats.\n");
            return false;
        }

        char *read_copy = mem_push(&SCRATCH_POOL, rs.st_size);
        read(read_fd, read_copy, rs.st_size);
#ifdef DEBUG
        write(bak_fd, read_copy, rs.st_size);
        close(bak_fd);
#endif
        close(read_fd);

        // Overwrite original doc
        int write_fd = openat(repo_fd, doc->save_path, O_WRONLY | O_TRUNC);
        if (write_fd == -1)
        {
            fprintf(stderr, "[ERROR %d] Couldn't open file for writing!\n", errno);
            return false;
        }

        rev_t *rev = doc->revisions + i;

        char *cur = rev->op;

        while(next_op_code(&cur))
        {
            char *buffer = NULL;
            int len;

            // Remember 'i' and 'd' must be swapped here
            switch(*cur)
            {
                case 'i':
                    len = get_instruction_len(++cur);

                    // Skip 'len' letters after read cursor
                    read_copy += len;
                    break;
                case 'd':
                    len = get_instruction_len(++cur);

                    // Write from op instruction
                    write(write_fd, cur, len);
                    break;
                case 'r':
                    len = get_retain_val(++cur);

                    // Write from original
                    write(write_fd, read_copy, len);

                    // Move read cursor forward
                    read_copy += len;
                    break;
            }
        }

        // Save changes
        close(write_fd);
        mem_pop(&read_copy, &SCRATCH_POOL, rs.st_size);
    }

    return 0;
}

/*
 * Process each revision from first to last
 * Commit changes to git repo
 */
int revise_and_commit(int repo_fd, doc_t *doc, git_repository *repo)
{
    for (int i = 0; i < doc->rev_cnt; i++)
    {
        // Read a copy of doc into memory
        int read_fd = openat(repo_fd, doc->save_path, O_RDONLY);
        if (read_fd == -1)
        {
            fprintf(stderr, "[ERROR %d] Failed to open document for copying!\n", errno);
            return false;
        }

        struct stat rs;
        if (fstat(read_fd, &rs) == -1)
        {
            fprintf(stderr, "Failed to retrieve document file stats.\n");
            return false;
        }

        char *read_copy = mem_push(&SCRATCH_POOL, rs.st_size);
        read(read_fd, read_copy, rs.st_size);
        close(read_fd);

        // Overwrite original doc
        int write_fd = openat(repo_fd, doc->save_path, O_WRONLY | O_TRUNC);
        if (write_fd == -1)
        {
            fprintf(stderr, "[ERROR %d] Couldn't open file for writing!\n", errno);
            return false;
        }

        rev_t *rev = doc->revisions + i;

        char *cur = rev->op;

        while(next_op_code(&cur))
        {
            char *buffer = NULL;
            int len;

            // TODO : Determine if the switch from here and `revert_doc()`
            // can be efficiently pulled out into a separate function
            switch(*cur)
            {
                case 'i':
                    len = get_instruction_len(++cur);

                    // Write from op instruction
                    write(write_fd, cur, len);
                    break;
                case 'd':
                    len = get_instruction_len(++cur);

                    // Skip 'len' letters after read cursor
                    read_copy += len;
                    break;
                case 'r':
                    len = get_retain_val(++cur);

                    // Write from original
                    write(write_fd, read_copy, len);

                    // Move read cursor forward
                    read_copy += len;
                    break;
            }
        }

        // Save changes
        close(write_fd);
        mem_pop(&read_copy, &SCRATCH_POOL, rs.st_size);

        // Update repo
        // TODO : Determine method to combine multiple revisions into one commit
        if (add_and_commit(repo, doc->save_path, rev->num) < 0)
        {
            return -1;
        }
    }

    return 0;
}

/*
 * Ops are "null terminated", with "unit separated" instructions.
 * An op may consist of multiple instructions, denoted by a single
 * char at the head - 'i', 'd' and 'r' for "insert", "delete" and "retain" respectively.
 * 'i' and 'd' are followed by the text to insert or delete.
 * 'r' is followed by an integer character count.
 */
int process_revisions(int repo_fd, git_repository *repo)
{
    for (doc_t *doc = DOC_LIST; doc < DOC_LIST + DOC_CNT; doc++)
    {
        char *doc_path = doc->save_path;

        if (doc->rev_num == 0)
        {
            if (QUIET == 0)
            {
                fprintf(stdout, "[INFO] No revisions for '%s'. Simply `add` and `commit`...\n", doc_path);
            }

            // Revisionless doc
            if (add_and_commit(repo, doc->save_path, 0) < 0)
            {
                return -1;
            }

            continue;
        }

        // Initially check the first rev op to see if we can skip doc reversion.
        int reset = reset_check(doc->revisions->op);

        if (reset)
        {
            if (QUIET == 0)
            {
                fprintf(stdout, "[INFO] Clear '%s'...\n", doc_path);
            }

            // Revert document to blank state
            int doc_fd = openat(repo_fd, doc_path, O_WRONLY | O_TRUNC);
            if (doc_fd == -1)
            {
                fprintf(stderr, "[ERROR] Failed to open %s\n", doc_path);
                close(doc_fd);
                return -1;
            }
            close(doc_fd);
        }
        else
        {
            if (QUIET == 0)
            {
                fprintf(stdout, "[INFO] Revert '%s' to original state...\n", doc_path);
            }

            // Revert to initial state
            revert_doc(repo_fd, doc);
        }

        if (QUIET == 0)
        {
            fprintf(stdout, "[INFO] Process Revisions for '%s'...\n", doc_path);
        }

        revise_and_commit(repo_fd, doc, repo);
    }

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
                QUIET = 1;
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
    mem_alloc(&MEM, MEGABYTE(2));

    // Initialise sub-pools
    mem_sub_alloc(&MEM, &STRUCT_POOL, KILOBYTE(512));
    mem_sub_alloc(&MEM, &STRING_POOL, KILOBYTE(512));
    mem_sub_alloc(&MEM, &SCRATCH_POOL, KILOBYTE(512));

    char *filepath = argv[optind];
    sqlite3 *db;

    if (QUIET == 0)
    {
        fprintf(stdout, "[INFO] Open database: %s\n", filepath);
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
                fprintf(stderr, "[ERROR %d] Directory already exists. Exiting.\n", errno);
                break;
            default:
                fprintf(stderr, "[ERROR %d] Failed to create working directory. (Ref: errno-base.h) Exiting\n", errno);
        }

        return 2;
    }

    // Intitialise git2 library
    git_libgit2_init();

    // Set up git repo
    git_repository *repo = NULL;

    if (QUIET == 0)
    {
        fprintf(stdout, "[INFO] Initialise git repo...\n");
    }

    HEAD = (git_commit **)mem_push(&SCRATCH_POOL, sizeof(git_commit **));

    // Git Init Repo
    int res = git_repository_init(&repo, repo_dir, false);
    if (res < 0)
    {
        git2_exit_with_error(res);
        goto CLEANUP;
    }

    git_initial_commit(repo);

    if (QUIET == 0)
    {
        fprintf(stdout, "[INFO] Import document data...\n");
    }

    // Store the repo file descriptor
    int repo_fd = open(repo_dir, O_DIRECTORY | O_RDONLY);

    char *sql_err = NULL;

    // DOC_LIST array will be stored contiguously in STRUCT_POOL
    // and populated by the following sqlite3_exec()
    DOC_LIST = (doc_t *)STRUCT_POOL.cur;

    // Query to select and store all relevent document data
    char *file_query = "SELECT id, path, contents, length(contents) AS content_len, revNum AS rev_num FROM Documents ORDER BY id ASC";

    // Process each target file in database
    if (sqlite3_exec(db, file_query, prepare_doc_cb, &repo_fd, &sql_err) != SQLITE_OK)
    {
        fprintf(stderr, "[ERROR] Failed to retrieve target filenames from database\n");
        fprintf(stderr, "[SQLERR] %s\n", sql_err);

        goto CLEANUP;

        return 3;
    }

    if (QUIET == 0)
    {
        fprintf(stdout, "[INFO] Importing revision data...\n");
    }

    // REV_LIST array will be stored contiguously in STRUCT_POOL
    // and populated by the following sqlite3_exec()
    // TODO : Implement better memory alignment
    REV_LIST = (rev_t *)STRUCT_POOL.cur;

    // Query to select relevant revision data - with optimal ordering
    char *rev_query = "SELECT document_id AS doc_id, revNum AS rev_num, operation AS op, length(operation) AS op_len FROM Revisions ORDER BY document_id ASC, revNum ASC";

    // Store data on all revisions in database
    if (sqlite3_exec(db, rev_query, process_rev_cb, 0, &sql_err) != SQLITE_OK)
    {
        fprintf(stderr, "Failed to retrieve revisions from database\n");
        fprintf(stderr, "[ERROR: SQL] %s\n", sql_err);

        goto CLEANUP;

        return 3;
    }

    if (process_revisions(repo_fd, repo) != 0)
    {
        fprintf(stderr, "[ERROR] Processing failed. Aborting\n");
    }

CLEANUP:

    if (QUIET == 0)
    {
        fprintf(stdout, "[INFO] Cleaning up memory...\n");
    }

    sqlite3_free(sql_err);
    sqlite3_close(db);

    git_repository_free(repo);

    // Clean up libgit2 global state (not strictly necessary)
    git_libgit2_shutdown();

    close(repo_fd);

    mem_free(&MEM);

    return 0;
}
