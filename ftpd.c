#include "ftpd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <sys/types.h>
#include <signal.h>

#include "ftp_listener.h"

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

  /* Gets user's args */
  if (GetOptions(argc, argv, &port, &address,
                 &max_clients, &user_name, &dir_path) == 0) {
    perror("ftp option parse error.");
  }

  printf("ftp option parse success.\n");

  /* Checks the required parameters */
  if (user_name == NULL || dir_path == NULL) {
    PrintUsage("missing user and/or directory name");
    exit(1);
  }
  user_info = getpwnam(user_name);
  if (user_info == NULL) {
    fprintf(stderr, "%s: invalid user name\n", exe_name);
    exit(1);
  }

  printf("ftp user user and dir check success.\n");

  /* change to root directory */
  if (chroot(dir_path) != 0) {
    perror("chroot directory error");
    exit(1);
  }
  if (chdir("/") != 0) {
    perror("change to root directory error");
    exit(1);
  }

  printf("ftp root directory change success.\n");

  /* Avoids SIGPIPE on socket activity */
  signal(SIGPIPE, SIG_IGN);

  printf("ftp signal pipe ignore success.\n");

  /* Creates the main listener */
  if (!FtpListenerInit(&ftp_listener, address, port,
                       max_clients, INACTIVITY_TIMEOUT)) {
    perror("ftp listner init error.");
    exit(1);
  }

  printf("ftp listener init success.\n");

  /* Start the listener */
  if (FtpListenerStart(&ftp_listener) == 0) {
    perror("ftp listener start error.");
    exit(1);
  }

  printf("ftp server listening...\n");

  /* wait for a SIGTERM and exit gracefully */
  sigemptyset(&term_signal);
  sigaddset(&term_signal, SIGTERM);
  sigaddset(&term_signal, SIGINT);
  pthread_sigmask(SIG_BLOCK, &term_signal, NULL);
  sigwait(&term_signal, &sig);
  if (sig == SIGTERM) {
    printf("SIGTERM received, shutting down\n");
  } else {
    printf("SIGINT received, shutting down\n");
  }

  /* Stop the server */
  FtpListenerStop(&ftp_listener);
  printf("all connections finished, FTP server exiting.\n");
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
