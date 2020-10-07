#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>

#include "liefdevice.h"

static UART_Config uartConfig;

int LiefDevice_Open(UART_Id uartId)
{
	UART_InitConfig(&uartConfig);
	uartConfig.baudRate = 9600;
	uartConfig.flowControl = UART_FlowControl_None;
	int uartFd = UART_Open(uartId, &uartConfig);
	assert(uartFd >= 0);

	return uartFd;
}

int LiefDevice_SendText(int uartFd, const char* msg)
{
	size_t msgLen = strlen(msg);
	int byteSent = write(uartFd, msg, msgLen+1);
	return byteSent;
}

ssize_t LiefDevice_ReceiveMessage(int uartFd, const char* msg, size_t msgMaxLen)
{
	ssize_t readLen = read(uartFd, (void*)msg, msgMaxLen);
	return readLen;
}

int LiefDevice_Close(int uartFd)
{

}
