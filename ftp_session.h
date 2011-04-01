#ifndef FTP_SESSION_H
#define FTP_SESSION_H

#include <limits.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <arpa/ftp.h>
#include "telnet_session.h"

/* data path chosen */
#define DATA_PORT     0
#define DATA_PASSIVE  1

/* space required for text representation of address and port,
   e.g. "192.168.0.1 port 1024" or
        "2001:3333:DEAD:BEEF:0666:0013:0069:0042 port 65535" */
#define ADDRPORT_STRLEN 58

/* structure encapsulating an FTP session's information */
typedef struct {
  /* flag whether session is active */
  int session_active;

  /* incremented for each command */
  unsigned long command_number;

  /* options about transfer set by user */
  int data_type;
  int file_structure;

  /* offset to begin sending file from */
  off_t file_offset;
  unsigned long file_offset_command_number;

  /* address of client */
  struct sockaddr_in client_addr;
  char client_addr_str[ADDRPORT_STRLEN];

  /* address of server */
  struct sockaddr_in server_addr;

  /* telnet session to encapsulate control channel logic */
  TelnetSession *telnet_session;

  /* current working directory of this connection */
  char dir[PATH_MAX+1];

  /* data channel information, including type,
   * and client address or server port depending on type */
  int data_channel;
  struct sockaddr_in data_port;
  int server_fd;
} FtpSession;

int FtpSessionInit(FtpSession *f, const struct sockaddr_in *client_addr,
                   const struct sockaddr_in *server_addr,
                   TelnetSession *t, const char *dir);
void FtpSessionDrop(FtpSession *f, const char *reason);
void FtpSessionRun(FtpSession *f);
void FtpSessionReply(FtpSession *f, int code, const char *fmt, ...);
void FtpSessionDestroy(FtpSession *f);

#endif /* FTP_SESSION_H */
