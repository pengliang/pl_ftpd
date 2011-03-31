#include "ftp_command.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <assert.h>

/* argument types */
#define ARG_NONE              0
#define ARG_STRING            1
#define ARG_OPTIONAL_STRING   2
#define ARG_HOST_PORT         3
#define ARG_TYPE              4
#define ARG_STRUCTURE         5
#define ARG_MODE              6
#define ARG_OFFSET            7

/* our FTP commands */
struct {
  char *name;
  int arg_type;
} command_def[] = {
  { "AUTH", ARG_STRING          },
  { "USER", ARG_STRING          },
  { "PASS", ARG_STRING          },
  { "CWD",  ARG_STRING          },
  { "CDUP", ARG_NONE            },
  { "QUIT", ARG_NONE            },
  { "PORT", ARG_HOST_PORT       },
  { "PASV", ARG_NONE            },
  { "TYPE", ARG_TYPE            },
  { "STRU", ARG_STRUCTURE       },
  { "MODE", ARG_MODE            },
  { "RETR", ARG_STRING          },
  { "STOR", ARG_STRING          },
  { "PWD",  ARG_NONE            },
  { "LIST", ARG_OPTIONAL_STRING },
  { "NLST", ARG_OPTIONAL_STRING },
  { "SYST", ARG_NONE            },
  { "HELP", ARG_OPTIONAL_STRING },
  { "NOOP", ARG_NONE            },
  { "REST", ARG_OFFSET          },
  { "SIZE", ARG_STRING          },
  { "MDTM", ARG_STRING          }
};

static const int kCommandNum = sizeof(command_def) / sizeof(command_def[0]);

/* prototypes */
static const char *CopyLine(char *dst, const char *src);
static const char *ParseHostPort(struct sockaddr_in *addr, const char *s);
static const char *ParseNumber(int *num, const char *s, int max_num);
static const char *ParseOffset(off_t *ofs, const char *s);

int FtpCommandParse(const char *input, FtpCommand *cmd) {
  int i, len, match;
  FtpCommand tmp;
  int c;
  const char *optional_number;

  assert(input != NULL);
  assert(cmd != NULL);

  /* see if our input starts with a valid command */
  match = -1;
  for (i = 0; (i < kCommandNum) && (match == -1); ++i) {
    len = strlen(command_def[i].name);
    if (strncasecmp(input, command_def[i].name, len) == 0) {
      match = i;
    }
  }

  /* if we didn't find a match, return error */
  if (match == -1) {
    return COMMAND_UNRECOGNIZED;
  }

  assert(match >= 0);
  assert(match < kCommandNum);

  /* copy our command */
  strcpy(tmp.command, command_def[match].name);

  /* advance input past the command */
  input += strlen(command_def[match].name);

  /* now act based on the command */
  switch (command_def[match].arg_type) {
    case ARG_NONE:
      tmp.num_arg = 0;
      break;
    case ARG_STRING:
      if (*input != ' ') {
        goto Parameter_Error;
      }
      ++input;
      input = CopyLine(tmp.arg[0].string, input);
      tmp.num_arg = 1;
      break;
    case ARG_OPTIONAL_STRING:
      if (*input == ' ') {
        ++input;
        input = CopyLine(tmp.arg[0].string, input);
        tmp.num_arg = 1;
      } else {
        tmp.num_arg = 0;
      }
      break;
    case ARG_HOST_PORT:
      if (*input != ' ') {
        goto Parameter_Error;
      }
      input++;
      /* parse the host & port information (if any) */
      input = ParseHostPort(&tmp.arg[0].host_port, input);
      if (input == NULL) {
        goto Parameter_Error;
      }
      tmp.num_arg = 1;
      break;
    case ARG_TYPE:
      if (*input != ' ') {
        goto Parameter_Error;
      }
      input++;

      c = toupper(*input);
      if ((c == 'A') || (c == 'E')) {
        tmp.arg[0].string[0] = c;
        tmp.arg[0].string[1] = '\0';
        input++;
        if (*input == ' ') {
          input++;
          c = toupper(*input);
          if ((c != 'N') && (c != 'T') && (c != 'C')) {
            goto Parameter_Error;
          }
          tmp.arg[1].string[0] = c;
          tmp.arg[1].string[1] = '\0';
          input++;
          tmp.num_arg = 2;
        } else {
          tmp.num_arg = 1;
        }
      } else if (c == 'I') {
        tmp.arg[0].string[0] = 'I';
        tmp.arg[0].string[1] = '\0';
        input++;
        tmp.num_arg = 1;
      } else if (c == 'L') {
        tmp.arg[0].string[0] = 'L';
        tmp.arg[0].string[1] = '\0';
        input++;
        input = ParseNumber(&tmp.arg[1].num, input, 255);
        if (input == NULL) {
          goto Parameter_Error;
        }
        tmp.num_arg = 2;
      } else {
        goto Parameter_Error;
      }
      break;
    case ARG_STRUCTURE:
      if (*input != ' ') {
        goto Parameter_Error;
      }
      input++;
      c = toupper(*input);
      if ((c != 'F') && (c != 'R') && (c != 'P')) {
        goto Parameter_Error;
      }
      input++;
      tmp.arg[0].string[0] = c;
      tmp.arg[0].string[1] = '\0';
      tmp.num_arg = 1;
      break;
    case ARG_MODE:
      if (*input != ' ') {
        goto Parameter_Error;
      }
      input++;
      c = toupper(*input);
      if ((c != 'S') && (c != 'B') && (c != 'C')) {
        goto Parameter_Error;
      }
      input++;
      tmp.arg[0].string[0] = c;
      tmp.arg[0].string[1] = '\0';
      tmp.num_arg = 1;
      break;
    case ARG_OFFSET:
      if (*input != ' ') {
        goto Parameter_Error;
      }
      input++;
      input = ParseOffset(&tmp.arg[0].offset, input);
      if (input == NULL) {
        goto Parameter_Error;
      }
      tmp.num_arg = 1;
      break;
    default:
      assert(0);
  }

  /* check for our ending newline */
  if (*input != '\n') {
Parameter_Error:
    return COMMAND_PARAMETERS_ERROR;
  }

  /* return our result */
  *cmd = tmp;
  return 0;
}

