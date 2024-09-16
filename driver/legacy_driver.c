// https://www.codeproject.com/Articles/5362869/Tracing-and-Logging-Technologies-on-Windows-Part-2
// https://github.com/tandasat/DebugLogger
//
// This is a legacy driver (NT V4 style) because I'm not able to start a WDF (Pnp) software-only driver.
// https://www.osronline.com/custom.cfm%5Ename=articlePrint.cfm&id=570.htm

#include <fltKernel.h>
#include <wdm.h>


//
// The size of log buffer in bytes. Two buffers of this size will be allocated.
// Change this as needed.
//
static const ULONG k_DebugLogBufferSize = PAGE_SIZE * 8;

//
// The pool tag.
//
static const ULONG k_PoolTag = 'LgbD';

//
// The maximum characters the DbgPrint family can handle at once.
//
#define MAXDBGPRINTLOGLENGTH 512

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
// The active and inactive buffer layout.
//
typedef struct _DEBUG_LOG_BUFFER
{
    //
    // The pointer to the buffer storing the sequence of DEBUG_LOG_ENTRYs.
    //
    PDEBUG_LOG_ENTRY LogEntries;

    //
    // The offset to the address where the next DEBUG_LOG_ENTRY should be saved,
    // counted from LogEntries.
    //
    ULONG NextLogOffset;

    //
    // How many bytes are not save into LogEntries due to lack of space.
    //
    ULONG OverflowedLogSize;
} DEBUG_LOG_BUFFER, *PDEBUG_LOG_BUFFER;

//
// The structure used by the debug print callback.
//
typedef struct _PAIRED_DEBUG_LOG_BUFFER
{
    //
    // Indicates whether ActiveLogBuffer and InactiveLogBuffer are usable.
    //
    BOOLEAN BufferValid;

    //
    // The lock must be held before accessing any other fields of this structure.
    //
    EX_SPIN_LOCK ActiveLogBufferLock;

    //
    // The pointers to two buffers: active and inactive. Active buffer is used
    // by the debug print callback and to save new messages as they comes in.
    // Inactive buffer is buffer accessed and cleared up by the flush buffer thread.
    //
    PDEBUG_LOG_BUFFER ActiveLogBuffer;
    PDEBUG_LOG_BUFFER InactiveLogBuffer;
} PAIRED_DEBUG_LOG_BUFFER, *PPAIRED_DEBUG_LOG_BUFFER;

//
// Buffer structures as global variables. Initialized by StartDebugPrintCallback
// and cleaned up by StopDebugPrintCallback.
//
static DEBUG_LOG_BUFFER g_LogBuffer1;
static DEBUG_LOG_BUFFER g_LogBuffer2;
static PAIRED_DEBUG_LOG_BUFFER g_PairedLogBuffer;

//
// The space to save old debug filter states for all components. Used by
// EnableVerboseDebugOutput and DisableVerboseDebugOutput.
//
static ULONG g_DebugFilterStates[DPFLTR_ENDOFTABLE_ID];

