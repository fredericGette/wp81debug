// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include "targetver.h"

#include <stdio.h>
#include <tchar.h>

// TODO: reference additional headers your program requires here

#include <windows.h>
#include <stdlib.h>
#include <initguid.h> // This turns DEFINE_GUID into a definition

//
// Define the method codes for how buffers are passed for I/O and FS controls
//

#define METHOD_BUFFERED                 0
#define METHOD_IN_DIRECT                1
#define METHOD_OUT_DIRECT               2
#define METHOD_NEITHER                  3

#define FILE_ANY_ACCESS                 0
#define FILE_SPECIAL_ACCESS    (FILE_ANY_ACCESS)
#define FILE_READ_ACCESS          ( 0x0001 )    // file & pipe
#define FILE_WRITE_ACCESS         ( 0x0002 )    // file & pipe

//
// Macro definition for defining IOCTL and FSCTL function control codes.  Note
// that function codes 0-2047 are reserved for Microsoft Corporation, and
// 2048-4095 are reserved for customers.
//

#define CTL_CODE( DeviceType, Function, Method, Access ) (                 \
    ((DeviceType) << 16) | ((Access) << 14) | ((Function) << 2) | (Method) \
)

typedef int WINBOOL, *PWINBOOL, *LPWINBOOL;

typedef ULONG64 TRACEHANDLE, *PTRACEHANDLE;



typedef struct _EVENT_DESCRIPTOR {

	USHORT      Id;
	UCHAR       Version;
	UCHAR       Channel;
	UCHAR       Level;
	UCHAR       Opcode;
	USHORT      Task;
	ULONGLONG   Keyword;

} EVENT_DESCRIPTOR, *PEVENT_DESCRIPTOR;

typedef const EVENT_DESCRIPTOR *PCEVENT_DESCRIPTOR;

typedef struct _EVENT_HEADER {

	USHORT              Size;                   // Event Size
	USHORT              HeaderType;             // Header Type
	USHORT              Flags;                  // Flags
	USHORT              EventProperty;          // User given event property
	ULONG               ThreadId;               // Thread Id
	ULONG               ProcessId;              // Process Id
	LARGE_INTEGER       TimeStamp;              // Event Timestamp
	GUID                ProviderId;             // Provider Id
	EVENT_DESCRIPTOR    EventDescriptor;        // Event Descriptor
	union {
		struct {
			ULONG       KernelTime;             // Kernel Mode CPU ticks
			ULONG       UserTime;               // User mode CPU ticks
		} DUMMYSTRUCTNAME;
		ULONG64         ProcessorTime;          // Processor Clock 
												// for private session events
	} DUMMYUNIONNAME;
	GUID                ActivityId;             // Activity Id

} EVENT_HEADER, *PEVENT_HEADER;

typedef struct _ETW_BUFFER_CONTEXT {
	union {
		struct {
			UCHAR ProcessorNumber;
			UCHAR Alignment;
		} DUMMYSTRUCTNAME;
		USHORT ProcessorIndex;
	} DUMMYUNIONNAME;
	USHORT  LoggerId;
} ETW_BUFFER_CONTEXT, *PETW_BUFFER_CONTEXT;

typedef struct _EVENT_HEADER_EXTENDED_DATA_ITEM {

	USHORT      Reserved1;                      // Reserved for internal use
	USHORT      ExtType;                        // Extended info type 
	struct {
		USHORT  Linkage : 1;       // Indicates additional extended 
								   // data item
		USHORT  Reserved2 : 15;
	};
	USHORT      DataSize;                       // Size of extended info data
	ULONGLONG   DataPtr;                        // Pointer to extended info data

} EVENT_HEADER_EXTENDED_DATA_ITEM, *PEVENT_HEADER_EXTENDED_DATA_ITEM;

typedef struct _EVENT_RECORD {

	EVENT_HEADER        EventHeader;            // Event header
	ETW_BUFFER_CONTEXT  BufferContext;          // Buffer context
	USHORT              ExtendedDataCount;      // Number of extended
												// data items
	USHORT              UserDataLength;         // User data length
	PEVENT_HEADER_EXTENDED_DATA_ITEM            // Pointer to an array of 
		ExtendedData;           // extended data items                                               
	PVOID               UserData;               // Pointer to user data
	PVOID               UserContext;            // Context from OpenTrace
} EVENT_RECORD, *PEVENT_RECORD;

typedef struct _EVENT_RECORD
EVENT_RECORD, *PEVENT_RECORD;

