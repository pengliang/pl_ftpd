#ifndef FTP_COMMAND_HANDLER_H
#define FTP_COMMAND_HANDLER_H

#include "ftp_session.h"
#include "ftp_command.h"

/* command handlers */
void DoUser(FtpSession *f, const FtpCommand *cmd);
void DoPass(FtpSession *f, const FtpCommand *cmd);
void DoCwd(FtpSession *f, const FtpCommand *cmd);
void DoCdup(FtpSession *f, const FtpCommand *cmd);
void DoQuit(FtpSession *f, const FtpCommand *cmd);
void DoPort(FtpSession *f, const FtpCommand *cmd);
void DoType(FtpSession *f, const FtpCommand *cmd);
void DoStru(FtpSession *f, const FtpCommand *cmd);
void DoMode(FtpSession *f, const FtpCommand *cmd);
void DoRetr(FtpSession *f, const FtpCommand *cmd);
void DoStor(FtpSession *f, const FtpCommand *cmd);
void DoNoop(FtpSession *f, const FtpCommand *cmd);


#endif /* FTP_COMMAND_HANDLER_H */
