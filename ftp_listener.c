#include "ftp_listener.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>

#include "ftpd.h"
#include "ftp_log.h"
#include "ftp_session.h"
#include "ftp_connection.h"
#include "telnet_session.h"

static const int kAddrBufLen = 100;

static int SocketSetup(char *address, int port);

/* initialize an FTP listener */
int FtpListenerInit(FtpListener *f, char *address, int port,
                    int max_connections, int inactivity_timeout) {
  int sock_fd;
  int pipefds[2];
  char dir[PATH_MAX + 1];

  assert(f != NULL);
  assert(port >= 0);
  assert(port < 65536);
  assert(max_connections > 0);

  /* Gets current directory */
  if (getcwd(dir, sizeof(dir)) == NULL) {
    FtpLog(LOG_ERROR, "error getting current directory;");
    return 0;
  }

  /* Sets up socket */
  sock_fd = SocketSetup(address, port);
  if (-1 == sock_fd) {
    FtpLog(LOG_ERROR, "error setting up socket;");
    return 0;
  }

  /* Creates a pipe to wake up our listening thread */
  if (pipe(pipefds) != 0) {
    close(sock_fd);
    FtpLog(LOG_ERROR, "error creating pipe for internal use;");
    return 0;
  }

  /* Loads the values into the structure */
  f->sock_fd = sock_fd;
  f->max_connections = max_connections;
  f->num_connections = 0;
  f->inactivity_timeout = inactivity_timeout;
  f->shutdown_request_send_fd = pipefds[1];
  f->shutdown_request_recv_fd = pipefds[0];
  pthread_mutex_init(&f->mutex, NULL);
  pthread_cond_init(&f->shutdown_cond, NULL);

  assert(strlen(dir) < sizeof(f->dir));
  strcpy(f->dir, dir);
  f->listener_running = 0;

  return 1;
}

/* Ready to accept connections */
int FtpListenerStart(FtpListener *f) {
  pthread_t thread_id;

  if (pthread_create(&thread_id, NULL,
                     (void *(*)(void *))FtpConnectionAcceptor, f)) {
    FtpLog(LOG_ERROR, "unable to create ftp listening thread");
    return 0;
  }

  f->listener_running = 1;
  f->listener_thread = thread_id;

  return 1;
}


void FtpListenerStop(FtpListener *f) {
  /* write a byte to the listening thread - this will wake it up */
  write(f->shutdown_request_send_fd, "", 1);

  /* wait for client connections to complete */
  pthread_mutex_lock(&f->mutex);

  while (f->num_connections > 0) {
    pthread_cond_wait(&f->shutdown_cond, &f->mutex);
  }

  pthread_mutex_unlock(&f->mutex);
}

static int SocketSetup(char *address, int port) {
  struct sockaddr_in sock_addr;
  int reuseaddr = 1;
  int sock_fd;
  int flags;
  char buf[kAddrBufLen + 1];

  memset(buf, 0, kAddrBufLen + 1);
  memset(&sock_addr, 0, sizeof(struct sockaddr_in));

  if (address == NULL) {
    /* Just supports ipv4 now. */
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_addr.s_addr = INADDR_ANY;
  } else {
    /* Gets the kinds of address we want. */
    int gai_err = 0;
    struct addrinfo hints;
    struct addrinfo *res;

    memset(&hints, 0, sizeof(hints));

    /* Only Ipv4 allowed */
    hints.ai_family = AF_INET;
    /* For wildcard IP address */
    hints.ai_flags  = AI_PASSIVE;
    /* Only stream sockets */
    hints.ai_socktype = SOCK_STREAM;
    /* Only TCP protocol */
    hints.ai_protocol = IPPROTO_TCP;

    gai_err = getaddrinfo(address, NULL, &hints, &res);
    if (gai_err != 0) {
      FtpLog(LOG_ERROR, "Error: parsing server socket address;\n %s\n",
             gai_strerror(gai_err));
      return -1;
    }

    /* Just use the first valid address */
    memcpy(&sock_addr, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
  }

  /* Checks socket address */
  if (!inet_ntop(sock_addr.sin_family, (void *)(&sock_addr.sin_addr),
                 buf, sizeof(buf))) {
    FtpLog(LOG_ERROR, "Error: converting server address to ASCII");
    return -1;
  }

  sock_addr.sin_port = port == 0 ? htons(DEFAULT_FTP_PORT) : htons(port);

  /* Creates socket */
  sock_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (sock_fd == -1) {
    FtpLog(LOG_ERROR, "error: creating socket; %s", strerror(errno));
    return -1;
  }

  reuseaddr = 1;
  if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR,
                 (void *)&reuseaddr, sizeof(int)) !=0) {
    close(sock_fd);
    FtpLog(LOG_ERROR, "error setting socket to reuse address; %s", strerror(errno));
    return -1;
  }

  if (bind(sock_fd, (struct sockaddr *)&sock_addr,
           sizeof(struct sockaddr_in)) != 0)  {
    close(sock_fd);
    FtpLog(LOG_ERROR, "error binding address; %s", strerror(errno));
    return -1;
  }

  if (listen(sock_fd, SOMAXCONN) != 0) {
    close(sock_fd);
    FtpLog(LOG_ERROR, "error setting socket to listen; %s", strerror(errno));
    return -1;
  }

  /* prevent socket from blocking on accept() */
  flags = fcntl(sock_fd, F_GETFL);
  if (flags == -1) {
    close(sock_fd);
    FtpLog(LOG_ERROR, "error getting flags on socket; %s", strerror(errno));
    return -1;
  }
  if (fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK) != 0) {
    close(sock_fd);
    FtpLog(LOG_ERROR, "error setting socket to non-blocking; %s", strerror(errno));
    return -1;
  }

  return sock_fd;
}
