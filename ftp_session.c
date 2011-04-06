#include "ftp_session.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <pthread.h>
#include <arpa/ftp.h>
#include <assert.h>

#include "ftpd.h"
#include "telnet_session.h"
#include "ftp_command.h"
#include "ftp_command_handler.h"
#include "ftp_log.h"

struct {
  char *name;
  void (*func)(FtpSession *f, const FtpCommand *cmd);
} command_func[] = {
  { "user", DoUser  },
  { "pass", DoPass  },
  { "cwd",  DoCwd   },
  { "cdup", DoCdup  },
  { "quit", DoQuit  },
  { "pwd",  DoPwd   },
  { "retr", DoRetr  },
  { "stor", DoStor  },
  { "noop", DoNoop  },
  { "list", DoList  },
  { "nlst", DoNlst  },
  { "rest", DoRest  },
  { "mdtm", DoMdtm  },
  { "port", DoPort  },
  { "pasv", DoPasv  },
  { "type", DoType  },
  { "stru", DoStru  },
  { "mode", DoMode  },
};

static const int kCommandFuncNum = sizeof(command_func) / sizeof(command_func[0]);

static void GetAddrStr(const struct sockaddr_in *s, char *buf, int bufsiz);
static void SendReadme(const FtpSession *f, int code);

int FtpSessionInit(FtpSession *f,
                   const struct sockaddr_in *client_addr,
                   const struct sockaddr_in *server_addr,
                   TelnetSession *t,
                   const char *dir) {
  assert(f != NULL);
  assert(client_addr != NULL);
  assert(server_addr != NULL);
  assert(t != NULL);
  assert(dir != NULL);
  assert(strlen(dir) <= PATH_MAX);

  f->session_active = 1;
  f->command_number = 0;

  f->data_type = TYPE_A;
  f->file_structure = STRU_F;

  f->file_offset = 0;
  f->file_offset_command_number = ULONG_MAX;

  f->client_addr = *client_addr;
  GetAddrStr(client_addr, f->client_addr_str, sizeof(f->client_addr_str));

  f->server_addr = *server_addr;

  f->telnet_session = t;
  assert(strlen(dir) < sizeof(f->dir));
  strcpy(f->dir, dir);

  f->data_channel = DATA_PORT;
  f->data_port = *client_addr;
  f->server_fd = -1;

  return 1;
}

void FtpSessionDrop(FtpSession *f, const char *reason) {
  assert(reason != NULL);

  /* say goodbye */
  FtpSessionReply(f, 421, "%s.", reason);
}

void FtpSessionReply(FtpSession *f, int code, const char *fmt, ...) {
  char buf[256];
  va_list ap;

  assert(code >= 100);
  assert(code <= 559);
  assert(fmt != NULL);

  /* prepend our code to the buffer */
  sprintf(buf, "%d", code);
  buf[3] = ' ';

  /* add the formatted output of the caller to the buffer */
  va_start(ap, fmt);
  vsnprintf(buf + 4, sizeof(buf) - 4, fmt, ap);
  va_end(ap);

  //syslog(LOG_DEBUG, "%s %s", f->client_addr_str, buf);

  /* send the output to the other side */
  TelnetPrintLine(f->telnet_session, buf);
}

