#ifndef FTP_CONNECTION_H
#define FTP_CONNECTION_H

#include "ftp_listener.h"
#include "ftp_session.h"
#include "telnet_session.h"

/* information for a specific connection */
typedef struct FtpConnection {
    FtpListener *ftp_listener;
    TelnetSession telnet_session;
    FtpSession ftp_session;

    struct FtpConnection *next;
} FtpConnection;

void *FtpConnectionAcceptor(FtpListener *f);

#endif /* FTP_CONNECTION_H */
