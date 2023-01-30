#include "kernel/syscall.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
  int p[2];
  if (pipe(p) < 0) {
    printf("make pipe failed");
    exit(1);
  }
  int w_fd = p[1];
  int r_fd = p[0];

  if (fork() > 0) {
    // parent
    char tmp;
    int pid = getpid();
    write(w_fd, "0", 1);
    read(r_fd, &tmp, 1);
    printf("%d: received pong\n", pid);
  } else {
    // child
    char tmp;
    int pid = getpid();
    read(r_fd, &tmp, 1);
    printf("%d: received ping\n", pid);
    write(w_fd, "0", 1);
  }
  close(w_fd);
  close(r_fd);

  exit(0);
}