/*!
    @brief Saves a single line debug message to the active buffer.

    @param[in] Timestamp - The time stamp of when the log message was sent.

    @param[in] LogLine - The single line, null-terminated debug log message.
        Does not include "\n".

    @param[in,out] PairedLogBuffer - Buffer to save the message.
*/
VOID SaveDebugOutputLine (const LARGE_INTEGER* Timestamp, PCSTR LogLine, PPAIRED_DEBUG_LOG_BUFFER PairedLogBuffer)
{
    USHORT logLineLength;
    ULONG logEntrySize;
    BOOLEAN lockAcquired;
    PDEBUG_LOG_ENTRY logEntry;

    lockAcquired = FALSE;
	

    //
    // Get the length of the message in characters. The message should never be
    // an empty (as per behavior of strtok_s) and should never be longer than
    // what the DbgPrint family can handle.
    //
    logLineLength = (USHORT)(strlen(LogLine));
    if ((logLineLength == 0) || (logLineLength > MAXDBGPRINTLOGLENGTH))
    {
        NT_ASSERT(FALSE);
        goto Exit;
    }

    //
    // Unlikely but one can output \r\n. Ignore this to normalize contents.
    //
    if (LogLine[logLineLength - 1] == '\r')
    {
        if ((--logLineLength) == 0)
        {
            goto Exit;
        }
    }

    logEntrySize = RTL_SIZEOF_THROUGH_FIELD(DEBUG_LOG_ENTRY, LogLineLength) +
        logLineLength;

    //
    // Acquire the lock to safely modify active buffer.
    //
    ExAcquireSpinLockExclusiveAtDpcLevel(&PairedLogBuffer->ActiveLogBufferLock);
    lockAcquired = TRUE;

    //
    // Bail out if a concurrent thread invalidated buffer.
    //
    if (PairedLogBuffer->BufferValid == FALSE)
    {
        goto Exit;
    }

    //
    // If the remaining buffer is not large enough to save this message, count
    // up the overflowed size and bail out.
    //
    if (PairedLogBuffer->ActiveLogBuffer->NextLogOffset + logEntrySize > k_DebugLogBufferSize)
    {
        PairedLogBuffer->ActiveLogBuffer->OverflowedLogSize += logEntrySize;
        goto Exit;
    }

    //
    // There are sufficient room to save the message. Get the address to save
    // the message within active buffer. 
    //
    logEntry = (PDEBUG_LOG_ENTRY)(Add2Ptr(
                                PairedLogBuffer->ActiveLogBuffer->LogEntries,
                                PairedLogBuffer->ActiveLogBuffer->NextLogOffset));

    //
    // Save this message and update the offset to the address to save the next
    // message.
    //
    logEntry->Timestamp = *Timestamp;
    logEntry->LogLineLength = logLineLength;
    RtlCopyMemory(logEntry->LogLine, LogLine, logLineLength);
    PairedLogBuffer->ActiveLogBuffer->NextLogOffset += logEntrySize;

Exit:
    if (lockAcquired != FALSE)
    {
        ExReleaseSpinLockExclusiveFromDpcLevel(&PairedLogBuffer->ActiveLogBufferLock);
    }
    return;
}

/*!
    @brief Saves the debug log messages to active buffer.

    @param[in] Output - The formatted debug log message given to the API family.

    @param[in,out] PairedLogBuffer - Buffer to save the message.
*/
VOID SaveDebugOutput (const STRING* Output, PPAIRED_DEBUG_LOG_BUFFER PairedLogBuffer)
{
    CHAR ouputBuffer[MAXDBGPRINTLOGLENGTH + 1];
    PSTR strtokContext;
    PSTR logLine;
    LARGE_INTEGER timestamp;

    //
    // Capture when the debug log message is sent.
    //
    KeQuerySystemTimePrecise(&timestamp);

    //
    // Ignore an empty message as it is not interesting.
    //
    if (Output->Length == 0)
    {
        goto Exit;
    }

    //
    // The message should be shorter than what the DbgPrint family can handle at
    // one call.
    //
    if (Output->Length > MAXDBGPRINTLOGLENGTH)
    {
        NT_ASSERT(FALSE);
        goto Exit;
    }

    //
    // Copy the message as a null-terminated string.
    //
    RtlCopyMemory(ouputBuffer, Output->Buffer, Output->Length);
    ouputBuffer[Output->Length] = ANSI_NULL;

    //
    // Split it with \n and save each split message. Note that strtok_s removes
    // "\n\n", so empty lines are not saved.
    //
    strtokContext = NULL;
    logLine = strtok_s(ouputBuffer, "\n", &strtokContext);
    while (logLine != NULL)
    {
        SaveDebugOutputLine(&timestamp, logLine, PairedLogBuffer);
        logLine = strtok_s(NULL, "\n", &strtokContext);
    }

Exit:
    return;
}

/*!
    @brief The callback routine for the DbgPrint family.

    @param[in] Output - The formatted debug log message given to the API family.

    @param[in] ComponentId - The ComponentId given to the API family.

    @param[in] Level - The Level given to the API family.
*/
VOID DebugPrintCallback (PSTRING Output, ULONG ComponentId, ULONG Level)
{
    KIRQL oldIrql;

    UNREFERENCED_PARAMETER(ComponentId);
    UNREFERENCED_PARAMETER(Level);

    //
    // IRQL is expected to be SYNCH_LEVEL already, but raise it to make sure
    // as an expected IRQL of this callback is not documented anywhere.
    //
    oldIrql = KeRaiseIrqlToSynchLevel();

    //
    // Do actual stuff with context.
    //
    SaveDebugOutput(Output, &g_PairedLogBuffer);

    KeLowerIrql(oldIrql);
    return;
}

