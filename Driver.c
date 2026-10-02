/*
 * R2Fix final source, revision 2026100203.
 * Satechi R2 ST-BTMR2M: length-preserving HID report-descriptor repair.
 *
 * Derived from repair build 2026100202, verified on the user's Windows device.
 * Registry diagnostics and their work item have been removed. Filter placement,
 * forwarding, bounds checks and the proven three-byte correction are retained.
 */
#include <ntddk.h>
#include <wdf.h>
#include <hidport.h>

#define R2FIX_REPORT_LENGTH 336UL

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD R2FixEvtDeviceAdd;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL R2FixEvtIoDeviceControl;
EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL R2FixEvtIoInternalDeviceControl;
EVT_WDF_REQUEST_COMPLETION_ROUTINE R2FixReportCompletion;

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    WDF_DRIVER_CONFIG config;

    WDF_DRIVER_CONFIG_INIT(&config, R2FixEvtDeviceAdd);
    return WdfDriverCreate(
        DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &config,
        WDF_NO_HANDLE);
}

NTSTATUS
R2FixEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
)
{
    WDFDEVICE device;
    WDF_IO_QUEUE_CONFIG queueConfig;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(Driver);

    WdfFdoInitSetFilter(DeviceInit);
    status = WdfDeviceCreate(
        &DeviceInit, WDF_NO_OBJECT_ATTRIBUTES, &device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &queueConfig, WdfIoQueueDispatchParallel);
    /* Descriptor queries during device start must not wait for D0. */
    queueConfig.PowerManaged = WdfFalse;
    queueConfig.EvtIoDeviceControl = R2FixEvtIoDeviceControl;
    queueConfig.EvtIoInternalDeviceControl = R2FixEvtIoInternalDeviceControl;
    return WdfIoQueueCreate(
        device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
}

static VOID
R2FixForwardIoctl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ ULONG IoControlCode
)
{
    WDFDEVICE device;
    WDF_REQUEST_SEND_OPTIONS options;
    NTSTATUS status;

    device = WdfIoQueueGetDevice(Queue);
    if (IoControlCode == IOCTL_HID_GET_REPORT_DESCRIPTOR) {
        /* Keep the HID header unchanged, requesting the original 336 bytes. */
        WdfRequestFormatRequestUsingCurrentType(Request);
        WdfRequestSetCompletionRoutine(Request, R2FixReportCompletion, NULL);
        if (!WdfRequestSend(
            Request, WdfDeviceGetIoTarget(device), WDF_NO_SEND_OPTIONS)) {
            status = WdfRequestGetStatus(Request);
            WdfRequestComplete(Request, status);
        }
        return;
    }

    /* Do not reformat a request sent with SEND_AND_FORGET. No buffer access. */
    WDF_REQUEST_SEND_OPTIONS_INIT(
        &options, WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);
    if (!WdfRequestSend(Request, WdfDeviceGetIoTarget(device), &options)) {
        status = WdfRequestGetStatus(Request);
        WdfRequestComplete(Request, status);
    }
    /* A successful send transfers ownership; do not touch Request again. */
}

VOID
R2FixReportCompletion(
    _In_ WDFREQUEST Request,
    _In_ WDFIOTARGET Target,
    _In_ PWDF_REQUEST_COMPLETION_PARAMS CompletionParams,
    _In_ WDFCONTEXT Context
)
{
    static const UCHAR original[] =
        {0x81, 0x03, 0xC0, 0xC0, 0x00, 0x05, 0x0C, 0x09, 0x01};
    NTSTATUS lowerStatus;
    ULONG_PTR information;
    WDFMEMORY memory;
    PUCHAR buffer;
    size_t bufferLength;

    UNREFERENCED_PARAMETER(Target);
    UNREFERENCED_PARAMETER(Context);

    lowerStatus = CompletionParams->IoStatus.Status;
    information = CompletionParams->IoStatus.Information;
    if (NT_SUCCESS(lowerStatus) &&
        NT_SUCCESS(WdfRequestRetrieveOutputMemory(Request, &memory))) {
        buffer = (PUCHAR)WdfMemoryGetBuffer(memory, &bufferLength);
        if (buffer != NULL && bufferLength >= R2FIX_REPORT_LENGTH &&
            information == R2FIX_REPORT_LENGTH &&
            RtlCompareMemory(&buffer[306], original, sizeof(original)) ==
                sizeof(original)) {
            /* 05 0C and 06 0C 00 both encode Usage Page 0x000C.
             * Absorb the stray 00 into the valid longer item. Only these
             * three bytes change; no moving, shrinking or padding. */
            buffer[310] = 0x06;
            buffer[311] = 0x0C;
            buffer[312] = 0x00;
        }
    }

    /* A lookup failure or mismatch must not replace the downstream result. */
    WdfRequestCompleteWithInformation(Request, lowerStatus, information);
}

VOID
R2FixEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
)
{
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);
    R2FixForwardIoctl(Queue, Request, IoControlCode);
}

VOID
R2FixEvtIoInternalDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
)
{
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);
    R2FixForwardIoctl(Queue, Request, IoControlCode);
}
