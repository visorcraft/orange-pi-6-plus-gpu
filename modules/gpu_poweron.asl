DefinitionBlock ("gpu_poweron.aml", "SSDT", 2, "CIXHAK", "GPUPON", 0x00000001)
{
    Scope (\_SB)
    {
        Method (GPON, 0, Serialized)
        {
            OperationRegion (PDRG, SystemMemory, 0x15000000, 0x20)
            Field (PDRG, DWordAcc, NoLock, Preserve)
            {
                Offset (0x10),
                PASS, 32,
                ENBL, 32,
                BUSY, 32
            }

            Local0 = 10000000
            Local1 = BUSY
            Local1 = ((Local1 >> 16) & 0xFFFF)
            While (((Local1 != Zero) && (Local0 != Zero)))
            {
                Local0--
                Local1 = BUSY
                Local1 = ((Local1 >> 16) & 0xFFFF)
            }

            If ((Local0 == Zero))
            {
                Return (0xDEAD0001)
            }

            ENBL = One

            Local0 = 10000000
            Local1 = PASS
            Local1 = ((Local1 >> One) & 0x03)
            While (((Local1 != 0x03) && (Local0 != Zero)))
            {
                Local0--
                Local1 = PASS
                Local1 = ((Local1 >> One) & 0x03)
            }

            If ((Local0 == Zero))
            {
                Return (0xDEAD0002)
            }

            ENBL = Zero
            Return (Zero)
        }
    }
}
