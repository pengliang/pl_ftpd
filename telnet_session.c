#include "telnet_session.h"
#include <stdio.h>
#include <stdlib.h>
#include <arpa/telnet.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

enum InputState {
  kNormal,
  kIAC,
  kWILL,
  kWONT,
  kDO,
  kDONT,
  kCR
};

static int MaxInputRead(TelnetSession *t);
static void ProcessData(TelnetSession *t, int wait_flag);
static void ReadData(TelnetSession *t);
static void ProcessInputChar(TelnetSession *t, int c);
static void AddIncomingChar(TelnetSession *t, int c);
static int TakeIncomingChar(TelnetSession *t);
static void AddOutgoingChar(TelnetSession *t, int c);
static void WriteData(TelnetSession *t);

void TelnetServerInit(TelnetSession *t, int in, int out) {
  assert(t != NULL);
  assert(in >= 0);
  assert(out >= 0);

  t->in_fd = in;
  t->in_errno = 0;
  t->in_eof = 0;
  t->in_take = t->in_add = 0;
  t->in_buflen = 0;
  t->in_status = kNormal;

  t->out_fd = out;
  t->out_errno = 0;
  t->out_eof = 0;
  t->out_take = t->out_add = 0;
  t->out_buflen = 0;

  ProcessData(t, 0);
}

void TelnetDestroy(TelnetSession *t) {
  close(t->in_fd);
  if (t->out_fd != t->in_fd) {
    close(t->out_fd);
  }

  t->in_fd = -1;
  t->out_fd = -1;
}

int TelnetPrint(TelnetSession *t, const char *s) {
  int len, amt_printed;

  len = strlen(s);
  if (len == 0) {
    return 1;
  }

  amt_printed = 0;
  do {
    if ((t->out_errno != 0) || (t->out_eof != 0)) {
      return 0;
    }
    while ((amt_printed < len) && (t->out_buflen < BUF_LEN)) {
      AddOutgoingChar(t, s[amt_printed]);
      amt_printed++;
    }
    ProcessData(t, 1);
  } while (amt_printed < len);

  while (t->out_buflen > 0) {
    if ((t->out_errno != 0) || (t->out_eof != 0)) {
      return 0;
    }
    ProcessData(t, 1);
  }

  return 1;
}

int TelnetPrintLine(TelnetSession *t, const char *s) {
  if (!TelnetPrint(t, s)) {
    return 0;
  }
  if (!TelnetPrint(t, "\015\012")) {
    return 0;
  }
  return 1;
}

int TelnetReadLine(TelnetSession *t, char *buf, int buflen) {
  int amt_read;

  amt_read = 0;
  for (;;) {
    if ((t->in_errno != 0) || (t->in_eof != 0)) {
      return 0;
    }

    while (t->in_buflen > 0) {
      if (amt_read == buflen-1) {
        buf[amt_read] = '\0';
        return 1;
      }
      assert(amt_read >= 0);
      assert(amt_read < buflen);

      buf[amt_read] = TakeIncomingChar(t);

      if (buf[amt_read] == '\012') {
        assert(amt_read + 1 >= 0);
        assert(amt_read + 1 < buflen);

        buf[amt_read+1] = '\0';
        return 1;
      }
      amt_read++;
    }

    ProcessData(t, 1);
  }
}