/*!
    @brief Enables all levels of debug print for all components.

    @details This function enables all debug print while saving the previous
        state into g_DebugFilterStates for restore.
*/
VOID EnableVerboseDebugOutput ()
{
    ULONG statesOfAllLevels;
    BOOLEAN state;

    //
    // For all components.
    //
    for (ULONG componentId = 0; componentId < DPFLTR_ENDOFTABLE_ID; ++componentId)
    {
        //
        // For all levels (levels are 0-31) of the component.
        //
        statesOfAllLevels = 0;
        for (ULONG level = 0; level < 32; ++level)
        {
            //
            // Get the current state, and save it as a single bit onto the 32bit
            // integer (statesOfAllLevels).
            //
            state = (BOOLEAN)(DbgQueryDebugFilterState(componentId, level));
            SetFlag(statesOfAllLevels, (state << level));

            NT_VERIFY(NT_SUCCESS(DbgSetDebugFilterState(componentId, level, TRUE)));
        }
        g_DebugFilterStates[componentId] = statesOfAllLevels;
    }
}

/*!
    @brief Starts the debug print callback.

    @details This function takes two buffers to be initialized, and one paired
        buffer, which essentially references to those two buffers. All of them
        are initialized in this function.

    @param[out] LogBufferActive - Debug log buffer to use initially.

    @param[out] LogBufferInactive - Debug log buffer to be inactive initially.

    @param[out] PairedLogBuffer - A buffer pair to be used in the debug print
        callback.

    @return STATUS_SUCCESS or an appropriate status code.
*/
NTSTATUS StartDebugPrintCallback (PDEBUG_LOG_BUFFER LogBufferActive, PDEBUG_LOG_BUFFER LogBufferInactive, PPAIRED_DEBUG_LOG_BUFFER PairedLogBuffer)
{
    NTSTATUS status;
    PDEBUG_LOG_ENTRY logEntries1, logEntries2;
	
	DbgPrint("wp81dbgPrint!Begin StartDebugPrintCallback");

    RtlZeroMemory(LogBufferActive, sizeof(*LogBufferActive));
    RtlZeroMemory(LogBufferInactive, sizeof(*LogBufferInactive));
    RtlZeroMemory(PairedLogBuffer, sizeof(*PairedLogBuffer));

    logEntries2 = NULL;

	DbgPrint("wp81dbgPrint!k_DebugLogBufferSize = %lu", k_DebugLogBufferSize);

    //
    // Allocate log buffers.
    //
    logEntries1 = (PDEBUG_LOG_ENTRY)(ExAllocatePoolWithTag(
														NonPagedPoolNx,
														k_DebugLogBufferSize,
														k_PoolTag));
	DbgPrint("wp81dbgPrint!logEntries1 = 0x%p", logEntries1);
    if (logEntries1 == NULL)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    logEntries2 = (PDEBUG_LOG_ENTRY)(ExAllocatePoolWithTag(
														NonPagedPoolNx,
														k_DebugLogBufferSize,
														k_PoolTag));
	DbgPrint("wp81dbgPrint!logEntries2 = 0x%p", logEntries2);													
    if (logEntries2 == NULL)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    //
    // Initialize buffer variables, and mark the paired buffer as valid. This
    // lets the debug print callback use this paired buffer.
    //
    LogBufferActive->LogEntries = logEntries1;
    LogBufferInactive->LogEntries = logEntries2;
    PairedLogBuffer->ActiveLogBuffer = LogBufferActive;
    PairedLogBuffer->InactiveLogBuffer = LogBufferInactive;
    PairedLogBuffer->BufferValid = TRUE;

    //
    // We have set up everything the debug print callback needs. Start it.
    //
    status = DbgSetDebugPrintCallback(DebugPrintCallback, TRUE);
    if (!NT_SUCCESS(status))
    {
        goto Exit;
    }

    //
    // All good. Enable all levels of debug print for all components.
    //
    EnableVerboseDebugOutput();

    DbgPrintEx(DPFLTR_IHVDRIVER_ID,
               DPFLTR_INFO_LEVEL,
               "Starting debug print logging.\n");

Exit:
    if (!NT_SUCCESS(status))
    {
        if (logEntries2 != NULL)
        {
            ExFreePoolWithTag(logEntries2, k_PoolTag);
        }
        if (logEntries1 != NULL)
        {
            ExFreePoolWithTag(logEntries1, k_PoolTag);
        }
    }
	
	DbgPrint("wp81dbgPrint!End StartDebugPrintCallback");
    return status;
}

