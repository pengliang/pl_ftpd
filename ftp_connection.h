#ifndef FTP_CONNECTION_H
#define FTP_CONNECTION_H

#include "ftp_listener.h"
#include "ftp_session.h"
#include "telnet_session.h"

typedef struct sockaddr_in SockAddr4;

/* information for a specific connection */
typedef struct FtpConnection {
  FtpListener *ftp_listener;
  TelnetSession telnet_session;
  FtpSession ftp_session;
} FtpConnection;

void *FtpConnectionAcceptor(FtpListener *f);

#endif /* FTP_CONNECTION_H */
