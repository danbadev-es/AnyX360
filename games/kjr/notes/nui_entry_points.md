# Entry points of 0x82CE47E8-0x82CFA000 called from outside it

37 entry points; 2 more functions of the range are only referenced from data.

## 0x82CE47E8 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x827D9800 in 0x827D97E0: strings -
  - args: -
  - result: cmplwi r3,0, cmplwi r3,5

## 0x82CE9220 (size 0x98)
- reaches: EtxProducerUnregister, KeSetBasePriorityThread, KeSetEvent, KeWaitForSingleObject, NtClose, NtSetEvent, ObDereferenceObject, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlLeaveCriticalSection, XamXStudioRequest
- call 0x827E149C in 0x827E13F8: strings -
  - args: -
  - result: unused

## 0x82CEB078 (size 0x158)
- reaches: EtxProducerRegister, EtxProducerUnregister, ExCreateThread, KeResumeThread, KeSetAffinityThread, KeSetBasePriorityThread, KeSetEvent, KeWaitForSingleObject, KfAcquireSpinLock, KfReleaseSpinLock, NtClose, NtCreateEvent, NtSetEvent, ObCreateObject, ObDereferenceObject, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlInitializeCriticalSection, RtlLeaveCriticalSection, RtlNtStatusToDosError, XGetVideoMode, XamContentClose, XamContentOpenFile, XamGetCurrentTitleId, XamNuiCameraTiltSetCallback, XamNuiIdentityGetSessionId, XamUserNuiGetUserIndex, XamXStudioRequest
- call 0x827E15B4 in 0x827E1508: strings -
  - args: r4=field 108(r11)
  - result: cmpwi r3,0

## 0x82CEB1D0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x827E40F0 in 0x827E40D0: strings -
  - args: r3=copy of r4
  - result: unused
- call 0x827E4564 in 0x827E4488: strings -
  - args: r3=copy of r31
  - result: unused
- call 0x827E5734 in 0x827E5518: strings -
  - args: -
  - result: unused

## 0x82CEB1D8 (size 0x17C)
- reaches: RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x827E5298 in 0x827E51E8: strings -
  - args: r3=copy of r24, r4=copy of r26, r5=copy of r29, r6=static 0x827E4AE8, r7=copy of r28
  - result: unused

## 0x82CEB358 (size 0x104)
- reaches: RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x827E515C in 0x827E5100: strings -
  - args: r3=copy of r28, r4=constant 0 (0x0), r5=static 0x827E4908, r6=copy of r31
  - result: unused

## 0x82CEB460 (size 0xDC)
- reaches: RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x827E3B74 in 0x827E3B40: strings -
  - args: r3=copy of r31, r4=stack buffer r1+80
  - result: unused
- call 0x827E3F94 in 0x827E3F60: strings -
  - args: r3=copy of r31, r4=stack buffer r1+80
  - result: unused
- call 0x827E459C in 0x827E4488: strings -
  - args: r3=copy of r24, r4=stack buffer r1+80
  - result: unused
- call 0x827E526C in 0x827E51E8: strings -
  - args: r4=stack buffer r1+80
  - result: unused
- call 0x827E564C in 0x827E5518: strings -
  - args: r3=copy of r30, r4=stack buffer r1+88
  - result: unused

## 0x82CEB670 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x827EBD6C in 0x827EBD58: strings -
  - args: -
  - result: unused

## 0x82CEB790 (size 0xEC)
- reaches: RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x827EC65C in 0x827EC630: strings -
  - args: r3=stack buffer r1+80, r9=field 4(r4), r10=stack buffer r1+80
  - result: unused

## 0x82CEBA28 (size 0x118)
- reaches: pure (no imports reached)
- call 0x827E83D4 in 0x827E8378: strings -
  - args: r3=field 24(r31), r4=stack buffer r1+80
  - result: unused

## 0x82CEBC58 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82D17B14 in 0x82D17B00: strings -
  - args: r3=constant 0 (0x0)
  - result: unused
- call 0x82D17C94 in 0x82D17C80: strings -
  - args: r3=constant 0 (0x0)
  - result: unused
