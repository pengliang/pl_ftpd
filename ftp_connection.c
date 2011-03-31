#include "ftp_connection.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include "ftp_log.h"

static const int kMaxAcceptErrorNum = 10;

static void *ConnectionHandler(FtpConnection *info);
static void ConnectionCleanup(FtpConnection *info);

/* handle incoming connections */
void *FtpConnectionAcceptor(FtpListener *f) {
  int num_error = 0, accept_fd;
  socklen_t addr_len;
  socklen_t tcp_nodelay = 1;

  SockAddr4 client_addr, server_addr;
  FtpConnection *info;

  pthread_t thread_id;
  fd_set read_fds;

  num_error = 0;
  while (1) {
    FD_ZERO(&read_fds);
    FD_SET(f->sock_fd, &read_fds);
    FD_SET(f->shutdown_request_recv_fd, &read_fds);

    /* Waiting util at least one become ready for input */
    select(FD_SETSIZE, &read_fds, NULL, NULL, NULL);

    /* If data arrived on our pipe, we've been asked to exit */
    if (FD_ISSET(f->shutdown_request_recv_fd, &read_fds)) {
      close(f->sock_fd);
      FtpLog(LOG_INFO, "listener shut down, no longer accepting connections");
      pthread_exit(NULL);
    }

    /* otherwise accept our pending connection (if any) */
    addr_len = sizeof(SockAddr4);
    accept_fd = accept(f->sock_fd, (struct sockaddr *)&client_addr, &addr_len);

    if (accept_fd < 0) {
      if ((errno == ECONNABORTED) || (errno == ECONNRESET)) {
        FtpLog(LOG_ERROR, "interruption accepting FTP connection;");
      } else {
        FtpLog(LOG_ERROR, "error accepting FTP connection;");
        ++num_error;
      }
      if (num_error >= kMaxAcceptErrorNum) {
        FtpLog(LOG_ERROR, "too many consecutive errors, FTP server exiting");
        return NULL;
      }
      continue;
    }

    if (setsockopt(accept_fd, IPPROTO_TCP, TCP_NODELAY,
                   &tcp_nodelay, sizeof(socklen_t)) != 0) {
      FtpLog(LOG_ERROR, "error in setsockopt(), FTP server dropping connection;");
      close(accept_fd);
      continue;
    }

    addr_len = sizeof(SockAddr4);
    if (getsockname(accept_fd, (struct sockaddr *)&server_addr, &addr_len) == -1) {
      FtpLog(LOG_ERROR, "error in getsockname(), FTP server dropping connection;");
      close(accept_fd);
      continue;
    }

    info = (FtpConnection *)malloc(sizeof(FtpConnection));
    if (info == NULL) {
      FtpLog(LOG_ERROR, "out of memory, FTP server dropping connection");
      close(accept_fd);
      continue;
    }

    info->ftp_listener = f;
    /* Inits telnet session for ftp control connection */
    TelnetServerInit(&info->telnet_session, accept_fd, accept_fd);
    if (!FtpSessionInit(&info->ftp_session, &client_addr, &server_addr,
                        &info->telnet_session, f->dir)) {
      FtpLog(LOG_ERROR, "error initializing FTP session, FTP server exiting;");
      close(accept_fd);
      TelnetDestroy(&info->telnet_session);
      free(info);
      continue;
    }

    /* Now, connection accepted */
    if (pthread_create(&thread_id, NULL,
                       (void * (*)(void *))ConnectionHandler, info) != 0) {
      FtpLog(LOG_ERROR, "error creating new thread;");
      close(accept_fd);
      TelnetDestroy(&info->telnet_session);
      free(info);
    }

    num_error = 0;
  }
}

static void *ConnectionHandler(FtpConnection *info) {
  FtpListener *f;
  int num_connections = 0, thread_cancel_old_type;
  char *client_addr_str;
  uint16_t client_port;
  char drop_reason[80];

  /* for ease of use only */
  f = info->ftp_listener;
  client_addr_str = inet_ntoa(info->ftp_session.client_addr.sin_addr);
  client_port = ntohs(info->ftp_session.client_addr.sin_port);

  /* don't save state for pthread_join() */
  pthread_detach(pthread_self());
  pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &thread_cancel_old_type);
  /* set up our cleanup handler */
  pthread_cleanup_push((void (*)())ConnectionCleanup, info);

  /* process global data */
  pthread_mutex_lock(&f->mutex);

  num_connections = ++f->num_connections;

  pthread_mutex_unlock(&f->mutex);

  FtpLog(LOG_INFO, "%s port %d connection requesting ...",
         client_addr_str, client_port);

  /* handle the session */
  if (num_connections <= f->max_connections) {
    FtpSessionRun(&info->ftp_session);
  } else {
    sprintf(drop_reason, "Too many users logged in (%d logins maximum)",
            f->max_connections);

    FtpSessionDrop(&info->ftp_session, drop_reason);

    FtpLog(LOG_ERROR, "%s port exceeds max users (%d), dropping connection",
           client_addr_str, client_port, num_connections);
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

  FtpLog(LOG_INFO, "%s port %d disconnected.",
         inet_ntoa(info->ftp_session.client_addr.sin_addr),
         ntohs(info->ftp_session.client_addr.sin_port));

  pthread_mutex_unlock(&f->mutex);

  FtpSessionDestroy(&info->ftp_session);
  TelnetDestroy(&info->telnet_session);

  free(info);
}
