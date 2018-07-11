#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>

#include "md5.h"

#define FUSE_USE_VERSION 31
#include <fuse.h>

#define DWRITE(str) {write(debugFD, str, strlen(str));write(debugFD, "\n", 1);}

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

// static int md5hash(const char* filename, unsigned char* hash)
// {
// 	MD5_CTX ctx;
// 	char buffer[512];

// 	MD5_Init(&ctx);

// 	int fd = open(filename, O_RDONLY);

// 	if(fd == -1)
// 		return -1;

// 	ssize_t bytes = read(fd, buffer, 512);
// 	while(bytes > 0)
// 	{
// 		MD5_Update(&ctx, buffer, bytes);
// 		bytes = read(fd, buffer, 512);
// 	}

// 	MD5_Final(hash, &ctx);

// 	close(fd);

// 	return 0;
// }

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

	// DWRITE("Creating /home/osp-user/.CONTAINER");

	// mkdir("/home/osp-user/.CONTAINER", 0777);

	return NULL;
}

/**@brief Used to retrieve file attributes.
 */
// static int dedupGetAttr(const char* path, struct stat* stbuf, struct fuse_file_info* fi)
// {
// 	int rc;
	
// 	// if (fi != NULL)
// 	// 	rc = fstat(fi->fh, stbuf);
// 	// else
// 		rc = lstat(path, stbuf);
	
// 	return (rc != 0) ? -errno : 0;
// }

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
	int fd;

	DWRITE("OPEN");
	DWRITE(path);
	
	int f = creat("/home/osp-user/.CONTAINER/1313", 0777);

	if(f < 0) 
	{
		DWRITE("1313 fail");
		DWRITE(strerror(errno));
	}
	else
	{ 
		close(f);
	}

	// fd = open(path, fi->flags);
	fd = open(path, O_RDWR | O_CREAT, 0777);
	
	if (fd != 0)
	{
		DWRITE(strerror(errno));
		return -errno;
	}
	
	fi->fh = fd;
	return 0;
}

static int xmp_chmod(const char *path, mode_t mode,
		     struct fuse_file_info *fi)
{
	(void) fi;
	int res;

	res = chmod(path, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_chown(const char *path, uid_t uid, gid_t gid,
		     struct fuse_file_info *fi)
{
	(void) fi;
	int res;

	res = lchown(path, uid, gid);
	if (res == -1)
		return -errno;

	return 0;
}

/**@brief Used to read file contents.
 */
static int dedupRead(const char* path, char* buf, size_t size, off_t offset,
                     struct fuse_file_info* fi)
{
	DWRITE("READ");
	DWRITE(path);

		int fd;
	int res;

	if(fi == NULL)
		fd = open(path, O_RDONLY);
	else
		fd = fi->fh;
	
	if (fd == -1)
		return -errno;

	res = pread(fd, buf, size, offset);
	if (res == -1)
		res = -errno;

	if(fi == NULL)
		close(fd);
	return res;

	// return (pread(fi->fh, buf, size, offset) != 0) ? -errno : 0;
}

/**@brief Used to write file contents.
 */
static int dedupWrite(const char* path, const char* buf, size_t size,
                      off_t offset, struct fuse_file_info* fi)
{
	DWRITE("WRITE");
	DWRITE(path);

	int fd;
	int res;

	(void) fi;
	if(fi == NULL)
		fd = open(path, O_WRONLY);
	else
		fd = fi->fh;
	
	if (fd == -1)
		return -errno;

	res = pwrite(fd, buf, size, offset);
	if (res == -1)
		res = -errno;

	if(fi == NULL)
		close(fd);
	return res;

	// unsigned char *hash = malloc(sizeof(unsigned char) * 33);
	// hash[33] = 0x00;

	// char hashPath[64];

	// // TODO: rebuild hashes

	// dedupCreate(path, 0777, fi);

	// int fd = open(path, O_WRONLY);
	// int error = (pwrite(fd, buf, size, offset) != 0) ? -errno : 0;

	// md5hash(path, hash);	

	// snprintf(hashPath, 64, "/home/osp-user/.CONTAINER/%s", hash);

	// creat(hashPath, 0777);

	// DWRITE(hashPath);

	// free(hash);

	// return error;
}

static int dedupUnlink(const char* path)
{
	DWRITE("UNLINK");
	DWRITE(path);
	return (unlink(path) != 0) ? -errno : 0;
}

static int dedupRmdir(const char* path)
{
	DWRITE("RMDIR");
	DWRITE(path);
	return (rmdir(path) != 0) ? -errno : 0;
}

/* from fuse passthrough example */
static int xmp_mknod(const char *path, mode_t mode, dev_t rdev)
{
	int res;

	/* On Linux this could just be 'mknod(path, mode, rdev)' but this
	   is more portable */
	if (S_ISREG(mode)) {
		res = open(path, O_CREAT | O_EXCL | O_WRONLY, mode);
		if (res >= 0)
			res = close(res);
	} else if (S_ISFIFO(mode))
		res = mkfifo(path, mode);
	else
		res = mknod(path, mode, rdev);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
	(void) fi;
	int res;

	res = lstat(path, stbuf);

	stbuf->st_uid = getuid();
	stbuf->st_gid = getgid();
	stbuf->st_atime = time( NULL );
	stbuf->st_mtime = time( NULL );

	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_access(const char *path, int mask)
{
	int res;

	res = access(path, mask);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_readlink(const char *path, char *buf, size_t size)
{
	int res;

	res = readlink(path, buf, size - 1);
	if (res == -1)
		return -errno;

	buf[res] = '\0';
	return 0;
}

static int xmp_release(const char *path, struct fuse_file_info *fi)
{
	(void) path;
	close(fi->fh);
	return 0;
}

/**@brief Maps the callback functions to FUSE operations.
 */
static const struct fuse_operations dedupOper = {
	.init           = dedupInit,
	//.getattr        = dedupGetAttr,
	.getattr        = xmp_getattr,
	.readdir        = dedupReadDir,
	.mkdir          = dedupMkdir,
	.create         = dedupCreate,
	.open           = dedupOpen,
	.read           = dedupRead,
	.write          = dedupWrite,
	.unlink			= dedupUnlink,
	.rmdir 			= dedupRmdir,
	.mknod = xmp_mknod,
	.chmod = xmp_chmod,
	.chown = xmp_chown,
	.access = xmp_access,
	.readlink = xmp_readlink,
	.release = xmp_release
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
