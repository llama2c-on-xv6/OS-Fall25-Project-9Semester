#include "kernel/types.h"
#include <stddef.h>
#include <stdarg.h>

#ifdef LAB_MMAP

typedef long int off_t;
#endif

typedef unsigned long size_t;

#define SBRK_ERROR ((char *)-1)

struct stat;


#define stdout 1
#define stderr 2




// system calls
int fork(void);
int exit(int) __attribute__((noreturn));
int wait(int*);
int pipe(int*);
int write(int, const void*, int);
int read(int, void*, int);
int close(int);
int kill(int);
int exec(const char*, char**);
int open(const char*, int);
int mknod(const char*, short, short);
int unlink(const char*);
int fstat(int fd, struct stat*);
int link(const char*, const char*);
int mkdir(const char*);
int chdir(const char*);
int dup(int);
int getpid(void);
int pause(int);
int uptime(void);
#ifdef LAB_NET
int bind(uint16);
int unbind(uint16);
int send(uint16, uint32, uint16, char *, uint32);
int recv(uint16, uint32*, uint16*, char *, uint32);
#endif
#ifdef LAB_PGTBL
int ugetpid(void);
uint64 pgpte(void*);
void kpgtbl(void);
#endif
uint64 rdtime(void); //Rdtime

// ulib.c
int stat(const char*, struct stat*);
//char* strcpy(char*, const char*);
void *memmove(void*, const void*, int);
char* strchr(const char*, char c);

char* gets(char*, int max);
//uint strlen(const char*);
//void* memset(void*, int, uint);
int atoi(const char*);
int memcmp(const void *, const void *, uint);
//void *memcpy(void *, const void *, uint);



uint64 sys_sbrk(int n, int t);
char* sbrk(int);
char* sbrklazy(int);
#ifdef LAB_LOCK
int statistics(void*, int);
#endif



// string / memory
void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);
int strcmp(const char *, const char *);
size_t strlen(const char *);
char *strcpy(char *, const char *);
char *strchr(const char *, char);

// character
int isprint(int);
int isspace(int);

// formatting
int printf(const char *, ...);
int fprintf(int, const char *, ...);
int fdprintf(int, const char *, ...);
int sprintf(char *, const char *, ...);
int vsnprintf(char *, size_t, const char *, __builtin_va_list);

// scanning
int sscanf(const char *, const char *, ...);
int vsscanf(const char *, const char *, __builtin_va_list);

// umalloc.c
void* malloc(uint);
void free(void*);
