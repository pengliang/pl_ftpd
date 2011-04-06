#include "ftp_command_handler.h"
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/ftp.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <assert.h>

#include "ftp_session.h"
#include "ftp_command.h"
#include "ftp_log.h"
#include "file_list.h"

/*====== Ftp Access Control Commands Handler ================ */

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


void DoQuit(FtpSession *f, const FtpCommand *cmd){
  assert(f != NULL);
  assert(cmd != NULL);
  assert(cmd->num_arg == 0);

  FtpSessionReply(f, 221, "Service closing control connection.");
  f->session_active = 0;
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
    FtpSessionReply(f, 550, "Directory change failed; %s", strerror(errno));
    return;
  }

  /* if everything is okay, gets new directory's full path */
  if (getcwd(dir, sizeof(dir)) == NULL) {
    assert(chdir(f->dir) == 0);
    FtpSessionReply(f, 550, "Directory change failed. %s", strerror(errno));
  } else {
    strcpy(f->dir, dir);
    FtpSessionReply(f, 250, "Directory change to %s successful.", f->dir);
  }
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

/*====== Ftp Transfer Parameters Commands Handler =========== */
static void InitPassivePort();
static int GetPassivePort();
static int SetPasv(FtpSession *f, struct sockaddr_in *bind_addr);

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

/* pick a server port to listen for connection on */
void DoPasv(FtpSession *f, const FtpCommand *cmd) {
  int socket_fd;
  unsigned int addr;
  int port;

  assert(f != NULL);
  assert(cmd != NULL);
  assert(cmd->num_arg == 0);

  socket_fd = SetPasv(f, &f->server_addr);
  if (socket_fd == -1) {
    return;
  }

  /* report port to client */
  addr = ntohl(f->server_addr.sin_addr.s_addr);
  port = ntohs(f->server_addr.sin_port);
  FtpSessionReply(f, 227, "Entering Passive Mode (%d,%d,%d,%d,%d,%d).",
                  addr >> 24,	(addr >> 16) & 0xff, (addr >> 8) & 0xff,
                  addr & 0xff, port >> 8, port & 0xff);

  /* close any outstanding PASSIVE port */
  if (f->data_channel == DATA_PASSIVE) {
    close(f->server_fd);
  }
  f->data_channel = DATA_PASSIVE;
  f->server_fd = socket_fd;
}

/* seed the random number generator used to pick a port */
static void InitPassivePort() {
  struct timeval tv;
  unsigned short int seed[3];

  gettimeofday(&tv, NULL);
  seed[0] = (tv.tv_sec >> 16) & 0xFFFF;
  seed[1] = tv.tv_sec & 0xFFFF;
  seed[2] = tv.tv_usec & 0xFFFF;
  seed48(seed);
}

/* pick a port to try to bind() for passive FTP connections */
static int GetPassivePort() {
  static pthread_once_t once_control = PTHREAD_ONCE_INIT;
  static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  int port;

  /* initialize the random number generator the first time we're called */
  pthread_once(&once_control, InitPassivePort);

  /* pick a random port between 1024 and 65535, inclusive */
  pthread_mutex_lock(&mutex);
  port = (lrand48() % 64512) + 1024;
  pthread_mutex_unlock(&mutex);

  return port;
}

static int SetPasv(FtpSession *f, struct sockaddr_in *bind_addr) {
  int socket_fd;
  int port;

  assert(f != NULL);
  assert(bind_addr != NULL);

  socket_fd = socket(bind_addr->sin_family, SOCK_STREAM, 0);
  if (socket_fd == -1) {
    FtpSessionReply(f, 500, "Error creating server socket; %s.", strerror(errno));
    return -1;
  }

  for (;;) {
    port = GetPassivePort();
    bind_addr->sin_port = htons(port);
    if (bind(socket_fd, (struct sockaddr *)bind_addr, sizeof(struct sockaddr)) == 0) {
      break;
    }
    if (errno != EADDRINUSE) {
      FtpSessionReply(f, 500, "Error binding server port; %s.", strerror(errno));
      close(socket_fd);
      return -1;
    }
  }

  if (listen(socket_fd, 1) != 0) {
    FtpSessionReply(f, 500, "Error listening on server port; %s.", strerror(errno));
    close(socket_fd);
    return -1;
  }

  return socket_fd;
}

/*====== Ftp Service Commands Handler ======================= */
static int OpenDataConnection(FtpSession *f);
static void GetAbsolutePath(char *fname, size_t fname_len,
                            const char *dir, const char *file);

void DoNoop(FtpSession *f, const FtpCommand *cmd){
  assert(f != NULL);
  assert(cmd != NULL);
  assert(cmd->num_arg == 0);

  FtpSessionReply(f, 200, "Command okay.");
  return;
}

