/* Ref: https://www.tutorialspoint.com/sqlite/sqlite_c_cpp.htm */

#include <unistd.h>     // getopt
#include <stdio.h>
#include <sqlite3.h>

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
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s [-q --quiet] [-v --verbsose] [-o output-dir] database.db\n", argv[0]);
        return 1;
    }

    char *filepath = argv[1];
    sqlite3 *db;

    int res = sqlite3_open(filepath, &db);

    if (res != SQLITE_OK)
    {
        fprintf(stderr, "Failed to open %s : %s\n", filepath, sqlite3_errmsg(db));
        return 2;
    }

    // TODO :
    //
    // - Create a working directory
    // - `git init` inside that directory
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
    return 0;
}
