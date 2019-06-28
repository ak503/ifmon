    #include <sys/types.h>

    #include <sys/socket.h>

    #include <stdio.h>

    #include <netinet/in.h>

    #include <sys/time.h>

    #include <sys/ioctl.h>

    #include <unistd.h>

    #include <stdlib.h>


#include <sys/un.h>
#define SOCKFILE "/var/run/ifmon.socket"

    int main() {

     int server_sockfd, client_sockfd;

     int server_len, client_len;

     struct sockaddr_un server_address;

     struct sockaddr_un client_address;

     int result;

     fd_set readfds, testfds;
     //server_sockfd = socket(AF_INET, SOCK_STREAM, 0);

server_sockfd = socket(AF_UNIX, SOCK_STREAM, 0);

//     server_address.sin_family = AF_INET;
server_address.sun_family = AF_UNIX;

 //    server_address.sin_addr.s_addr = htonl(INADDR_ANY);

//
strncpy(server_address.sun_path, SOCKFILE, sizeof(SOCKFILE)-1);
	server_address.sun_path[sizeof(SOCKFILE)-1] = '\0';
	unlink(SOCKFILE);
  //   server_address.sin_port = htons(9734);

     server_len = sizeof(server_address);

     bind(server_sockfd, (struct sockaddr *)&server_address, server_len);
     listen(server_sockfd, 5);

     FD_ZERO(&readfds);

     FD_SET(server_sockfd, &readfds);
     while(1) {

      char ch;
char buf[1024];
      int fd;

      int nread;

      testfds = readfds;

      printf("server waiting\n");

      result = select(FD_SETSIZE, &testfds, (fd_set *)0,

       (fd_set *)0, (struct timeval *)0);

      if (result < 1) {

       perror("server5");

       exit(1);

      }
      for (fd = 0; fd < FD_SETSIZE; fd++) {

       if (FD_ISSET(fd, &testfds)) {
      if (fd == server_sockfd) {

         client_len = sizeof(client_address);

         client_sockfd = accept(server_sockfd,

          (struct sockaddr*)&client_address, &client_len);

         FD_SET(client_sockfd, &readfds);

         printf("adding client on fd %d\n", client_sockfd);

        }
       else {

       int  bytes_read = recv(fd, buf, 1024, 0);
	//printf("a %d\n", a);
	//printf("nread %d\n", nread);
	sleep(1);
        

          //printf("bytes_read client on fd %d %d\n",fd, bytes_read);
	 if(bytes_read <= 0)
                {
		printf("remove client on fd %d\n", client_sockfd);
                    // Соединение разорвано, удаляем сокет из множества
                    close(fd);
			FD_CLR(fd, &readfds);
                   // clients.erase(*it);
                    continue;
                }

         

        }

       }

      }

     }

    }
