#define SBRK_ERROR ((char *)-1)

struct stat;

/**
 * Using <extern "C"> is crucial!
 *
 * This is due to C++'s compiler "mangle" C++ function names, while C compiler does not.
 * For example, if I call a system call from a C++ file and compiler with C++ compiler
 * (such as `printf`), the C++ compiler will assume that `printf` is a C++ function and
 * will mangle the name to something like `sajhfgbkadjsrghbk_printf()`, which will reside
 * in the compiled object file.
 *
 * Then the linker will try to find the definition of that mangled name, which obviously
 * doesn't exists, and thus causing the linker error: `undefined reference to `printf(char const*, ...)'`
 *
 * The solution to this problem is explicitly tell the C++ compiler to NOT mangle names of C functions,
 * such as the function calls, using `extern` keyword.
 * If the file being compiled is using C++ compiler, then extern "C" block will kick in.
 */
#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// system calls
int fork(void);
int exit(int) __attribute__((noreturn));
int wait(int *);
int pipe(int *);
int write(int, const void *, int);
int read(int, void *, int);
int close(int);
int kill(int);
int exec(const char *, char **);
int open(const char *, int);
int mknod(const char *, short, short);
int unlink(const char *);
int fstat(int fd, struct stat *);
int link(const char *, const char *);
int mkdir(const char *);
int chdir(const char *);
int dup(int);
int getpid(void);
char *sys_sbrk(int, int);
int pause(int);
int uptime(void);
int sleep(int);

// ulib.c
int stat(const char *, struct stat *);
char *strcpy(char *, const char *);
void *memmove(void *, const void *, int);
char *strchr(const char *, char c);
int strcmp(const char *, const char *);
char *gets(char *, int max);
uint strlen(const char *);
void *memset(void *, int, uint);
int atoi(const char *);
int memcmp(const void *, const void *, uint);
void *memcpy(void *, const void *, uint);
char *sbrk(int);
char *sbrklazy(int);

// printf.c
void fprintf(int, const char *, ...) __attribute__((format(printf, 2, 3)));
void printf(const char *, ...) __attribute__((format(printf, 1, 2)));

// umalloc.c
void *malloc(uint);
void free(void *);

#ifdef __cplusplus
}
#endif // __cplusplus
