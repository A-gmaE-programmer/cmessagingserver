#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>

struct threadArgs
{
  int *connfd;
  char *username;
  int namelen;
};

void *reveiveMsg(void *vargp)
{
  struct threadArgs *me = (struct threadArgs *) vargp;
  int valread, i;
  char buffer[4096];
  char c;

  while(1)
  {
    memset(buffer, 0, sizeof(buffer));
    buffer[0] = '<';
    for (i = 1; i < me->namelen + 1; ++i)
      buffer[i] = me->username[i-1];
    buffer[i] = '>';
    buffer[i+1] = ' ';
    c = ' ';
    for (i = me->namelen + 3; i < 4095 && c != '\n' && c != '\0'; ++i)
    {
      c = getchar();
      buffer[i] = c;
    }
    write(*me->connfd, buffer, strlen(buffer));
  }

  return NULL;
}

int main(int argc, char const* argv[])
{
  int status, valread, client_fd, namelen;
  struct sockaddr_in serv_addr;
  char buffer[4096] = { 0 };
  char username[21] = { 0 };
  pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

  if (argc != 3)
  {
    fprintf(stderr, "usage: %s <ip> <port>\nE.g. %s 127.0.0.1 8000", argv[0], argv[0]);
    exit(1);
  }
  int portno = atoi(argv[2]);
  if (!portno)
  {
    fprintf(stderr, "port must a number\n");
    exit(1);
  }

  printf("Username: ");
  scanf("%20s", username);
  namelen = strlen(username);


  if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
  {
    printf("\n Socket creation error \n");
    return -1;
  }

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(portno);

  if (inet_pton(AF_INET, argv[1], &serv_addr.sin_addr) <= 0)
  {
    printf("\nInvalid address/ Address not supported \n");
    return -1;
  }

  if ((status = connect(client_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr))) < 0)
  {
    printf("\nConnection Failed \n");
    return -1;
  }

  pthread_t tid;
  struct threadArgs notme;
  notme.connfd = &client_fd;
  notme.username = username;
  notme.namelen = namelen;
  pthread_create(&tid, NULL, reveiveMsg, (void *) &notme);

  while (1)
  {
    memset(buffer, 0, sizeof(buffer));
    valread = read(client_fd, buffer, 4096);
    if (valread <= 0)
    {
      fprintf(stderr, "ERROR reading from socket\n");
      exit(1);
    }
    printf("%s", buffer);
  }
  close(client_fd);
  return 0;
}
