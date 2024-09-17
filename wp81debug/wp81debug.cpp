// wp81debug.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#define IOCTL_DBG_PRINT CTL_CODE(0x8000, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define SHARED_MEMORY_SIZE 4096 // Shared memory size is 4KB.

struct DBWIN_BUFFER
{
	DWORD   dwProcessId;
	char    data[SHARED_MEMORY_SIZE - sizeof(DWORD)];
};

BOOL isRunning = TRUE;

//
// The format of a single debug log message stored in DEBUG_LOG_BUFFER::LogEntries.
//
#include <pshpack1.h>
typedef struct _DEBUG_LOG_ENTRY
{
	//
	// The system time of when this message is seen in the debug print callback.
	//
	LARGE_INTEGER Timestamp;

	//
	// The length of the message stored in LogLine in characters.
	//
	USHORT LogLineLength;

	//
	// The debug log message, not including terminating null, '\r' or '\n'.
	//
	CHAR LogLine[ANYSIZE_ARRAY];
} DEBUG_LOG_ENTRY, *PDEBUG_LOG_ENTRY;
static_assert(sizeof(DEBUG_LOG_ENTRY) == 11, "Must be packed for space");
#include <poppack.h>

BOOL WINAPI consoleHandler(DWORD signal)
{
	switch (signal)
	{
	case CTRL_C_EVENT:
		isRunning = FALSE;
		// Signal is handled - don't pass it on to the next handler.
		return TRUE;
	default:
		// Pass signal on to the next handler.
		return FALSE;
	}
}

// https://www.codeproject.com/Articles/23776/Mechanism-of-OutputDebugString
int printDebugOutputString()
{
	FILETIME SystemFileTime;

	// Event signaling when OutputDebugString finishes writing to shared memory
	HANDLE hEventDataReady = OpenEvent(
		SYNCHRONIZE,
		FALSE,
		L"DBWIN_DATA_READY"
	);
	if (hEventDataReady == NULL) {
		// The event doesn't already exist.
		// This is often the case when no one else is listening to OutputDebugString messages.
		hEventDataReady = CreateEventW(
			NULL,
			FALSE,	// auto-reset
			FALSE,	// initial state: nonsignaled
			L"DBWIN_DATA_READY"
		);

		if (hEventDataReady == NULL) {
			printf("Failed to create event DBWIN_DATA_READY: 0x%08X\n", GetLastError());
			return 1;
		}
	}

	// Event signaling when the shared memory is ready to accept new data from OutputDebugString
	HANDLE hEventBufferReady = OpenEvent(
		EVENT_ALL_ACCESS,
		FALSE,
		L"DBWIN_BUFFER_READY"
	);
	if (hEventBufferReady == NULL) {
		// The event doesn't already exist.
		// Same case as DBWIN_DATA_READY, we have to create the event object.
		hEventBufferReady = CreateEventW(
			NULL,
			FALSE,	// auto-reset
			TRUE,	// initial state: signaled
			L"DBWIN_BUFFER_READY"
		);

		if (hEventBufferReady == NULL) {
			printf("Failed to create event DBWIN_BUFFER_READY: 0x%08X\n", GetLastError());
			return 1;
		}
	}

	HANDLE hSharedMemory = OpenFileMappingW(
		FILE_MAP_READ,
		FALSE,
		L"DBWIN_BUFFER"
	);
	if (hSharedMemory == NULL) {
		// The file mapping doesn't already exist.
		// Same case as DBWIN_DATA_READY, we have to create the file mapping object.
		hSharedMemory = CreateFileMappingW(
			INVALID_HANDLE_VALUE, // Don't map a file in the file system.
			NULL, // No inheritance required, and default security.
			PAGE_READWRITE, // Allow write for OutputDebugString
			0,
			SHARED_MEMORY_SIZE, // Size of the shared memory
			L"DBWIN_BUFFER"
		);

		if (hSharedMemory == NULL) {
			printf("Failed to create file mapping DBWIN_BUFFER: 0x%08X\n", GetLastError());
			return 1;
		}
	}

	DBWIN_BUFFER* dbwinBuffer = (DBWIN_BUFFER*)MapViewOfFile(
		hSharedMemory,
		SECTION_MAP_READ,
		0, // start offset high
		0, // start offset low
		0  // length, 0 = from the start offset to the en of the file.
	);

	SetConsoleCtrlHandler(consoleHandler, TRUE);
	printf("Listening to OutputDebugString...Press Ctrl-C to stop.\n");

	while (isRunning)
	{
		if (WaitForSingleObject(hEventDataReady, 100) == WAIT_OBJECT_0) { // Wait 100ms max.

																		  // Timestamp
			GetSystemTimeAsFileTime(&SystemFileTime);

			// Print the content of the shared memory.
			printf("%010u.%010u %05d %s", SystemFileTime.dwHighDateTime, SystemFileTime.dwLowDateTime, dbwinBuffer->dwProcessId, dbwinBuffer->data);

			if (dbwinBuffer->data[strlen(dbwinBuffer->data) - 1] != 10) // LF
			{
				// No CRLF at the end of the message.
				// We add it manually
				printf("\n");
			}

			// Shared memory is ready for a new message.
			SetEvent(hEventBufferReady);
		}
	}

	UnmapViewOfFile(dbwinBuffer);
	CloseHandle(hSharedMemory);
	CloseHandle(hEventBufferReady);
	CloseHandle(hEventDataReady);

	return 0;
}

