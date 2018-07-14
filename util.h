#ifndef _UTIL_H_
#define _UTIL_H_

#define CONTAINER_PATH "/home/osp-user/.dedupFS/"
#define COUNT_PATH CONTAINER_PATH"count/"
#define DEDUP_PATH CONTAINER_PATH"user"

typedef enum { DIR_INCREMENT, DIR_DECREMENT } direction_e;

int updateReferenceCount(const char* hash, direction_e direction);
char* magicPath(const char* path);
void copyFile(char* srcPath, char* destPath);
int md5hash(const char* filename, unsigned char* hash);

#endif