void FtpSessionRun(FtpSession *f) {
  char buf[2048];
  int len = 0, i = 0, cmd_parse_ret = 0;
  FtpCommand cmd;

  /* say hello */
  SendReadme(f, 220);
  FtpSessionReply(f, 220, "Service ready for new user.");

  /* process commands */
  while (f->session_active &&
        TelnetReadLine(f->telnet_session, buf, sizeof(buf))) {
    /* increase our command count */
    if (f->command_number == ULONG_MAX) {
      f->command_number = 0;
    } else {
      f->command_number++;
    }

    /* make sure we read a whole line */
    len = strlen(buf);
    if (buf[len-1] != '\n') {
      FtpSessionReply(f, 500, "Command line too long.");
      while (TelnetReadLine(f->telnet_session, buf, sizeof(buf))) {
        len = strlen(buf);
        if (buf[len-1] == '\n') {
          break;
        }
      }
      goto next_command;
    }

    /* parse the line */
    if ((cmd_parse_ret = FtpCommandParse(buf, &cmd)) != 0) {
      if (cmd_parse_ret == COMMAND_PARAMETERS_ERROR) {
        FtpSessionReply(f, 501, "Syntax error in parameters or arguments of command %s.", buf);
      } else {
        FtpSessionReply(f, 500, "Syntax error, command %s unrecognized.", buf);
      }
      goto next_command;
    }

    FtpLog(LOG_INFO, "%s", cmd.command);

    /* dispatch the command */
    for (i = 0; i < kCommandFuncNum; i++) {
      if (strcasecmp(cmd.command, command_func[i].name) == 0) {
        (command_func[i].func)(f, &cmd);
        goto next_command;
      }
    }

    /* oops, we don't have this command (shouldn't happen - shrug) */
    FtpSessionReply(f, 502, "Command not implemented.");

next_command: {}
  }
}

void FtpSessionDestroy(FtpSession *f) {
  if (f->server_fd != -1) {
    close(f->server_fd);
    f->server_fd = -1;
  }
}

static void GetAddrStr(const struct sockaddr_in *s, char *buf, int bufsiz) {
    unsigned int addr;
    int port;

    assert(s != NULL);
    assert(buf != NULL);

    /* buf must be able to contain (at least) a string representing an
     * ipv4 addr, followed by the string " port " (6 chars) and the port
     * number (which is 5 chars max), plus the '\0' character. */ 
    assert(bufsiz >= (INET_ADDRSTRLEN + 12));

    addr = ntohl(s->sin_addr.s_addr);
    port = ntohs(s->sin_port);
    snprintf(buf, bufsiz, "%d.%d.%d.%d port %d",
             (addr >> 24) & 0xff,
             (addr >> 16) & 0xff,
             (addr >> 8)  & 0xff,
             addr & 0xff,
             port);
}

static void SendReadme(const FtpSession *f, int code) {
  char file_path[PATH_MAX + 1];
  struct stat stat_buf;
  char buf[4096], code_str[8];
  char *p, *nl;
  int fd, read_ret, len, dir_len, line_len;

  assert(code >= 100);
  assert(code <= 559);

  /* set up for early exit */
  fd = -1;

  /* verify our README wouldn't be too long */
  dir_len = strlen(f->dir);
  if ((dir_len + 1 + sizeof(README_FILE_NAME)) > sizeof(file_path)) {
    goto exit_SendReadme;
  }

  /* create a README file full path */
  strcpy(file_path, f->dir);
 // strcat(file_path, "/");
  strcat(file_path, README_FILE_NAME);

  /* open our file */
  fd = open(file_path, O_RDONLY);
  if (fd == -1) {
    goto exit_SendReadme;
  }

  /* verify this isn't a directory */
  if (fstat(fd, &stat_buf) != 0) {
    goto exit_SendReadme;
  }

  if (S_ISDIR(stat_buf.st_mode)) {
    goto exit_SendReadme;
  }

  /* convert our code to a buffer */
  assert(code >= 100);
  assert(code <= 999);
  sprintf(code_str, "%03d-", code);

  /* read and send */
  read_ret = read(fd, buf, sizeof(buf));
  if (read_ret > 0) {
    TelnetPrint(f->telnet_session, code_str);
    while (read_ret > 0) {
      p = buf;
      len = read_ret;
      nl = memchr(p, '\n', len);
      while ((len > 0) && (nl != NULL)) {
        *nl = '\0';
        TelnetPrintLine(f->telnet_session, p);
        line_len = nl - p;
        len -= line_len + 1;
        if (len > 0) {
          TelnetPrint(f->telnet_session, code_str);
        }
        p = nl+1;
        nl = memchr(p, '\n', len);
      }
      if (len > 0) {
        TelnetPrint(f->telnet_session, p);
      }

      read_ret = read(fd, buf, sizeof(buf));
    }
  }

  /* cleanup and exit */
exit_SendReadme:
  if (fd != -1) {
    close(fd);
  }
}

