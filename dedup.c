#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>

#define FUSE_USE_VERSION 31
#include <fuse.h>

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

/**@brief Initializes the file system.
 */
static void* dedupInit(struct fuse_conn_info* conn,
                       struct fuse_config* cfg)
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
	return NULL;
}

/**@brief Used to retrieve file attributes.
 */
static int dedupGetAttr(const char* path, struct stat* stbuf,
                        struct fuse_file_info* fi)
{
	int rc;
	
	if (fi != NULL)
		rc = fstat(fi->fh, stbuf);
	else
		rc = lstat(path, stbuf);
	
	return (rc != 0) ? -errno : 0;
}

/**@brief Used to retrieve directory entries.
 */
static int dedupReadDir(const char* path, void* buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info* fi,
                        enum fuse_readdir_flags flags)
{
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
		
		if (filler(buf, entry->d_name, &st, 0, 0))
			break;
	}
	
	closedir(dir);
	return 0;
}

/**@brief Used to create directories.
 */
static int dedupMkdir(const char* path, mode_t mode)
{
	return (mkdir(path, mode) != 0) ? -errno : 0;
}

/**@brief Used to create files.
 */
static int dedupCreate(const char* path, mode_t mode,
                       struct fuse_file_info* fi)
{
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
	int fd;
	
	fd = open(path, fi->flags);
	if (fd != 0)
		return -errno;
	
	fi->fh = fd;
	return 0;
}

/**@brief Used to read file contents.
 */
static int dedupRead(const char* path, char* buf, size_t size, off_t offset,
                     struct fuse_file_info* fi)
{
	return (pread(fi->fh, buf, size, offset) != 0) ? -errno : 0;
}

/**@brief Used to write file contents.
 */
static int dedupWrite(const char* path, const char* buf, size_t size,
                      off_t offset, struct fuse_file_info* fi)
{
	return (pwrite(fi->fh, buf, size, offset) != 0) ? -errno : 0;
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