NTSTATUS StartDebugPrintLogging()
{
    NTSTATUS status;

    //
    // Start debug print callback that saves debug print messages into one of
    // those two buffers.
    //
    status = StartDebugPrintCallback(&g_LogBuffer1, &g_LogBuffer2, &g_PairedLogBuffer);

    return status;
}

/*!
    @brief Disables verbose debug output by restoring filter states to original.
*/
VOID DisableVerboseDebugOutput ()
{
    ULONG states;
    BOOLEAN oldState;

    for (ULONG componentId = 0; componentId < DPFLTR_ENDOFTABLE_ID; ++componentId)
    {
        states = g_DebugFilterStates[componentId];
        for (ULONG level = 0; level < 32; ++level)
        {
            //
            // Get the bit corresponding to the "level" from "states", and set
            // that bit as a new state (restore).
            //
            oldState = BooleanFlagOn(states, (1 << level));
            NT_VERIFY(NT_SUCCESS(DbgSetDebugFilterState(componentId, level, oldState)));
        }
    }
}

/*!
    @brief Stops the debug print callback and cleans up paired buffer.

    @param[in,out] PairedLogBuffer - The paired buffer associated to clean up.
*/
VOID StopDebugPrintCallback (PPAIRED_DEBUG_LOG_BUFFER PairedLogBuffer)
{
    KIRQL oldIrql;
	
	DbgPrint("wp81dbgPrint!Begin StopDebugPrintCallback");

    //
    // Restore debug filters to the previous states.
    //
    DisableVerboseDebugOutput();

    //
    // Stop the callback.
    //
    NT_VERIFY(NT_SUCCESS(DbgSetDebugPrintCallback(DebugPrintCallback, FALSE)));

    //
    // Let us make sure no one is touching the paired buffer. Without this, it
    // is possible that the callback is still running concurrently on the other
    // processor and touching the paired buffer.
    //
    oldIrql = ExAcquireSpinLockExclusive(&PairedLogBuffer->ActiveLogBufferLock);

    //
    // Free both buffer and mark this paired buffer as invalid, so the other
    // thread waiting on this skin lock can tell the buffer is no longer valid
    // when the spin lock was released.
    //
    ExFreePoolWithTag(PairedLogBuffer->ActiveLogBuffer->LogEntries, k_PoolTag);
    ExFreePoolWithTag(PairedLogBuffer->InactiveLogBuffer->LogEntries, k_PoolTag);
    PairedLogBuffer->BufferValid = FALSE;

    ExReleaseSpinLockExclusive(&PairedLogBuffer->ActiveLogBufferLock, oldIrql);
	
	DbgPrint("wp81dbgPrint!End StopDebugPrintCallback");
}

VOID StopDebugPrintLoggging()
{
    StopDebugPrintCallback(&g_PairedLogBuffer);
}

NTSTATUS CompleteIrp(PIRP Irp, NTSTATUS status, ULONG_PTR info) {
	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = info;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}

// The driver is stopping
void DriverUnload(PDRIVER_OBJECT DriverObject) {
	
	DbgPrint("wp81dbgPrint!DriverUnload");
	
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\wp81dbgprint");
	// delete symbolic link
	IoDeleteSymbolicLink(&symLink);
	// delete device object
	IoDeleteDevice(DriverObject->DeviceObject);
	
	StopDebugPrintLoggging();
}

NTSTATUS DeviceCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
	UNREFERENCED_PARAMETER(DeviceObject);
	
	DbgPrint("wp81dbgPrint!DeviceCreate");
	
	CompleteIrp(Irp, STATUS_SUCCESS, 0);
	
	return STATUS_SUCCESS;
}

NTSTATUS DeviceClose(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
	UNREFERENCED_PARAMETER(DeviceObject);
	
	DbgPrint("wp81dbgPrint!DeviceClose");
	
	CompleteIrp(Irp, STATUS_SUCCESS, 0);
	
	return STATUS_SUCCESS;
}

