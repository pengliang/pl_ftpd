#include "file_list.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <stdarg.h>
#include <errno.h>
#include <assert.h>
#include "ftp_log.h"

typedef struct {
  char name[PATH_MAX + 1];
  char full_path[PATH_MAX + 1];
  struct stat stat;
} FileInfo;

static void fdprintf(int fd, const char *fmt, ...);
static int GetFileList(const char *dir_name,
                       FileInfo **file_info_list, int *num_files);
static int GetAbsolutePath(char *abs_path, int abs_len, const char *rel_path);

int PrintFileNameList(int out_fd, const char *dir_name) {
  DIR *dp;
  struct dirent *ep;

  assert(out_fd >= 0);

  dp = opendir(dir_name);
  if (dp != NULL) {
    while (ep = readdir(dp)) {
      fdprintf(out_fd, "%s\r\n", ep->d_name);
    }
    closedir (dp);
  } else {
    return 0;
  }

  return 1;
}

int PrintFileFullList(int out_fd, const char *dir_name) {
  char file_link[PATH_MAX + 1];
  mode_t mode;
  time_t now;
  struct tm tm_now;
  FileInfo *file_info = NULL;
  int num_files = 0, link_len = 0, i = 0;
  double file_age;
  char date_buf[20];

  assert(out_fd >= 0);

  if (!GetFileList(dir_name, &file_info, &num_files)) {
    return 0;
  }

  /* outputs the total number */
  if (num_files == 0) {
    fdprintf(out_fd, "total 0\r\n");
    return 1;
  } else {
    fdprintf(out_fd, "total %lu\r\n", num_files);
  }

  time(&now);
  for (i = 0; i < num_files; ++i) {
    mode = file_info[i].stat.st_mode;

    /* outputs file type */
    switch (mode & S_IFMT) {
      case S_IFSOCK:  fdprintf(out_fd, "s"); break;
      case S_IFLNK:   fdprintf(out_fd, "l"); break;
      case S_IFBLK:   fdprintf(out_fd, "b"); break;
      case S_IFDIR:   fdprintf(out_fd, "d"); break;
      case S_IFCHR:   fdprintf(out_fd, "c"); break;
      case S_IFIFO:   fdprintf(out_fd, "p"); break;
      default:        fdprintf(out_fd, "-");
    }

    /* out_fdput permissions */
    fdprintf(out_fd, (mode & S_IRUSR) ? "r" : "-");
    fdprintf(out_fd, (mode & S_IWUSR) ? "w" : "-");
    if (mode & S_ISUID) {
      fdprintf(out_fd, (mode & S_IXUSR) ? "s" : "S");
    } else {
      fdprintf(out_fd, (mode & S_IXUSR) ? "x" : "-");
    }
    fdprintf(out_fd, (mode & S_IRGRP) ? "r" : "-");
    fdprintf(out_fd, (mode & S_IWGRP) ? "w" : "-");
    if (mode & S_ISGID) {
      fdprintf(out_fd, (mode & S_IXGRP) ? "s" : "S");
    } else {
      fdprintf(out_fd, (mode & S_IXGRP) ? "x" : "-");
    }
    fdprintf(out_fd, (mode & S_IROTH) ? "r" : "-");
    fdprintf(out_fd, (mode & S_IWOTH) ? "w" : "-");
    if (mode & S_ISVTX) {
      fdprintf(out_fd, (mode & S_IXOTH) ? "t" : "T");
    } else {
      fdprintf(out_fd, (mode & S_IXOTH) ? "x" : "-");
    }

    /* out_fdput link & ownership information */
    fdprintf(out_fd, " %3d %-8d %-8d ",
             file_info[i].stat.st_nlink,
             file_info[i].stat.st_uid,
             file_info[i].stat.st_gid);

    /* out_fdput either i-node information or size */
    fdprintf(out_fd, "%8lu ", (unsigned long)file_info[i].stat.st_size);

    /* out_fdput date */
    localtime_r(&file_info[i].stat.st_mtime, &tm_now);
    file_age = difftime(now, file_info[i].stat.st_mtime);
    if ((file_age > 60 * 60 * 24 * 30 * 6) || (file_age < -(60 * 60 * 24 * 30 * 6))) {
      strftime(date_buf, sizeof(date_buf), "%b %e  %Y", &tm_now);
    } else {
      strftime(date_buf, sizeof(date_buf), "%b %e %H:%M", &tm_now);
    }
    fdprintf(out_fd, "%s ", date_buf);

    /* out_fdput filename */
    fdprintf(out_fd, "%s", file_info[i].name);

    /* display symbolic link information */
    if ((mode & S_IFMT) == S_IFLNK) {
      link_len = readlink(file_info[i].name, file_link, sizeof(file_link));
      if (link_len > 0) {
        fdprintf(out_fd, " -> ");
        file_link[link_len] = '\0';
        fdprintf(out_fd, "%s", file_link);
      }
    }

    /* advance to next line */
    fdprintf(out_fd, "\r\n");
  }

  /* free memory & return */
  free(file_info);
  return 1;
}

