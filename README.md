# RU File System (RUFS)

## Description

RU File System (RUFS) is a user-level file system built using the FUSE library. It provides a file abstraction layer, organizing data and indexing information on a virtual disk. This project includes detailed testcases to help with debugging and development. RUFS emulates various OS-level mechanisms in C, focusing on file system logic and organization without dealing with the complexities of operating a physical disk.

## Features

- **User-level file system:** Built with the FUSE library.
- **Flat file storage:** Emulates a working disk by storing all data in a single flat file.
- **Basic file operations:** Includes create, read, write, and delete operations.
- **Directory structure:** Supports nested directories and file lookup.
- **Bitmap management:** Efficiently manages available inodes and data blocks.
- **Inode management:** Handles file and directory metadata.

## Installation

To install the FUSE driver on your system, use the following commands:

```sh
sudo apt-get update
sudo apt-get install libfuse-dev
```
Clone the repository and navigate to the project directory:

```sh
git clone https://github.com/yourusername/rufs.git
cd rufs
```
Usage
Running RUFS
Compile the RUFS code and the benchmark tests using the provided Makefile:

```sh
make
make -C benchmark
```
Mount the file system using FUSE:

```sh
./rufs <mount_point>
```
## Example Commands
After mounting RUFS, you can use standard file system commands:

```sh
# Create a directory
mkdir <mount_point>/testdir

# Create a file
touch <mount_point>/testfile.txt

# List directory contents
ls <mount_point>

# Write to a file
echo "Hello, RUFS!" > <mount_point>/testfile.txt

# Read from a file
cat <mount_point>/testfile.txt
```

## Project Structure
- code/block.c: Basic block operations, acts as a disk driver reading blocks from disk.
- code/block.h: Block layer headers, configures block size.
- code/rufs.c: User-facing file system operations.
- code/rufs.h: Contains inode, superblock, and dirent structures.
- code/benchmark/simple_test.c: Simple test case for initial development.
- code/benchmark/test_cases.c: More comprehensive test cases.
- Makefile: For compiling RUFS and benchmark code.