- call 0x82D17CC8 in 0x82D17CA8: strings -
  - args: r3=constant 0 (0x0)
  - result: unused
- call 0x82D17D28 in 0x82D17CA8: strings -
  - args: r3=static 0x82D17BE0
  - result: r30 = r3
- call 0x82D17DB8 in 0x82D17D68: strings -
  - args: r3=constant 0 (0x0)
  - result: stores r3 to 40(r31)

## 0x82CEC038 (size 0x1F0)
- reaches: KeResetEvent, KeWaitForSingleObject, RtlEnterCriticalSection, RtlLeaveCriticalSection, XamXStudioRequest
- call 0x827E8E34 in 0x827E8DC8: strings -
  - args: r3=field 76(r31), r4=field 24(r31), r8=constant 255 (0xFF)
  - result: r30 = r3, cmpwi r3,0

## 0x82CECF10 (size 0x190)
- reaches: RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x827EC850 in 0x827EC7A0: strings -
  - args: r3=field 4(r6), r4=constant 0 (0x0), r5=field 108(r1), r6=field 116(r1), r8=stack buffer r1+88, r9=stack buffer r1+84
  - result: cmpwi r3,0
- call 0x827ECA50 in 0x827EC9E0: strings -
  - args: r3=field 4(r10), r4=constant 0 (0x0), r5=field 92(r1), r6=field 100(r1), r7=copy of r28, r8=stack buffer r1+84, r9=stack buffer r1+80
  - result: cmpwi r3,0

## 0x82CED0A0 (size 0x310)
- reaches: KeResetEvent, KeWaitForSingleObject, ObDereferenceObject, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlLeaveCriticalSection, RtlNtStatusToDosError
- call 0x827EB934 in 0x827EB908: strings -
  - args: r3=field 36(r3), r4=field 28(r3), r5=r3+16
  - result: cmpwi r3,0

## 0x82CED3B0 (size 0x14C)
- reaches: ObDereferenceObject, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlLeaveCriticalSection, RtlNtStatusToDosError
- call 0x827EBA30 in 0x827EBA08: strings -
  - args: r3=field 36(r3), r4=field 16(r3)
  - result: unused

## 0x82CEEB98 (size 0x8C4)
- reaches: KeSetEvent, KfAcquireSpinLock, KfReleaseSpinLock, ObCreateObject, ObDereferenceObject, ObOpenObjectByPointer, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlLeaveCriticalSection, RtlNtStatusToDosError
- call 0x827EB8C0 in 0x827EB878: strings -
  - args: r3=field 12(r31), r5=constant 0x00010000, r6=field 24(r31), r7=constant 0 (0x0), r8=stack buffer r31+36
  - result: unused

## 0x82CEF530 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82CFA0E0 in 0x82CFA070: strings -
  - args: r3=constant 0 (0x0), r4=constant 1 (0x1)
  - result: unused
- call 0x82CFA214 in 0x82CFA070: strings -
  - args: r3=constant 0 (0x0), r4=constant 1 (0x1)
  - result: unused
- call 0x82CFA6B0 in 0x82CFA698: strings -
  - args: r3=constant 0 (0x0), r4=constant 1 (0x1)
  - result: unused
- call 0x82CFA8F4 in 0x82CFA8D0: strings -
  - args: r3=constant 0 (0x0), r4=constant 1 (0x1)
  - result: unused

## 0x82CEF788 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x827ECEB4 in 0x827ECE70: strings -
  - args: -
  - result: unused

## 0x82CEF838 (size 0x1E4)
- reaches: KeWaitForSingleObject, RtlNtStatusToDosError
- call 0x82CFACFC in 0x82CFACA0: strings -
  - args: r3=constant 0 (0x0), r4=constant 1 (0x1), r5=constant 3 (0x3), r6=stack buffer r1+84
  - result: cmpwi r3,0

## 0x82CEFA20 (size 0x220)
- reaches: KeWaitForSingleObject, RtlNtStatusToDosError
- call 0x82CFA228 in 0x82CFA070: strings -
  - args: r3=constant 0 (0x0), r4=constant 1 (0x1), r5=constant 0 (0x0), r6=stack buffer r1+80
  - result: r31 = r3