int printDebugPrint()
{
	SetConsoleCtrlHandler(consoleHandler, TRUE);
	printf("Listening to DbgPrint...Press Ctrl-C to stop.\n");

	HANDLE hDevice = CreateFileA("\\\\.\\wp81dbgprint", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hDevice == INVALID_HANDLE_VALUE)
	{
		printf("Failed to open device! 0x%08X\n", GetLastError());
		return 1;
	}

	PVOID pOutputBuffer = malloc(8 + 8 + 32768); // ULONG NextLogOffset + ULONG OverflowedLogSize + DEBUG_LOG_ENTRY LogEntries
	CHAR line[512 + 1];
	DWORD returned;
	BOOL success;
	ULONG NextLogOffset;
	ULONG OverflowedLogSize;
	while (isRunning)
	{
		ZeroMemory(pOutputBuffer, 8 + 8 + 32768);
		success = DeviceIoControl(hDevice, IOCTL_DBG_PRINT, NULL, 0, pOutputBuffer, 8 + 8 + 32768, &returned, NULL);
		if (!success)
		{
			printf("Failed to send DeviceIoControl! 0x%08X", GetLastError());
			free(pOutputBuffer);
			CloseHandle(hDevice);
			return 1;
		}

		NextLogOffset = *(PULONG)pOutputBuffer;
		OverflowedLogSize = *(PULONG)((PCHAR)pOutputBuffer + 8);

		//
		// Iterate all saved debug log messages (if exist).
		//
		for (ULONG offset = 0; offset < NextLogOffset; /**/)
		{
			PDEBUG_LOG_ENTRY logEntry = (PDEBUG_LOG_ENTRY)((PCHAR)pOutputBuffer + 8 + 8 + offset);
			ZeroMemory(line, 512 + 1);
			memcpy(line, logEntry->LogLine, logEntry->LogLineLength);
			printf("%010u.%010u %s\n", logEntry->Timestamp.HighPart, logEntry->Timestamp.LowPart, line);

			//
			// Compute the offset to the next entry by adding the size of the current
			// entry.
			//
			offset += RTL_SIZEOF_THROUGH_FIELD(DEBUG_LOG_ENTRY, LogLineLength) +
				logEntry->LogLineLength;
		}

		if (OverflowedLogSize > 0)
		{
			printf("***** Missed %lu lines *****\n", OverflowedLogSize);
		}

		Sleep(100);
	}

	free(pOutputBuffer);
	CloseHandle(hDevice);

	return 0;
}

void printUsage(CHAR *commandName)
{
	printf("\n");
	printf("Usage: %s dbgprint|outputstring\n\n", commandName);
	printf("  Print debug logs of running processes.\n\n");
	printf("  Parameters:\n\n");
	printf("    \"dbgprint\"\t\tPrint \"DebugPrint\" of running \"kernel mode\" processes.\n");
	printf("    \"outputstring\"\tPrint \"OuputDebugString\" of running \"user mode\" processes.\n");
}


int main(int arg, char* argv[])
{
	BOOL dbgprint = false;

	if (arg != 2)
	{
		printUsage(argv[0]);
		return 1;
	}
	else if (_stricmp("dbgprint", argv[1]) == 0)
	{
		dbgprint = true;
	}
	else if (_stricmp("outputstring", argv[1]) == 0)
	{
		dbgprint = false;
	}
	else
	{
		printUsage(argv[0]);
		return 1;
	}
	
	int result = 0;

	if (dbgprint)
	{
		result = printDebugPrint();
	}
	else
	{
		result = printDebugOutputString();
	}

    return result;
}

