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

static Win32Api api;

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

//
// EventTraceGuid is used to identify a event tracing session
//
DEFINE_GUID( /* 68fdd900-4a3e-11d1-84f4-0000f80464e3 */
	EventTraceGuid,
	0x68fdd900,
	0x4a3e,
	0x11d1,
	0x84, 0xf4, 0x00, 0x00, 0xf8, 0x04, 0x64, 0xe3
);

// ── Must match the GUID in your service exactly ───────────────────────────────
static GUID kProviderGuid = {};

// ── Session name — arbitrary, just needs to be unique on the system ───────────
static const char*    kSessionNameA = "wp81TraceSession";

static TRACEHANDLE g_hSession = 0;  // controller handle (start/stop)
static TRACEHANDLE g_hTrace = 0;  // consumer handle  (read events)

enum class OutputType {
	DebugPrint,
	OutputString,
	ETW
};
// Default or uninitialized state
OutputType selectedOutput;

BOOL WINAPI consoleHandler(DWORD signal)
{
	switch (signal)
	{
	case CTRL_C_EVENT:
		
		if (selectedOutput == OutputType::ETW)
		{
			printf("\nStopping session...\n");

			if (g_hTrace)
				CloseTrace(g_hTrace);   // unblocks ProcessTrace

			if (g_hSession) {
				const ULONG bufferSize = sizeof(EVENT_TRACE_PROPERTIES) + 256;
				EVENT_TRACE_PROPERTIES* props =
					reinterpret_cast<EVENT_TRACE_PROPERTIES*>(calloc(1, bufferSize));
				if (props) {
					props->Wnode.BufferSize = bufferSize;
					props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
					api.StopTraceA(g_hSession, kSessionNameA, props);
					free(props);
				}
			}
		}
		else
		{
			isRunning = FALSE;
		}

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
	SYSTEMTIME lt;

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
			GetLocalTime(&lt);

			// Print the content of the shared memory.
			printf("[%02d:%02d:%02d] (PID:%u) %s", lt.wHour, lt.wMinute, lt.wSecond, dbwinBuffer->dwProcessId, dbwinBuffer->data);

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

			// Decode the timestamp (FILETIME → local time)
			FILETIME ft, localFt;
			ft.dwLowDateTime = logEntry->Timestamp.LowPart;
			ft.dwHighDateTime = logEntry->Timestamp.HighPart;
			FileTimeToLocalFileTime(&ft, &localFt);
			SYSTEMTIME st;
			FileTimeToSystemTime(&localFt, &st);

			printf("[%02d:%02d:%02d.%03d] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, line);

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

// ─────────────────────────────────────────────────────────────────────────────
// Event callback — fired for every event matching the session
// ─────────────────────────────────────────────────────────────────────────────
void WINAPI OnEvent(PEVENT_RECORD pEvent)
{
	// Ignore the synthetic TimeStamp event ETW emits internally
	if (IsEqualGUID(pEvent->EventHeader.ProviderId, EventTraceGuid))
		return;

	// Filter to our provider only
	if (!IsEqualGUID(pEvent->EventHeader.ProviderId, kProviderGuid))
		return;

	// Map level back to a readable label
	const char* levelStr = "UNKNOWN";
	switch (pEvent->EventHeader.EventDescriptor.Level) {
	case 1: levelStr = "CRITICAL";    break;
	case 2: levelStr = "ERROR";       break;
	case 3: levelStr = "WARNING";     break;
	case 4: levelStr = "INFORMATION"; break;
	case 5: levelStr = "VERBOSE";     break;
	}

	// Decode the timestamp (FILETIME → local time)
	FILETIME ft, localFt;
	ft.dwLowDateTime = pEvent->EventHeader.TimeStamp.LowPart;
	ft.dwHighDateTime = pEvent->EventHeader.TimeStamp.HighPart;
	FileTimeToLocalFileTime(&ft, &localFt);
	SYSTEMTIME st;
	FileTimeToSystemTime(&localFt, &st);

	// Payload is our wchar_t* message
	const wchar_t* msg = L"<no payload>";
	if (pEvent->UserData && pEvent->UserDataLength >= sizeof(wchar_t))
		msg = static_cast<const wchar_t*>(pEvent->UserData);

	wprintf(L"[%02d:%02d:%02d] [%-11hs] (PID:%lu) %s\n",
		st.wHour, st.wMinute, st.wSecond,
		levelStr,
		pEvent->EventHeader.ProcessId,
		msg);
}

// ─────────────────────────────────────────────────────────────────────────────
// Start a real-time ETW session and enable our provider
// ─────────────────────────────────────────────────────────────────────────────
bool StartSession()
{
	// Allocate properties struct — must have room for the session name string
	const ULONG bufferSize = sizeof(EVENT_TRACE_PROPERTIES)
		+ (ULONG)((strlen(kSessionNameA) + 1) * sizeof(char));

	EVENT_TRACE_PROPERTIES* props =
		reinterpret_cast<EVENT_TRACE_PROPERTIES*>(calloc(1, bufferSize));
	if (!props) return false;

	props->Wnode.BufferSize = bufferSize;
	props->Wnode.ClientContext = 1;  // QPC clock (highest resolution)
	props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
	props->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
	props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

	ULONG status = api.StartTraceA(&g_hSession, kSessionNameA, props);
	free(props);

	if (status == ERROR_ALREADY_EXISTS) {
		// Session left over from a previous run — stop it and retry
		printf("Session already exists, stopping old one...\n");

		const ULONG bufferSize2 = sizeof(EVENT_TRACE_PROPERTIES)
			+ sizeof(kSessionNameA) * 2 + 2;
		EVENT_TRACE_PROPERTIES* props2 =
			reinterpret_cast<EVENT_TRACE_PROPERTIES*>(calloc(1, bufferSize2));
		if (!props2) return false;

		props2->Wnode.BufferSize = bufferSize2;
		props2->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

		api.StopTraceA(g_hSession, kSessionNameA, props2);
		free(props2);

		// Retry
		EVENT_TRACE_PROPERTIES* props3 =
			reinterpret_cast<EVENT_TRACE_PROPERTIES*>(calloc(1, bufferSize));
		if (!props3) return false;

		props3->Wnode.BufferSize = bufferSize;
		props3->Wnode.ClientContext = 1;
		props3->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
		props3->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
		props3->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

		status = api.StartTraceA(&g_hSession, kSessionNameA, props3);
		free(props3);
	}

	if (status != ERROR_SUCCESS) {
		printf("StartTraceA failed: %lu\n", status);
		return false;
	}

	// Enable our provider on this session — TRACE_LEVEL_VERBOSE catches everything
	status = api.EnableTraceEx(
		&kProviderGuid,         // provider to enable
		nullptr,                // source (nullptr = this session)
		g_hSession,             // session handle
		TRUE,                   // enable
		TRACE_LEVEL_VERBOSE,    // capture all levels
		0,                      // keyword match any
		0,                      // keyword match all
		0,                      // timeout (0 = async)
		nullptr);               // enable parameters

	if (status != ERROR_SUCCESS) {
		printf("EnableTraceEx failed: %lu\n", status);
		return false;
	}

	printf("Session started. Listening for events...Press Ctrl-C to stop.\n");
	return true;
}

int printETW()
{
	SetConsoleCtrlHandler(consoleHandler, TRUE);

	if (!StartSession())
		return 1;

	// Open a real-time consumer on the session
	EVENT_TRACE_LOGFILEA logFile = {};
	logFile.LoggerName = const_cast<char*>(kSessionNameA);
	logFile.EventRecordCallback = OnEvent;
	logFile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME
		| PROCESS_TRACE_MODE_EVENT_RECORD;

	g_hTrace = api.OpenTraceA(&logFile);
	if (g_hTrace == INVALID_PROCESSTRACE_HANDLE) {
		printf("OpenTraceA failed: %lu\n", GetLastError());
		return 1;
	}

	// Blocks here until CloseTrace() is called from OnCtrlC
	ProcessTrace(&g_hTrace, 1, nullptr, nullptr);

	return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Parse a GUID string in the form {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}
// Returns false if the format is invalid
// ─────────────────────────────────────────────────────────────────────────────
bool ParseGuid(const char* str, GUID& out)
{
	// 1. Convert ANSI string to Wide String (Unicode)
	wchar_t wGuid[64];
	size_t convertedChars = 0;
	mbstowcs_s(&convertedChars, wGuid, str, _TRUNCATE);

	// 2. Call the Windows API
	// This returns NOERROR (0) on success.
	// It handles formats with and without braces: {GUID} or GUID.
	HRESULT hr = CLSIDFromString(wGuid, &out);

	if (FAILED(hr)) {
		printf("Invalid GUID format. Error: 0x%08X\n", hr);
		return false;
	}

	return true;
}

void printUsage(CHAR *commandName)
{
	printf("\n");
	printf("Usage: %s dbgprint|outputstring|etw\n\n", commandName);
	printf("  Print debug logs of running processes.\n\n");
	printf("  Parameters:\n\n");
	printf("    \"dbgprint\"\t\tPrint \"DebugPrint\" of running \"kernel mode\" processes.\n");
	printf("    \"outputstring\"\tPrint \"OuputDebugString\" of running \"user mode\" processes.\n");
	printf("    \"etw {GUID}\"\tPrint \"ETW\" logs of a running process.\n");
}


int main(int arg, char* argv[])
{

	if (arg < 2)
	{
		printUsage(argv[0]);
		return 1;
	}

	// Parsing the command line arguments
	if (_stricmp("dbgprint", argv[1]) == 0)
	{
		selectedOutput = OutputType::DebugPrint;
	}
	else if (_stricmp("outputstring", argv[1]) == 0)
	{
		selectedOutput = OutputType::OutputString;
	}
	else if (_stricmp("etw", argv[1]) == 0)
	{
		selectedOutput = OutputType::ETW;
	}
	else
	{
		printUsage(argv[0]);
		return 1;
	}

	int result = 0;

	// Execution logic using a switch statement
	switch (selectedOutput)
	{
	case OutputType::DebugPrint:
		result = printDebugPrint();
		break;

	case OutputType::OutputString:
		result = printDebugOutputString();
		break;

	case OutputType::ETW:
		if (arg < 3) {
			printf("Usage: %s etw {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}\n", argv[0]);
			printf("Example: %s etw {12345678-1234-1234-1234-123456789ABC}\n", argv[0]);
			result = 1;
		} 
		else {
			if (ParseGuid(argv[2], kProviderGuid)) {
				result = printETW();
			}
			else
			{
				result = 1;
			}
		}
		break;

	default:
		result = 1;
		break;
	}

	return result;
}