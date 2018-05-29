/*
 * PROJECT:     ReactOS Intel PRO/1000 Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Interrupt handlers
 * COPYRIGHT:   Copyright 2013 Cameron Gutman (cameron.gutman@reactos.org)
 *              Copyright 2018 Mark Jansen (mark.jansen@reactos.org)
 */

#include "nic.h"

#include <debug.h>

VOID
NTAPI
MiniportISR(
    OUT PBOOLEAN InterruptRecognized,
    OUT PBOOLEAN QueueMiniportHandleInterrupt,
    IN NDIS_HANDLE MiniportAdapterContext)
{
    ULONG Value;
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)MiniportAdapterContext;

    Value = NICInterruptRecognized(Adapter, InterruptRecognized);
    InterlockedOr(&Adapter->InterruptPending, Value);

    if (!(*InterruptRecognized))
    {
        /* This is not ours. */
        *QueueMiniportHandleInterrupt = FALSE;
        return;
    }

    /* Mark the events pending service */
    *QueueMiniportHandleInterrupt = TRUE;
}

VOID
NTAPI
MiniportHandleInterrupt(
    IN NDIS_HANDLE MiniportAdapterContext)
{
    ULONG Value;
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)MiniportAdapterContext;
    volatile PE1000_TRANSMIT_DESCRIPTOR TransmitDescriptor;

    NDIS_DbgPrint(MAX_TRACE, ("Called.\n"));

    Value = InterlockedExchange(&Adapter->InterruptPending, 0);

    NdisDprAcquireSpinLock(&Adapter->Lock);

    if (Value & E1000_IMS_LSC)
    {
        ULONG Status;
        NdisDprReleaseSpinLock(&Adapter->Lock);
        Value &= ~E1000_IMS_LSC;
        NDIS_DbgPrint(MIN_TRACE, ("Link status changed!.\n"));

        NICUpdateLinkStatus(Adapter);

        Status = Adapter->MediaState == NdisMediaStateConnected ? NDIS_STATUS_MEDIA_CONNECT : NDIS_STATUS_MEDIA_DISCONNECT;
        NdisMIndicateStatus(Adapter->AdapterHandle, Status, NULL, 0);
        NdisMIndicateStatusComplete(Adapter->AdapterHandle);

        NdisDprAcquireSpinLock(&Adapter->Lock);
    }

    if (Value & E1000_IMS_TXDW)
    {
        while (Adapter->TxFull || Adapter->LastTxDesc != Adapter->CurrentTxDesc)
        {
            TransmitDescriptor = Adapter->TransmitDescriptors + Adapter->LastTxDesc;

            if (!(TransmitDescriptor->Status & E1000_TDESC_STATUS_DD))
            {
                /* Not processed yet */
                break;
            }

            Adapter->LastTxDesc = (Adapter->LastTxDesc + 1) % NUM_TRANSMIT_DESCRIPTORS;
            Value &= ~E1000_IMS_TXDW;
            Adapter->TxFull = FALSE;
            NDIS_DbgPrint(MAX_TRACE, ("CurrentTxDesc:%u, LastTxDesc:%u\n", Adapter->CurrentTxDesc, Adapter->LastTxDesc));
        }
    }


    ASSERT(Value == 0);
}