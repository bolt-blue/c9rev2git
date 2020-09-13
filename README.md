# Convert c9 Revisions to Git Repo

## Description
A simple app that converts document revisions stored in a 'c9' (or other compatible database)
into a git repository.
'c9' here refers to the cloud9 IDE, see [here](https://github.com/c9) for src reference.

## Build Dependencies:
Both of these will likely already be available as packages on your system.
- [libgit2](https://github.com/libgit2/libgit2)
- [sqlite3](https://sqlite.org/src/doc/trunk/README.md)

## Clone and Compile
This can be done on a machine of your choice, or just on the 'c9' IDE.
Open a terminal and:
    $> cd [to a directory of your choice]
    $> git clone https://github.com/bolt-blue/cs50-fp.git
    $> make

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
- [suggestions?]