- call 0x82CFA294 in 0x82CFA070: strings -
  - args: r3=constant 0 (0x0), r4=constant 1 (0x1), r5=constant 7 (0x7), r6=stack buffer r1+84
  - result: cmpwi r3,0
- call 0x82CFA2E0 in 0x82CFA070: strings -
  - args: r3=constant 0 (0x0), r4=constant 1 (0x1), r5=constant 6 (0x6), r6=stack buffer r1+88, r10=constant 0 (0x0)
  - result: r31 = r3
- call 0x82CFA2FC in 0x82CFA070: strings -
  - args: r3=constant 0 (0x0), r4=constant 1 (0x1), r5=constant 8 (0x8), r6=stack buffer r1+92
  - result: r31 = r3
- call 0x82CFA6C4 in 0x82CFA698: strings -
  - args: r3=constant 0 (0x0), r4=constant 1 (0x1), r5=constant 6 (0x6), r6=stack buffer r1+84
  - result: r31 = r3
- call 0x82CFA6E0 in 0x82CFA698: strings -
  - args: r3=constant 0 (0x0), r4=constant 1 (0x1), r5=constant 7 (0x7), r6=stack buffer r1+80
  - result: r31 = r3
- call 0x82CFA6FC in 0x82CFA698: strings -
  - args: r3=constant 0 (0x0), r4=constant 1 (0x1), r5=constant 8 (0x8), r6=stack buffer r1+88
  - result: r31 = r3
- call 0x82CFA908 in 0x82CFA8D0: strings -
  - args: r3=constant 0 (0x0), r4=constant 1 (0x1), r5=constant 0 (0x0), r6=copy of r31
  - result: unused
- call 0x82CFA91C in 0x82CFA8D0: strings -
  - args: r3=constant 0 (0x0), r4=constant 1 (0x1), r5=constant 6 (0x6), r6=copy of r30
  - result: unused
- call 0x82CFA930 in 0x82CFA8D0: strings -
  - args: r3=constant 0 (0x0), r4=constant 1 (0x1), r5=constant 7 (0x7), r6=copy of r29
  - result: unused
- call 0x82CFA944 in 0x82CFA8D0: strings -
  - args: r3=constant 0 (0x0), r4=constant 1 (0x1), r5=constant 8 (0x8), r6=copy of r28
  - result: unused

## 0x82CF00B0 (size 0x1DC)
- reaches: KeWaitForSingleObject, RtlNtStatusToDosError
- call 0x827ECC74 in 0x827ECC38: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=stack buffer r1+96
  - result: unused
- call 0x827ECC84 in 0x827ECC38: strings -
  - args: r3=constant 0 (0x0), r4=constant 5 (0x5), r5=stack buffer r1+92
  - result: unused
- call 0x827ECC94 in 0x827ECC38: strings -
  - args: r3=constant 0 (0x0), r4=constant 6 (0x6), r5=stack buffer r1+88
  - result: unused
- call 0x827ECCA4 in 0x827ECC38: strings -
  - args: r3=constant 0 (0x0), r4=constant 8 (0x8), r5=stack buffer r1+84
  - result: unused
- call 0x827ECCB4 in 0x827ECC38: strings -
  - args: r3=constant 0 (0x0), r4=constant 7 (0x7), r5=stack buffer r1+80
  - result: unused

## 0x82CF0290 (size 0xD0)
- reaches: KfAcquireSpinLock, KfReleaseSpinLock, RtlNtStatusToDosError
- call 0x82CFA200 in 0x82CFA070: strings -
  - args: r3=constant 0 (0x0), r4=constant 1 (0x1)
  - result: unused
- call 0x82CFA338 in 0x82CFA070: strings -
  - args: r3=constant 0 (0x0), r4=constant 1 (0x1)
  - result: unused
- call 0x82CFA7C8 in 0x82CFA698: strings -
  - args: r3=constant 0 (0x0), r4=constant 1 (0x1)
  - result: unused
- call 0x82CFA950 in 0x82CFA8D0: strings -
  - args: r3=constant 0 (0x0), r4=constant 1 (0x1)
  - result: unused

