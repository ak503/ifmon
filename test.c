#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>

//char *socket_path = "./socket";
char *socket_path = "/tmp/t.s";

char msg1[] = "Hello there!\n";

int main(int argc, char *argv[]) {
  struct sockaddr_un addr;
  char buf[100];
  int fd,cl,rc;

  if (argc > 1) socket_path=argv[1];

  if ( (fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
    perror("socket error");
    exit(-1);
  }

  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  if (*socket_path == '\0') {
    *addr.sun_path = '\0';
    strncpy(addr.sun_path+1, socket_path+1, sizeof(addr.sun_path)-2);
  } else {
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path)-1);
    unlink(socket_path);
  }

  if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
    perror("bind error");
    exit(-1);
  }

  if (listen(fd, 5) == -1) {
    perror("listen error");
    exit(-1);
  }

  while (1) {
    if ( (cl = accept(fd, NULL, NULL)) == -1) {
      perror("accept error");
      continue;
    }

    while ( (rc=write(cl,msg1,sizeof(msg1))) > 0) {
      printf("write %u bytes: %.*s\n", rc, rc, msg1);
    }
    if (rc == -1) {
      perror("write");
      exit(-1);
    }
    else if (rc == 0) {
      printf("EOF\n");
      close(cl);
    }
  }


  return 0;
}