typedef struct _WNODE_HEADER
{
	ULONG BufferSize;        // Size of entire buffer inclusive of this ULONG
	ULONG ProviderId;    // Provider Id of driver returning this buffer
	union
	{
		ULONG64 HistoricalContext;  // Logger use
		struct
		{
			ULONG Version;           // Reserved
			ULONG Linkage;           // Linkage field reserved for WMI
		} DUMMYSTRUCTNAME;
	} DUMMYUNIONNAME;

	union
	{
		ULONG CountLost;         // Reserved
		HANDLE KernelHandle;     // Kernel handle for data block
		LARGE_INTEGER TimeStamp; // Timestamp as returned in units of 100ns
								 // since 1/1/1601
	} DUMMYUNIONNAME2;
	GUID Guid;                  // Guid for data block returned with results
	ULONG ClientContext;
	ULONG Flags;             // Flags, see below
} WNODE_HEADER, *PWNODE_HEADER;

typedef struct _EVENT_TRACE_PROPERTIES {
	WNODE_HEADER Wnode;
	//
	// data provided by caller
	ULONG BufferSize;                   // buffer size for logging (kbytes)
	ULONG MinimumBuffers;               // minimum to preallocate
	ULONG MaximumBuffers;               // maximum buffers allowed
	ULONG MaximumFileSize;              // maximum logfile size (in MBytes)
	ULONG LogFileMode;                  // sequential, circular
	ULONG FlushTimer;                   // buffer flush timer, in seconds
	ULONG EnableFlags;                  // trace enable flags
	LONG  AgeLimit;                     // unused

										// data returned to caller
	ULONG NumberOfBuffers;              // no of buffers in use
	ULONG FreeBuffers;                  // no of buffers free
	ULONG EventsLost;                   // event records lost
	ULONG BuffersWritten;               // no of buffers written to file
	ULONG LogBuffersLost;               // no of logfile write failures
	ULONG RealTimeBuffersLost;          // no of rt delivery failures
	HANDLE LoggerThreadId;              // thread id of Logger
	ULONG LogFileNameOffset;            // Offset to LogFileName
	ULONG LoggerNameOffset;             // Offset to LoggerName
} EVENT_TRACE_PROPERTIES, *PEVENT_TRACE_PROPERTIES;

#define WNODE_FLAG_TRACED_GUID   0x00020000 // denotes a trace

#define EVENT_TRACE_REAL_TIME_MODE          0x00000100  // Real time mode on

#define TRACE_LEVEL_VERBOSE     5   // Detailed traces from intermediate steps
typedef struct _EVENT_FILTER_DESCRIPTOR
EVENT_FILTER_DESCRIPTOR, *PEVENT_FILTER_DESCRIPTOR;

typedef struct _EVENT_TRACE_HEADER {        // overlays WNODE_HEADER
	USHORT          Size;                   // Size of entire record
	union {
		USHORT      FieldTypeFlags;         // Indicates valid fields
		struct {
			UCHAR   HeaderType;             // Header type - internal use only
			UCHAR   MarkerFlags;            // Marker - internal use only
		} DUMMYSTRUCTNAME;
	} DUMMYUNIONNAME;
	union {
		ULONG       Version;
		struct {
			UCHAR   Type;                   // event type
			UCHAR   Level;                  // trace instrumentation level
			USHORT  Version;                // version of trace record
		} Class;
	} DUMMYUNIONNAME2;
	ULONG           ThreadId;               // Thread Id
	ULONG           ProcessId;              // Process Id
	LARGE_INTEGER   TimeStamp;              // time when event happens
	union {
		GUID        Guid;                   // Guid that identifies event
		ULONGLONG   GuidPtr;                // use with WNODE_FLAG_USE_GUID_PTR
	} DUMMYUNIONNAME3;
	union {
		struct {
			ULONG   KernelTime;             // Kernel Mode CPU ticks
			ULONG   UserTime;               // User mode CPU ticks
		} DUMMYSTRUCTNAME;
		ULONG64     ProcessorTime;          // Processor Clock
		struct {
			ULONG   ClientContext;          // Reserved
			ULONG   Flags;                  // Event Flags
		} DUMMYSTRUCTNAME2;
	} DUMMYUNIONNAME4;
} EVENT_TRACE_HEADER, *PEVENT_TRACE_HEADER;

#define PROCESS_TRACE_MODE_REAL_TIME                0x00000100
#define PROCESS_TRACE_MODE_EVENT_RECORD             0x10000000

typedef struct _EVENT_TRACE {
	EVENT_TRACE_HEADER      Header;             // Event trace header
	ULONG                   InstanceId;         // Instance Id of this event
	ULONG                   ParentInstanceId;   // Parent Instance Id.
	GUID                    ParentGuid;         // Parent Guid;
	PVOID                   MofData;            // Pointer to Variable Data
	ULONG                   MofLength;          // Variable Datablock Length
	union {
		ULONG               ClientContext;
		ETW_BUFFER_CONTEXT  BufferContext;
	} DUMMYUNIONNAME;
} EVENT_TRACE, *PEVENT_TRACE;