void DoPwd(FtpSession *f, const FtpCommand *cmd) {
  assert(f != NULL);
  assert(cmd != NULL);
  assert(cmd->num_arg == 0);

  FtpSessionReply(f, 257, "\"%s\" is current directory", f->dir);
}

void DoMdtm(FtpSession *f, const FtpCommand *cmd) {
  const char *file_name;
  char full_path[PATH_MAX + 1 + MAX_STRING_LEN];
  struct stat stat_buf;

  assert(f != NULL);
  assert(cmd != NULL);
  assert(cmd->num_arg == 1);

  /* create an absolute name for file */
  file_name = cmd->arg[0].string;
  GetAbsolutePath(full_path, sizeof(full_path), f->dir, file_name);

  /* get the file information */
  if (stat(full_path, &stat_buf) != 0) {
    FtpSessionReply(f, 550, "Error getting file status; %s: %s.",
                    full_path, strerror(errno));
  } else {
    struct tm mtime;
    char time_buf[16];

    gmtime_r(&stat_buf.st_mtime, &mtime);
    strftime(time_buf, sizeof(time_buf), "%4Y%2m%2d%2H%2M%2S", &mtime);
    FtpSessionReply(f, 213, time_buf);
  }
}

void DoRest(FtpSession *f, const FtpCommand *cmd) {
  assert(f != NULL);
  assert(cmd != NULL);
  assert(cmd->num_arg == 1);

  if (f->data_type != TYPE_I) {
    FtpSessionReply(f, 555, "Restart not possible in ASCII mode.");
  } else if (f->file_structure != STRU_F) {
    FtpSessionReply(f, 555, "Restart only possible with FILE structure.");
  } else {
    f->file_offset = cmd->arg[0].offset;
    f->file_offset_command_number = f->command_number;
    FtpSessionReply(f, 350, "Restart okay, awaiting file retrieval request.");
  }
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
    GetAbsolutePath(dir_path, PATH_MAX, f->dir, cmd->arg[0].string);
   /* strcpy(dir_path, cmd->arg[0].string); */
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

void DoSyst(FtpSession *f, const FtpCommand *cmd) {
  assert(f != NULL);
  assert(cmd != NULL);
  assert(cmd->num_arg == 0);

  FtpSessionReply(f, 215, "UNIX.");
  return;
}

void DoStor(FtpSession *f, const FtpCommand *cmd){
  assert(f != NULL);
  assert(cmd != NULL);
  assert(cmd->num_arg == 1);

  FtpSessionReply(f, 553, "Server will not store files.");
}

static int ConvertNewlines(char *dst, const char *src, int srclen) {
  int i;
  int dstlen;

  assert(dst != NULL);
  assert(src != NULL);

  dstlen = 0;
  for (i=0; i < srclen; ++i) {
    if (src[i] == '\n') {
      dst[dstlen++] = '\r';
    }
    dst[dstlen++] = src[i];
  }
  return dstlen;
}

static int WriteFully(int fd, const char *buf, int buflen) {
  int amt_written;
  int write_ret;

  amt_written = 0;
  while (amt_written < buflen) {
    write_ret = write(fd, buf + amt_written, buflen - amt_written);
    if (write_ret <= 0) {
      return -1;
    }
    amt_written += write_ret;
  }

  return amt_written;
}

enum {
  kFileStatError = 1,
  kFileIsDirError,
  kFileResetError,
  kFileReadingError,
  kFileWritingError,
  kFileSendingError
};

static int SendFile(FtpSession *f, int file_fd, int out_fd, int *send_size) {
  int file_size = 0;
  struct stat stat_buf;

  if (fstat(file_fd, &stat_buf) != 0) {
    FtpSessionReply(f, 550, "Error getting file information; %s.", strerror(errno));
    return kFileStatError;
  }

  if (S_ISDIR(stat_buf.st_mode)) {
    FtpSessionReply(f, 550, "Error, file is a directory.");
    return kFileIsDirError;
  }

  /* if the last command was a REST command, restart at the */
  /* requested position in the file                         */
  if ((f->file_offset_command_number == (f->command_number - 1)) && (f->file_offset > 0)) {
    if (lseek(file_fd, f->file_offset, SEEK_SET) == -1) {
      FtpSessionReply(f, 550, "Error seeking to restart position; %s.", strerror(errno));
      return kFileResetError;
    }
  }

  if (f->data_type == TYPE_A) {
    int read_ret = 0;
    char buf[4096], converted_buf[8192];
    int converted_buflen;

    for (;;) {
      read_ret = read(file_fd, buf, sizeof(buf));
      if (read_ret == -1) {
        FtpSessionReply(f, 550, "Error reading from file; %s.", strerror(errno));
        return kFileReadingError;
      }
      if (read_ret == 0) {
        return 0;
      }
      converted_buflen = ConvertNewlines(converted_buf, buf, read_ret);
      if (WriteFully(out_fd, converted_buf, converted_buflen) == -1) {
        FtpSessionReply(f, 550, "Error writing to data connection; %s.", strerror(errno));
        return kFileWritingError;
      }
      file_size += converted_buflen;
    }
  } else if (f->data_type == TYPE_I) {
    int offset = 0, sendfile_ret = 0, amt_to_send;

    offset = f->file_offset;
    file_size = stat_buf.st_size - offset;
    while (offset < stat_buf.st_size) {
      amt_to_send = stat_buf.st_size - offset;
      if (amt_to_send > 65536) {
        amt_to_send = 65536;
      }
      sendfile_ret = sendfile(out_fd,
                              file_fd,
                              &offset,
                              amt_to_send);
      if (sendfile_ret != amt_to_send) {
        FtpSessionReply(f, 550, "Error sending file; %s.", strerror(errno));
        return kFileSendingError;
      }
    }
  }

  *send_size = file_size;
  return 0;
}

static struct timeval IntervalTime(struct timeval start, struct timeval end) {
  struct timeval interval;

  /* calculate transfer rate */
  interval.tv_sec = end.tv_sec - start.tv_sec;
  interval.tv_usec = end.tv_usec - start.tv_usec;
  while (interval.tv_usec >= 1000000) {
    interval.tv_sec++;
    interval.tv_usec -= 1000000;
  }
  while (interval.tv_usec < 0) {
    interval.tv_sec--;
    interval.tv_usec += 1000000;
  }

  return interval;
}

void DoRetr(FtpSession *f, const FtpCommand *cmd){
  int file_fd, socket_fd;
  char full_path[PATH_MAX + 1 + MAX_STRING_LEN];
  const char *file_name;
  int file_size;
  struct timeval start_timestamp, end_timestamp, transfer_time;

  assert(f != NULL);
  assert(cmd != NULL);
  assert(cmd->num_arg == 1);

  /* set up for exit */
  file_fd = -1;
  socket_fd = -1;

  /* create an absolute name for our file */
  file_name = cmd->arg[0].string;
  GetAbsolutePath(full_path, sizeof(full_path), f->dir, file_name);

  /* open file */
  file_fd = open(full_path, O_RDONLY);
  if (file_fd == -1) {
    FtpSessionReply(f, 550, "Error opening file; %s.", strerror(errno));
    goto exit_retr;
  }

  /* ready to transfer */
  FtpSessionReply(f, 150, "About to open data connection.");

  /* mark start time */
  gettimeofday(&start_timestamp, NULL);

  /* open data connection */
  socket_fd = OpenDataConnection(f);
  if (socket_fd == -1) {
    goto exit_retr;
  }

  /* Sends the file */
  file_size = 0;
  if (SendFile(f, file_fd, socket_fd, &file_size)) {
    goto exit_retr;
  }

  /* disconnect */
  close(socket_fd);
  socket_fd = -1;

  FtpSessionReply(f, 226, "File transfer complete.");

  /* mark end time */
  gettimeofday(&end_timestamp, NULL);

  transfer_time = IntervalTime(start_timestamp, end_timestamp);

  /* Logs the transfer */
  FtpLog(LOG_INFO,
         "%s retrieved \"%s\", %ld bytes in %d.%06d seconds",
         f->client_addr_str,
         full_path,
         file_size,
         transfer_time.tv_sec,
         transfer_time.tv_usec);

exit_retr:
  f->file_offset = 0;
  if (socket_fd != -1) {
    close(socket_fd);
  }
  if (file_fd != -1) {
    close(file_fd);
  }
}

/* convert the user-entered file name into a full path on our local drive */
static void GetAbsolutePath(char *fname, size_t fname_len,
                            const char *dir, const char *file) {
  assert(fname != NULL);
  assert(dir != NULL);
  assert(file != NULL);

  if (*file == '/') {
    /* absolute path, use as input */
    assert(strlen(file) < fname_len);
    strcpy(fname, file);
  } else {
    /* construct a file name based on our current directory */
    assert(strlen(dir) + 1 + strlen(file) < fname_len);
    strcpy(fname, dir);
    /* add a seperating '/' if we're not at the root */
    if (fname[1] != '\0') {
      strcat(fname, "/");
    }
    /* and of course the actual file name */
    strcat(fname, file);
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
