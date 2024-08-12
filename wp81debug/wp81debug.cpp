// wp81debug.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#define SHARED_MEMORY_SIZE 4096 // Shared memory size is 4KB.

struct DBWIN_BUFFER
{
	DWORD   dwProcessId;
	char    data[SHARED_MEMORY_SIZE - sizeof(DWORD)];
};

BOOL isRunning = TRUE;

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
int main()
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