typedef struct _TRACE_LOGFILE_HEADER {
	ULONG           BufferSize;         // Logger buffer size in Kbytes
	union {
		ULONG       Version;            // Logger version
		struct {
			UCHAR   MajorVersion;
			UCHAR   MinorVersion;
			UCHAR   SubVersion;
			UCHAR   SubMinorVersion;
		} VersionDetail;
	} DUMMYUNIONNAME;
	ULONG           ProviderVersion;    // defaults to NT version
	ULONG           NumberOfProcessors; // Number of Processors
	LARGE_INTEGER   EndTime;            // Time when logger stops
	ULONG           TimerResolution;    // assumes timer is constant!!!
	ULONG           MaximumFileSize;    // Maximum in Mbytes
	ULONG           LogFileMode;        // specify logfile mode
	ULONG           BuffersWritten;     // used to file start of Circular File
	union {
		GUID LogInstanceGuid;           // For RealTime Buffer Delivery
		struct {
			ULONG   StartBuffers;       // Count of buffers written at start.
			ULONG   PointerSize;        // Size of pointer type in bits
			ULONG   EventsLost;         // Events losts during log session
			ULONG   CpuSpeedInMHz;      // Cpu Speed in MHz
		} DUMMYSTRUCTNAME;
	} DUMMYUNIONNAME2;
#if defined(_WMIKM_)
	PWCHAR          LoggerName;
	PWCHAR          LogFileName;
	RTL_TIME_ZONE_INFORMATION TimeZone;
#else
	LPWSTR          LoggerName;
	LPWSTR          LogFileName;
	TIME_ZONE_INFORMATION TimeZone;
#endif
	LARGE_INTEGER   BootTime;
	LARGE_INTEGER   PerfFreq;           // Reserved
	LARGE_INTEGER   StartTime;          // Reserved
	ULONG           ReservedFlags;      // ClockType
	ULONG           BuffersLost;
} TRACE_LOGFILE_HEADER, *PTRACE_LOGFILE_HEADER;

typedef VOID(WINAPI *PEVENT_CALLBACK)(PEVENT_TRACE pEvent);

typedef VOID(WINAPI *PEVENT_RECORD_CALLBACK) (PEVENT_RECORD EventRecord);

typedef struct _EVENT_TRACE_LOGFILEA
EVENT_TRACE_LOGFILEA, *PEVENT_TRACE_LOGFILEA;

typedef ULONG(WINAPI * PEVENT_TRACE_BUFFER_CALLBACKA)
(PEVENT_TRACE_LOGFILEA Logfile);

struct _EVENT_TRACE_LOGFILEA {
	LPSTR                   LogFileName;      // Logfile Name
	LPSTR                   LoggerName;       // LoggerName
	LONGLONG                CurrentTime;      // timestamp of last event
	ULONG                   BuffersRead;      // buffers read to date
	union {
		ULONG               LogFileMode;      // Mode of the logfile
		ULONG               ProcessTraceMode; // Processing flags
	} DUMMYUNIONNAME;
	EVENT_TRACE             CurrentEvent;     // Current Event from this stream
	TRACE_LOGFILE_HEADER    LogfileHeader;    // logfile header structure
	PEVENT_TRACE_BUFFER_CALLBACKA             // callback before each buffer
		BufferCallback;   // is read

						  //
						  // following variables are filled for BufferCallback.
						  //
	ULONG                   BufferSize;
	ULONG                   Filled;
	ULONG                   EventsLost;
	//
	// following needs to be propaged to each buffer
	//
	union {
		PEVENT_CALLBACK         EventCallback;  // callback for every event
		PEVENT_RECORD_CALLBACK  EventRecordCallback;
	} DUMMYUNIONNAME2;


	ULONG                   IsKernelTrace;  // TRUE for kernel logfile

	PVOID                   Context;        // reserved for internal use
};

#define INVALID_PROCESSTRACE_HANDLE ((TRACEHANDLE)INVALID_HANDLE_VALUE)
#define WINOLEAPI        EXTERN_C DECLSPEC_IMPORT HRESULT STDAPICALLTYPE

typedef wchar_t OLECHAR;
typedef const OLECHAR *LPCOLESTR;

