#include "ftp_log.h"
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>

static const char *log_header[] = {
  "Debug", "Info", "Warning", "Error"
};

static const int kMaxCode = sizeof(log_header) / sizeof(log_header[0]);

int FtpLog(int code, const char *fmt, ...) {
  va_list ap;

  assert(code < kMaxCode);

  va_start(ap, fmt);
  printf("%s: ", log_header[code]);
  vprintf(fmt, ap);

  if (code == LOG_ERROR) {
    perror(NULL);
  }
  putchar('\n');
  va_end(ap);

  return 0;
}