static void fdprintf(int fd, const char *fmt, ...) {
  char buf[PATH_MAX + 1];
  size_t buflen;
  va_list ap;
  size_t amt_written;
  int write_ret;

  assert(fd >= 0);
  assert(fmt != NULL);

  va_start(ap, fmt);
  buflen = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (buflen <= 0) {
    return;
  }
  if (buflen >= sizeof(buf)) {
    buflen = sizeof(buf) - 1;
  }

  amt_written = 0;
  while (amt_written < buflen) {
    write_ret = write(fd, buf + amt_written, buflen - amt_written);
    if (write_ret <= 0) {
      return;
    }
    amt_written += write_ret;
  }
}

static int GetFileList(const char *full_path, FileInfo **file_info_list, int *num_files) {
  int n = 0, i = 0;
  struct dirent **file_list;
  FileInfo *file_info = NULL;
  struct stat file_stat;

  if (stat(full_path, &file_stat) == -1) {
    return 0;
  }

  if (!S_ISDIR(file_stat.st_mode)) {
    *num_files = 1;
    file_info = (FileInfo *)malloc(sizeof(FileInfo) * 1);
    strcpy(file_info->name, full_path);
    strcpy(file_info->full_path, full_path);
    memcpy(&file_info->stat, &file_stat, sizeof(file_stat));
    *file_info_list = file_info;
    return 1;
  }

  n = scandir(full_path, &file_list, NULL, alphasort);
  *num_files = n;

  if (n == -1) {
    return 0;
  } else if (n == 0) {
    return 1;
  }

  file_info = (FileInfo *)malloc(sizeof(FileInfo) * n);
  *file_info_list = file_info;

  if (file_info == NULL) {
    return 0;
  }

  for (i = 0; i < n; ++i) {
    assert(sizeof(file_info[i].name) >= strlen(file_list[i]->d_name));
    strcpy(file_info[i].name, file_list[i]->d_name);
    strcpy(file_info[i].full_path, full_path);
    strcpy(file_info[i].full_path, "/");
    strcat(file_info[i].full_path, file_list[i]->d_name);
    free(file_list[i]);
  }
  free(file_list);

  for (i = 0; i < n; ++i) {
    if (lstat(file_info[i].full_path, &file_info[i].stat) == -1) {
      return 0;
    }
  }
  return 1;
}

/*
static int GetAbsolutePath(char *abs_path, int abs_len, const char *rel_path) {
  const char *p;
  char *prev_dir, *path_end, *n;
  char cur_path[PATH_MAX + 1];
  int len;

  assert(abs_path != NULL);
  assert(abs_len > 0);
  assert(rel_path != NULL);

  p = rel_path;

  if (*p == '/') {
    // if this starts with a '/' it is an absolute path
    strcpy(abs_path, "/");
    do {
      p++;
    } while (*p == '/');

    strcat(abs_path, p);

    return 1;
  }

  // otherwise it's a relative path
  assert(getcwd(cur_path, sizeof(cur_path)) != 0);
  assert(strlen(cur_path) < (size_t)abs_len);
  strcpy(abs_path, cur_path);

  // append on each directory in relative path, handling "." and ".."
  while (*p != '\0') {
    // find the end of the next directory (either at '/' or '\0')
    if ((n = strchr(p, '/')) == NULL) {
      n = strchr(p, '\0');
    }
    len = n - p;
    if ((len == 1) && (p[0] == '.')) {
      // do nothing with "."
    } else if ((len == 2) && (p[0] == '.') && (p[1] == '.')) {
      // change to previous directory with ".."
      prev_dir = strrchr(abs_path, '/');
      assert(prev_dir != NULL);
      *prev_dir = '\0';
      if (prev_dir == abs_path) {
        strcpy(abs_path, "/");
      }
    } else {
      // otherwise add to current directory
      if ((strlen(abs_path) + 1 + len) > PATH_MAX) {
        return 0;
      }
      // append a '/' unless we were at the root directory
      path_end = strchr(abs_path, '\0');
      if (path_end != abs_path + 1) {
        *path_end++ = '/';
      }
      // add the directory itself
      while (p != n) {
        *path_end++ = *p++;
      }
      *path_end = '\0';
    }
    // advance to next directory to check
    p = n;
    // skip '/' characters
    while (*p == '/') {
      p++;
    }
  }

  return 1;
}
*/
