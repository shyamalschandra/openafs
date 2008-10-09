/*
 * Copyright (c) 2008 Kernel Drivers, LLC.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice,
 *   this list of conditions and the following disclaimer in the
 *   documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of Kernel Drivers, LLC nor the names of its
 *   contributors may be
 *   used to endorse or promote products derived from this software without
 *   specific prior written permission from Kernel Drivers, LLC.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

//
// File: AFSGeneric.cpp
//

#include "AFSCommon.h"

//
// Function: AFSExceptionFilter
//
// Description:
//
//      This function is the exception handler
//
// Return:
//
//      A status is returned for the function
//

ULONG 
AFSExceptionFilter( IN ULONG Code, 
                    IN PEXCEPTION_POINTERS ExceptPtrs)
{

    PEXCEPTION_RECORD ExceptRec;
    PCONTEXT Context;

    __try
    {

        ExceptRec = ExceptPtrs->ExceptionRecord;

        Context = ExceptPtrs->ContextRecord;

        DbgPrint("**** Exception Caught in AFS Redirector ****\n");

        DbgPrint("\n\nPerform the following WnDbg Cmds:\n");
        DbgPrint("\n\t.exr %08lX ;  .cxr %08lX ;  .kb\n\n", ExceptRec, Context);
        
        DbgPrint("**** Exception Complete from AFS Redirector ****\n");

        AFSBreakPoint();
    }
    __except( EXCEPTION_EXECUTE_HANDLER)
    {

        NOTHING;
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

//
// Function: AFSAcquireExcl()
//
// Purpose: Called to acquire a resource exclusive with optional wait
//
// Parameters: 
//                PERESOURCE Resource - Resource to acquire
//                BOOLEAN Wait - Whether to block
//
// Return:
//                BOOLEAN - Whether the mask was acquired
//

BOOLEAN
AFSAcquireExcl( IN PERESOURCE Resource,
                IN BOOLEAN wait)
{

    BOOLEAN bStatus = FALSE;

    //
    // Normal kernel APCs must be disabled before calling 
    // ExAcquireResourceExclusiveLite. Otherwise a bugcheck occurs.
    //

    KeEnterCriticalRegion();

    bStatus = ExAcquireResourceExclusiveLite( Resource, 
                                              wait);

    if( !bStatus)
    {

        KeLeaveCriticalRegion();
    }

    return bStatus;
}

//
// Function: AFSAcquireShared()
//
// Purpose: Called to acquire a resource shared with optional wait
//
// Parameters: 
//                PERESOURCE Resource - Resource to acquire
//                BOOLEAN Wait - Whether to block
//
// Return:
//                BOOLEAN - Whether the mask was acquired
//

BOOLEAN
AFSAcquireShared( IN PERESOURCE Resource,
                  IN BOOLEAN wait)
{

    BOOLEAN bStatus = FALSE;

    KeEnterCriticalRegion();

    bStatus = ExAcquireResourceSharedLite( Resource,
                                           wait);

    if( !bStatus) 
    {

        KeLeaveCriticalRegion();
    }

    return bStatus;
}

//
// Function: AFSReleaseResource()
//
// Purpose: Called to release a resource
//
// Parameters: 
//                PERESOURCE Resource - Resource to release
//
// Return:
//                None
//

void
AFSReleaseResource( IN PERESOURCE Resource)
{

    ExReleaseResourceLite( Resource);

    KeLeaveCriticalRegion();

    return;
}

void
AFSConvertToShared( IN PERESOURCE Resource)
{

    ExConvertExclusiveToSharedLite( Resource);

    return;
}

//
// Function: AFSCompleteRequest
//
// Description:
//
//      This function completes irps
//
// Return:
//
//      A status is returned for the function
//

void
AFSCompleteRequest( IN PIRP Irp,
                    IN ULONG Status)
{

    Irp->IoStatus.Status = Status;

    IoCompleteRequest( Irp,
                       IO_NO_INCREMENT);

    return;
}

//
// Function: AFSBuildCRCTable
//
// Description:
//
//      This function builds the CRC table for mapping filenames to a CRC value.
//
// Return:
//
//      A status is returned for the function
//

void 
AFSBuildCRCTable()
{
    ULONG crc;
    int i, j;

    for ( i = 0; i <= 255; i++) 
    {
        crc = i;
        for ( j = 8; j > 0; j--) 
        {
            if (crc & 1)
            {
                crc = ( crc >> 1 ) ^ CRC32_POLYNOMIAL;
            }
            else
            {
                crc >>= 1;
            }
        }

        AFSCRCTable[ i ] = crc;
    }
}

//
// Function: AFSGenerateCRC
//
// Description:
//
//      Given a device and filename this function generates a CRC
//
// Return:
//
//      A status is returned for the function
//

ULONG
AFSGenerateCRC( IN PUNICODE_STRING FileName,
                IN BOOLEAN UpperCaseName)
{

    ULONG crc;
    ULONG temp1, temp2;
    UNICODE_STRING UpcaseString;
    WCHAR *lpbuffer;
    USHORT size = 0;

    if( !AFSCRCTable[1])
    {
        AFSBuildCRCTable();
    }

    crc = 0xFFFFFFFFL;

    if( UpperCaseName)
    {

        RtlUpcaseUnicodeString( &UpcaseString,
                                FileName,
                                TRUE);

        lpbuffer = UpcaseString.Buffer;

        size = (UpcaseString.Length/sizeof( WCHAR));
    }
    else
    {

        lpbuffer = FileName->Buffer;

        size = (FileName->Length/sizeof( WCHAR));
    }

    while (size--) 
    {
        temp1 = (crc >> 8) & 0x00FFFFFFL;        
        temp2 = AFSCRCTable[((int)crc ^ *lpbuffer++) & 0xff];
        crc = temp1 ^ temp2;
    }

    if( UpperCaseName)
    {

        RtlFreeUnicodeString( &UpcaseString);
    }

    crc ^= 0xFFFFFFFFL;

    return crc;
}

BOOLEAN 
AFSAcquireForLazyWrite( IN PVOID Context,
                        IN BOOLEAN Wait)
{

    AFSPrint("AFSAcquireForLazyWrite Called for Fcb %08lX\n", Context);


    return TRUE;
}

VOID 
AFSReleaseFromLazyWrite( IN PVOID Context)
{

    AFSPrint("AFSReleaseForLazyWrite Called for Fcb %08lX\n", Context);

    return;
}


BOOLEAN 
AFSAcquireForReadAhead( IN PVOID Context,
                        IN BOOLEAN Wait)
{

    AFSPrint("AFSAcquireForReadAhead Called for Fcb %08lX\n", Context);

    return TRUE;
}

VOID 
AFSReleaseFromReadAhead( IN PVOID Context)
{

    AFSPrint("AFSReleaseForReadAhead Called for Fcb %08lX\n", Context);

    return;
}

void *
AFSLockSystemBuffer( IN PIRP Irp,
                     IN ULONG Length)
{

    NTSTATUS Status = STATUS_SUCCESS;
    void *pAddress = NULL;

    //
    // When locking the buffer, we must ensure the underlying device does not perform buffered IO operations. If it does,
    // then the buffer will come from the AssociatedIrp->SystemAddress pointer, not the user buffer.
    //

    if (Irp->MdlAddress == NULL) 
    {

        Irp->MdlAddress = IoAllocateMdl( Irp->UserBuffer,
                                         Length,
                                         FALSE,
                                         FALSE,
                                         Irp);

        if( Irp->MdlAddress != NULL) 
        {

            //
            //  Lock the new Mdl in memory.
            //

            __try 
            {
                PIO_STACK_LOCATION pIoStack;
                pIoStack = IoGetCurrentIrpStackLocation( Irp);

                
                MmProbeAndLockPages( Irp->MdlAddress, KernelMode, 
                        (pIoStack->MajorFunction == IRP_MJ_READ) ? IoWriteAccess : IoReadAccess);

                pAddress = MmGetSystemAddressForMdlSafe( Irp->MdlAddress, NormalPagePriority );

            } 
            __except( AFSExceptionFilter( GetExceptionCode(), GetExceptionInformation()) )
            {

                IoFreeMdl( Irp->MdlAddress );
                Irp->MdlAddress = NULL;
                pAddress = NULL;
            }
        }
    } 
    else 
    {

        pAddress = MmGetSystemAddressForMdlSafe( Irp->MdlAddress, NormalPagePriority );
    }

    return pAddress;
}

void *
AFSMapToService( IN PIRP Irp,
                 IN ULONG ByteCount)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    void *pMappedBuffer = NULL;
    AFSDeviceExt *pDevExt = (AFSDeviceExt *)AFSDeviceObject->DeviceExtension;
    KAPC stApcState;

    __Enter
    {

        if( pDevExt->Specific.Control.ServiceProcess == NULL)
        {

            try_return( ntStatus = STATUS_DEVICE_NOT_READY);
        }

        if( Irp->MdlAddress == NULL)
        {

            if( AFSLockSystemBuffer( Irp,
                                     ByteCount) == NULL)
            {

                try_return( ntStatus = STATUS_INSUFFICIENT_RESOURCES);
            }
        }

        //
        // Attach to the service process for mapping
        //

        KeStackAttachProcess( pDevExt->Specific.Control.ServiceProcess,
                              (PRKAPC_STATE)&stApcState);

        pMappedBuffer = MmMapLockedPagesSpecifyCache( Irp->MdlAddress,
                                                      UserMode,
                                                      MmCached,
                                                      NULL,
                                                      FALSE,
                                                      NormalPagePriority);

        KeUnstackDetachProcess( (PRKAPC_STATE)&stApcState);

try_exit:

        NOTHING;
    }

    return pMappedBuffer;
}

NTSTATUS
AFSUnmapServiceMappedBuffer( IN void *MappedBuffer,
                             IN PMDL Mdl)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    void *pMappedBuffer = NULL;
    AFSDeviceExt *pDevExt = (AFSDeviceExt *)AFSDeviceObject->DeviceExtension;
    KAPC stApcState;

    __Enter
    {

        if( pDevExt->Specific.Control.ServiceProcess == NULL)
        {

            try_return( ntStatus = STATUS_DEVICE_NOT_READY);
        }

        if( Mdl != NULL)
        {

            //
            // Attach to the service process for mapping
            //

            KeStackAttachProcess( pDevExt->Specific.Control.ServiceProcess,
                                  (PRKAPC_STATE)&stApcState);

            MmUnmapLockedPages( MappedBuffer,
                                Mdl);

            KeUnstackDetachProcess( (PRKAPC_STATE)&stApcState);
        }

try_exit:

        NOTHING;
    }

    return ntStatus;
}

NTSTATUS
AFSReadRegistry( IN PUNICODE_STRING RegistryPath)
{

    NTSTATUS ntStatus        = STATUS_SUCCESS;
    ULONG Default            = 0;
    UNICODE_STRING paramPath;
    ULONG Value                = 0;
    RTL_QUERY_REGISTRY_TABLE paramTable[2];
    UNICODE_STRING defaultUnicodeName;
    WCHAR SubKeyString[]    = L"\\Parameters";

    //
    // Setup the paramPath buffer.
    //

    paramPath.MaximumLength = RegistryPath->Length + sizeof( SubKeyString); 
    paramPath.Buffer = (PWSTR)ExAllocatePoolWithTag( PagedPool,
                                                     paramPath.MaximumLength,
                                                     AFS_GENERIC_MEMORY_TAG);
 
    RtlInitUnicodeString( &defaultUnicodeName, 
                          L"NO NAME");

    //
    // If it exists, setup the path.
    //

    if( paramPath.Buffer != NULL) 
    { 
        
        //
        // Move in the paths
        //

        RtlCopyMemory( &paramPath.Buffer[ 0], 
                       &RegistryPath->Buffer[ 0], 
                       RegistryPath->Length);
        
        RtlCopyMemory( &paramPath.Buffer[ RegistryPath->Length / 2], 
                       SubKeyString, 
                       sizeof( SubKeyString)); 
 
        paramPath.Length = paramPath.MaximumLength; 

        RtlZeroMemory( paramTable, 
                       sizeof( paramTable));

        Value = 0;

        //
        // Setup the table to query the registry for the needed value
        //

        paramTable[0].Flags = RTL_QUERY_REGISTRY_DIRECT; 
        paramTable[0].Name = AFS_REG_DEBUG_FLAGS; 
        paramTable[0].EntryContext = &Value;
        
        paramTable[0].DefaultType = REG_DWORD; 
        paramTable[0].DefaultData = &Default; 
        paramTable[0].DefaultLength = sizeof (ULONG) ; 

        //
        // Query the registry
        //

        ntStatus = RtlQueryRegistryValues( RTL_REGISTRY_ABSOLUTE | RTL_REGISTRY_OPTIONAL, 
                                           paramPath.Buffer, 
                                           paramTable, 
                                           NULL, 
                                           NULL);

        if( NT_SUCCESS( ntStatus))
        {

            AFSDebugFlags = Value;
        }


        RtlZeroMemory( paramTable, 
                       sizeof( paramTable));

        Value = 0;

        //
        // Setup the table to query the registry for the needed value
        //

        paramTable[0].Flags = RTL_QUERY_REGISTRY_DIRECT; 
        paramTable[0].Name = AFS_REG_DEBUG_LEVEL; 
        paramTable[0].EntryContext = &Value;
        
        paramTable[0].DefaultType = REG_DWORD; 
        paramTable[0].DefaultData = &Default; 
        paramTable[0].DefaultLength = sizeof (ULONG) ; 

        //
        // Query the registry
        //

        ntStatus = RtlQueryRegistryValues( RTL_REGISTRY_ABSOLUTE | RTL_REGISTRY_OPTIONAL, 
                                           paramPath.Buffer, 
                                           paramTable, 
                                           NULL, 
                                           NULL);

        if( NT_SUCCESS( ntStatus))
        {

            AFSDebugLevel = Value;
        }

        //
        // Now get ready to set up for MaxServerDirty
        //

        paramTable[0].Flags = RTL_QUERY_REGISTRY_DIRECT; 
        paramTable[0].Name = AFS_REG_MAX_DIRTY; 
        paramTable[0].EntryContext = &Value;
        
        paramTable[0].DefaultType = REG_DWORD; 
        paramTable[0].DefaultData = &Default; 
        paramTable[0].DefaultLength = sizeof (ULONG) ; 

        //
        // Query the registry
        //

        ntStatus = RtlQueryRegistryValues( RTL_REGISTRY_ABSOLUTE | RTL_REGISTRY_OPTIONAL, 
                                           paramPath.Buffer, 
                                           paramTable, 
                                           NULL, 
                                           NULL);

        if( NT_SUCCESS( ntStatus))
        {

            AFSMaxDirtyFile = Value;
        }

        //
        // MaxIO
        //

        paramTable[0].Flags = RTL_QUERY_REGISTRY_DIRECT; 
        paramTable[0].Name = AFS_REG_MAX_IO; 
        paramTable[0].EntryContext = &Value;
        
        paramTable[0].DefaultType = REG_DWORD; 
        paramTable[0].DefaultData = &Default; 
        paramTable[0].DefaultLength = sizeof (ULONG) ; 

        //
        // Query the registry
        //

        ntStatus = RtlQueryRegistryValues( RTL_REGISTRY_ABSOLUTE | RTL_REGISTRY_OPTIONAL, 
                                           paramPath.Buffer, 
                                           paramTable, 
                                           NULL, 
                                           NULL);

        if( NT_SUCCESS( ntStatus))
        {

            AFSMaxDirectIo = Value;
        }

        //
        // Free up the buffer
        //

        ExFreePool( paramPath.Buffer);

        ntStatus = STATUS_SUCCESS;
    } 
    else 
    {
        ntStatus = STATUS_INSUFFICIENT_RESOURCES;
    }

    return ntStatus;
}

NTSTATUS
AFSInitializeControlFilter()
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    AFSDeviceExt *pDeviceExt = (AFSDeviceExt *)AFSDeviceObject->DeviceExtension;

    __Enter
    {

        //
        // Initialize the comm pool resources
        //

        ExInitializeResourceLite( &pDeviceExt->Specific.Control.CommServiceCB.IrpPoolLock);

        ExInitializeResourceLite( &pDeviceExt->Specific.Control.CommServiceCB.ResultPoolLock);

        ExInitializeResourceLite( &pDeviceExt->Specific.Control.ExtentReleaseResource);


        //
        // And the events
        //

        KeInitializeEvent( &pDeviceExt->Specific.Control.CommServiceCB.IrpPoolHasEntries,
                           NotificationEvent,
                           FALSE);

        KeInitializeEvent( &pDeviceExt->Specific.Control.ExtentReleaseEvent,
                           NotificationEvent,
                           FALSE);

        pDeviceExt->Specific.Control.ExtentReleaseSequence = 0;

        //
        // Set the initial state of the irp pool
        //

        pDeviceExt->Specific.Control.CommServiceCB.IrpPoolControlFlag = POOL_INACTIVE;

        //
        // Initialize the provider list lock
        //

        ExInitializeResourceLite( &AFSProviderListLock);
    }

    return ntStatus;
}

NTSTATUS
AFSDefaultDispatch( IN PDEVICE_OBJECT DeviceObject,
                    IN PIRP Irp)
{

    NTSTATUS            ntStatus = STATUS_INVALID_DEVICE_REQUEST;
    PIO_STACK_LOCATION  pIrpSp = IoGetCurrentIrpStackLocation( Irp);

    AFSPrint("AFSDefaultDispatch Handling %08lX\n", pIrpSp->MajorFunction);

    AFSCompleteRequest( Irp, 
                        ntStatus);

    return ntStatus;
}

NTSTATUS
AFSInitializeDirectory( IN AFSFcb *Dcb)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    AFSDirEntryCB *pDirNode = NULL;
    AFSDirEnumEntry stDirEnumEntry;
    UNICODE_STRING uniDirName;

    __Enter
    {

        uniDirName.Length = 0;
        uniDirName.MaximumLength = 2 * sizeof( WCHAR);

        uniDirName.Buffer = (WCHAR *)ExAllocatePoolWithTag( PagedPool,
                                                            uniDirName.MaximumLength,
                                                            AFS_GENERIC_MEMORY_TAG);

        if( uniDirName.Buffer == NULL)
        {

            try_return( ntStatus = STATUS_INSUFFICIENT_RESOURCES);
        }

        RtlZeroMemory( &stDirEnumEntry,
                       sizeof( AFSDirEnumEntry));

        stDirEnumEntry.CreationTime = Dcb->ParentFcb->DirEntry->DirectoryEntry.CreationTime;

        stDirEnumEntry.LastAccessTime = Dcb->ParentFcb->DirEntry->DirectoryEntry.LastAccessTime;

        stDirEnumEntry.LastWriteTime = Dcb->ParentFcb->DirEntry->DirectoryEntry.LastAccessTime;

        stDirEnumEntry.FileAttributes = FILE_ATTRIBUTE_DIRECTORY;

        uniDirName.Length = sizeof( WCHAR);

        uniDirName.Buffer[ 0] = L'.';

        uniDirName.Buffer[ 1] = L'\0';

        //
        // Initialize the entry
        //

        pDirNode = AFSInitDirEntry( &Dcb->ParentFcb->DirEntry->DirectoryEntry.FileId,
                                    &uniDirName,
                                    NULL,
                                    &stDirEnumEntry,
                                    (ULONG)-1);

        if( pDirNode == NULL)
        {

            try_return( ntStatus = STATUS_INSUFFICIENT_RESOURCES);
        }

        //
        // And insert the node into the directory list
        // This is the first entry in the lsit
        //

        Dcb->Specific.Directory.DirectoryNodeListHead = pDirNode;

        Dcb->Specific.Directory.DirectoryNodeListTail = pDirNode;

        SetFlag( pDirNode->Flags, AFS_DIR_ENTRY_NOT_IN_PARENT_TREE);

        //
        // And the .. entry
        //

        uniDirName.Length = 2 * sizeof( WCHAR);

        uniDirName.Buffer[ 0] = L'.';

        uniDirName.Buffer[ 1] = L'.';

        //
        // Initialize the entry
        //

        pDirNode = AFSInitDirEntry( &Dcb->ParentFcb->DirEntry->DirectoryEntry.FileId,
                                    &uniDirName,
                                    NULL,
                                    &stDirEnumEntry,
                                    (ULONG)-2);

        if( pDirNode == NULL)
        {

            try_return( ntStatus = STATUS_INSUFFICIENT_RESOURCES);
        }

        //
        // And insert the node into the directory list
        // This is the second entry in the list
        //

        Dcb->Specific.Directory.DirectoryNodeListTail->ListEntry.fLink = (void *)pDirNode;

        pDirNode->ListEntry.bLink = (void *)Dcb->Specific.Directory.DirectoryNodeListTail;

        Dcb->Specific.Directory.DirectoryNodeListTail = pDirNode;

        SetFlag( pDirNode->Flags, AFS_DIR_ENTRY_NOT_IN_PARENT_TREE);

try_exit:

        if( uniDirName.Buffer != NULL)
        {

            ExFreePool( uniDirName.Buffer);
        }

        if( !NT_SUCCESS( ntStatus))
        {

            if( Dcb->Specific.Directory.DirectoryNodeListHead != NULL)
            {

                ExFreePool( Dcb->Specific.Directory.DirectoryNodeListHead);

                Dcb->Specific.Directory.DirectoryNodeListHead = NULL;

                Dcb->Specific.Directory.DirectoryNodeListTail = NULL;
            }
        }
    }

    return ntStatus;
}

NTSTATUS
AFSInitNonPagedDirEntry( IN AFSDirEntryCB *DirNode)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;

    __Enter
    {

        //
        // Initialize the dir entry resource
        //

        ExInitializeResourceLite( &DirNode->NPDirNode->Lock);

    }

    return ntStatus;
}

AFSDirEntryCB *
AFSInitDirEntry( IN AFSFileID *ParentFileID,
                 IN PUNICODE_STRING FileName,
                 IN PUNICODE_STRING TargetName,
                 IN AFSDirEnumEntry *DirEnumEntry,
                 IN ULONG FileIndex)
{

    AFSDirEntryCB *pDirNode = NULL;
    NTSTATUS ntStatus = STATUS_SUCCESS;
    ULONG ulEntryLength = 0;
    AFSDirEnumEntry *pDirEnumCB = NULL;
    AFSFileID stTargetFileID;
    AFSFcb *pVcb = NULL;
    AFSDeviceExt *pDeviceExt = (AFSDeviceExt *)AFSRDRDeviceObject->DeviceExtension;

    __Enter
    {
        ulEntryLength = sizeof( AFSDirEntryCB) + 
                                     FileName->Length;
        
        if( TargetName != NULL)
        {
            
            ulEntryLength += TargetName->Length;
        }

        pDirNode = (AFSDirEntryCB *)ExAllocatePoolWithTag( PagedPool,         
                                                           ulEntryLength,
                                                           AFS_DIR_ENTRY_TAG);

        if( pDirNode == NULL)
        {

            try_return( ntStatus = STATUS_INSUFFICIENT_RESOURCES);
        }

        AFSAllocationMemoryLevel += ulEntryLength;

        RtlZeroMemory( pDirNode,
                       ulEntryLength);

        //
        // Allocate the non paged portion
        //

        pDirNode->NPDirNode = (AFSNonPagedDirNode *)ExAllocatePoolWithTag( NonPagedPool,  
                                                                           sizeof( AFSNonPagedDirNode),
                                                                           AFS_DIR_ENTRY_NP_TAG);

        if( pDirNode->NPDirNode == NULL)
        {

            ExFreePool( pDirNode);

            AFSAllocationMemoryLevel -= ulEntryLength;

            try_return( ntStatus = STATUS_INSUFFICIENT_RESOURCES);
        }

        //
        // Initialize the non-paged portion of the dire entry
        //

        AFSInitNonPagedDirEntry( pDirNode);

        //
        // Setup the names in the entry
        //

        if( FileName->Length > 0)
        {

            pDirNode->DirectoryEntry.FileName.Length = FileName->Length;

            pDirNode->DirectoryEntry.FileName.MaximumLength = FileName->Length;

            pDirNode->DirectoryEntry.FileName.Buffer = (WCHAR *)((char *)pDirNode + sizeof( AFSDirEntryCB));

            RtlCopyMemory( pDirNode->DirectoryEntry.FileName.Buffer,
                           FileName->Buffer,
                           pDirNode->DirectoryEntry.FileName.Length);       

            //
            // Create a CRC for the file
            //

            pDirNode->CaseSensitiveTreeEntry.HashIndex = AFSGenerateCRC( &pDirNode->DirectoryEntry.FileName,
                                                                         FALSE);

            pDirNode->CaseInsensitiveTreeEntry.HashIndex = AFSGenerateCRC( &pDirNode->DirectoryEntry.FileName,
                                                                           TRUE);
        }

        if( TargetName != NULL &&
            TargetName->Length > 0)
        {

            pDirNode->DirectoryEntry.TargetName.Length = TargetName->Length;

            pDirNode->DirectoryEntry.TargetName.MaximumLength = pDirNode->DirectoryEntry.TargetName.Length;

            pDirNode->DirectoryEntry.TargetName.Buffer = (WCHAR *)((char *)pDirNode + sizeof( AFSDirEntryCB) + pDirNode->DirectoryEntry.FileName.Length);

            RtlCopyMemory( pDirNode->DirectoryEntry.TargetName.Buffer,
                           TargetName->Buffer,
                           pDirNode->DirectoryEntry.TargetName.Length);                                   
        }

        pDirNode->DirectoryEntry.FileIndex = FileIndex; 

        //
        // Populate the rest of the data
        //

        if( ParentFileID != NULL)
        {

            pDirNode->DirectoryEntry.ParentId = *ParentFileID;
        }

        pDirNode->DirectoryEntry.FileId = DirEnumEntry->FileId;

        pDirNode->DirectoryEntry.TargetFileId = DirEnumEntry->TargetFileId;

        pDirNode->DirectoryEntry.Expiration = DirEnumEntry->Expiration;

        pDirNode->DirectoryEntry.DataVersion = DirEnumEntry->DataVersion;

        pDirNode->DirectoryEntry.FileType = DirEnumEntry->FileType;

        pDirNode->DirectoryEntry.CreationTime = DirEnumEntry->CreationTime;

        pDirNode->DirectoryEntry.LastAccessTime = DirEnumEntry->LastAccessTime;

        pDirNode->DirectoryEntry.LastWriteTime = DirEnumEntry->LastWriteTime;

        pDirNode->DirectoryEntry.ChangeTime = DirEnumEntry->ChangeTime;

        pDirNode->DirectoryEntry.EndOfFile = DirEnumEntry->EndOfFile;

        pDirNode->DirectoryEntry.AllocationSize = DirEnumEntry->AllocationSize;

        pDirNode->DirectoryEntry.FileAttributes = DirEnumEntry->FileAttributes;

        pDirNode->DirectoryEntry.EaSize = DirEnumEntry->EaSize;

        pDirNode->DirectoryEntry.Links = DirEnumEntry->Links;

        //
        // If this is a symlink then we need to add in the directory attribute
        //

        if( pDirNode->DirectoryEntry.FileType == 0)
        {

            //
            // Set this node as a directory and set the AFS_DIR_ENTRY_NOT_EVALUATED flag
            //
            
            pDirNode->DirectoryEntry.FileType = AFS_FILE_TYPE_DIRECTORY;

            SetFlag( pDirNode->Flags, AFS_DIR_ENTRY_NOT_EVALUATED);
        }

        if( pDirNode->DirectoryEntry.FileType == AFS_FILE_TYPE_DIRECTORY ||
            pDirNode->DirectoryEntry.FileType == AFS_FILE_TYPE_SYMLINK)
        {

            pDirNode->DirectoryEntry.FileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
        }
        else if( pDirNode->DirectoryEntry.FileType == AFS_FILE_TYPE_MOUNTPOINT)
        {

            pDirNode->DirectoryEntry.FileAttributes |= (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY);

            //
            // See if the target already exists for this mount point
            //

            if( pDirNode->DirectoryEntry.TargetFileId.Hash == 0)
            {                

                ntStatus = AFSEvaluateTargetByID( &pDirNode->DirectoryEntry.FileId,
                                                  &pDirEnumCB);

                if( !NT_SUCCESS( ntStatus))
                {

                    try_return( ntStatus);
                }

                //
                // If the target fid is zero it could not be evaluated
                //

                if( pDirEnumCB->TargetFileId.Hash != 0)
                {

                    //
                    // Update the target fid in the volume entry
                    //

                    pDirNode->DirectoryEntry.TargetFileId = pDirEnumCB->TargetFileId;
                }

                ExFreePool( pDirEnumCB);
            }

            AFSAcquireShared( &pDeviceExt->Specific.RDR.VolumeTreeLock, TRUE);

            //
            // Locate the volume node
            //

            AFSLocateHashEntry( pDeviceExt->Specific.RDR.VolumeTree.TreeHead,
                                AFSCreateHighIndex( &pDirNode->DirectoryEntry.TargetFileId),
                                &pVcb);

            AFSReleaseResource( &pDeviceExt->Specific.RDR.VolumeTreeLock);

            if( pVcb == NULL)
            {

                //
                // Initialize the volume root for the target of this mount point
                // We don't worry about errors here since we will re-initialize it
                // if the target does not exist when we walk the nodes
                //

                AFSInitRootFcb( pDirNode,
                                NULL);
            }
        }

try_exit:

        if( !NT_SUCCESS( ntStatus))
        {

            if( pDirNode != NULL)
            {

                if( pDirNode->NPDirNode != NULL)
                {

                    ExDeleteResourceLite( &pDirNode->NPDirNode->Lock);

                    ExFreePool( pDirNode->NPDirNode);
                }

                ExFreePool( pDirNode);

                pDirNode = NULL;

                AFSAllocationMemoryLevel -= ulEntryLength;
            }
        }
    }

    return pDirNode;
}

BOOLEAN
AFSCheckForReadOnlyAccess( IN ACCESS_MASK DesiredAccess)
{

    BOOLEAN bReturn = TRUE;

    //
    // Get rid of anything we don't know about
    //

    DesiredAccess = (DesiredAccess   &
                          ( DELETE |
                            READ_CONTROL |
                            WRITE_OWNER |
                            WRITE_DAC |
                            SYNCHRONIZE |
                            ACCESS_SYSTEM_SECURITY |
                            FILE_WRITE_DATA |
                            FILE_READ_EA |
                            FILE_WRITE_EA |
                            FILE_READ_ATTRIBUTES |
                            FILE_WRITE_ATTRIBUTES |
                            FILE_LIST_DIRECTORY |
                            FILE_TRAVERSE |
                            FILE_DELETE_CHILD |
                            FILE_APPEND_DATA));                       

    if( FlagOn( DesiredAccess, ~( READ_CONTROL |
                                 WRITE_OWNER |
                                 WRITE_DAC |
                                 SYNCHRONIZE |
                                 ACCESS_SYSTEM_SECURITY |
                                 FILE_READ_DATA |
                                 FILE_READ_EA |
                                 FILE_WRITE_EA |
                                 FILE_READ_ATTRIBUTES |
                                 FILE_WRITE_ATTRIBUTES |
                                 FILE_EXECUTE |
                                 FILE_LIST_DIRECTORY |
                                 FILE_TRAVERSE))) 
    {

        //
        // A write access is set ...
        //

        bReturn = FALSE;
    }

    return bReturn;
}

NTSTATUS
AFSEvaluateNode( IN AFSFcb *Fcb)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    AFSDirEnumEntry *pDirEntry = NULL;

    __Enter
    {

        ntStatus = AFSEvaluateTargetByID( &Fcb->DirEntry->DirectoryEntry.FileId,
                                          &pDirEntry);

        if( !NT_SUCCESS( ntStatus) ||
            pDirEntry->FileType == 0)
        {

            try_return( ntStatus = STATUS_OBJECT_NAME_NOT_FOUND);
        }

        Fcb->DirEntry->DirectoryEntry.TargetFileId = pDirEntry->TargetFileId;

        Fcb->DirEntry->DirectoryEntry.Expiration = pDirEntry->Expiration;

        Fcb->DirEntry->DirectoryEntry.DataVersion = pDirEntry->DataVersion;

        Fcb->DirEntry->DirectoryEntry.FileType = pDirEntry->FileType;

        Fcb->DirEntry->DirectoryEntry.CreationTime = pDirEntry->CreationTime;

        Fcb->DirEntry->DirectoryEntry.LastAccessTime = pDirEntry->LastAccessTime;

        Fcb->DirEntry->DirectoryEntry.LastWriteTime = pDirEntry->LastWriteTime;

        Fcb->DirEntry->DirectoryEntry.ChangeTime = pDirEntry->ChangeTime;

        Fcb->DirEntry->DirectoryEntry.EndOfFile = pDirEntry->EndOfFile;

        Fcb->DirEntry->DirectoryEntry.AllocationSize = pDirEntry->AllocationSize;

        Fcb->DirEntry->DirectoryEntry.FileAttributes = pDirEntry->FileAttributes;

        Fcb->DirEntry->DirectoryEntry.EaSize = pDirEntry->EaSize;

        Fcb->DirEntry->DirectoryEntry.Links = pDirEntry->Links;

        if( Fcb->DirEntry->DirectoryEntry.FileType == AFS_FILE_TYPE_DIRECTORY)
        {

            Fcb->DirEntry->DirectoryEntry.FileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
        }
        else if( Fcb->DirEntry->DirectoryEntry.FileType == AFS_FILE_TYPE_SYMLINK)
        {

            Fcb->DirEntry->DirectoryEntry.FileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
        }
        else if( Fcb->DirEntry->DirectoryEntry.FileType == AFS_FILE_TYPE_MOUNTPOINT)
        {

            Fcb->DirEntry->DirectoryEntry.FileAttributes |= FILE_ATTRIBUTE_REPARSE_POINT;
        }

try_exit:

        if( pDirEntry != NULL)
        {

            ExFreePool( pDirEntry);
        }
    }

    return ntStatus;
}

NTSTATUS
AFSInvalidateCache( IN AFSInvalidateCacheCB *InvalidateCB)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    AFSFcb      *pDcb = NULL, *pFcb = NULL, *pNextFcb = NULL, *pVcb = NULL;
    AFSDeviceExt *pDevExt = (AFSDeviceExt *) AFSRDRDeviceObject->DeviceExtension;
    AFSDirEntryCB *pCurrentDirEntry = NULL;
    BOOLEAN     bIsChild = FALSE;
    ULONGLONG   ullIndex = 0;

    __Enter
    {

        //
        // If this is for the entire volume then flush the root directory
        //

        if( InvalidateCB->WholeVolume)
        {

            AFSRemoveAFSRoot();

            try_return( ntStatus);
        }

        //
        // Need to locate the Fcb for the directory to purge
        //
    
        AFSAcquireShared( &pDevExt->Specific.RDR.VolumeTreeLock, TRUE);

        //
        // Locate the volume node
        //

        ullIndex = AFSCreateHighIndex( &InvalidateCB->FileID);

        ntStatus = AFSLocateHashEntry( pDevExt->Specific.RDR.VolumeTree.TreeHead,
                                       ullIndex,
                                       &pVcb);

        if( pVcb != NULL)
        {

            AFSAcquireShared( pVcb->Specific.VolumeRoot.FileIDTree.TreeLock,
                              TRUE);
        }

        AFSReleaseResource( &pDevExt->Specific.RDR.VolumeTreeLock);

        if( !NT_SUCCESS( ntStatus) ||
            pVcb == NULL) 
        {
            try_return( ntStatus = STATUS_UNSUCCESSFUL);
        }

        //
        // Now locate the Fcb in this volume
        //

        ullIndex = AFSCreateLowIndex( &InvalidateCB->FileID);

        ntStatus = AFSLocateHashEntry( pVcb->Specific.VolumeRoot.FileIDTree.TreeHead,
                                       ullIndex,
                                       &pDcb);

        if( pDcb != NULL)
        {

            InterlockedIncrement( &pDcb->OpenReferenceCount);
        }

        AFSReleaseResource( pVcb->Specific.VolumeRoot.FileIDTree.TreeLock);

        if( !NT_SUCCESS( ntStatus) ||
            pDcb == NULL) 
        {
            try_return( ntStatus = STATUS_UNSUCCESSFUL);
        }

        //
        // Indicate this node requires re-evaluation
        //

        SetFlag( pDcb->DirEntry->Flags, AFS_DIR_ENTRY_NOT_EVALUATED);

        //
        // Get the target of the node if it is a mount point or sym link
        //

        if( pDcb->DirEntry->DirectoryEntry.FileType == AFS_FILE_TYPE_SYMLINK)
        {

            //
            // Check if we have a target Fcb for this node
            //

            if( pDcb->Specific.SymbolicLink.TargetFcb == NULL)
            {

                //
                // Go retrieve the target entry for this node
                //

                ntStatus = AFSBuildTargetDirectory( pDcb);

                if( !NT_SUCCESS( ntStatus))
                {
                    
                    AFSReleaseResource( &pDcb->NPFcb->Resource);

                    try_return( ntStatus);
                }
            }

            //
            // Swap out where we are in the chain
            //

            pDcb = pDcb->Specific.SymbolicLink.TargetFcb;
        }
        else if( pDcb->DirEntry->DirectoryEntry.FileType == AFS_FILE_TYPE_MOUNTPOINT)
        {

            //
            // Check if we have a target Fcb for this node
            //

            if( pDcb->Specific.MountPoint.TargetFcb == NULL)
            {

                AFSAcquireShared( pDevExt->Specific.RDR.VolumeTree.TreeLock,
                                  TRUE);

                ullIndex = AFSCreateHighIndex( &pDcb->DirEntry->DirectoryEntry.TargetFileId);

                AFSLocateHashEntry( pDevExt->Specific.RDR.VolumeTree.TreeHead,
                                    ullIndex,
                                    &pDcb->Specific.MountPoint.TargetFcb);

                AFSReleaseResource( pDevExt->Specific.RDR.VolumeTree.TreeLock);
            }

            ASSERT( pDcb->Specific.MountPoint.TargetFcb != NULL);

            //
            // Swap out where we are in the chain
            //

            pDcb = pDcb->Specific.MountPoint.TargetFcb;
        }

        //
        // Depending on the reason for invalidation then perform work on the node
        //

        switch( InvalidateCB->Reason)
        {

            case AFS_INVALIDATE_DELETED:
            {

                //
                // In this case we have the most work to do ...
                // Perform the invalidation depending on the type of node
                //

                switch( pDcb->DirEntry->DirectoryEntry.FileType)
                {

                    case AFS_FILE_TYPE_DIRECTORY:
                    {

                        AFSAcquireExcl( pDcb->Specific.Directory.DirectoryNodeHdr.TreeLock,
                                        TRUE);

                        //
                        // Reset the directory list information
                        //

                        pCurrentDirEntry = pDcb->Specific.Directory.DirectoryNodeListHead;

                        while( pCurrentDirEntry != NULL)
                        {

                            pFcb = pCurrentDirEntry->Fcb;

                            if( pFcb != NULL)
                            {

                                //
                                // Do this prior to blocking the Fcb since there could be a thread
                                // holding the Fcb waiting for an extent
                                //

                                if( pFcb->Header.NodeTypeCode == AFS_FILE_FCB)
                                {

                                    //
                                    // Clear out anybody waiting for the extents.
                                    //

                                    AFSAcquireExcl( &pFcb->NPFcb->Specific.File.ExtentsResource, 
                                                    TRUE);

                                    pFcb->NPFcb->Specific.File.ExtentsRequestStatus = STATUS_CANCELLED;

                                    KeSetEvent( &pFcb->NPFcb->Specific.File.ExtentsRequestComplete,
                                                0,
                                                FALSE);

                                    AFSReleaseResource( &pFcb->NPFcb->Specific.File.ExtentsResource);

                                    //
                                    // Now flush any dirty extents
                                    //
                                    (VOID) AFSFlushExtents( pFcb);
                                        
                                    //
                                    // And get rid of them (not this involves waiting
                                    // for any writes or reads to the cache to complete
                                    //
                                    (VOID) AFSTearDownFcbExtents( pFcb);
                                        
                                }

                                AFSAcquireExcl( &pFcb->NPFcb->Resource,
                                                TRUE);

                                if( pFcb->DirEntry != NULL)
                                {

                                    pFcb->DirEntry->Fcb = NULL;

                                    SetFlag( pFcb->DirEntry->Flags, AFS_DIR_RELEASE_DIRECTORY_NODE);
                                }

                                SetFlag( pFcb->Flags, AFS_FCB_INVALID);

                                AFSAcquireExcl( pFcb->RootFcb->Specific.VolumeRoot.FileIDTree.TreeLock,
                                                TRUE);

                                AFSRemoveHashEntry( &pFcb->RootFcb->Specific.VolumeRoot.FileIDTree.TreeHead,
                                                    &pFcb->TreeEntry);

                                AFSReleaseResource( pFcb->RootFcb->Specific.VolumeRoot.FileIDTree.TreeLock);

                                ClearFlag( pFcb->Flags, AFS_FCB_INSERTED_ID_TREE);

                                AFSReleaseResource( &pFcb->NPFcb->Resource);

                                AFSRemoveDirNodeFromParent( pDcb,
                                                            pCurrentDirEntry);
                            }
                            else
                            {

                                AFSDeleteDirEntry( pDcb,
                                                   pCurrentDirEntry);
                            }

                            pCurrentDirEntry = pDcb->Specific.Directory.DirectoryNodeListHead;
                        }

                        pDcb->Specific.Directory.DirectoryNodeHdr.CaseSensitiveTreeHead = NULL;

                        pDcb->Specific.Directory.DirectoryNodeHdr.CaseInsensitiveTreeHead = NULL;

                        pDcb->Specific.Directory.DirectoryNodeListHead = NULL;

                        pDcb->Specific.Directory.DirectoryNodeListHead = NULL;

                        //
                        // Indicate the node is NOT initialized
                        //

                        ClearFlag( pDcb->Flags, AFS_FCB_DIRECTORY_ENUMERATED);

                        AFSReleaseResource( pDcb->Specific.Directory.DirectoryNodeHdr.TreeLock);

                        //
                        // Tag the Fcbs as invalid so they are torn down
                        // Do this for any which has this node as a parent or
                        // this parent somewhere in the chain
                        //

                        AFSAcquireShared( pDcb->RootFcb->Specific.VolumeRoot.FcbListLock,
                                          TRUE);

                        pFcb = pDcb->RootFcb->Specific.VolumeRoot.FcbListHead;

                        while( pFcb != NULL)
                        {

                            bIsChild = AFSIsChildOfParent( pDcb,
                                                           pFcb);

                            if( bIsChild)
                            {
                                if( pFcb->Header.NodeTypeCode == AFS_FILE_FCB)
                                {

                                    //
                                    // Clear out the extents
                                    //

                                    AFSAcquireExcl( &pFcb->NPFcb->Specific.File.ExtentsResource, 
                                                    TRUE);

                                    pFcb->NPFcb->Specific.File.ExtentsRequestStatus = STATUS_CANCELLED;

                                    KeSetEvent( &pFcb->NPFcb->Specific.File.ExtentsRequestComplete,
                                                0,
                                                FALSE);

                                    AFSReleaseResource( &pFcb->NPFcb->Specific.File.ExtentsResource);
                                }

                                AFSAcquireExcl( &pFcb->NPFcb->Resource,
                                                TRUE);

                                SetFlag( pFcb->Flags, AFS_FCB_INVALID);

                                AFSAcquireExcl( pFcb->RootFcb->Specific.VolumeRoot.FileIDTree.TreeLock,
                                                TRUE);

                                AFSRemoveHashEntry( &pFcb->RootFcb->Specific.VolumeRoot.FileIDTree.TreeHead,
                                                    &pFcb->TreeEntry);

                                AFSReleaseResource( pFcb->RootFcb->Specific.VolumeRoot.FileIDTree.TreeLock);

                                ClearFlag( pFcb->Flags, AFS_FCB_INSERTED_ID_TREE);

                                AFSReleaseResource( &pFcb->NPFcb->Resource);
                            }

                            pNextFcb = (AFSFcb *)pFcb->ListEntry.fLink;
                            
                            pFcb = pNextFcb;
                        }

                        AFSReleaseResource( pDcb->RootFcb->Specific.VolumeRoot.FcbListLock);

                        break;
                    }

                    case AFS_FILE_TYPE_FILE:
                    {

                        AFSAcquireExcl( &pDcb->NPFcb->Specific.File.ExtentsResource, 
                                        TRUE);

                        pDcb->NPFcb->Specific.File.ExtentsRequestStatus = STATUS_CANCELLED;

                        KeSetEvent( &pDcb->NPFcb->Specific.File.ExtentsRequestComplete,
                                    0,
                                    FALSE);

                        AFSReleaseResource( &pDcb->NPFcb->Specific.File.ExtentsResource);

                        //
                        // Now flush any dirty extents
                        //
                        (VOID) AFSFlushExtents( pDcb);
                                        
                        //
                        // And get rid of them (not this involves waiting
                        // for any writes or reads to the cache to complete
                        //
                        (VOID) AFSTearDownFcbExtents( pDcb);

                        break;
                    }

                    default:
                    {

                        ASSERT( FALSE);

                        break;
                    }
                }

                // Need to acquire the parent node in this case so drop the Fcb and re-acquire in correct
                // order. Of course do this only if the dir entry is in the parent list
                //

                if( !BooleanFlagOn( pDcb->DirEntry->Flags, AFS_DIR_RELEASE_DIRECTORY_NODE))
                {

                    ASSERT( pDcb->ParentFcb != NULL);
                    
                    AFSAcquireExcl( &pDcb->ParentFcb->NPFcb->Resource,
                                    TRUE);

                    AFSAcquireExcl( &pDcb->NPFcb->Resource,
                                    TRUE);

                    //
                    // Remove the Dir entry from the parent list
                    //

                    AFSRemoveDirNodeFromParent( pDcb->ParentFcb,
                                                pDcb->DirEntry);

                    SetFlag( pDcb->DirEntry->Flags, AFS_DIR_RELEASE_DIRECTORY_NODE);

                    AFSReleaseResource( &pDcb->ParentFcb->NPFcb->Resource);

                    AFSReleaseResource( &pDcb->NPFcb->Resource);
                }

                break;
            }

            default:
            {

                break;
            }
        }        

try_exit:

        if( pDcb != NULL)
        {

            InterlockedDecrement( &pDcb->OpenReferenceCount);
        }        
    }

    return ntStatus;
}

BOOLEAN
AFSIsChildOfParent( IN AFSFcb *Dcb,
                    IN AFSFcb *Fcb)
{

    BOOLEAN bIsChild = FALSE;
    AFSFcb *pCurrentFcb = Fcb;

    while( pCurrentFcb != NULL)
    {

        if( pCurrentFcb->ParentFcb == Dcb)
        {

            bIsChild = TRUE;

            break;
        }

        pCurrentFcb = pCurrentFcb->ParentFcb;
    }

    return bIsChild;
}

NTSTATUS
AFSRetrieveTargetFID( IN AFSFcb *Fcb,
                      OUT AFSFileID *FileId)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    AFSDirEnumEntry *pDirEntry = NULL;
    AFSFileID stTargetFID;

    __Enter
    {

        //
        // MP or SL, need to determine what the target is
        //

        if( Fcb->DirEntry->DirectoryEntry.TargetFileId.Hash != 0)
        {

            //
            // We know this is either a SL or MP so start with the target
            //

            stTargetFID = Fcb->DirEntry->DirectoryEntry.TargetFileId;
        }
        else
        {

            stTargetFID = Fcb->DirEntry->DirectoryEntry.FileId;
        }

        //
        // Loop until we get to the directory of the node
        //

        while( TRUE)
        {

            ntStatus = AFSEvaluateTargetByID( &stTargetFID,
                                              &pDirEntry);

            if( !NT_SUCCESS( ntStatus))
            {

                break;
            }

            //
            // Is this a directory?
            //

            if( pDirEntry->FileType == AFS_FILE_TYPE_DIRECTORY)
            {

                //
                // Found the end point
                //

                *FileId = pDirEntry->FileId;

                ExFreePool( pDirEntry);

                break;
            }

            stTargetFID = pDirEntry->TargetFileId;

            ExFreePool( pDirEntry);
        }
    }

    return ntStatus;
}

inline
ULONGLONG
AFSCreateHighIndex( IN AFSFileID *FileID)
{

    ULONGLONG ullIndex = 0;

    ullIndex = (((ULONGLONG)FileID->Cell << 32) | FileID->Volume);

    return ullIndex;
}

inline
ULONGLONG
AFSCreateLowIndex( IN AFSFileID *FileID)
{

    ULONGLONG ullIndex = 0;

    ullIndex = (((ULONGLONG)FileID->Vnode << 32) | FileID->Unique);

    return ullIndex;
}

BOOLEAN
AFSCheckAccess( IN ACCESS_MASK DesiredAccess,
                IN ACCESS_MASK GrantedAccess)
{

    BOOLEAN bAccessGranted = TRUE;

    //
    // Check if we are asking for read/write and granted only read only
    // NOTE: There will be more checks here
    //

    if( !AFSCheckForReadOnlyAccess( DesiredAccess) &&
        AFSCheckForReadOnlyAccess( GrantedAccess))
    {

        bAccessGranted = FALSE;
    }

    return bAccessGranted;
}

NTSTATUS
AFSGetDriverStatus( IN AFSDriverStatusRespCB *DriverStatus)
{

    NTSTATUS         ntStatus = STATUS_SUCCESS;
    AFSDeviceExt    *pControlDevExt = (AFSDeviceExt *)AFSDeviceObject->DeviceExtension;

    //
    // Start with read
    //

    DriverStatus->Status = AFS_DRIVER_STATUS_READY;

    if( AFSGlobalRoot == NULL ||
        !BooleanFlagOn( AFSGlobalRoot->Flags, AFS_FCB_DIRECTORY_ENUMERATED))
    {

        //
        // We are not ready
        //

        DriverStatus->Status = AFS_DRIVER_STATUS_NOT_READY;
    }

    if( pControlDevExt->Specific.Control.CommServiceCB.IrpPoolControlFlag != POOL_ACTIVE)
    {

        //
        // No service yet
        //

        DriverStatus->Status = AFS_DRIVER_STATUS_NO_SERVICE;
    }

    return ntStatus;
}

NTSTATUS
AFSSetSysNameInformation( IN AFSSysNameNotificationCB *SysNameInfo,
                          IN ULONG SysNameInfoBufferLength)
{

    NTSTATUS         ntStatus = STATUS_SUCCESS;
    AFSDeviceExt    *pControlDevExt = (AFSDeviceExt *)AFSDeviceObject->DeviceExtension;
    AFSSysNameCB    *pSysName = NULL;
    ERESOURCE       *pSysNameLock = NULL;
    AFSSysNameCB   **pSysNameListHead = NULL, **pSysNameListTail = NULL;
    ULONG            ulIndex = 0;
    __Enter
    {

        //
        // Depending on the architecture of the information, set up the lsit
        //

        if( SysNameInfo->Architecture == AFS_SYSNAME_ARCH_32BIT)
        {

            pSysNameLock = &pControlDevExt->Specific.Control.SysName32ListLock;

            pSysNameListHead = &pControlDevExt->Specific.Control.SysName32ListHead;

            pSysNameListTail = &pControlDevExt->Specific.Control.SysName32ListTail;
        }
        else
        {

#if defined(_WIN64)

            pSysNameLock = &pControlDevExt->Specific.Control.SysName64ListLock;

            pSysNameListHead = &pControlDevExt->Specific.Control.SysName64ListHead;

            pSysNameListTail = &pControlDevExt->Specific.Control.SysName64ListTail;

#else

            try_return( ntStatus = STATUS_INVALID_PARAMETER);
#endif
        }

        //
        // Process the request
        //

        AFSAcquireExcl( pSysNameLock,
                        TRUE);

        //
        // If we already have a list, then tear it down
        //

        if( *pSysNameListHead != NULL)
        {

            AFSResetSysNameList( *pSysNameListHead);

            *pSysNameListHead = NULL;
        }

        //
        // Loop through the entries adding in a node for each
        //

        while( ulIndex < SysNameInfo->NumberOfNames)
        {

            pSysName = (AFSSysNameCB *)ExAllocatePoolWithTag( PagedPool,
                                                              sizeof( AFSSysNameCB) + 
                                                                        SysNameInfo->SysNames[ ulIndex].Length +
                                                                        sizeof( WCHAR),
                                                              AFS_SYS_NAME_NODE_TAG);

            if( pSysName == NULL)
            {

                //
                // Reset the current list
                //

                AFSResetSysNameList( *pSysNameListHead);

                *pSysNameListHead = NULL;

                try_return( ntStatus = STATUS_INSUFFICIENT_RESOURCES);
            }

            RtlZeroMemory( pSysName,
                           sizeof( AFSSysNameCB) + 
                                   SysNameInfo->SysNames[ ulIndex].Length +
                                   sizeof( WCHAR));

            pSysName->SysName.Length = (USHORT)SysNameInfo->SysNames[ ulIndex].Length;

            pSysName->SysName.MaximumLength = pSysName->SysName.Length + sizeof( WCHAR);

            pSysName->SysName.Buffer = (WCHAR *)((char *)pSysName + sizeof( AFSSysNameCB));

            RtlCopyMemory( pSysName->SysName.Buffer,
                           SysNameInfo->SysNames[ ulIndex].String,
                           pSysName->SysName.Length);

            if( *pSysNameListHead == NULL)
            {

                *pSysNameListHead = pSysName;
            }
            else
            {

                (*pSysNameListTail)->fLink = pSysName;
            }

            *pSysNameListTail = pSysName;                               

            ulIndex++;
        }

try_exit:


        AFSReleaseResource( pSysNameLock);
    }

    return ntStatus;
}

void
AFSResetSysNameList( IN AFSSysNameCB *SysNameList)
{

    AFSSysNameCB *pNextEntry = NULL, *pCurrentEntry = SysNameList;

    while( pCurrentEntry != NULL)
    {

        pNextEntry = pCurrentEntry->fLink;

        ExFreePool( pCurrentEntry);

        pCurrentEntry = pNextEntry;
    }

    return;
}

NTSTATUS
AFSSubstituteSysName( IN UNICODE_STRING *ComponentName,
                      IN UNICODE_STRING *SubstituteName,
                      IN ULONG StringIndex)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    AFSDeviceExt    *pControlDevExt = (AFSDeviceExt *)AFSDeviceObject->DeviceExtension;
    AFSSysNameCB    *pSysName = NULL;
    ERESOURCE       *pSysNameLock = NULL;
    ULONG            ulIndex = 1;
    USHORT           usIndex = 0;
    UNICODE_STRING   uniSysName;

    __Enter
    {

#if defined(_WIN64)

        if( IoIs32bitProcess( NULL))
        {

            pSysNameLock = &pControlDevExt->Specific.Control.SysName32ListLock;

            pSysName = &pControlDevExt->Specific.Control.SysName32ListHead;
        }
        else
        {

            pSysNameLock = &pControlDevExt->Specific.Control.SysName64ListLock;

            pSysName = &pControlDevExt->Specific.Control.SysName64ListHead;
        }
#else

        pSysNameLock = &pControlDevExt->Specific.Control.SysName32ListLock;

        pSysName = pControlDevExt->Specific.Control.SysName32ListHead;

#endif

        AFSAcquireShared( pSysNameLock,
                          TRUE);

        //
        // Find where we are in the list
        //

        while( pSysName != NULL &&
            ulIndex < StringIndex)
        {

            pSysName = pSysName->fLink;

            ulIndex++;
        }

        if( pSysName == NULL)
        {

            try_return( ntStatus = STATUS_OBJECT_NAME_NOT_FOUND);
        }

        RtlInitUnicodeString( &uniSysName,
                              L"@SYS");
        //
        // If it is a full component of @SYS then just substitue the
        // name in
        //

        if( RtlCompareUnicodeString( &uniSysName,
                                     ComponentName,
                                     TRUE) == 0)
        {

            SubstituteName->Length = pSysName->SysName.Length;
            SubstituteName->MaximumLength = SubstituteName->Length;

            SubstituteName->Buffer = (WCHAR *)ExAllocatePoolWithTag( PagedPool,
                                                                     SubstituteName->Length,
                                                                     AFS_NAME_BUFFER_TAG);

            if( SubstituteName->Buffer == NULL)
            {

                try_return( ntStatus = STATUS_INSUFFICIENT_RESOURCES);
            }

            RtlCopyMemory( SubstituteName->Buffer,
                           pSysName->SysName.Buffer,
                           pSysName->SysName.Length);
        }
        else
        {

            usIndex = 0;

            while( ComponentName->Buffer[ usIndex] != L'@')
            {

                usIndex++;
            }

            SubstituteName->Length = (usIndex * sizeof( WCHAR)) + pSysName->SysName.Length;
            SubstituteName->MaximumLength = SubstituteName->Length;

            SubstituteName->Buffer = (WCHAR *)ExAllocatePoolWithTag( PagedPool,
                                                                     SubstituteName->Length,
                                                                     AFS_NAME_BUFFER_TAG);

            if( SubstituteName->Buffer == NULL)
            {

                try_return( ntStatus = STATUS_INSUFFICIENT_RESOURCES);
            }

            RtlCopyMemory( SubstituteName->Buffer,
                           ComponentName->Buffer,
                           usIndex * sizeof( WCHAR));

            RtlCopyMemory( &SubstituteName->Buffer[ usIndex],
                           pSysName->SysName.Buffer,
                           pSysName->SysName.Length);
        }

try_exit:

        AFSReleaseResource( pSysNameLock);
    }

    return ntStatus;
}

NTSTATUS
AFSSubstituteNameInPath( IN UNICODE_STRING *FullPathName,
                         IN UNICODE_STRING *ComponentName,
                         IN UNICODE_STRING *SubstituteName,
                         IN BOOLEAN FreePathName)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    UNICODE_STRING uniPathName;
    USHORT usPrefixNameLen = 0;

    __Enter
    {

        //
        // If the passed in name can handle the additional length
        // then just moves things around
        //

        if( FullPathName->MaximumLength > FullPathName->Length - 
                                                    ComponentName->Length + 
                                                    SubstituteName->Length)
        {

            usPrefixNameLen = (USHORT)(ComponentName->Buffer - FullPathName->Buffer);

            if( FullPathName->Length > usPrefixNameLen + ComponentName->Length)
            {

                RtlMoveMemory( &FullPathName->Buffer[ ((usPrefixNameLen + SubstituteName->Length)/sizeof( WCHAR)) + 1],
                               &FullPathName->Buffer[ ((usPrefixNameLen + ComponentName->Length)/sizeof( WCHAR)) + 1],
                               FullPathName->Length - usPrefixNameLen - ComponentName->Length);
            }

            RtlCopyMemory( &FullPathName->Buffer[ (usPrefixNameLen/sizeof( WCHAR)) + 1],
                           SubstituteName->Buffer,
                           SubstituteName->Length);

            FullPathName->Length = FullPathName->Length - 
                                          ComponentName->Length + 
                                          SubstituteName->Length;

            try_return( ntStatus);
        }
        
        //
        // Need to re-allocate the buffer
        //

        uniPathName.Length = FullPathName->Length - 
                                         ComponentName->Length + 
                                         SubstituteName->Length;

        uniPathName.MaximumLength = FullPathName->MaximumLength + PAGE_SIZE;

        uniPathName.Buffer = (WCHAR *)ExAllocatePoolWithTag( PagedPool,
                                                             uniPathName.MaximumLength,
                                                             AFS_NAME_BUFFER_TAG);

        if( uniPathName.Buffer == NULL)
        {

            try_return( ntStatus = STATUS_INSUFFICIENT_RESOURCES);
        }

        usPrefixNameLen = (USHORT)(ComponentName->Buffer - FullPathName->Buffer);

        usPrefixNameLen *= sizeof( WCHAR);

        RtlZeroMemory( uniPathName.Buffer,
                       uniPathName.MaximumLength);

        RtlCopyMemory( uniPathName.Buffer,
                       FullPathName->Buffer,
                       usPrefixNameLen);

        RtlCopyMemory( &uniPathName.Buffer[ (usPrefixNameLen/sizeof( WCHAR))],
                       SubstituteName->Buffer,
                       SubstituteName->Length);

        if( FullPathName->Length > usPrefixNameLen + ComponentName->Length)
        {

            RtlCopyMemory( &uniPathName.Buffer[ (usPrefixNameLen + SubstituteName->Length)/sizeof( WCHAR)],
                           &FullPathName->Buffer[ (usPrefixNameLen + ComponentName->Length)/sizeof( WCHAR)],
                           FullPathName->Length - usPrefixNameLen - ComponentName->Length);
        }

        if( FreePathName)
        {

            ExFreePool( FullPathName->Buffer);
        }

        *FullPathName = uniPathName;

try_exit:

        NOTHING;
    }

    return ntStatus;
}

void
AFSInitServerStrings()
{

    //
    // Initialize the server name
    //

    AFSReadServerName();

    //
    // The PIOCtl file name
    //

    RtlInitUnicodeString( &AFSPIOCtlName,
                          AFS_PIOCTL_FILE_INTERFACE_NAME);

    //
    // And the global root share name
    //

    RtlInitUnicodeString( &AFSGlobalRootName,
                          AFS_GLOBAL_ROOT_SHARE_NAME);

    return;
}

NTSTATUS
AFSReadServerName()
{

    NTSTATUS ntStatus        = STATUS_SUCCESS;
    ULONG Default            = 0;
    UNICODE_STRING paramPath;
    RTL_QUERY_REGISTRY_TABLE paramTable[2];

    __Enter
    {

        //
        // Setup the paramPath buffer.
        //

        paramPath.MaximumLength = PAGE_SIZE; 
        paramPath.Buffer = (PWSTR)ExAllocatePoolWithTag( PagedPool,
                                                         paramPath.MaximumLength,
                                                         AFS_GENERIC_MEMORY_TAG);
     
        //
        // If it exists, setup the path.
        //

        if( paramPath.Buffer == NULL) 
        { 

            try_return( ntStatus = STATUS_INSUFFICIENT_RESOURCES);
        }
        
        //
        // Move in the paths
        //

        RtlCopyMemory( &paramPath.Buffer[ 0], 
                       L"\\TransarcAFSDaemon\\Parameters", 
                       58);
        
        paramPath.Length = 58; 

        RtlZeroMemory( paramTable, 
                       sizeof( paramTable));

        //
        // Setup the table to query the registry for the needed value
        //

        AFSServerName.Length = 0;
        AFSServerName.MaximumLength = 0;
        AFSServerName.Buffer = NULL;

        paramTable[0].Flags = RTL_QUERY_REGISTRY_DIRECT; 
        paramTable[0].Name = AFS_NETBIOS_NAME; 
        paramTable[0].EntryContext = &AFSServerName;
        
        paramTable[0].DefaultType = REG_NONE; 
        paramTable[0].DefaultData = NULL; 
        paramTable[0].DefaultLength = 0;

        //
        // Query the registry
        //

        ntStatus = RtlQueryRegistryValues( RTL_REGISTRY_SERVICES, 
                                           paramPath.Buffer, 
                                           paramTable, 
                                           NULL, 
                                           NULL);

        //
        // Free up the buffer
        //

        ExFreePool( paramPath.Buffer);

try_exit:

        if( !NT_SUCCESS( ntStatus))
        {

            RtlInitUnicodeString( &AFSServerName,
                                  L"AFS");
        }
    } 

    return ntStatus;
}
