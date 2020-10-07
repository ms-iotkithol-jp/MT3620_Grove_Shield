#pragma once

#include "applibs_versions.h"
#include <applibs/uart.h>

#ifdef __cplusplus
extern "C" {
#endif
	int LiefDevice_Open(UART_Id uartId);
	int LiefDevice_SendText(int uartFd, const char* msg);
	ssize_t LiefDevice_ReceiveMessage(int uartFd, const char* msg, size_t msgMaxLen);
	int LiefDevice_Close(int uartFd);

#ifdef __cplusplus
}
#endif


