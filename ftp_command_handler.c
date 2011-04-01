#include "ftp_command_handler.h"
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <arpa/ftp.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include "ftp_session.h"
#include "ftp_command.h"
#include "ftp_log.h"
#include "file_list.h"

static void ChangeDir(FtpSession *f, const char *new_dir);
static int OpenDataConnection(FtpSession *f);

void DoUser(FtpSession *f, const FtpCommand *cmd) {
  const char *user;

  assert(f != NULL);
  assert(cmd != NULL);
  assert(cmd->num_arg == 1);

  user = cmd->arg[0].string;
  if (strcasecmp(user, "ftp") && strcasecmp(user, "anonymous")) {
    FtpLog(LOG_INFO, "%s attempted to log in as \"%s\"", f->client_addr_str, user);
    FtpSessionReply(f, 530, "Only anonymous FTP supported.");
  } else {
    FtpSessionReply(f, 331, "Send e-mail address as password.");
  }
}

void DoPass(FtpSession *f, const FtpCommand *cmd) {
  const char *password;

  assert(f != NULL);
  assert(cmd != NULL);
  assert(cmd->num_arg == 1);

  password = cmd->arg[0].string;
  FtpLog(LOG_INFO, "%s reports e-mail address \"%s\"", f->client_addr_str, password);
  FtpSessionReply(f, 230, "User logged in, proceed.");
}

void DoCwd(FtpSession *f, const FtpCommand *cmd){
  assert(f != NULL);
  assert(cmd != NULL);
  assert(cmd->num_arg == 1);

  ChangeDir(f, cmd->arg[0].string);
}

void DoCdup(FtpSession *f, const FtpCommand *cmd){
  assert(f != NULL);
  assert(cmd != NULL);
  assert(cmd->num_arg == 0);

  ChangeDir(f, "..");
}

void DoPwd(FtpSession *f, const FtpCommand *cmd) {
  assert(f != NULL);
  assert(cmd != NULL);
  assert(cmd->num_arg == 0);

  FtpSessionReply(f, 257, "\"%s\" is current directory", f->dir);

}

static void SendFileList(FtpSession *f, const FtpCommand *cmd,
                         int (*PrintFileListFunc)(int fd, const char *dir)) {
  int fd;
  char dir_path[PATH_MAX + 1];
  int send_ok;

  assert(f != NULL);
  assert(cmd != NULL);
  assert((cmd->num_arg == 0) || (cmd->num_arg == 1));

  /* For exit */
  fd = -1;

  /* Figures out what parameters to use */
  if (cmd->num_arg == 0) {
    strcpy(dir_path, "./");
  } else {
    assert(cmd->num_arg == 1);
    strcpy(dir_path, cmd->arg[0].string);
  }

  /* Ready to list */
  FtpSessionReply(f, 150, "File status okay; about to open data connection.");

  /* Opens data connection */
  fd = OpenDataConnection(f);
  if (fd == -1) {
    FtpSessionReply(f, 425, "Can't open data connection");
    goto exit;
  }

  FtpSessionReply(f, 125, "Data connection already open; transfer starting.");

  send_ok = PrintFileListFunc(fd, dir_path);

  if (send_ok) {
    FtpSessionReply(f, 226, "Transfer complete.");
  } else {
    FtpSessionReply(f, 451, "Transfer aborted, local error in processing; %s", strerror(errno));
  }

exit:
  if (fd != -1) {
    close(fd);
  }

}

void DoList(FtpSession *f, const FtpCommand *cmd) {
  SendFileList(f, cmd, PrintFileFullList);
}

void DoNlst(FtpSession *f, const FtpCommand *cmd) {
  SendFileList(f, cmd, PrintFileNameList);
}

void DoQuit(FtpSession *f, const FtpCommand *cmd){
  assert(f != NULL);
  assert(cmd != NULL);
  assert(cmd->num_arg == 0);

  FtpSessionReply(f, 221, "Service closing control connection.");
  f->session_active = 0;
}

void DoPort(FtpSession *f, const FtpCommand *cmd){
  const struct sockaddr_in *host_port;

  assert(f != NULL);
  assert(cmd != NULL);
  assert(cmd->num_arg == 1);

  host_port = &cmd->arg[0].host_port;
  assert(host_port->sin_family == AF_INET);

  if (ntohs(host_port->sin_port) < IPPORT_RESERVED) {
    FtpSessionReply(f, 500, "Port may not be less than 1024, which is reserved.");
  } else {
    /* close any outstanding PASSIVE port */
    if (f->data_channel == DATA_PASSIVE) {
      close(f->server_fd);
      f->server_fd = -1;
    }
    f->data_channel = DATA_PORT;
    f->data_port = *host_port;
    FtpSessionReply(f, 200, "Command okay.");
  }
}

