#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <time.h>

#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include "util.h"

#define FUSE_USE_VERSION 31
#include <fuse.h>

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

	/* create some working directories for our file system */
	printf("Creating container\n");
	/* Here we store the data file */
	mkdir("/home/osp-user/.CONTAINER/", 777);

	/* Here we store how many times each data file is referenced */
	printf("Creating count\n");
	mkdir("/home/osp-user/.CONTAINER/count/", 777);

	/* Here we store the files containing the hashes ('userspace files') */
	printf("Creating dedupFS\n");
	mkdir("/home/osp-user/dedupFS/", 777);

	return NULL;
}

 /**@brief Used to create directories.
 */
static int dedupMkdir(const char* path, mode_t mode)
{
	char* nPath  = magicPath(path);

	printf("dedupMkdir(%s)\n", nPath);

	int res = (mkdir(nPath, mode) != 0) ? -errno : 0;

	free(nPath);

	return res;
}

/**@brief Used to retrieve file attributes.
 */
static int dedupGetAttr(const char* path, struct stat* stbuf, struct fuse_file_info* fi)
{
	char* nPath = magicPath(path);

	printf("dedupGetAttr(%s)\n", nPath);

	int rc = lstat(nPath, stbuf);
	
	free(nPath);
	
	return (rc != 0) ? -errno : 0;
}

/**@brief Used to retrieve directory entries.
 */
static int dedupReadDir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi, enum fuse_readdir_flags flags)
{
	char* newPath = magicPath(path);

	printf("dedupReadDir(%s)\n", newPath);

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

	printf("dedupCreate(%s)\n", nPath);

	/* check if the file exists already */
	if(access(nPath, F_OK) != -1) 
	{
		errno = EEXIST;

		/* return an error if yes */
		free(nPath);
		return -EEXIST;
	}
	else
	{
		/* else, create the file */
		int fd = creat(nPath, mode);

		if(fd < 0)
		{
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
	
	printf("dedupOpen(%s)\n", nPath);

	int fd = open(nPath, fi->flags);

	if (fd < 0)
	{
		free(nPath);
		return -errno;
	}

	close(fd);
	free(nPath);
	return 0;
}

/**@brief Used to read file contents.
 */
static int dedupRead(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi)
{
	char* nPath = magicPath(path);

	printf("dedupRead(%s)\n", nPath);

	char hashBuf[33];
	hashBuf[32] = 0x00;

	/* Read the hash from the userspace file. */
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

	/* if there was no hash read, we just return 0 
	 * to indicate that no data was read */
	if(res == 0)
	{
		free(nPath);
		return 0;
	}

	/* build the path to the data file in the container */
	char name[128];
	snprintf(name, 128, "/home/osp-user/.CONTAINER/%s", hashBuf);

	fd = open(name, O_RDWR);

	if(fd < 0)
	{
		free(nPath);
		return -errno;
	}

	/* and read the real data */
	res = pread(fd, buf, size, offset);

	close(fd);

	free(nPath);
	return res;
}

/**@brief Used to write file contents.
 */
static int dedupWrite(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi)
{
	char* nPath = magicPath(path);

	printf("dedupWrite(%s)\n", nPath);

	(void) fi;
	int res = 0;
	int fileFD = open(nPath, O_RDWR);
	
	/* open the file the user specified */
	if (fileFD < 0)
	{
		res = -errno;
	}

	/* and read the hash in there */
	char hashBuf[33];
	hashBuf[32] = 0x00;
	res = pread(fileFD, hashBuf, 32, 0);

	if (res == -1)
	{
		res = -errno;
	}

	close(fileFD);

	char* oldName = calloc(128, sizeof(char));

	if(res == 0)
	{
		/* if no data was read, we create a new temporary file */
		strcpy(oldName, "/home/osp-user/.CONTAINER/temp");
		int f = creat(oldName, 0644);
		close(f);
	}
	else
	{
		/* if the hash was read, we copy the file with the given hash into a temporary file */
		snprintf(oldName, 128, "/home/osp-user/.CONTAINER/%s", hashBuf);
		copyFile(oldName, "/home/osp-user/.CONTAINER/temp");

		updateReferenceCount(hashBuf, DIR_DECREMENT);
	}

	/* write to that temporary file */
	int hashFD = open("/home/osp-user/.CONTAINER/temp", O_RDWR);
	int resFinal = pwrite(hashFD, buf, size, offset);

	if(resFinal == -1)
	{
		resFinal = -errno;
	}

	close(hashFD);

	/* calculate the md5 hash of the new file */
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

	free(hash);

	char newName[128];
	snprintf(newName, 128, "/home/osp-user/.CONTAINER/%s", hashStr);

	/* save the new hash in the user specified file */
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

	/* and rename the temp file to the new hash */
	rename("/home/osp-user/.CONTAINER/temp", newName);
	/* finally, we update the reference counter */
	updateReferenceCount(hashStr, DIR_INCREMENT);

	free(oldName);
	free(nPath);
	return resFinal;
}

/**
 * Removes a file from the userspace and also removes 
 * the data in the container, if possible.
 */
static int dedupUnlink(const char* path)
{
	char hashBuf[33];
	hashBuf[32] = 0x00;

	char* nPath = magicPath(path);

	printf("dedupUnlink(%s)\n", nPath);

	/* load the hash from the userspace file */
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

	/* delete the userspace file */
	if(unlink(nPath) != 0)
	{
		free(nPath);
		return -errno;
	}

	/* if no hash was read because the file was empty we 
	 * dont have to delete anything from the container */
	if(res == 0)
	{
		free(nPath);
		return 0;
	}

	/* update the reference counter */
	int count = updateReferenceCount(hashBuf, DIR_DECREMENT);

	/* if no more files reference this hash(file) */
	if(count == 0)
	{
		/* we delete it and its counter-file */
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

/**
 * @brief Deletes a directory and its contents.
 */
static int dedupRmdir(const char* path)
{
	char* nPath = magicPath(path);

	printf("dedupRmdir(%s)\n", nPath);

	int res = (rmdir(nPath) != 0) ? -errno : 0;

	free(nPath);

	return res;
}

/**
 * @brief Change the last access time of the file.
 */
static int dedupUtimens(const char* path, const struct timespec tv[2], struct fuse_file_info* fi)
{
	(void)fi;
	char* nPath = magicPath(path);

	printf("dedupUtimens(%s)\n", nPath);

	int res = (utimensat(AT_FDCWD, nPath, tv, 0) == 0) ? 0 : -errno;

	free(nPath);

	return res;
}

/**
 * @brief Change the owner of the file.
 */
static int dedupChown(const char* path, uid_t uid, gid_t gid, struct fuse_file_info* fi)
{
	(void)fi;
	char* nPath = magicPath(path);
	
	printf("dedupChown(%s)\n", nPath);

	int res = (chown(nPath, uid, gid) == 0) ? 0 : -errno;

	free(nPath);

	return res;
}

/**
 * @brief Change the mode bits of the file.
 */
static int dedupChmod(const char* path, mode_t mode, struct fuse_file_info* fi)
{
	(void)fi;
	char* nPath = magicPath(path);

	printf("dedupChmod(%s)\n", nPath);

	int res = (chmod(nPath, mode) == 0) ? 0 : -errno;

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
	.utimens		= dedupUtimens,
	.chown 			= dedupChown,
	.chmod 			= dedupChmod
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