## 0x82CF03F0 (size 0x90)
- reaches: KfAcquireSpinLock, KfReleaseSpinLock, RtlNtStatusToDosError
- call 0x82CFACD8 in 0x82CFACA0: strings -
  - args: r3=constant 0 (0x0), r4=constant 1 (0x1)
  - result: cmpwi r3,0

## 0x82CF0480 (size 0xA8)
- reaches: KfAcquireSpinLock, KfReleaseSpinLock, RtlNtStatusToDosError
- call 0x827ECFA0 in 0x827ECE70: strings -
  - args: -
  - result: unused

## 0x82CF0628 (size 0x2C0)
- reaches: KfAcquireSpinLock, KfReleaseSpinLock, RtlNtStatusToDosError
- call 0x82CFACC4 in 0x82CFACA0: strings -
  - args: r3=constant 0 (0x0), r4=constant 1 (0x1), r5=constant 0 (0x0), r6=constant 1 (0x1)
  - result: cmpwi r3,0
- call 0x82CFAD4C in 0x82CFACA0: strings -
  - args: r3=constant 0 (0x0), r4=constant 1 (0x1), r5=constant 0 (0x0), r6=constant 0 (0x0), r10=constant 0x83790000
  - result: cmpwi r3,0

## 0x82CF08E8 (size 0x1D8)
- reaches: KfAcquireSpinLock, KfReleaseSpinLock, RtlNtStatusToDosError
- call 0x82CFA0FC in 0x82CFA070: strings -
  - args: r3=constant 0 (0x0), r4=constant 1 (0x1), r5=constant 0 (0x0)
  - result: r31 = r3
- call 0x82CFA1B8 in 0x82CFA070: strings -
  - args: r3=constant 0 (0x0), r4=constant 1 (0x1), r5=constant 7 (0x7); f1=copy of f0
  - result: r31 = r3
- call 0x82CFA1D4 in 0x82CFA070: strings -
  - args: r3=constant 0 (0x0), r4=constant 1 (0x1), r5=constant 6 (0x6); f1=copy of f30
  - result: r31 = r3
- call 0x82CFA1F0 in 0x82CFA070: strings -
  - args: r3=constant 0 (0x0), r4=constant 1 (0x1), r5=constant 8 (0x8); f1=copy of f31
  - result: r31 = r3

## 0x82CF0AC0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x827ECC20 in 0x827ECBF8: strings -
  - args: r3=stack buffer r1+80, r9=field 8(r4), r10=field 4(r4)
  - result: unused

## 0x82CF0AD0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x827ECED0 in 0x827ECE70: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=field 20(r31)
  - result: unused
- call 0x827ECEEC in 0x827ECE70: strings -
  - args: r3=constant 0 (0x0), r4=constant 1 (0x1), r5=field 24(r31)
  - result: unused
- call 0x827ECF08 in 0x827ECE70: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2), r5=field 28(r31)
  - result: unused

## 0x82CF0AE0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x827ECF24 in 0x827ECE70: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0); f1=loaded from 0(r31)
  - result: unused
- call 0x827ECF40 in 0x827ECE70: strings -
  - args: r3=constant 0 (0x0), r4=constant 5 (0x5); f1=loaded from 4(r31)
  - result: unused
- call 0x827ECF5C in 0x827ECE70: strings -
  - args: r3=constant 0 (0x0), r4=constant 6 (0x6); f1=loaded from 8(r31)
  - result: unused
- call 0x827ECF78 in 0x827ECE70: strings -
  - args: r3=constant 0 (0x0), r4=constant 8 (0x8); f1=loaded from 12(r31)
  - result: unused
- call 0x827ECF94 in 0x827ECE70: strings -
  - args: r3=constant 0 (0x0), r4=constant 7 (0x7); f1=loaded from 16(r31)
  - result: unused

## 0x82CF0B00 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82D00DC0 in 0x82D00CD0: strings -
  - args: -
  - result: r5 = r3
- call 0x82D054F4 in 0x82D05490: strings -
  - args: -
  - result: r5 = r3

