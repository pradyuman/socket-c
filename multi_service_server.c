/* Generic */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Network */
#include <netdb.h>
#include <sys/socket.h>

#define BUF_SIZE 100
#define PING_SIZE 68

// Get addr information (used to bindListener)
struct addrinfo *getAddrInfo(char* port, int socktype) {
  int r;
  struct addrinfo hints, *getaddrinfo_res;

  // Setup hints
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_flags = AI_PASSIVE;
  hints.ai_socktype = socktype;
  if ((r = getaddrinfo(NULL, port, &hints, &getaddrinfo_res))) {
    fprintf(stderr, "[getAddrInfo:21:getaddrinfo] %s\n", gai_strerror(r));
    return NULL;
  }

  return getaddrinfo_res;
}

// Bind Listener
int bindListener(struct addrinfo *info) {
  if (info == NULL) return -1;

  int serverfd;
  for (;info != NULL; info = info->ai_next) {
    if ((serverfd = socket(info->ai_family,
                           info->ai_socktype,
                           info->ai_protocol)) < 0) {
      perror("[bindListener:35:socket]");
      continue;
    }

    int opt = 1;
    if (setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(int)) < 0) {
      perror("[bindListener:43:setsockopt]");
      return -1;
    }

    if (bind(serverfd, info->ai_addr, info->ai_addrlen) < 0) {
      close(serverfd);
      perror("[bindListener:49:bind]");
      continue;
    }

    freeaddrinfo(info);
    return serverfd;
  }

  freeaddrinfo(info);
  return -1;
}

void header(int handler, int status) {
  char header[1000] = {0};
  if (status == 0) {
    sprintf(header, "HTTP/1.0 200 OK\r\n\r\n");
  } else if (status == 1) {
    sprintf(header, "HTTP/1.0 403 Forbidden\r\n\r\n");
  } else {
    sprintf(header, "HTTP/1.0 404 Not Found\r\n\r\n");
  }
  send(handler, header, strlen(header), 0);
}

void resolve(int handler) {
  int status = 0;
  char buf[BUF_SIZE];
  char *method;
  char *filename;

  recv(handler, buf, BUF_SIZE, 0);
  method = strtok(buf, " ");
  if (strcmp(method, "GET") != 0) return;

  filename = strtok(NULL, " ");
  if (filename[0] == '/') filename++;
  if (access(filename, F_OK) != 0) {
    header(handler, 2);
    return;
  } else if (access(filename, R_OK) != 0){
    header(handler, 1);
    return;
  } else {
    header(handler, 0);
  }

  FILE *file = fopen(filename, "r");
  while(fgets(buf, BUF_SIZE, file)) {
    send(handler, buf, strlen(buf), 0);
    memset(buf, 0, BUF_SIZE);
  }
}

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "USAGE: ./multi_service_server <tcp port> <udp port>\n");
    return 1;
  }

  int tcpServer = bindListener(getAddrInfo(argv[1], SOCK_STREAM));
  if (tcpServer < 0) {
    fprintf(stderr, "[main:114:bindListener] Failed to bind at port %s (TCP)\n", argv[1]);
    return 2;
  }

  if (listen(tcpServer, 10) < 0) {
    perror("[main:82:listen]");
    return 3;
  }

  int udpServer = bindListener(getAddrInfo(argv[2], SOCK_DGRAM));
  if (udpServer < 0) {
    fprintf(stderr, "[main:125:bindListener] Failed to bind at port %s (UDP)\n", argv[2]);
    return 4;
  }

  signal(SIGCHLD, SIG_IGN);

  int handler, udpsize;
  fd_set sockset;
  socklen_t size;
  struct sockaddr_storage tcpClient, udpClient;
  while (1) {
    FD_ZERO(&sockset);
    FD_SET(tcpServer, &sockset);
    FD_SET(udpServer, &sockset);

    select(tcpServer > udpServer ? tcpServer + 1 : udpServer + 1,
           &sockset, NULL, NULL, NULL);
    if (FD_ISSET(udpServer, &sockset)) {
      char buf[PING_SIZE] = {0};
      size = sizeof(udpClient);
      udpsize = recvfrom(udpServer, buf, sizeof(buf), 0,
                         (struct sockaddr *)&udpClient, &size);
      *((uint32_t *)buf) = htonl(ntohl(*((uint32_t *)buf)) + 1);
      sendto(udpServer, buf, udpsize, 0,
             (struct sockaddr *)&udpClient, size);
    } else if (FD_ISSET(tcpServer, &sockset)) {
      size = sizeof(tcpClient);
      handler = accept(tcpServer, (struct sockaddr *)&tcpClient, &size);
      if (handler < 0) {
        perror("[main:82:accept]");
        continue;
      }

      // handle async
      switch (fork()) {
      case -1:
        perror("[main:88:fork]");
        break;
      case 0:
        close(tcpServer);
        resolve(handler);
        close(handler);
        exit(0);
      default:
        close(handler);
      }
    }
  }

  close(tcpServer);
  return 0;
}
