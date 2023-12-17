#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>

/* You need to change this macro to your TFS mount point*/
#define TESTDIR "/tmp/rr1268/mountdir"

#define N_FILES 100
#define BLOCKSIZE 4096
#define FSPATHLEN 256
#define ITERS 16
#define FILEPERM 0666
#define DIRPERM 0755

char buf[BLOCKSIZE];

int main(int argc, char **argv) {

	int i, fd = 0, ret = 0;
	struct stat st;

	if ((fd = creat(TESTDIR "/file", FILEPERM)) < 0) {
		perror("creat");
		printf("TEST 1: File create failure \n");
		exit(1);
	}
	printf("TEST 1: File create Success \n");


	/*Close operation*/	
	if (close(fd) < 0) {
		printf("TEST 3: File close failure \n");
	}
	printf("TEST 3: File close Success \n");

	printf("Benchmark completed \n");
	return 0;
}
