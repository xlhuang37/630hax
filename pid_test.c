#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("Please pass an integer argument\n");
    exit(-1);
  }
  int new_pid = atoi(argv[1]);

  printf("Setting pid to %d\n", new_pid);

  printf("My initial pid is %d\n", getpid());

  int fd = open("/proc/my_piddo", O_WRONLY);
  if (fd < 0) {
    printf("fd didn't open - %d\n", fd);
    exit(-1);
  } else {

    dprintf(fd, "%d\n", new_pid);
    close(fd);
  }

  printf("My new pid is %d\n", getpid());

}
