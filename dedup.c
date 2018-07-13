#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/wait.h>

#include "md5.h"

#define FUSE_USE_VERSION 31
#include <fuse.h>

typedef enum { INC, DEC } direction_e;

/*
 * Adds the value of direction to the reference counter of the file
 * with the given hash.
 * This is required to track how many files refer to the same
 * content.
 */
static int updateReferenceCount(const char* hash, direction_e direction)
{
	char path[128];
	snprintf(path, 128, "/home/osp-user/.CONTAINER/count/%s", hash);

	FILE* file;
	if( access( path, F_OK ) != -1 ) 
	{
		// exists
		file = fopen(path, "r+");
	} 
	else
	{
		// exists not
    	file = fopen(path, "w+");
	}
	printf("hash: %s\n", path);
	if(file == NULL)
	{
		printf("hash: %s\n", path);
		printf("cant open count: %s\n", strerror(errno));

		return 0;
	}

	int count;
	size_t n = fread(&count, sizeof(int), 1, file);

	// printf("n: %zu\ncount old: %d\n", n, count);

	if(n == 1)
		count = (direction == INC) ? (count + 1) : (count - 1);
	else 
		count = 1;

	// printf("count new: %d\n", count);

	fseek(file, 0, SEEK_SET);
	fwrite(&count, sizeof(int), 1, file);

	fclose(file);

	return count;
}

static char* magicPath(const char* path)
{
	char* newPath = malloc((strlen(path) * sizeof(char)) + 23);

	strcpy(newPath, "/home/osp-user/dedupFS");
	strcat(newPath, path);

	return newPath;
}

/**
 * copy the file from srcPath to a file at destPath
 */
static void copyFile(char* srcPath, char* destPath)
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
static int md5hash(const char* filename, unsigned char* hash)
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

 /**@brief Used to create directories.
 */
static int dedupMkdir(const char* path, mode_t mode)
{
	char* nPath  = magicPath(path);
	int res = (mkdir(nPath, mode) != 0) ? -errno : 0;
	free(nPath);

	return res;
}

/**@brief Initializes the file system.
 */
static void* dedupInit(struct fuse_conn_info* conn, struct fuse_config* cfg)
{
	// enables logging via printf() into the log file
	struct fuse_context* ctx = fuse_get_context();
	int fd = *(int*) ctx->private_data;
	dup2(fd, 1);
	close(fd);
	setbuf(stdout, NULL);

	cfg->use_ino = 1;
	cfg->entry_timeout = 0;
	cfg->attr_timeout = 0;
	cfg->negative_timeout = 0;

	mkdir("/home/osp-user/.CONTAINER/", 0777);

	mkdir("/home/osp-user/.CONTAINER/count/", 0777);

	mkdir("/home/osp-user/dedupFS/", 0777);

	return NULL;
}

/**@brief Used to retrieve file attributes.
 */
static int dedupGetAttr(const char* path, struct stat* stbuf, struct fuse_file_info* fi)
{
	char* nPath = magicPath(path);

	int rc = lstat(nPath, stbuf);
	
	free(nPath);
	
	return (rc != 0) ? -errno : 0;
}

/**@brief Used to retrieve directory entries.
 */
static int dedupReadDir(const char* path, void* buf, fuse_fill_dir_t filler, 
	off_t offset, struct fuse_file_info* fi, enum fuse_readdir_flags flags)
{
	char* newPath = magicPath(path);

	printf("dedupReadDir in: %s\n", newPath);

	DIR* dir;
	struct dirent* entry;
	
	if ((dir = opendir(newPath)) == NULL)
	{
		return -errno;
	}

	free(newPath);
	
	seekdir(dir, offset);
	while ((entry = readdir(dir)) != NULL)
	{
		struct stat st = {
			.st_ino = entry->d_ino,
			.st_mode = entry->d_type << 12
		};

		st.st_uid = getuid();
		st.st_gid = getgid();

		if (filler(buf, entry->d_name, &st, 0, 0))
		{
			break;
		}
	}
	
	closedir(dir);
	return 0;
}

/**@brief Used to create files.
 */
static int dedupCreate(const char* path, mode_t mode, struct fuse_file_info* fi)
{
	char* nPath = magicPath(path);

	if(access(nPath, F_OK) != -1) 
	{
		errno = EEXIST;

		free(nPath);
		return -EEXIST;
	}
	else
	{
		int fd = creat(nPath, mode);

		if(fd < 0)
		{
			printf("dedupCreate: %s\n", strerror(errno));

			free(nPath);
			return -errno;
		}
		else
		{ 
			close(fd);

			free(nPath);
			return 0;
		}
	}
}

/**@brief Used to open files.
 */
static int dedupOpen(const char* path, struct fuse_file_info* fi)
{
	if(fi) 
	{
		if(fi->fh) 
		{
			close(fi->fh);
		}
	}

	char* nPath = magicPath(path);
	
	int fd = open(nPath, fi->flags);

	printf("dedupOpen: %s\n", nPath);

	if (fd < 0)
	{
		printf("dedupOpen: %s\n", strerror(errno));

		free(nPath);
		return -errno;
	}

	close(fd);
	
	free(nPath);
	return 0;
}

/**@brief Used to read file contents.
 */
static int dedupRead(const char* path, char* buf, size_t size, off_t offset,
                     struct fuse_file_info* fi)
{
	char* nPath = magicPath(path);

	char hashBuf[33];
	hashBuf[32] = 0x00;

