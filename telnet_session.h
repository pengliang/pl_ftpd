#ifndef TELNET_SESSION_H
#define TELNET_SESSION_H

#define BUF_LEN 2048

/* information on a telnet session */
typedef struct {
    int in_fd;
    int in_errno;
    int in_eof;
    int in_take;
    int in_add;
    char in_buf[BUF_LEN];
    int in_buflen;

    int in_status;

    int out_fd;
    int out_errno;
    int out_eof;
    int out_take;
    int out_add;
    char out_buf[BUF_LEN];
    int out_buflen;
} TelnetSession;

/* functions */
void TelnetInit(TelnetSession *t, int in, int out);
void TelnetDestroy(TelnetSession *t);
int TelnetPrint(TelnetSession *t, const char *s);
int TelnetPrintLine(TelnetSession *t, const char *s);
int TelnetReadLine(TelnetSession *t, char *buf, int buflen);

#endif /* TELNET_SERVER_H */

