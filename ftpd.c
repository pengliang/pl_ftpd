#include "ftpd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>

#include "ftp_listener.h"
#include "ftp_log.h"

static const char *exe_name = "ftpd";

static void PrintUsage(const char *error);
static int GetOptions(int argc, char *argv[], int *port,
                       char **address, int *max_clients,
                       char **usr_name, char **dir_path);

int main(int argc, char *argv[]) {
  int i = 0, port = 0, max_clients = 0;
  char *user_name, *dir_path, *address;
  char temp_buf[256];
  struct passwd *user_info;
  int sig;
  sigset_t term_signal;
  FtpListener ftp_listener;

  /* Sets default option */
  port = FTP_PORT;
  user_name = NULL;
  dir_path = NULL;
  address = FTP_ADDRESS;
  max_clients = MAX_CLIENTS;

  /* grab our executable name */
  if (argc > 0) {
    exe_name = argv[0];
  }

  /* verify we're running as root */
  if (geteuid() != 0) {
    fprintf(stderr, "%s: program needs root permission to run\n", exe_name);
    exit(1);
  }

  /* Gets user's args */
  if (GetOptions(argc, argv, &port, &address,
                 &max_clients, &user_name, &dir_path) == 0) {
    FtpLog(LOG_ERROR, "ftp option parse error.");
  }

  /* Checks the required parameters */
  if (user_name == NULL || dir_path == NULL) {
    PrintUsage("missing user and/or directory name");
    exit(1);
  }
  user_info = getpwnam(user_name);
  if (user_info == NULL) {
    FtpLog(LOG_ERROR, "%s: invalid user name", exe_name);
    exit(1);
  }

  /* change to root directory */
  if (chroot(dir_path) != 0) {
    FtpLog(LOG_ERROR, "chroot directory error", strerror(errno));
    exit(1);
  }
  if (chdir("/") != 0) {
    FtpLog(LOG_ERROR, "change to root directory error; %s", strerror(errno));
    exit(1);
  }

  /* Avoids SIGPIPE on socket activity */
  signal(SIGPIPE, SIG_IGN);

  /* Creates the main listener */
  if (!FtpListenerInit(&ftp_listener, address, port,
                       max_clients, INACTIVITY_TIMEOUT)) {
    FtpLog(LOG_ERROR, "ftp listner init error.");
    exit(1);
  }

  FtpLog(LOG_INFO, "ftp listener init success.");

  /* set user to be as inoffensive as possible */
  if (setgid(user_info->pw_gid) != 0) {
    FtpLog(LOG_ERROR, "error changing group; %s", strerror(errno));
    exit(1);
  }

  if (setuid(user_info->pw_uid) != 0) {
    FtpLog(LOG_ERROR, "error changing group; %s", strerror(errno));
    exit(1);
  }

  FtpLog(LOG_INFO, "ftp running as gid: %d, uid: %d", user_info->pw_gid, user_info->pw_uid);

  /* Start the listener */
  if (FtpListenerStart(&ftp_listener) == 0) {
    FtpLog(LOG_ERROR, "ftp listener start error.");
    exit(1);
  }

  FtpLog(LOG_INFO, "ftp server listening...");

  /* wait for a SIGTERM and exit gracefully */
  sigemptyset(&term_signal);
  sigaddset(&term_signal, SIGTERM);
  sigaddset(&term_signal, SIGINT);
  pthread_sigmask(SIG_BLOCK, &term_signal, NULL);
  sigwait(&term_signal, &sig);
  if (sig == SIGTERM) {
    FtpLog(LOG_INFO, "SIGTERM received, shutting down\n");
  } else {
    FtpLog(LOG_INFO, "SIGINT received, shutting down\n");
  }

  /* Stop the server */
  FtpListenerStop(&ftp_listener);

  FtpLog(LOG_INFO, "all connections finished, FTP server exiting.\n");
  exit(0);
}

static int GetOptions(int argc, char *argv[], int *port,
                       char **address, int *max_clients,
                       char **user_name, char **dir_path) {
  int i = 0, num = 0;
  char temp_buf[256];
  char *end_ptr;

  /* Parses command-line arguments */
  for (i = 1; i < argc; ++i) {
    if (argv[i][0] == '-') {
      if (strcmp(argv[i], "-p") == 0) {
        if (++i > argc) {
          PrintUsage("missing port number");
          return 0;
        }
        num = strtol(argv[i], &end_ptr, 0);
        if ((num < MIN_PORT) || (num > MAX_PORT) || (*end_ptr != '\0')) {
          snprintf(temp_buf, sizeof(temp_buf),
                   "port must be a number between %d and %d",
                   MIN_PORT, MAX_PORT);
          PrintUsage(temp_buf);
          return 0;
        }
        *port = num;
      } else if (strcmp(argv[i], "-h") == 0) {
        PrintUsage(NULL);
      } else if (strcmp(argv[i], "-i") == 0) {
        if (++i > argc) {
          PrintUsage("missing interface");
          return 0;
        }
        *address = argv[i];
      } else if (strcmp(argv[i], "-m") == 0) {
        if (++i > argc) {
          PrintUsage("missing number of max clients");
          return 0;
        }
        num = strtol(argv[i], &end_ptr, 0);
        if ((num < MIN_PORT) || (num > MAX_PORT) || (*end_ptr != '\0')) {
          snprintf(temp_buf, sizeof(temp_buf),
                   "port must be a number between %d and %d",
                   MIN_PORT, MAX_PORT);
          PrintUsage(temp_buf);
          return 0;
        }
        *max_clients = num;
      } else {
        PrintUsage("Unknown option");
        return 0;
      }
    } else {
      if (*user_name == NULL) {
        *user_name = argv[i];
      } else if (*dir_path == NULL) {
        *dir_path = argv[i];
      } else {
        PrintUsage("too many arguments on the command line");
        return 0;
      }
    }
  }
  return 1;
}

static void PrintUsage(const char *error) {
  if (error != NULL) {
    fprintf(stderr, "%s: %s\n", exe_name, error);
  }

  fprintf(stderr,
          " Syntax: %s [ options... ] user_name root_directory\n", exe_name);
  fprintf(stderr,
          " Options:\n"
          " -p, <num>\n"
          "     Set the port to listen on (Default: %d)\n"
          " -i, <IP Address>\n"
          "     Set the interface to listen on (Default: all)\n"
          " -m, <num>\n"
          "     Set the number of clients allowed at one time (Default: %d)\n",
          DEFAULT_FTP_PORT, MAX_CLIENTS);
}
