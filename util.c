#include "util.h"
#include "md5.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>

/*
 * Adds the value of direction to the reference counter of the file
 * with the given hash.
 * This is required to track how many files refer to the same
 * content.
 */
int updateReferenceCount(const char* hash, direction_e direction)
{
	char path[128];
	snprintf(path, 128, "/home/osp-user/.CONTAINER/count/%s", hash);

	/* open the file containing the reference count for this hash 
	 * (and create it if it does not exist)
	 */
	FILE* file;
	if(access(path, F_OK) != -1) 
	{
		// exists
		file = fopen(path, "r+");
	} 
	else
	{
		// exists not
    	file = fopen(path, "w+");
	}

	if(file == NULL)
	{
		return -1;
	}

	/* read the counter */
	int count;
	size_t n = fread(&count, sizeof(int), 1, file);

	/* update the counter */
	if(n == 1)
	{
		count = (direction == DIR_INCREMENT) ? (count + 1) : (count - 1);
	}
	else 
	{
		count = 1;
	}

	/* write the counter back */
	fseek(file, 0, SEEK_SET);
	n = fwrite(&count, sizeof(int), 1, file);

	if(n != 1)
	{
		count = -1;
	}

	fclose(file);

	return count;
}

/**
 * This function adds a prefix to the path to avoid
 * fuse mapping the whole root filesystem again
 */
char* magicPath(const char* path)
{
	char* newPath = malloc((strlen(path) * sizeof(char)) + 23);

	strcpy(newPath, "/home/osp-user/dedupFS");
	strcat(newPath, path);

	return newPath;
}

/**
 * copy the file from srcPath to a file at destPath
 */
void copyFile(char* srcPath, char* destPath)
{
	/* Let the shell handle this, much easier */
	pid_t id;
	if((id = fork()) == 0)
	{
		execl("/bin/cp", "-pf", srcPath, destPath, (char *)0);
	}
	{
		waitpid(id, NULL, 0);
	}
}

/**
 * Calculate the md5 hash in hash of the file pointed to by
 * filename (a path).
 * Check md5.h and md5.c for legal information and licence
 * and stuff.
 */
int md5hash(const char* filename, unsigned char* hash)
{
	MD5_CTX ctx;
	char buffer[512];

	/* initalize the md5 process */
	MD5_Init(&ctx);

	/* open the file */
	int fd = open(filename, O_RDWR);

	if(fd < 0)
		return -errno;

	/* md5 processes data in blocks of 512, so read 512 blocks */
	ssize_t bytes = read(fd, buffer, 512);
	while(bytes > 0)
	{
		MD5_Update(&ctx, buffer, bytes);
		bytes = read(fd, buffer, 512);
	}

	/* finish it up */
	MD5_Final(hash, &ctx);

	close(fd);

	return 0;
}