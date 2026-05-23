void GCInitializeLCDDriver(GC *pGC);
void GCShutdownOLEDDriver(GC *pGC);
GCBOOL GCCreateLCDDeviceBitmap(
    GC *pGC,
    unsigned long width,
    unsigned long height,
    unsigned long address);
void GPUSuspendEndAccessBatchFlushes(BOOL bDisable);
void GCHWAccelerateContext(GC *pGC);
