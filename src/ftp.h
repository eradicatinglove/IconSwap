#pragma once
#ifdef __cplusplus
extern "C" {
#endif

bool        ftpStart(void);
void        ftpStop(void);
bool        ftpIsRunning(void);
const char* ftpGetIp(void);
const char* ftpGetStatus(void);
int         ftpGetFilesReceived(void);

#ifdef __cplusplus
}
#endif