ULONG_PTR FlushDebugLogEntries(PVOID pOutputBuffer)
{
    NTSTATUS status;
    PPAIRED_DEBUG_LOG_BUFFER pairedLogBuffer;
    KIRQL oldIrql;
    PDEBUG_LOG_BUFFER oldLogBuffer;

    status = STATUS_SUCCESS;
    pairedLogBuffer = &g_PairedLogBuffer;

    //
    // Swap active buffer and inactive buffer.
    //
    oldIrql = ExAcquireSpinLockExclusive(&pairedLogBuffer->ActiveLogBufferLock);
    oldLogBuffer = pairedLogBuffer->ActiveLogBuffer;
    pairedLogBuffer->ActiveLogBuffer = pairedLogBuffer->InactiveLogBuffer;
    pairedLogBuffer->InactiveLogBuffer = oldLogBuffer;
    ExReleaseSpinLockExclusive(&pairedLogBuffer->ActiveLogBufferLock, oldIrql);

    NT_ASSERT(pairedLogBuffer->ActiveLogBuffer != pairedLogBuffer->InactiveLogBuffer);

	RtlCopyMemory(pOutputBuffer, &(oldLogBuffer->NextLogOffset), 8);
	RtlCopyMemory(((PCHAR)pOutputBuffer)+8, &(oldLogBuffer->OverflowedLogSize), 8);
	RtlCopyMemory(((PCHAR)pOutputBuffer)+16, oldLogBuffer->LogEntries, oldLogBuffer->NextLogOffset);

	ULONG_PTR sizeCopied = 8+8+oldLogBuffer->NextLogOffset;

    //
    // Finally, clear the previously active buffer.
    //
    oldLogBuffer->NextLogOffset = 0;
    oldLogBuffer->OverflowedLogSize = 0;
	
	return sizeCopied;
}

NTSTATUS DriverDispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	NTSTATUS status = STATUS_SUCCESS;
	ULONG IoControlCode;
	size_t InputBufferLength;
	size_t OutputBufferLength;
	PVOID pBuffer;
	ULONG_PTR information = 0;

	// https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/buffer-descriptions-for-i-o-control-codes#method_neither
	IoControlCode = Irp->Tail.Overlay.CurrentStackLocation->Parameters.DeviceIoControl.IoControlCode;
	InputBufferLength = Irp->Tail.Overlay.CurrentStackLocation->Parameters.DeviceIoControl.InputBufferLength;
	OutputBufferLength = Irp->Tail.Overlay.CurrentStackLocation->Parameters.DeviceIoControl.OutputBufferLength;	

	//DbgPrint("wp81dbgPrint!DriverDispatch IoControlCode=0x%X InputBufferLength=0x%X OutputBufferLength=0x%X",IoControlCode,InputBufferLength,OutputBufferLength);

	pBuffer = Irp->AssociatedIrp.SystemBuffer;
	//DbgPrint("wp81dbgPrint!DriverDispatch pBuffer=0x%p", pBuffer);
	
	information = FlushDebugLogEntries(pBuffer);

	return CompleteIrp(Irp, status, information);
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
	
	UNREFERENCED_PARAMETER(RegistryPath);
	
	DbgPrint("wp81dbgPrint!DriverEntry");
	
	DriverObject->DriverUnload = DriverUnload;
	DriverObject->MajorFunction[IRP_MJ_CREATE] = DeviceCreate;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = DeviceClose;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DriverDispatch;
	
	UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\wp81dbgprint");
	
	PDEVICE_OBJECT DeviceObject;
	NTSTATUS status = IoCreateDevice(
		DriverObject, // our driver object
		0, // no need for extra bytes
		&devName, // the device name
		FILE_DEVICE_UNKNOWN, // device type
		0, // characteristics flags
		FALSE, // not exclusive
		&DeviceObject); // the resulting pointer
	if (!NT_SUCCESS(status)) {
		DbgPrint("wp81dbgPrint!Failed to create device object (0x%X)", status);
		return status;
	}
	
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\wp81dbgprint");
	status = IoCreateSymbolicLink(&symLink, &devName);
	if (!NT_SUCCESS(status)) {
		DbgPrint("wp81dbgPrint!Failed to create symbolic link (0x%X)\n", status);
		IoDeleteDevice(DeviceObject);
		return status;
	}
	
	status = StartDebugPrintLogging();
	if (!NT_SUCCESS(status)) {
		DbgPrint("wp81dbgPrint!Failed to start DebugPrint logging (0x%X)\n", status);
		IoDeleteDevice(DeviceObject);
		return status;
	}
	
	return STATUS_SUCCESS;
}