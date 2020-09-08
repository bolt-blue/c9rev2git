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
 * Process each file revision
 *   - Update the file
 *   - Create a new `git commit`
 */
static int
process_revision_cb(void *unused, int col_cnt, char **col_data, char **col_names)
{
    // TODO :
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

    return 0;
}

/*
 * Process each target file
 *   - Create any directory tree as required
 *   - Create a new file
 *   - Process the revisions
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
*/
static int
process_target_cb(void *repo_fd, int col_cnt, char **col_data, char **col_names)
{
    char *doc_id = col_data[0];
    char *path   = col_data[1];

    // Assuming 512 is long enough to account for most reasonable directory tree depth
    char dir_path[512];

    //   - Process 'path' and separate any directories from filename
    int i = 0, dir_len = 0, filename_len = 0;

    for (; path[i] != '\0'; i++)
    {
        // Create any necessary directories as we find them
        if (path[i] == '/')
        {
            strncpy(dir_path, path, i);

            // Always remember the null terminator
            dir_path[i] = '\0';

            // Update total directory path length
            dir_len = i;

            if (mkdirat(*(int *)repo_fd, dir_path, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) == -1)
            {
                if (errno == EEXIST)
                {
                    // Don't worry if the directory already exists
                    continue;
                }
                else
                {
                    // Trigger a sqlite abort
                    fprintf(stderr, "[Error] Failed to create directory '%s'. Aborting...\n", dir_path);

                    // TODO : Implement "clean up" on failure, in main(), and remove this
                    fprintf(stderr, "[WARNING] This may leave file and/or directory artefacts.\n");

                    return 1;
                }
            }
        }
    }

    // This will inherently account for the null terminator
    filename_len = i - dir_len;

    // Remember to account for null terminator
    char filename[filename_len + 1];
    strncpy(filename, path + dir_len + 1, filename_len);

    // Query to select file revision data
    char rev_query[255];
    sprintf(rev_query, "SELECT * FROM Revisions WHERE document_id = %s", doc_id);

    printf("[DEBUG] dir: %-40s | filename: %-30s | doc_id: %5s\n", dir_path, filename, doc_id);

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
int
main(int argc, char **argv)
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

    char *sql_err = NULL;

    // Extract target filenames from database
    // Query to select filenames and id's
    char *file_query = "SELECT id, path FROM Documents";

    res = sqlite3_exec(db, file_query, process_target_cb, &repo_fd, &sql_err);
    if (res != SQLITE_OK)
    {
        fprintf(stderr, "Failed to retrieve target filenames from database\n");
        fprintf(stderr, "[Error: SQL] %s\n", sql_err);

        sqlite3_free(sql_err);
        sqlite3_close(db);

        return 3;
    }

    // Clean up

    sqlite3_close(db);

    // Free repo initialsed by libgit2
    git_repository_free(repo);

    // Clean up libgit2 state (not strictly necessary)
    git_libgit2_shutdown();

    close(repo_fd);

    return 0;
}
