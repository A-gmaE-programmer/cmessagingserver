/* messageserver.c - Simple message server that can be connected to using netcat
 * usage: messageserver <port>
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFSIZE 4096
#define MAXCON 10

struct client
{
  // Connection info
  int connfd; /* connection socket */
  int clientlen; /* byte size of client's address */
  struct sockaddr_in clientaddr; /* client addr */
  int n; /* readbytes */
  
  // Thread stuff
  pthread_t tid;
  pthread_mutex_t *lock;
  char *buffer;
  pthread_cond_t *newMsg;
};

void error(char *msg)
{
  perror(msg);
  exit(1);
}

// Threads that handle recieving messages from clients
void *clientreceive(void *vargp)
{
  struct client *me = (struct client *) vargp;
  char localbuffer[BUFSIZE] = { 0 };
  int connected = 1;

  // Read messages from the client
  while (connected)
  {
    // Clear buffer and read string from client
    memset(localbuffer, 0, sizeof(localbuffer));
    me->n = read(me->connfd, localbuffer, BUFSIZE);

    if (me->n < 0)
      printf("ERROR reading from socket %d\n", me->connfd);

    // Check if client has disconnected
    if (me->n <= 0)
      connected = 0;
    if (!localbuffer[0])
      connected = 0;
    if (!connected)
      continue;

    // Wait to write buffer into main buffer 
    puts("Message received, locking mutex");
    pthread_mutex_lock(me->lock);
    puts("Mutex lock aquired, copying buffer");
    // Copy buffer into main buffer
    strcpy(me->buffer, localbuffer);
    // Signal sendThread to send out the message
    pthread_cond_signal(me->newMsg);
    pthread_mutex_unlock(me->lock);
    puts("Mutex lock released");
    
  }

  printf("Client disconnected, closing connfd %d\n", me->connfd);
  close(me->connfd);
  me->connfd = 0;

  return NULL;
}

// Thread that handles sending messages to clients
void *sendMessages(void *vargp)
{
  struct client *clients = (struct client *) vargp;
  int msgLen;

  // Aquire mutex lock
  pthread_mutex_lock(clients[0].lock);

  // Wait for a new message then send it to all clients
  while(1)
  {
    pthread_cond_wait(clients[0].newMsg, clients[0].lock);
    msgLen = strlen(clients[0].buffer);
    for (int i = 0; i < MAXCON; ++i)
    {
      if (clients[i].connfd != 0)
      {
        printf("Writing to socket: %d, file descriptor: %d\n", i, clients[i].connfd);
        clients[i].n = write(clients[i].connfd, clients[0].buffer, msgLen);
        if (clients[i].n < 0)
          puts("ERROR writing to socket");
      }
    }
  }

  return NULL;
}

int main(int argc, char **argv)
{
  int i;
  int clientIndex = -1;
  pthread_t sendMsgTid;

  // Variables that must be managed carefully
  pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
  char buffer[BUFSIZE];
  pthread_cond_t newMsg = PTHREAD_COND_INITIALIZER;

  // Variables for the server connection
  int listenfd; /* listening socket */
  int portno; /* port to listen on */
  struct sockaddr_in serveraddr; /* server's addr */
  int optval; /* flag value for setsockopt */

  // Variables for the server clients
  struct client clients[MAXCON]; /* list of clients */
  struct hostent *hostp; /* client host info */
  char *hostname;
  char hostport[16] = {};
  char *hostaddrp; /* dotted decimal host addr string */

  // Check command line arguments
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  portno = atoi(argv[1]);
  if (portno == 0)
    error("Invalid port");

  // Create a socket
  listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0)
    error("ERROR opening socket");

  // Allow the process to reuse the port immediately after restartoptval = 1;
  optval = 1;
  setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, 
	     (const void *)&optval , sizeof(int));

  // Build the server's internet address
  memset((char *) &serveraddr, 0, sizeof(serveraddr));
  serveraddr.sin_family = AF_INET; // IPV4 Internet
  serveraddr.sin_addr.s_addr = htonl(INADDR_ANY); // Accept reqs to any IP
  serveraddr.sin_port = htons((unsigned short) portno); // Set port

  // Bind the listening socket to its port
  if (bind(listenfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0)
    error("ERROR on binding");

  // Start listening for connection requests
  // Allow 5 requests to queue up
  if (listen(listenfd, 5) < 0)
    error("ERROR on listen");

  // initialize clientlist by setting clientlen
  memset(&clients, 0, sizeof(clients));
  for (i = 0; i < MAXCON; ++i)
  {
    clients[i].clientlen = sizeof(clients[i].clientaddr);
    clients[i].connfd = 0;
    clients[i].lock = &lock;
    clients[i].buffer = buffer;
    clients[i].newMsg = &newMsg;
  }

  // Start thread responsible for sending out messages
  pthread_create(&sendMsgTid, NULL, sendMessages, (void *) clients);

  /* main loop: wait for a connection request, print out client data, send clients off to a seperate thread */
  while (1)
  {
    // Look for an open slot for a new client and the wait for a connection request
    if (clientIndex >= MAXCON) clientIndex = -1;
    if (clients[++clientIndex].connfd != 0) continue;
    
    // Wait for a connetion request and accept it
    clients[clientIndex].connfd = accept(
      listenfd,
      (struct sockaddr *) &clients[clientIndex].clientaddr, 
      (uint *) &clients[clientIndex].clientlen
    );
    if (clients[clientIndex].connfd < 0)
      error("ERROR accepting");

    printf("Connected to client at index: %d\n", clientIndex);
    printf("Current connfd list: [%d", clients[0].connfd);
    for (int c = 1; c < MAXCON; ++c) { printf(", %d", clients[c].connfd); }
    printf("]\n");

    // Get and print information about host
    struct sockaddr_in clientaddr = clients[clientIndex].clientaddr;
    hostp = gethostbyaddr((const char *) &clientaddr.sin_addr.s_addr, 
			  sizeof(&clientaddr.sin_addr.s_addr), AF_INET);
    if (hostp == NULL)
      // error("ERROR on gethostbyaddr");
      hostname = "Unknown Host\0";
    else
      hostname = hostp->h_name;
    hostaddrp = inet_ntoa(clients[clientIndex].clientaddr.sin_addr);
    if (hostaddrp == NULL)
      error("ERROR on inet_ntoa\n");
    memset(hostport, 0, sizeof(hostport));
    sprintf(hostport, "%d", clientaddr.sin_port);
    puts("help");
    printf("server established connection with %s (%s:%s)\n", hostname, hostaddrp, hostport);

    pthread_create(&clients[clientIndex].tid, NULL, clientreceive, (void *) (struct client *) &clients[clientIndex]);
  }

}
