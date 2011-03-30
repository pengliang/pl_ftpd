#include "ftp_connection.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>

static const int kMaxAcceptErrorNum = 10;

static void *ConnectionHandler(FtpConnection *info);
static void ConnectionCleanup(FtpConnection *info);

/* handle incoming connections */
void *FtpConnectionAcceptor(FtpListener *f) {
  int num_error;

  int fd;
  socklen_t addr_len;
  socklen_t tcp_nodelay = 1;

  struct sockaddr_in client_addr, server_addr;
  FtpConnection *info;
  pthread_t thread_id;

  fd_set read_fds;

  num_error = 0;
  while (1) {
    FD_ZERO(&read_fds);
    FD_SET(f->fd, &read_fds);
    FD_SET(f->shutdown_request_recv_fd, &read_fds);

    /* Waiting util at least one become ready for input */
    select(FD_SETSIZE, &read_fds, NULL, NULL, NULL);

    /* If data arrived on our pipe, we've been asked to exit */
    if (FD_ISSET(f->shutdown_request_recv_fd, &read_fds)) {
      close(f->fd);
      perror("listener no longer accepting connections");
      pthread_exit(NULL);
    }

    /* otherwise accept our pending connection (if any) */
    addr_len = sizeof(struct sockaddr_in);
    fd = accept(f->fd, (struct sockaddr *)&client_addr, &addr_len);

    if (fd < 0) {
      if ((errno == ECONNABORTED) || (errno == ECONNRESET)) {
        perror("interruption accepting FTP connection;");
      } else {
        perror("error accepting FTP connection;");
        ++num_error;
      }
      if (num_error >= kMaxAcceptErrorNum) {
        perror("too many consecutive errors, FTP server exiting");
        return NULL;
      }
      continue;
    }

    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
                   &tcp_nodelay, sizeof(socklen_t)) != 0) {
      perror("error in setsockopt(), FTP server dropping connection;");
      close(fd);
      continue;
    }

    addr_len = sizeof(struct sockaddr_in);
    if (getsockname(fd, (struct sockaddr *)&server_addr, &addr_len) == -1) {
      perror("error in getsockname(), FTP server dropping connection;");
      close(fd);
      continue;
    }

    info = (FtpConnection *)malloc(sizeof(FtpConnection));
    if (info == NULL) {
      perror("out of memory, FTP server dropping connection");
      close(fd);
      continue;
    }

    info->ftp_listener = f;
    TelnetServerInit(&info->telnet_session, fd, fd);
    if (!FtpSessionInit(&info->ftp_session, &client_addr, &server_addr,
                        &info->telnet_session, f->dir)) {
      perror("error initializing FTP session, FTP server exiting;");
      close(fd);
      TelnetDestroy(&info->telnet_session);
      free(info);
      continue;
    }

    if (pthread_create(&thread_id, NULL,
                       (void * (*)(void *))ConnectionHandler, info) != 0) {
      perror("error creating new thread;");
      close(fd);
      TelnetDestroy(&info->telnet_session);
      free(info);
    }

    num_error = 0;
  }
}

static void *ConnectionHandler(FtpConnection *info) {
  FtpListener *f;
  int num_connections;
  char drop_reason[80];

  /* for ease of use only */
  f = info->ftp_listener;

  /* don't save state for pthread_join() */
  pthread_detach(pthread_self());
  /* set up our cleanup handler */
  pthread_cleanup_push((void (*)())ConnectionCleanup, info);
 // pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &oldtype);

  /* process global data */
  pthread_mutex_lock(&f->mutex);

  num_connections = ++f->num_connections;
  fprintf(stdout, "%s port %d connection requesting ... \n",
         inet_ntoa(info->ftp_session.client_addr.sin_addr),
         ntohs(info->ftp_session.client_addr.sin_port));

  pthread_mutex_unlock(&f->mutex);

  /* handle the session */
  if (num_connections <= f->max_connections) {
    fprintf(stdout, "%s port %d connected.\n",
         inet_ntoa(info->ftp_session.client_addr.sin_addr),
         ntohs(info->ftp_session.client_addr.sin_port));
    FtpSessionRun(&info->ftp_session);
  } else {
    /* too many users */
    sprintf(drop_reason,
            "Too many users logged in, dropping connection (%d logins maximum)",
            f->max_connections);
    FtpSessionDrop(&info->ftp_session, drop_reason);

    /* log the rejection */
    pthread_mutex_lock(&f->mutex);

    fprintf(stderr, "%s port exceeds max users (%d), dropping connection",
            inet_ntoa(info->ftp_session.client_addr.sin_addr),
            ntohs(info->ftp_session.client_addr.sin_port),
            num_connections);
    pthread_mutex_unlock(&f->mutex);
  }

  /* exunt (pop calls cleanup function) */
  pthread_cleanup_pop(1);

  /* return for form :) */
  return NULL;
}

/* clean up a connection */
static void ConnectionCleanup(FtpConnection *info) {
  FtpListener *f;

  f = info->ftp_listener;

  pthread_mutex_lock(&f->mutex);

  f->num_connections--;
  pthread_cond_signal(&f->shutdown_cond);

  printf("%s port %d disconnected.\n",
         inet_ntoa(info->ftp_session.client_addr.sin_addr),
         ntohs(info->ftp_session.client_addr.sin_port));

  pthread_mutex_unlock(&f->mutex);

  FtpSessionDestroy(&info->ftp_session);
  TelnetDestroy(&info->telnet_session);

  free(info);
}
