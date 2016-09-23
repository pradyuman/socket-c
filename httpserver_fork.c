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

// Get addr information (used to bindListener)
struct addrinfo *getAddrInfo(char* port) {
  int r;
  struct addrinfo hints, *getaddrinfo_res;
  // Setup hints
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_flags = AI_PASSIVE;
  hints.ai_socktype = SOCK_STREAM;
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
  if (argc != 2) {
    fprintf(stderr, "USAGE: ./httpserver_fork <port>\n");
    return 1;
  }

  fd_set readfs;
  FD_ZERO(&readfs);

  // bind a listener
  int server = bindListener(getAddrInfo(argv[1]));
  if (server < 0) {
    fprintf(stderr, "[main:72:bindListener] Failed to bind at port %s\n", argv[1]);
    return 2;
  }

  if (listen(server, 10) < 0) {
    perror("[main:82:listen]");
    return 3;
  }

  // silently reap children
  signal(SIGCHLD, SIG_IGN);

  // accept incoming requests asynchronously
  int handler;
  socklen_t size;
  struct sockaddr_storage client;
  while (1) {
    size = sizeof(client);
    handler = accept(server, (struct sockaddr *)&client, &size);
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
      close(server);
      resolve(handler);
      close(handler);
      exit(0);
    default:
      close(handler);
    }
  }

  close(server);
  return 0;
}