/* receive any incoming data, send any pending data */
static void ProcessData(TelnetSession *t, int wait_flag) {
  fd_set read_fds;
  fd_set write_fds;
  fd_set except_fds;
  struct timeval tv_zero;
  struct timeval *tv;

  FD_ZERO(&read_fds);
  FD_ZERO(&write_fds);
  FD_ZERO(&except_fds);

  if (wait_flag) {
    tv = NULL;
  } else {
    tv_zero.tv_sec = 0;
    tv_zero.tv_usec = 0;
    tv = &tv_zero;
  }

  /* Checks for input if we can accept input */
  if ((t->in_errno == 0) && (t->in_eof == 0) && (MaxInputRead(t) > 0)) {
    FD_SET(t->in_fd, &read_fds);
    FD_SET(t->in_fd, &except_fds);
  }

  /* Checks for output if we have pending output */
  if ((t->out_errno == 0) && (t->out_eof == 0) && (t->out_buflen > 0)) {
    FD_SET(t->out_fd, &write_fds);
    FD_SET(t->out_fd, &except_fds);
  }

  /* See if there's anything to do */
  if (select(FD_SETSIZE, &read_fds, &write_fds, &except_fds, tv) > 0) {
    if (FD_ISSET(t->in_fd, &except_fds)) {
      t->in_eof = 1;
    } else if (FD_ISSET(t->in_fd, &read_fds)) {
      ReadData(t);
    }
    if (FD_ISSET(t->out_fd, &except_fds)) {
      t->out_eof = 1;
    } else if (FD_ISSET(t->out_fd, &write_fds)) {
      WriteData(t);
    }
  }
}

static void ReadData(TelnetSession *t) {
  int read_ret, i = 0;
  char buf[BUF_LEN];

  /* read as much data as we have buffer space for */
  assert(MaxInputRead(t) <= BUF_LEN);
  read_ret = read(t->in_fd, buf, MaxInputRead(t));

  /* handle three possible read results */
  if (read_ret == -1) {
    t->in_errno = errno;
  } else if (read_ret == 0) {
    t->in_eof = 1;
  } else {
    for (i = 0; i < read_ret; ++i) {
      ProcessInputChar(t, (unsigned char)buf[i]);
    }
  }
}

/* process a single character */
static void ProcessInputChar(TelnetSession *t, int c) {
  switch (t->in_status) {
    case kIAC:
      switch (c) {
        case WILL:
          t->in_status = kWILL;
          break;
        case WONT:
          t->in_status = kWONT;
          break;
        case DO:
          t->in_status = kDO;
          break;
        case DONT:
          t->in_status = kDONT;
          break;
        case IAC:
          AddIncomingChar(t, IAC);
          t->in_status = kNormal;
          break;
        default:
          t->in_status = kNormal;
      }
      break;
    case kWILL:
      AddOutgoingChar(t, IAC);
      AddOutgoingChar(t, DONT);
      AddOutgoingChar(t, c);
      t->in_status = kNormal;
      break;
    case kWONT:
      t->in_status = kNormal;
      break;
    case kDO:
      AddOutgoingChar(t, IAC);
      AddOutgoingChar(t, WONT);
      AddOutgoingChar(t, c);
      t->in_status = kNormal;
      break;
    case kDONT:
      t->in_status = kNormal;
      break;
    case kCR:
      AddIncomingChar(t, '\012');
      if (c != '\012') {
        AddIncomingChar(t, c);
      }
      t->in_status = kNormal;
      break;
    case kNormal:
      if (c == IAC) {
        t->in_status = kIAC;
      } else if (c == '\015') {
        t->in_status = kCR;
      } else {
        AddIncomingChar(t, c);
      }
  }
}

/* add a single character, wrapping buffer if necessary (should never occur) */
static void AddIncomingChar(TelnetSession *t, int c) {
  assert(t->in_add >= 0);
  assert(t->in_add < BUF_LEN);

  t->in_buf[t->in_add] = c;
  t->in_add++;
  if (t->in_add == BUF_LEN) {
    t->in_add = 0;
  }

  if (t->in_buflen == BUF_LEN) {
    t->in_take++;
    if (t->in_take == BUF_LEN) {
      t->in_take = 0;
    }
  } else {
    t->in_buflen++;
  }
}

/* remove a single character */
static int TakeIncomingChar(TelnetSession *t) {
  int c;

  assert(t->in_take >= 0);
  assert(t->in_take < BUF_LEN);

  c = t->in_buf[t->in_take];
  t->in_take++;
  if (t->in_take == BUF_LEN) {
    t->in_take = 0;
  }
  t->in_buflen--;

  return c;
}

