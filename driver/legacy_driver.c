// https://www.codeproject.com/Articles/5362869/Tracing-and-Logging-Technologies-on-Windows-Part-2
// https://github.com/tandasat/DebugLogger
//
// This is a legacy driver (NT V4 style) because I'm not able to start a WDF (Pnp) software-only driver.
// https://www.osronline.com/custom.cfm%5Ename=articlePrint.cfm&id=570.htm

#include <wdm.h>

NTSTATUS StartDebugPrintLogging()
{
    // NTSTATUS status;
    // BOOLEAN callbackStarted;

    // //
    // // Start debug print callback that saves debug print messages into one of
    // // those two buffers.
    // //
    // status = StartDebugPrintCallback(&g_LogBuffer1, &g_LogBuffer2, &g_PairedLogBuffer);
    // if (!NT_SUCCESS(status))
    // {
		// return status;
    // }

    // //
    // // Starts the flush buffer thread that write the saved debug print
    // // messages into a log file and clears the buffer.
    // //
    // status = StartFlushBufferThread(&g_ThreadContext);
    // if (!NT_SUCCESS(status))
    // {
		// StopDebugPrintCallback(&g_PairedLogBuffer);
		// return status;
    // }

    return STATUS_SUCCESS;
}

VOID StopDebugPrintLoggging()
{
    // StopFlushBufferThread(&g_ThreadContext);
    // StopDebugPrintCallback(&g_PairedLogBuffer);
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

NTSTATUS DriverDispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	NTSTATUS status = STATUS_SUCCESS;
	ULONG IoControlCode;
	size_t InputBufferLength;
	size_t OutputBufferLength;
	PVOID pOutputBuffer;
	PVOID pInputBuffer;
	ULONG_PTR information = 0;

	// https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/buffer-descriptions-for-i-o-control-codes#method_neither
	IoControlCode = Irp->Tail.Overlay.CurrentStackLocation->Parameters.DeviceIoControl.IoControlCode;
	InputBufferLength = Irp->Tail.Overlay.CurrentStackLocation->Parameters.DeviceIoControl.InputBufferLength;
	OutputBufferLength = Irp->Tail.Overlay.CurrentStackLocation->Parameters.DeviceIoControl.OutputBufferLength;	

	DbgPrint("wp81dbgPrint!DriverDispatch IoControlCode=0x%X InputBufferLength=0x%X OutputBufferLength=0x%X",IoControlCode,InputBufferLength,OutputBufferLength);

	pInputBuffer = Irp->Tail.Overlay.CurrentStackLocation->Parameters.DeviceIoControl.Type3InputBuffer;
	pOutputBuffer = Irp->UserBuffer;
	DbgPrint("wp81dbgPrint!DriverDispatch pInputBuffer=0x%p pOutputBuffer=0x%p", pInputBuffer, pOutputBuffer);

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