void DoType(FtpSession *f, const FtpCommand *cmd){
  char type;
  char form;
  int cmd_okay;

  assert(f != NULL);
  assert(cmd != NULL);
  assert(cmd->num_arg >= 1);
  assert(cmd->num_arg <= 2);

  type = cmd->arg[0].string[0];
  if (cmd->num_arg == 2) {
    form = cmd->arg[1].string[0];
  } else {
    form = 0;
  }

  cmd_okay = 0;
  if (type == 'A') {
    if ((cmd->num_arg == 1) || ((cmd->num_arg == 2) && (form == 'N'))) {
      f->data_type = TYPE_A;
      cmd_okay = 1;
    }
  } else if (type == 'I') {
    f->data_type = TYPE_I;
    cmd_okay = 1;
  }

  if (cmd_okay) {
    FtpSessionReply(f, 200, "Command okay.");
  } else {
    FtpSessionReply(f, 504, "Command not implemented for that parameter.");
  }
}

void DoStru(FtpSession *f, const FtpCommand *cmd){
  char structure;
  int cmd_okay;

  assert(f != NULL);
  assert(cmd != NULL);
  assert(cmd->num_arg == 1);

  structure = cmd->arg[0].string[0];
  cmd_okay = 0;
  if (structure == 'F') {
    f->file_structure = STRU_F;
    cmd_okay = 1;
  } else if (structure == 'R') {
    f->file_structure = STRU_R;
    cmd_okay = 1;
  }

  if (cmd_okay) {
    FtpSessionReply(f, 200, "Command okay.");
  } else {
    FtpSessionReply(f, 504, "Command not implemented for that parameter.");
  }
}

void DoMode(FtpSession *f, const FtpCommand *cmd){
  char mode;

  assert(f != NULL);
  assert(cmd != NULL);
  assert(cmd->num_arg == 1);

  mode = cmd->arg[0].string[0];
  if (mode == 'S') {
    FtpSessionReply(f, 200, "Command okay.");
  } else {
    FtpSessionReply(f, 504, "Command not implemented for that parameter.");
  }
}

void DoRetr(FtpSession *f, const FtpCommand *cmd){
  FtpSessionReply(f, 230, "User logged in, proceed.");
}

void DoStor(FtpSession *f, const FtpCommand *cmd){
  FtpSessionReply(f, 230, "User logged in, proceed.");
}

void DoNoop(FtpSession *f, const FtpCommand *cmd){
  FtpSessionReply(f, 230, "User logged in, proceed.");
}

static void ChangeDir(FtpSession *f, const char *new_dir) {
  char dir[PATH_MAX + 1];

  assert(f != NULL);
  assert(new_dir != NULL);
  assert(strlen(new_dir) <= PATH_MAX);

  /* first change to current dir*/
  assert(chdir(f->dir) == 0);
  /* then, change to new dir from current dir */
  if(chdir(new_dir) == -1) {
    switch (errno) {
      case EACCES:
        FtpSessionReply(f, 550, "Directory change failed; permission denied.");
        break;
      case ENAMETOOLONG:
        FtpSessionReply(f, 550, "Directory change failed; path is too long.");
        break;
      case ENOTDIR:
        FtpSessionReply(f, 550, "Directory change failed; path is not a directory.");
        break;
      case ENOENT:
        FtpSessionReply(f, 550, "Directory change failed; path does not exist.");
        break;
      default:
        FtpSessionReply(f, 550, "Directory change failed.");
        break;
    }
    return;
  }

  /* if everything is okay, change into the directory */
  if (getcwd(dir, sizeof(dir)) == NULL) {
    FtpLog(LOG_ERROR, "error getting current directory; %s", strerror(errno));
    assert(chdir(f->dir) == 0);
    FtpSessionReply(f, 550, "Directory change failed. %s", strerror(errno));
  } else {
    strcpy(f->dir, dir);
    FtpSessionReply(f, 250, "Directory change to %s successful.", f->dir);
  }
}

static int OpenDataConnection(FtpSession *f) {
  int socket_fd;
  struct sockaddr_in addr;
  unsigned addr_len;

  assert((f->data_channel == DATA_PORT) ||
         (f->data_channel == DATA_PASSIVE));

  if (f->data_channel == DATA_PORT) {
    socket_fd = socket(f->data_port.sin_family, SOCK_STREAM, IPPROTO_TCP);
    if (socket_fd == -1) {
      FtpSessionReply(f, 425, "Error creating socket; %s.", strerror(errno));
      return -1;
    }
    if (connect(socket_fd, (struct sockaddr *)&f->data_port,
                sizeof(f->data_port)) != 0) {
      FtpSessionReply(f, 425, "Error connecting; %s.", strerror(errno));
	    close(socket_fd);
	    return -1;
    }
  } else {
    assert(f->data_channel == DATA_PASSIVE);
    addr_len = sizeof(struct sockaddr_in);
    socket_fd = accept(f->server_fd, (struct sockaddr *)&addr, &addr_len);
    if (socket_fd == -1) {
      FtpSessionReply(f, 425, "Error accepting connection; %s.", strerror(errno));
      return -1;
    }
    if (memcmp(&f->client_addr.sin_addr, &addr.sin_addr, sizeof(struct in_addr)))	{
      FtpSessionReply(f, 425, "Error accepting connection; connection from invalid IP.");
      close(socket_fd);
      return -1;
    }
  }

  return socket_fd;
}

