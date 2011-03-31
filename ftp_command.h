/*
 * The following comands are parsed:
 *
 * USER <SP> <username>
 * PASS <SP> <password>
 * CWD  <SP> <pathname>
 * CDUP
 * QUIT
 * PORT <SP> <host-port>
 * PASV
 * TYPE <SP> <type-code>
 * STRU <SP> <structure-code>
 * MODE <SP> <mode-code>
 * RETR <SP> <pathname>
 * STOR <SP> <pathname>
 * PWD
 * LIST [ <SP> <pathname> ]
 * NLST [ <SP> <pathname> ]
 * SYST
 * HELP [ <SP> <string> ]
 * NOOP
 * REST <SP> <offset>
 */

#ifndef FTP_COMMAND_H
#define FTP_COMMAND_H

#include <netinet/in.h>
#include <limits.h>
#include <sys/types.h>

/* maximum possible number of arguments */
#define MAX_ARG 2

/* command parse error */
#define COMMAND_UNRECOGNIZED 1
#define COMMAND_PARAMETERS_ERROR 2

/* maximum string length */
#define MAX_STRING_LEN PATH_MAX

typedef struct {
  char command[5];
  int num_arg;
  union {
    char string[MAX_STRING_LEN + 1];
    struct sockaddr_in host_port;
    int num;
    off_t offset;
  } arg[MAX_ARG];
} FtpCommand;

int FtpCommandParse(const char *input, FtpCommand *cmd);

#endif /* FTP_COMMAND_H */
