#include <ntifs.h>

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS MapFileAsImageSection(
    _In_ PCUNICODE_STRING pPath,
    _Out_ PHANDLE phFile,
    _Out_ PHANDLE phSection,
    _Outptr_result_maybenull_ PVOID* ppMappedBase
)
{
    NTSTATUS status = STATUS_SUCCESS;

    HANDLE hFile = NULL;
    HANDLE hSection = NULL;

    PVOID mappedBase = NULL;
    SIZE_T viewSize = 0;

    OBJECT_ATTRIBUTES objAttr;
    IO_STATUS_BLOCK iosb = {};

    // Validate parameters.
    if (pPath == NULL ||
        phFile == NULL ||
        phSection == NULL ||
        ppMappedBase == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *phFile = NULL;
    *phSection = NULL;
    *ppMappedBase = NULL;

    // These operations require PASSIVE_LEVEL.
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    // Validate the supplied path.
    if (pPath->Buffer == NULL ||
        pPath->Length == 0 ||
        pPath->Length > pPath->MaximumLength ||
        (pPath->Length % sizeof(WCHAR)) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    InitializeObjectAttributes(
        &objAttr,
        (PUNICODE_STRING)pPath,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
        NULL,
        NULL
    );

    // Open the image file.
    status = ZwOpenFile(
        &hFile,
        FILE_READ_DATA | FILE_EXECUTE | SYNCHRONIZE,
        &objAttr,
        &iosb,
        FILE_SHARE_READ | FILE_SHARE_DELETE,
        FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE
    );

    if (!NT_SUCCESS(status))
    {
        DbgPrintEx(
            DPFLTR_IHVDRIVER_ID,
            DPFLTR_ERROR_LEVEL,
            "MapFileAsImageSection: ZwOpenFile failed: 0x%08X\n",
            status
        );

        goto Cleanup;
    }

    InitializeObjectAttributes(
        &objAttr,
        NULL,
        OBJ_KERNEL_HANDLE,
        NULL,
        NULL
    );

    // Create image section.
    status = ZwCreateSection(
        &hSection,
        SECTION_QUERY | SECTION_MAP_READ,
        &objAttr,
        NULL,
        PAGE_READONLY,
        SEC_IMAGE,
        hFile
    );

    if (!NT_SUCCESS(status))
    {
        DbgPrintEx(
            DPFLTR_IHVDRIVER_ID,
            DPFLTR_ERROR_LEVEL,
            "MapFileAsImageSection: ZwCreateSection failed: 0x%08X\n",
            status
        );

        goto Cleanup;
    }

    // Map into the current process.
    status = ZwMapViewOfSection(
        hSection,
        ZwCurrentProcess(),
        &mappedBase,
        0,
        0,
        NULL,
        &viewSize,
        ViewUnmap,
        0,
        PAGE_READONLY
    );

    if (!NT_SUCCESS(status))
    {
        DbgPrintEx(
            DPFLTR_IHVDRIVER_ID,
            DPFLTR_ERROR_LEVEL,
            "MapFileAsImageSection: ZwMapViewOfSection failed: 0x%08X\n",
            status
        );

        goto Cleanup;
    }

    // Transfer ownership to caller.
    *phFile = hFile;
    *phSection = hSection;
    *ppMappedBase = mappedBase;

    hFile = NULL;
    hSection = NULL;
    mappedBase = NULL;

Cleanup:

    if (mappedBase != NULL)
    {
        NTSTATUS unmapStatus;

        unmapStatus = ZwUnmapViewOfSection(
            ZwCurrentProcess(),
            mappedBase
        );

        if (!NT_SUCCESS(unmapStatus))
        {
            DbgPrintEx(
                DPFLTR_IHVDRIVER_ID,
                DPFLTR_ERROR_LEVEL,
                "MapFileAsImageSection: Cleanup unmap failed: 0x%08X\n",
                unmapStatus
            );
        }
    }

    if (hSection != NULL)
    {
        ZwClose(hSection);
    }

    if (hFile != NULL)
    {
        ZwClose(hFile);
    }

    return status;
}
