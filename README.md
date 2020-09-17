# Convert c9 Revisions to Git Repo

## Description
A simple app that converts document revisions stored in a 'c9' (or other compatible database)
into a git repository.<br>
'c9' here refers to the cloud9 IDE; see [here](https://github.com/c9) for source reference.

## Build Dependencies
Both of these will likely already be available as packages on your system.
- [libgit2](https://github.com/libgit2/libgit2)
- [sqlite3](https://sqlite.org/src/doc/trunk/README.md)

### Note
If you choose to use this directly from the IDE, `sqlite3.h` is already available.
To get `libgit2` (at time of writing), you can run:
```sh
#> apt-get install libgit2-26
OR
#> apt-get install libgit2-dev
```

## Clone and Compile
This can be done on a machine of your choice, or just on the 'c9' IDE.
Open a terminal and:
```sh
$> cd [to a directory of your choice]
$> git clone https://github.com/bolt-blue/c9rev2git.git
$> make
```

## Preparation
If working "locally", then first complete the following via the 'c9' IDE:

- There is a hidden directory with a lot of files, but likely only one that appears
  to be named as a SHA-1 hash (or at least some kind of hash).
  - Click the 'Settings' button in the file-tree view
  - Select 'Show Hidden Files'
  - Open '.c9', followed by the "hash-named" directory
  - Right click `collab.v3.db` and 'Save' to your system

- Alternatively, click 'File > Download Project' to create a full, local copy.
  - Then simply copy the target `collab.v3.db` to a working directory of your choice

## Usage
This assumes you are running from the build directory.
Provisions for actual installation on your system have not been considered here.

`$> ./c9rev2git [-q] [-o output-dir] database.db`
- `-q` Suppress informational output
- `-o` The name of the directory where the repo shall be created

## Feature Todo
- Allow selective conversion (group several revisions into one `commit`)
- [Suggestions?]
