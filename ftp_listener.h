#ifndef FTP_LISTENER_H
#define FTP_LISTENER_H

#define DEFAULT_FTP_PORT 21

#include <limits.h>
#include <pthread.h>

typedef struct {
  /* file descriptor incoming connections arrive on */
  int sock_fd;

  /* maximum number of connections */
  int max_connections;

  /* current number of connections */
  int num_connections;

  /* timeout (in seconds) for connections */
  int inactivity_timeout;

  /* starting directory */
  char dir[PATH_MAX + 1];

  /* boolean defining whether listener is running or not */
  int listener_running;

  /* thread identifier for listener */
  pthread_t listener_thread;

  /* end of pipe to wake up listening thread with */
  int shutdown_request_send_fd;

  /* end of pipe listening thread waits on */
  int shutdown_request_recv_fd;

  /* mutext to lock changes to this structure */
  pthread_mutex_t mutex;

  /* condition to signal thread requesting shutdown */
  pthread_cond_t shutdown_cond;

} FtpListener;

int FtpListenerInit(FtpListener *f, char *address, int port,
                    int max_connections, int inactivity_timeout);

#endif // FTP_SERVER_H
