#include "kernel/syscall.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

struct pipe_struct {
  int r_fd;
  int w_fd;
};

void create(struct pipe_struct* parent_fds) {
  int read_success = 0;
  int first_time = 1;
  int prime = 0, base = 0;
  int p[2];
  struct pipe_struct new_fds;

  close(parent_fds->w_fd);
  if (pipe(p) < 0) {
    exit(1);
  }
  new_fds.r_fd = p[0];
  new_fds.w_fd = p[1];

  while (1) {
    read_success = read(parent_fds->r_fd, &prime, 4);
    if (read_success == 0) {
      close(parent_fds->r_fd);
      close(new_fds.w_fd);
      int status = 0;
      wait(&status);
      exit(0);
    }
    if (first_time == 1) {
      first_time = 0;
      base = prime;
      printf("prime %d\n", base);
      if (fork() > 0) {
        // parent
      } else {
        // child
        create(&new_fds);
      }
    } else {
      if (prime % base != 0) {
        write(new_fds.w_fd, &prime, 4);
      }
    }
  }
}

int main(int argc, char *argv[]) {
  int p[2];
  struct pipe_struct fds;

  if (pipe(p) < 0) {
    exit(1);
  }
  fds.r_fd = p[0];
  fds.w_fd = p[1];

  if (fork() > 0) {
    // parent
    close(fds.r_fd);
    for (int i=2; i <= 35; ++i) {
      write(fds.w_fd, &i, 4);
    }
    close(fds.w_fd);
    int status = 0;
    wait(&status);
  } else {
    // child
    create(&fds);
  }

  exit(0);
}