	int fd = open(nPath, O_RDWR);
	
	if (fd < 0)
	{
		free(nPath);
		return -errno;
	}

	int res = pread(fd, hashBuf, 32, 0);

	if (res == -1)
	{
		res = -errno;
	}

	close(fd);

	char name[128];
	snprintf(name, 128, "/home/osp-user/.CONTAINER/%s", hashBuf);

	fd = open(name, O_RDWR);

	if(fd < 0)
	{
		free(nPath);
		return -errno;
	}

	res = pread(fd, buf, size, offset);

	close(fd);

	free(nPath);
	return res;
}

/**@brief Used to write file contents.
 */
static int dedupWrite(const char* path, const char* buf, size_t size,
                      off_t offset, struct fuse_file_info* fi)
{
	char* nPath = magicPath(path);

	(void) fi;
	int res = 0;
	int fileFD = open(nPath, O_RDWR);
	
	// printf("fileFD: %d\n", fileFD);

	if (fileFD < 0)
	{
		printf("1: %s\n", strerror(errno));
		res = -errno;
	}

	char hashBuf[33];
	hashBuf[32] = 0x00;
	res = pread(fileFD, hashBuf, 32, 0);

	if (res == -1)
	{
		printf("2: %s\n", strerror(errno));
		res = -errno;
	}

	close(fileFD);

	// printf("Reading file\n");

	char* oldName = calloc(128, sizeof(char));

	if(res == 0)
	{
		strcpy(oldName, "/home/osp-user/.CONTAINER/temp");
		int f = creat(oldName, 0644);
		close(f);
	}
	else
	{
		snprintf(oldName, 128, "/home/osp-user/.CONTAINER/%s", hashBuf);
		copyFile(oldName, "/home/osp-user/.CONTAINER/temp");
		updateReferenceCount(hashBuf, DEC);
	}

	int hashFD = open("/home/osp-user/.CONTAINER/temp", O_RDWR);

	int resFinal = pwrite(hashFD, buf, size, offset);

	if(resFinal == -1)
	{
		printf("3: %s\n", strerror(errno));
		resFinal = -errno;
	}

	close(hashFD);

	unsigned char *hash = calloc(32, sizeof(unsigned char));
	md5hash("/home/osp-user/.CONTAINER/temp", hash);
	char hashStr[33];
	hashStr[32] = 0x00;

	/* convert the 128 bit into an 32 byte ascii string */
	for(int i = 0; i < 16; i++)
	{
		if(hash[i] < 0x10)
		{
			/* add leading zeros to make sure every 
			 * hash string is exactly 32 characters */
			snprintf(hashStr + (i * 2), 32, "0%x", hash[i]);
		}
		else
		{
			snprintf(hashStr + (i * 2), 32, "%x", hash[i]);
		}
	}

	char newName[128];
	snprintf(newName, 128, "/home/osp-user/.CONTAINER/%s", hashStr);

	fileFD = open(nPath, O_RDWR);

	if(fileFD < 0)
	{
		printf("4: %s\n", strerror(errno));
		res = -errno;
	}

	res = pwrite(fileFD, hashStr, 32, 0);

	if(res == -1)
	{
		printf("5: %s\n", strerror(errno));
		res = -errno;
	}

	close(fileFD);

	rename("/home/osp-user/.CONTAINER/temp", newName);
	updateReferenceCount(hashStr, INC);

	free(oldName);

	free(nPath);
	return resFinal;
}

static int dedupUnlink(const char* path)
{
	char hashBuf[33];
	hashBuf[32] = 0x00;

	char* nPath = magicPath(path);

	int fd = open(nPath, O_RDWR);
	
	if (fd < 0)
	{
		free(nPath);
		return -errno;
	}

	int res = pread(fd, hashBuf, 32, 0);

	if (res == -1)
	{
		res = -errno;
	}

	close(fd);

	int count = updateReferenceCount(hashBuf, DEC);

	if(unlink(nPath) != 0)
	{
		free(nPath);
		return -errno;
	}

	if(count == 0)
	{
		char path2[128];
		snprintf(path2, 128, "/home/osp-user/.CONTAINER/%s", hashBuf);

		if(unlink(path2) != 0)
		{
			free(nPath);
			return -errno;
		}

		snprintf(path2, 128, "/home/osp-user/.CONTAINER/count/%s", hashBuf);
		
		if(unlink(path2) != 0)
		{
			free(nPath);
			return -errno;
		}
	}

	free(nPath);
	return 0;
}

static int dedupRmdir(const char* path)
{
	char* nPath = magicPath(path);
	int res = (rmdir(nPath) != 0) ? -errno : 0;
	free(nPath);
	return res;
}

/**@brief Maps the callback functions to FUSE operations.
 */
static const struct fuse_operations dedupOper = {
	.init           = dedupInit,
	.getattr        = dedupGetAttr,
	.readdir        = dedupReadDir,
	.mkdir          = dedupMkdir,
	.create         = dedupCreate,
	.open           = dedupOpen,
	.read           = dedupRead,
	.write          = dedupWrite,
	.unlink			= dedupUnlink,
	.rmdir 			= dedupRmdir,
};

int main(int argc, char* argv[])
{
	int fd;
	
	// create a log file for debugging purposes
	if ((fd = open("log.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0)
	{
		perror("Logfile");
		return -1;
	}
	
	return fuse_main(argc, argv, &dedupOper, &fd);
}
