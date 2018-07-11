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

#define DWRITE(str) {write(debugFD, str, strlen(str));write(debugFD, "\n", 1);}

static void updateReferenceCount(const char* hash, int direction)
{
	FILE* file = fopen("/home/osp-user/.CONTAINER/count", "rw");

	size_t length = 8;
	int i = 0;
	for (;;) 
	{
		char buffer[36];
    	size_t n = fread(buffer, 1, 36, file);

    	if (n < bufsize) 
    	{ 
    		break; 
    	}
    	else
    	{
    		if(strncmp(hash, buffer, 32) == 0)
    		{
    			int count = *(int*)(buffer + 32);

    			count += direction;

    			buffer[32] = (count >> 24) & 0xFF;
				buffer[33] = (count >> 16) & 0xFF;
				buffer[34] = (count >> 8) & 0xFF;
				buffer[35] = count & 0xFF;

				fseek(file, i * 36, SEEK_SET);
				fwrite(buffer, 1, 36, file);

				close(fd);

				return count;
    		}

    		i++;
    	}
	}

	fwrite(hash, 1, 32, file);
	int c = 0;
	fwrite(&c, 1, 4, file);

	close(fd);

	return 0;
}

static int debugFD;

/**@brief Calculates the hash value of a memory block relative to hash value of
 * the previous block.
 * @param key   pointer to the memory block
 * @param size  size of the memory block
 * @param hval  hash value of the previous memory block
 */
static uint64_t hash_cont(const void* key, size_t size, uint64_t hval)
{
	const char* ptr = key;
	
	while (size --> 0)
	{
		hval ^= *ptr;
		hval *= 0x100000001b3ull;
		++ptr;
	}
	
	return hval;
}

/**@brief Calculates the hash value of a first memory block.
 * @param key   pointer to the memory block
 * @param size  size of the memory block
 */
static inline uint64_t hash(const void* key, size_t size)
{
	return hash_cont(key, size, 0xcbf29ce484222325ull);
}

static void copyFile(char* srcPath, char* destPath)
{
	pid_t id;
	if((id = fork()) == 0)
	{
		execl("/bin/cp", "-pf", srcPath, destPath, (char *)0);
	}
	{
		waitpid(id, NULL, 0);
	}
}

static int md5hash(const char* filename, unsigned char* hash)
{
	MD5_CTX ctx;
	char buffer[512];

	MD5_Init(&ctx);

	int fd = open(filename, O_RDONLY);

	if(fd == -1)
		return -1;

	ssize_t bytes = read(fd, buffer, 512);
	while(bytes > 0)
	{
		MD5_Update(&ctx, buffer, bytes);
		bytes = read(fd, buffer, 512);
	}

	MD5_Final(hash, &ctx);

	close(fd);

	return 0;
}

 /**@brief Used to create directories.
 */
static int dedupMkdir(const char* path, mode_t mode)
{
	DWRITE("MKDIR");
	DWRITE(path);
	return (mkdir(path, mode) != 0) ? -errno : 0;
}

/**@brief Initializes the file system.
 */
static void* dedupInit(struct fuse_conn_info* conn, struct fuse_config* cfg)
{
	DWRITE("INIT");

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

	DWRITE("Creating /home/osp-user/.CONTAINER");

	mkdir("/home/osp-user/.CONTAINER", 0777);

	return NULL;
}

/**@brief Used to retrieve file attributes.
 */
static int dedupGetAttr(const char* path, struct stat* stbuf, struct fuse_file_info* fi)
{
	int rc = lstat(path, stbuf);
	
	return (rc != 0) ? -errno : 0;
}

/**@brief Used to retrieve directory entries.
 */
static int dedupReadDir(const char* path, void* buf, fuse_fill_dir_t filler, 
	off_t offset, struct fuse_file_info* fi, enum fuse_readdir_flags flags)
{
	DWRITE("READDIR");
	DWRITE(path);

	DIR* dir;
	struct dirent* entry;
	
	if ((dir = opendir(path)) == NULL)
		return -errno;
	
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
			break;
	}
	
	closedir(dir);
	return 0;
}

/**@brief Used to create files.
 */
static int dedupCreate(const char* path, mode_t mode,
                       struct fuse_file_info* fi)
{
	DWRITE("CREATE");
	DWRITE(path);

