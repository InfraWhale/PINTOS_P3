#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
extern struct lock filesys_lock;

void check_address(void *addr);
void halt(void);
void exit(int status);
int fork(const char *thread_name, struct intr_frame *f);
int wait(int pid);
void close(int fd);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);

#endif /* userprog/syscall.h */