/* add a single character, hopefully will never happen :) */
static void AddOutgoingChar(TelnetSession *t, int c) {
  assert(t->out_add >= 0);
  assert(t->out_add < BUF_LEN);

  t->out_buf[t->out_add] = c;
  t->out_add++;
  if (t->out_add == BUF_LEN) {
    t->out_add = 0;
  }

  if (t->out_buflen == BUF_LEN) {
    t->out_take++;
    if (t->out_take == BUF_LEN) {
      t->out_take = 0;
    }
  } else {
    t->out_buflen++;
  }
}

static void WriteData(TelnetSession *t) {
  int write_ret;

  if (t->out_take < t->out_add) {
    /* handle a buffer that looks like this:       */
    /*     |-- empty --|-- data --|-- empty --|    */
    assert(t->out_take >= 0);
    assert(t->out_take < BUF_LEN);
    assert(t->out_buflen > 0);
    assert(t->out_buflen + t->out_take <= BUF_LEN);
    write_ret = write(t->out_fd, t->out_buf + t->out_take, t->out_buflen);
  } else {
    /* handle a buffer that looks like this:       */
    /*     |-- data --|-- empty --|-- data --|     */
    assert(t->out_take >= 0);
    assert(t->out_take < BUF_LEN);
    assert((BUF_LEN - t->out_take) > 0);
    write_ret = write(t->out_fd, t->out_buf + t->out_take,
                      BUF_LEN - t->out_take);
  }

  /* handle three possible write results */
  if (write_ret == -1) {
    t->out_errno = errno;
  } else if (write_ret == 0) {
    t->out_eof = 1;
  } else {
    t->out_buflen -= write_ret;
    t->out_take += write_ret;
    if (t->out_take >= BUF_LEN) {
      t->out_take -= BUF_LEN;
    }
  }
}

/* Returnes the amount of a read */
static int MaxInputRead(TelnetSession *t) {
  int max_in, max_out;

  /* figure out how much space is available in the input buffer */
  if (t->in_buflen < BUF_LEN) {
    max_in = BUF_LEN - t->in_buflen;
  } else {
    max_in = 0;
  }

  /* worry about space in the output buffer (for DONT/WONT replies) */
  if (t->out_buflen < BUF_LEN) {
    max_out = BUF_LEN - t->out_buflen;
  } else {
    max_out = 0;
  }

  /* return the minimum of the two values */
  return (max_in < max_out) ? max_in : max_out;
}

#ifdef STUB_TEST
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <ctype.h>

int main() {
  TelnetSession t;
  char buf[64];
  int fd, newfd, val, i, error;
  struct sockaddr_in addr, newaddr;
  unsigned newaddrlen;

  memset(&addr, 0, sizeof(addr));
  memset(&newaddr, 0, sizeof(newaddr));

  assert((fd = socket(AF_INET, SOCK_STREAM, 0)) != -1);
  val = 1;
  assert(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) == 0);

  addr.sin_family = AF_INET;
  addr.sin_port = htons(23);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind failed.");
    exit(1);
  }
  assert(listen(fd, SOMAXCONN) == 0);

  signal(SIGPIPE, SIG_IGN);
  while ((newfd = accept(fd, (struct sockaddr *)&newaddr, &newaddrlen)) >= 0) {
    TelnetServerInit(&t, newfd, newfd);
    TelnetPrint(&t, "Password:");
    TelnetReadLine(&t, buf, sizeof(buf));
    for (i = 0; buf[i] != '\0'; ++i) {
      printf("[%02d]: 0x%02X ", i, buf[i] & 0xFF);
      if (isprint(buf[i])) {
        printf("'%c'\n", buf[i]);
      } else {
        printf("'\\x%02X'\n", buf[i] & 0xFF);
      }
    }
    TelnetPrintLine(&t, "Hello, world.");
    TelnetPrintLine(&t, "Ipso, facto");
    TelnetPrintLine(&t, "quid pro quo");
    sleep(1);
    close(newfd);
  }
  return 0;
}
#endif /* STUB_TEST */