	int fd;
	
	fd = open(path, fi->flags, mode);
	if (fd != 0)
		return -errno;
	
	fi->fh = fd;
	return 0;
}

/**@brief Used to open files.
 */
static int dedupOpen(const char* path, struct fuse_file_info* fi)
{
	DWRITE("OPEN");
	DWRITE(path);

	if(fi) 
	{
		if(fi->fh) 
		{
			close(fi->fh);
		}
	}

	int fd = open(path, O_CREAT | O_WRONLY, 0644);
	
	if (fd < 0)
	{
		if(errno != EEXIST)
		{
			DWRITE(strerror(errno));
			return -errno;
		}
	}

	close(fd);
	
	return 0;
}

/**@brief Used to read file contents.
 */
static int dedupRead(const char* path, char* buf, size_t size, off_t offset,
                     struct fuse_file_info* fi)
{
	DWRITE("READ");
	DWRITE(path);

	char hashBuf[33];
	hashBuf[32] = 0x00;

	int fd = open(path, O_RDONLY);
	
	if (fd < 0)
		return -errno;

	int res = pread(fd, hashBuf, 32, 0);

	if (res == -1)
		res = -errno;

	close(fd);

	char name[128];
	snprintf(name, 128, "/home/osp-user/.CONTAINER/%s", hashBuf);

	fd = open(name, O_RDONLY);

	res = pread(fd, buf, size, offset);

	close(fd);

	return res;
}

/**@brief Used to write file contents.
 */
static int dedupWrite(const char* path, const char* buf, size_t size,
                      off_t offset, struct fuse_file_info* fi)
{
	DWRITE("WRITE");
	DWRITE(path);

	(void) fi;
	int res = 0;
	int fileFD = open(path, O_RDONLY);
	
	if (fileFD < 0)
		res = -errno;

	char hashBuf[33];
	hashBuf[32] = 0x00;
	res = pread(fileFD, hashBuf, 32, 0);

	if (res == -1)
		res = -errno;

	close(fileFD);

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
		updateReferenceCount(hashBuf, -1);
	}

	int hashFD = open("/home/osp-user/.CONTAINER/temp", O_RDWR);

	res = pwrite(hashFD, buf, size, offset);

	if(res == -1)
		res = -errno;

	close(hashFD);

	unsigned char *hash = calloc(32, sizeof(unsigned char));
	md5hash("/home/osp-user/.CONTAINER/temp", hash);
	char hashStr[33];
	hashStr[32] = 0x00;
	for(int i = 0; i < 16; i++)
	{
		if(hash[i] < 0x10)
			snprintf(hashStr+(i * 2), 32, "0%x", hash[i]);
		else
			snprintf(hashStr+(i * 2), 32, "%x", hash[i]);
	}

	char newName[128];
	snprintf(newName, 128, "/home/osp-user/.CONTAINER/%s", hashStr);

	DWRITE(newName);

	fileFD = open(path, O_WRONLY | O_TRUNC);

	if(fileFD < 0)
	{
		res = -errno;
	}

	res = pwrite(fileFD, hashStr, 32, 0);

	if(res == -1)
		res = -errno;

	close(fileFD);

	rename("/home/osp-user/.CONTAINER/temp", newName);
	updateReferenceCount(hashStr, 1);

	free(oldName);

	return res;
}

static int dedupUnlink(const char* path)
{
	DWRITE("UNLINK");
	DWRITE(path);

	char hashBuf[33];
	hashBuf[32] = 0x00;

	int fd = open(path, O_RDONLY);
	
	if (fd < 0)
		return -errno;

	int res = pread(fd, hashBuf, 32, 0);

	if (res == -1)
		res = -errno;

	close(fd);

	int count = updateReferenceCount(hashBuf, -1);

	if(count == 0)
	{
		// delete from .container
	}

	return (unlink(path) != 0) ? -errno : 0;
}

static int dedupRmdir(const char* path)
{
	DWRITE("RMDIR");
	DWRITE(path);
	return (rmdir(path) != 0) ? -errno : 0;
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

	if ((debugFD = open("error.log", O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0)
	{
		perror("error file");
		return -1;
	}
	
	return fuse_main(argc, argv, &dedupOper, &fd);
}