## 0x82CF0D38 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82CFBA1C in 0x82CFB938: strings -
  - args: r3=constant 2 (0x2), r4=copy of r22, r5=field 80(r1), r6=field 84(r1), r8=copy of r30, r9=copy of r27
  - result: unused
- call 0x82CFBA44 in 0x82CFB938: strings -
  - args: r3=constant 2 (0x2), r4=copy of r22, r5=field 96(r1), r6=field 100(r1), r8=copy of r28, r9=copy of r29
  - result: unused
- call 0x82CFDC7C in 0x82CFDB88: strings -
  - args: r3=field 8(r11), r4=field 4(r31), r6=copy of r22, r8=stack buffer r1+88, r9=stack buffer r1+80
  - result: unused
- call 0x82CFDD3C in 0x82CFDB88: strings -
  - args: r3=field 8(r11), r4=field 4(r31), r6=copy of r22, r8=stack buffer r1+88, r9=stack buffer r1+80
  - result: unused
- call 0x82CFDE08 in 0x82CFDB88: strings -
  - args: r3=field 8(r11), r4=field 4(r31), r6=copy of r22, r8=stack buffer r1+88, r9=stack buffer r1+80
  - result: unused
- call 0x82CFDEC8 in 0x82CFDB88: strings -
  - args: r3=field 8(r11), r4=field 4(r31), r6=copy of r22, r8=stack buffer r1+88, r9=stack buffer r1+80
  - result: unused
- call 0x82CFE9AC in 0x82CFE8B8: strings -
  - args: r3=constant 2 (0x2), r4=copy of r30, r5=field 128(r1), r6=field 132(r1), r8=stack buffer r1+112, r9=stack buffer r1+116
  - result: unused
- call 0x82CFE9CC in 0x82CFE8B8: strings -
  - args: r3=constant 2 (0x2), r4=copy of r30, r5=field 136(r1), r6=field 140(r1), r8=stack buffer r1+120, r9=stack buffer r1+124
  - result: unused
- call 0x82CFEBC0 in 0x82CFEB28: strings -
  - args: r3=field 16(r29), r4=copy of r25, r5=field -4(r31), r6=field 0(r31), r8=copy of r30, r9=r30+4
  - result: unused

## 0x82CF1BE8 (size 0x50)
- reaches: pure (no imports reached)
- call 0x82CFE8F0 in 0x82CFE8B8: strings -
  - args: r3=stack buffer r1+80, r4=constant 0x00040000
  - result: unused
- call 0x82D0535C in 0x82D05328: strings -
  - args: r3=stack buffer r1+80, r4=constant 0x00010000
  - result: unused

## 0x82CF1C38 (size 0x90)
- reaches: pure (no imports reached)
- call 0x82CFEC14 in 0x82CFEB28: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x82CFEC44 in 0x82CFEB28: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x82CFEC8C in 0x82CFEB28: strings -
  - args: r3=stack buffer r1+80
  - result: unused

## 0x82CF1E60 (size 0xB4)
- reaches: pure (no imports reached)
- call 0x82CFC398 in 0x82CFC310: strings -
  - args: r3=copy of r28, r10=constant 500 (0x1F4)
  - result: unused

## 0x82CF1FB8 (size 0x5C)
- reaches: pure (no imports reached)
- call 0x82CFC03C in 0x82CFBFC0: strings -
  - args: r3=copy of r30, r4=copy of r31, r6=stack buffer r1+96
  - result: unused

## 0x82CF9F00 (size 0x5C)
- reaches: pure (no imports reached)
- call 0x82CFA014 in 0x82CF9F60: strings -
  - args: r3=r30+8, r4=constant 0 (0x0), r5=stack buffer r1+80
  - result: low byte (BOOL)

## 0x82CF9F60 (size 0x10C)
- reaches: pure (no imports reached)
- call 0x82CFAA18 in 0x82CFA998: strings -
  - args: r3=copy of r30, r4=stack buffer r1+96
  - result: r31 = r3
- call 0x82CFAA44 in 0x82CFA998: strings -
  - args: r3=copy of r30, r4=stack buffer r1+96
  - result: r31 = r3

## Referenced only from data
0x82CECED0, 0x82CEDC18
