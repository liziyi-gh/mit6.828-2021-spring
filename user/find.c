#include "kernel/syscall.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


int main(int argc, char *argv[]) {

  if (argc != 3) {
    printf("error argc\n");
    exit(0);
  }

  exit(0);
}
