/* Ref: https://www.tutorialspoint.com/sqlite/sqlite_c_cpp.htm */

#include <unistd.h>     // getopt

#include <stdio.h>
#include <errno.h>

#include <sys/stat.h>   // mkdir
#include <sys/types.h>  // mkdir

#include <git2.h>

#include <sqlite3.h>

/* ========================================================================== */

#define false 0
#define true 1

/* ========================================================================== */

enum OPTS
{
    // Must be powers of two
    QUIET = 0x1
};

/* ========================================================================== */

void print_usage()
{
    // TODO : Have make insert the binary name before compilation
    fprintf(stderr, "Usage: ./c9rev2git [-q] [-o output-dir] database.db\n");
}

void git2_exit_with_error(int error)
{
    // ref: https://libgit2.org/docs/guides/101-samples/
    const git_error *e = git_error_last();
    fprintf(stderr, "[Error %d/%d] %s\n", error, e->klass, e->message);
    exit(error);
}

/*
 * data      : Data provided in the 4th argument of `sqlite3_exec()`
 * col_cnt   : The number of columns in row
 * col_data  : An array of strings representing fields in the row
 * col_names : An array of strings representing column names
*/
static int
callback(void *data, int col_cnt, char **col_data, char **col_names)
{

    return 0;
}

int
main(int argc, char **argv)
{
    if (argc < 2)
    {
        print_usage();
        return 1;
    }

    int flags, opt;
    char *out_dir = "repo";

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
                out_dir = optarg;
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
    if (mkdir(out_dir, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) == -1)
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

    // Intitialise git2 library
    git_libgit2_init();

    // Set up git repo
    git_repository *repo = NULL;

    if ((flags & QUIET) == 0)
    {
        fprintf(stdout, "Initialising git repo...\n");
    }

    int res = git_repository_init(&repo, out_dir, false);
    if (res < 0)
    {
        git2_exit_with_error(res);
    }

    // TODO :
    //
    // - Get a list of all the stored filenames and their id's
    //
    // - For each filename:
    //   - Open new file for writing
    //   - Get a list of it's revision data
    //   - For each revision:
    //     ~ Add, Delete or Retain as necessary
    //     ~ Save the changes to disk
    //     ~ Run `git add`
    //     ~ Run `git commit` with basic message
    //

    // Query to select filenames
    char file_query[255] = "SELECT id, path, FROM Documents";

    // Query to select file revision data
    char rev_query[255];
    //sprintf(rev_query, "SELECT * FROM Revisions WHERE document_id = %s", doc_id);

    sqlite3_close(db);

    // Free repo initialsed by libgit2
    git_repository_free(repo);

    // Clean up libgit2 state (not strictly necessary)
    git_libgit2_shutdown();

    return 0;
}
