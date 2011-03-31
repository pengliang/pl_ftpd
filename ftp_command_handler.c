#include "ftp_command_handler.h"
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include "ftp_session.h"
#include "ftp_command.h"
#include "ftp_log.h"

static void ChangeDir(FtpSession *f, const char *new_dir);

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

void DoList(FtpSession *f, const FtpCommand *cmd) {

}

void DoNlst(FtpSession *f, const FtpCommand *cmd) {

}

void DoQuit(FtpSession *f, const FtpCommand *cmd){
  assert(f != NULL);
  assert(cmd != NULL);
  assert(cmd->num_arg == 0);

  FtpSessionReply(f, 221, "Service closing control connection.");
  f->session_active = 0;
}

void DoPort(FtpSession *f, const FtpCommand *cmd){
  FtpSessionReply(f, 230, "User logged in, proceed.");
}

void DoType(FtpSession *f, const FtpCommand *cmd){
  FtpSessionReply(f, 230, "User logged in, proceed.");
}

void DoStru(FtpSession *f, const FtpCommand *cmd){
  FtpSessionReply(f, 230, "User logged in, proceed.");
}

void DoMode(FtpSession *f, const FtpCommand *cmd){
  FtpSessionReply(f, 230, "User logged in, proceed.");
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
    FtpLog(LOG_ERROR, "error getting current directory;");
    assert(chdir(f->dir) == 0);
    FtpSessionReply(f, 550, "Directory change failed.");
  } else {
    strcpy(f->dir, dir);
    FtpSessionReply(f, 250, "Directory change to %s successful.", f->dir);
  }
}
