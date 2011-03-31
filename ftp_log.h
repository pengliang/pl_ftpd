#ifndef FTP_LOG_H
#define FTP_LOG_H

#define LOG_DEBUG 0
#define LOG_INFO 1
#define LOG_WARNING 2
#define LOG_ERROR 3

int FtpLog(int code, const char *fmt, ...);

#endif /* FTP_LOG_H */