extern "C" {
	WINBASEAPI HMODULE WINAPI LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
	WINBASEAPI HMODULE WINAPI GetModuleHandleW(LPCWSTR lpModuleName);
	WINBASEAPI HANDLE WINAPI OpenFileMappingW(DWORD dwDesiredAccess, WINBOOL bInheritHandle, LPCWSTR lpName);
	WINBASEAPI HANDLE WINAPI CreateFileMappingW(HANDLE hFile, LPSECURITY_ATTRIBUTES lpFileMappingAttributes, DWORD flProtect, DWORD dwMaximumSizeHigh, DWORD dwMaximumSizeLow, LPCWSTR lpName);
	WINBASEAPI LPVOID WINAPI MapViewOfFile(HANDLE hFileMappingObject, DWORD dwDesiredAccess, DWORD dwFileOffsetHigh, DWORD dwFileOffsetLow, SIZE_T dwNumberOfBytesToMap);
	WINBASEAPI WINBOOL WINAPI UnmapViewOfFile(LPCVOID lpBaseAddress);
	WINBASEAPI HANDLE WINAPI CreateEventW(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset, BOOL bInitialState, LPCWSTR lpName);
	WINBASEAPI DWORD WINAPI WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds);
	WINBASEAPI BOOL WINAPI FileTimeToLocalFileTime(CONST FILETIME * lpFileTime, LPFILETIME lpLocalFileTime);
	WINBASEAPI BOOL	WINAPI SetConsoleCtrlHandler(PHANDLER_ROUTINE HandlerRoutine, BOOL Add);

	WINBASEAPI HANDLE WINAPI CreateFileA(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
	WINBASEAPI BOOL WINAPI DeviceIoControl(HANDLE hDevice, DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned, LPOVERLAPPED lpOverlapped);
	WINBASEAPI VOID WINAPI Sleep(DWORD dwMilliseconds);

	ULONG WINAPI StartTraceA(PTRACEHANDLE TraceHandle, LPCSTR InstanceName, PEVENT_TRACE_PROPERTIES Properties);
	ULONG WINAPI StopTraceA(TRACEHANDLE TraceHandle, LPCSTR InstanceName, PEVENT_TRACE_PROPERTIES Properties);
	ULONG WINAPI EnableTraceEx(LPCGUID ProviderId, LPCGUID SourceId, TRACEHANDLE TraceHandle, ULONG IsEnabled, UCHAR Level, ULONGLONG MatchAnyKeyword, ULONGLONG MatchAllKeyword, ULONG EnableProperty, PEVENT_FILTER_DESCRIPTOR EnableFilterDesc);
	ULONG WINAPI CloseTrace(TRACEHANDLE TraceHandle);
	TRACEHANDLE WINAPI OpenTraceA(PEVENT_TRACE_LOGFILEA Logfile);
	ULONG WINAPI ProcessTrace(PTRACEHANDLE HandleArray, ULONG HandleCount, LPFILETIME StartTime, LPFILETIME EndTime);

	WINOLEAPI CLSIDFromString(LPCOLESTR lpsz, LPCLSID pclsid);
}

#define WIN32API_TOSTRING(x) #x

// Link exported function
#define WIN32API_INIT_PROC(Module, Name)  \
  Name(reinterpret_cast<decltype(&::Name)>( \
      ::GetProcAddress((Module), WIN32API_TOSTRING(Name))))

// Convenientmacro to declare function
#define WIN32API_DEFINE_PROC(Name) const decltype(&::Name) Name

class Win32Api {

private:
	// Returns a base address of KernelBase.dll
	static HMODULE GetKernelBase() {
		return GetBaseAddress(&::DisableThreadLibraryCalls);
	}

	// Returns a base address of the given address
	static HMODULE GetBaseAddress(const void *Address) {
		MEMORY_BASIC_INFORMATION mbi = {};
		if (!::VirtualQuery(Address, &mbi, sizeof(mbi))) {
			return nullptr;
		}
		const auto mz = *reinterpret_cast<WORD *>(mbi.AllocationBase);
		if (mz != IMAGE_DOS_SIGNATURE) {
			return nullptr;
		}
		return reinterpret_cast<HMODULE>(mbi.AllocationBase);
	}

public:
	const HMODULE m_Kernelbase;
	WIN32API_DEFINE_PROC(LoadLibraryExW);
	WIN32API_DEFINE_PROC(GetModuleHandleW);
	const HMODULE m_AdvApi32Legacy;
	WIN32API_DEFINE_PROC(EnableTraceEx);
	WIN32API_DEFINE_PROC(OpenTraceA);
	WIN32API_DEFINE_PROC(StartTraceA);
	WIN32API_DEFINE_PROC(StopTraceA);

	Win32Api()
		: m_Kernelbase(GetKernelBase()),
		WIN32API_INIT_PROC(m_Kernelbase, LoadLibraryExW),
		WIN32API_INIT_PROC(m_Kernelbase, GetModuleHandleW),
		m_AdvApi32Legacy(LoadLibraryExW(L"ADVAPI32LEGACY.dll", NULL, NULL)),
		WIN32API_INIT_PROC(m_AdvApi32Legacy, EnableTraceEx),
		WIN32API_INIT_PROC(m_AdvApi32Legacy, OpenTraceA),
		WIN32API_INIT_PROC(m_AdvApi32Legacy, StartTraceA),
		WIN32API_INIT_PROC(m_AdvApi32Legacy, StopTraceA)

	{};

};