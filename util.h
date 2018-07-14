#ifndef _UTIL_H_
#define _UTIL_H_

typedef enum { DIR_INCREMENT, DIR_DECREMENT } direction_e;

int updateReferenceCount(const char* hash, direction_e direction);
char* magicPath(const char* path);
void copyFile(char* srcPath, char* destPath);
int md5hash(const char* filename, unsigned char* hash);

#endif