/* copy a string terminated with a newline */
static const char *CopyLine(char *dst, const char *src) {
  int i = 0;

  assert(dst != NULL);
  assert(src != NULL);

  while (i <= MAX_STRING_LEN && src[i] != '\0' && src[i] != '\n') {
    dst[i] = src[i];
    ++i;
  }
  dst[i] = '\0';

  return src + i;
}

static const char *ParseHostPort(struct sockaddr_in *addr, const char *s) {
  int i;
  int octets[6];
  char addr_str[16];
  int port;
  struct in_addr in_addr;

  assert(addr != NULL);
  assert(s != NULL);

  /* scan in 5 pairs of "#," */
  for (i = 0; i < 5; ++i) {
    s = ParseNumber(&octets[i], s, 255);
    if (s == NULL) {
      return NULL;
    }
    if (*s != ',') {
      return NULL;
    }
    s++;
  }

  /* scan in ending "#" */
  s = ParseNumber(&octets[5], s, 255);
  if (s == NULL) {
    return NULL;
  }

  assert(octets[0] >= 0);
  assert(octets[0] <= 255);
  assert(octets[1] >= 0);
  assert(octets[1] <= 255);
  assert(octets[2] >= 0);
  assert(octets[2] <= 255);
  assert(octets[3] >= 0);
  assert(octets[3] <= 255);

  /* convert our number to a IP/port */
  sprintf(addr_str, "%d.%d.%d.%d",
          octets[0], octets[1], octets[2], octets[3]);
  port = (octets[4] << 8) | octets[5];

  if (inet_aton(addr_str, &in_addr) == 0) {
    return NULL;
  }

  addr->sin_family = AF_INET;
  addr->sin_addr = in_addr;
  addr->sin_port = htons(port);

  /* return success */
  return s;
}

/* scan the string for a number from 0 to max_num */
/* returns the next non-numberic character */
/* returns NULL if not at least one digit */
static const char *ParseNumber(int *num, const char *s, int max_num) {
  int tmp;
  int cur_digit;

  assert(s != NULL);
  assert(num != NULL);

  /* handle first character */
  if (!isdigit(*s)) {
    return NULL;
  }
  tmp = (*s - '0');
  s++;

  /* handle subsequent characters */
  while (isdigit(*s)) {
    cur_digit = (*s - '0');

    /* check for overflow */
    if ((max_num - cur_digit) < (tmp * 10)) {
      return NULL;
    }

    tmp *= 10;
    tmp += cur_digit;
    s++;
  }

  assert(tmp >= 0);
  assert(tmp <= max_num);

  /* return the result */
  *num = tmp;
  return s;
}

static const char *ParseOffset(off_t *ofs, const char *s) {
  off_t tmp_ofs;
  int cur_digit;
  off_t max_ofs;

  assert(ofs != NULL);
  assert(s != NULL);

  /* calculate maximum allowable offset based on size of off_t */
  if (sizeof(off_t) == 8) {
    max_ofs = (off_t)9223372036854775807LL;
  } else if (sizeof(off_t) == 4) {
    max_ofs = (off_t)2147483647LL;
  } else if (sizeof(off_t) == 2) {
    max_ofs = (off_t)32767LL;
  } else {
    max_ofs = 0;
  }
  assert(max_ofs != 0);
  /* handle first character */
  if (!isdigit(*s)) {
    return NULL;
  }
  tmp_ofs = (*s - '0');
  s++;

  /* handle subsequent characters */
  while (isdigit(*s)) {
    cur_digit = (*s - '0');

    /* check for overflow */
    if ((max_ofs - cur_digit) < (tmp_ofs * 10)) {
      return NULL;
    }

    tmp_ofs *= 10;
    tmp_ofs += cur_digit;
    s++;
  }
  /* return */
  *ofs = tmp_ofs;
  return s;
}

