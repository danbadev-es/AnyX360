# Entry points of 0x82803700-0x82870000 called from outside it

48 entry points; 104 more functions of the range are only referenced from data.

## 0x82806018 (size 0xB0)
- reaches: KeSetEvent, ObDereferenceObject, RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x82310C94 in 0x82310C80: strings -
  - args: -
  - result: unused
- call 0x82310D04 in 0x82310C80: strings -
  - args: -
  - result: unused

## 0x82806190 (size 0x104)
- reaches: RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x823C35E0 in 0x823C35C8: strings -
  - args: r3=stack buffer r1+80
  - result: unused

## 0x828067F0 (size 0x1A0)
- reaches: KeReleaseMutant, KeResetEvent, KeSetEvent, KeWaitForSingleObject, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x82310CA0 in 0x82310C80: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0)
  - result: unused
- call 0x82310D10 in 0x82310C80: strings -
  - args: r4=constant 2 (0x2)
  - result: unused
- call 0x823C5668 in 0x823C53A0: strings -
  - args: r10=constant 2 (0x2)
  - result: unused

## 0x82806990 (size 0x27C)
- reaches: KeReleaseMutant, KeResetEvent, KeSetEvent, KeWaitForSingleObject, RtlEnterCriticalSection, RtlLeaveCriticalSection, XamIsUIActive, XamXStudioRequest
- call 0x823C27B4 in 0x823C2790: strings -
  - args: r3=constant 0 (0x0), r4=field 21412(r3)
  - result: cmpwi r3,0

## 0x82807768 (size 0xF4)
- reaches: pure (no imports reached)
- call 0x823C70B0 in 0x823C6FB8: strings -
  - args: -
  - result: unused

## 0x828078B0 (size 0x1BC)
- reaches: KeSetEvent, KeWaitForSingleObject
- call 0x823C7504 in 0x823C74A0: strings -
  - args: r3=field 2732(r30), r4=copy of r31, r6=static 0x823C6C28, r7=copy of r29
  - result: cmpwi r3,0

## 0x82807A70 (size 0x128)
- reaches: KeSetEvent, KeWaitForSingleObject
- call 0x823C6CD4 in 0x823C6C90: strings -
  - args: r3=field 2732(r31), r4=constant 0 (0x0), r5=static 0x823C6C28, r6=copy of r30
  - result: cmpwi r3,0

## 0x82807B98 (size 0x100)
- reaches: KeWaitForSingleObject
- call 0x827571EC in 0x827571C8: strings -
  - args: r3=copy of r29, r4=stack buffer r1+80
  - result: unused

## 0x82807DB0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x823BFCB4 in 0x823BFC80: strings -
  - args: -
  - result: unused

## 0x828080F8 (size 0x5C)
- reaches: XamNuiCameraElevationGetAngle
- call 0x823C8C54 in 0x823C8C38: strings -
  - args: r3=stack buffer r1+80, r4=r3+384
  - result: cmpwi r3,0
- call 0x823C8E04 in 0x823C8DE8: strings -
  - args: r3=stack buffer r1+80, r4=r3+384
  - result: cmpwi r3,0

## 0x828081E0 (size 0xA4)
- reaches: XamNuiCameraElevationSetAngle, XexCheckExecutablePrivilege
- call 0x823C8818 in 0x823C8800: strings -
  - args: r3=constant 27 (0x1B)
  - result: cmpwi r3,0
- call 0x823C8978 in 0x823C8960: strings -
  - args: r3=constant -27 (0xFFE5)
  - result: cmpwi r3,0

## 0x82808B40 (size 0x1E4)
- reaches: ExGetXConfigSetting, KfAcquireSpinLock, KfReleaseSpinLock, NtWaitForSingleObjectEx, XamNuiCameraElevationGetAngle, XamNuiCameraElevationSetAngle
- call 0x823C86C4 in 0x823C8688: strings -
  - args: r3=constant 32 (0x20), r7=r3+176, r8=r3+144, r10=constant 0x82000000; f1=copy of f31, f2=copy of f3, f3=loaded from 7376(r11)
  - result: cmplwi r3,0
- call 0x823C8AFC in 0x823C8AC0: strings -
  - args: r3=constant 0 (0x0), r7=r3+176, r8=r3+144, r10=constant 0x82020000; f1=copy of f31, f2=copy of f31, f3=loaded from 7376(r10)
  - result: cmplwi r3,0

## 0x8280A350 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x823C09A8 in 0x823C0010: strings "show_gesture_troubleshooter"
  - args: -
  - result: unused

## 0x8280AA68 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x823C3DC8 in 0x823C3D90: strings -
  - args: r3=constant 0 (0x0), r4=stack buffer r1+96
  - result: unused

## 0x8280B258 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x823C3568 in 0x823C3550: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=stack buffer r1+80
  - result: unused
- call 0x823C3DAC in 0x823C3D90: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2), r5=stack buffer r1+80
  - result: unused

## 0x8280BF40 (size 0x58)
- reaches: DbgPrint, EtxProducerLog, KeWaitForSingleObject, KfAcquireSpinLock, KfReleaseSpinLock, RtlEnterCriticalSection, RtlLeaveCriticalSection, RtlNtStatusToDosError, XamGetCurrentTitleId
- call 0x823C3530 in 0x823C3508: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0)
  - result: unused
- call 0x823C3D34 in 0x823C3D00: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2), r10=constant 2 (0x2)
  - result: unused
- call 0x823C57F4 in 0x823C53A0: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 1 (0x1)
  - result: unused

## 0x8280BF98 (size 0x64)
- reaches: KfAcquireSpinLock, KfReleaseSpinLock, RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x823C35B4 in 0x823C3590: strings -
  - args: r3=constant 0 (0x0), r4=stack buffer r1+80
  - result: unused
- call 0x823C3D70 in 0x823C3D00: strings -
  - args: r3=constant 0 (0x0), r4=stack buffer r1+80
  - result: unused

## 0x8280C6E8 (size 0x328)
- reaches: KeResetEvent, KeWaitForSingleObject, ObDereferenceObject, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlLeaveCriticalSection, RtlNtStatusToDosError
- call 0x823C3320 in 0x823C32E0: strings -
  - args: r4=constant 0 (0x0)
  - result: unused

## 0x8280CA10 (size 0x14C)
- reaches: ObDereferenceObject, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlLeaveCriticalSection, RtlNtStatusToDosError
- call 0x823C33A4 in 0x823C32E0: strings -
  - args: r3=field 0(r31)
  - result: unused

## 0x8280D328 (size 0x21C)
- reaches: KeSetEvent, ObCreateObject, ObDereferenceObject, ObOpenObjectByPointer, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlLeaveCriticalSection, RtlNtStatusToDosError
- call 0x823C5690 in 0x823C53A0: strings "NuiImageStreamOpen:color"
  - args: r3=constant 2 (0x2), r4=constant 2 (0x2), r5=constant 0 (0x0), r6=constant 2 (0x2), r7=constant 0 (0x0), r8=r30+5192
  - result: unused
- call 0x823C56D0 in 0x823C53A0: strings "NuiImageStreamOpen:depth"
  - args: r3=constant 4 (0x4), r4=constant 1 (0x1), r5=constant 0 (0x0), r6=constant 2 (0x2), r7=constant 0 (0x0), r8=r30+5216
  - result: unused

## 0x82812D98 (size 0x334)
- reaches: DbgPrint, EtxProducerLog, EtxProducerUnregister, KeReleaseMutant, KeResetEvent, KeSetBasePriorityThread, KeSetEvent, KeWaitForSingleObject, NtClose, NtSetEvent, ObDereferenceObject, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlLeaveCriticalSection, XamGetActiveDashAppInfo, XamGetCurrentTitleId, XamXStudioRequest, XamXlfsUninitializeUploadQueue
- call 0x823C5B28 in 0x823C5A18: strings -
  - args: -
  - result: unused

## 0x828151F8 (size 0x2EC)
- reaches: DbgPrint, EtxProducerLog, EtxProducerRegister, EtxProducerUnregister, ExCreateThread, KeInitializeMutant, KeReleaseMutant, KeResumeThread, KeSetAffinityThread, KeSetBasePriorityThread, KeSetEvent, KeWaitForSingleObject, KfAcquireSpinLock, KfReleaseSpinLock, NtClose, NtCreateEvent, NtSetEvent, ObDereferenceObject, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlInitializeCriticalSection, RtlLeaveCriticalSection, RtlNtStatusToDosError, XGetVideoMode, XamContentClose, XamContentOpenFile, XamGetActiveDashAppInfo, XamGetCurrentTitleId, XamNuiCameraTiltSetCallback, XamNuiIdentityGetSessionId, XamUserNuiGetUserIndex, XamXStudioRequest, XamXlfsInitializeUploadQueue, XamXlfsUninitializeUploadQueue
- call 0x823C5598 in 0x823C53A0: strings "NuiInitialize"
  - args: r3=constant 16457 (0x4049), r4=constant -1 (0xFFFF)
  - result: unused

## 0x82815660 (size 0xF0)
- reaches: RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x823C82D0 in 0x823C8298: strings -
  - args: r3=copy of r29, r4=copy of r28
  - result: cmpwi r3,0

## 0x82815750 (size 0x9C)
- reaches: RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x823C80CC in 0x823C80A8: strings -
  - args: r3=constant 1 (0x1)
  - result: cmpwi r3,0
- call 0x823C81BC in 0x823C8198: strings -
  - args: r3=constant 0 (0x0)
  - result: cmpwi r3,0

## 0x828157F0 (size 0x80)
- reaches: DbgPrint
- call 0x824DA5EC in 0x824DA5A0: strings -
  - args: r3=field 80(r1), r4=copy of r31
  - result: unused

## 0x82815870 (size 0x80)
- reaches: DbgPrint
- call 0x824DA5F8 in 0x824DA5A0: strings -
  - args: r3=copy of r31
  - result: r30 = r3

## 0x82851418 (size 0x4E4)
- reaches: EtxProducerUnregister, ExRegisterTitleTerminateNotification, KeSetEvent, KeWaitForSingleObject, RtlEnterCriticalSection, RtlLeaveCriticalSection, XAudioUnregisterRenderDriverMECClient
- call 0x823C5B04 in 0x823C5A18: strings -
  - args: r3=copy of r29
  - result: cmplwi r3,0

## 0x82851900 (size 0x78)
- reaches: KeGetCurrentProcessType, RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x823C560C in 0x823C53A0: strings -
  - args: r3=copy of r27, r4=constant 1 (0x1), r5=static 0x823C3EB8
  - result: unused

## 0x82851978 (size 0xA4)
- reaches: RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x823C5AFC in 0x823C5A18: strings -
  - args: r3=copy of r29, r4=static 0x823C3EB8
  - result: unused

## 0x82852000 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x823C55F0 in 0x823C53A0: strings -
  - args: r3=constant 5 (0x5), r4=static 0x82CBFAC8, r5=constant 1 (0x1), r6=copy of r27, r7=constant 0 (0x0)
  - result: cmpwi r3,0

## 0x828650B8 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x823CC14C in 0x823CC040: strings -
  - args: -
  - result: unused
- call 0x823CC43C in 0x823CC338: strings -
  - args: -
  - result: unused
- call 0x823CC4B0 in 0x823CC338: strings -
  - args: -
  - result: unused

## 0x828650C8 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x823CAD74 in 0x823CAD28: strings -
  - args: r3=r3+12
  - result: unused
- call 0x823CC0EC in 0x823CC040: strings -
  - args: r3=r29+12, r4=constant 1 (0x1)
  - result: unused

## 0x828650E0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x823C9C00 in 0x823C9B80: strings -
  - args: r3=constant 64 (0x40)
  - result: unused

## 0x828650F0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x823CB020 in 0x823CAFE0: strings -
  - args: r3=constant 5 (0x5), r4=stack buffer r1+96, r5=stack buffer r1+80
  - result: cmpwi r3,0

## 0x82866988 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x823C9C40 in 0x823C9C20: strings -
  - args: -
  - result: unused
- call 0x823CC08C in 0x823CC040: strings -
  - args: -
  - result: unused
- call 0x823CC444 in 0x823CC338: strings -
  - args: -
  - result: unused

## 0x82866998 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x823CB074 in 0x823CAFE0: strings -
  - args: r3=copy of r30
  - result: unused

## 0x828669A8 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x823CC118 in 0x823CC040: strings -
  - args: r3=r29+12, r4=field 96(r31), r5=copy of r28
  - result: cmpwi r3,0

## 0x828669C8 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x823CC2D4 in 0x823CC258: strings -
  - args: r3=stack buffer r31+108, r4=field 0(r3), r5=constant 33 (0x21), r6=constant 1 (0x1), r7=copy of r30
  - result: unused

## 0x828669F0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x823CABB8 in 0x823CAB58: strings -
  - args: r3=stack buffer r31+108, r4=field 0(r27), r5=copy of r30
  - result: unused

## 0x82866A10 (size 0x48)
- reaches: pure (no imports reached)
- call 0x823CAC48 in 0x823CAB58: strings -
  - args: r3=stack buffer r31+108, r4=field 0(r27), r5=constant 0 (0x0), r6=stack buffer r31+2176, r7=constant 0 (0x0), r8=constant 1 (0x1), r10=stack buffer r31+84; f1=loaded from 3660(r11)
  - result: unused

## 0x82866A58 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x823CACB8 in 0x823CAC88: strings -
  - args: r3=r3+12
  - result: unused

## 0x82867B80 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x823C9C44 in 0x823C9C20: strings -
  - args: -
  - result: unused

## 0x82867B90 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x823CB9FC in 0x823CB988: strings -
  - args: r3=field 0(r3), r4=copy of r31, r5=constant 0 (0x0), r6=r30+12
  - result: r31 = r3

## 0x82867BB0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x823C9EE4 in 0x823C9EC0: strings -
  - args: r3=r3+12
  - result: unused

## 0x82867BC0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x823CC204 in 0x823CC1A8: strings -
  - args: r3=field -9208(r10), r4=stack buffer r31+92, r10=constant 0x82D60000
  - result: unused

## 0x82868AC0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x823CC5D0 in 0x823CC338: strings -
  - args: r3=field 0(r3), r4=constant 1 (0x1)
  - result: unused

## 0x82868B98 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x823C9BDC in 0x823C9B80: strings -
  - args: r3=stack buffer r1+80, r4=constant 3 (0x3)
  - result: r30 = r3

## 0x8286F238 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x823C2874 in 0x823C2810: strings -
  - args: r3=field 136(r31)
  - result: r4 = r3
- call 0x82411914 in 0x82411620: strings -
  - args: r3=stack buffer r31+112
  - result: r5 = r3
- call 0x82503E28 in 0x82503CC0: strings -
  - args: r3=copy of r30
  - result: unused
- call 0x8268D108 in 0x8268D0A0: strings -
  - args: r3=copy of r30
  - result: stores r3 to 88(r31)
- call 0x8268D15C in 0x8268D0A0: strings -
  - args: r3=copy of r30
  - result: stores r3 to 96(r31)
- call 0x8268D264 in 0x8268D0A0: strings -
  - args: r3=copy of r30
  - result: stores r3 to 104(r31)
- call 0x8268D2B8 in 0x8268D0A0: strings -
  - args: r3=copy of r30
  - result: stores r3 to 112(r31)
- call 0x8268D4D0 in 0x8268D418: strings -
  - args: r3=copy of r27
  - result: stores r3 to 88(r31)
- call 0x8268D524 in 0x8268D418: strings -
  - args: r3=copy of r27
  - result: stores r3 to 96(r31)
- call 0x8270F5C4 in 0x8270F268: strings -
  - args: r3=field 88(r30)
  - result: cmpwi r3,0, cmpwi r3,6
- call 0x8270F61C in 0x8270F268: strings -
  - args: r3=field 88(r30)
  - result: cmpwi r3,0
- call 0x8270F678 in 0x8270F268: strings -
  - args: r3=field 88(r30)
  - result: stores r3 to 104(r30)
- call 0x8270F948 in 0x8270F268: strings -
  - args: r3=field 88(r30)
  - result: cmpwi r3,0

## Referenced only from data
0x82805F4C, 0x8280C590, 0x8280D0A8, 0x8282B3A8, 0x8282C728, 0x8282FF70, 0x8282FFC0, 0x82830DB0, 0x82831150, 0x82831230, 0x82832730, 0x82834230, 0x82836598, 0x82836640, 0x828369E8, 0x82836A18, 0x82836AC0, 0x82836E00, 0x82837F60, 0x82837FA0, 0x82838810, 0x82838850, 0x828389C8, 0x82838B20, 0x82838B90, 0x82841060, 0x828410F8, 0x828412C0, 0x828421E8, 0x82842248, 0x82842318, 0x828425B8, 0x82842888, 0x828439C8, 0x82843A48, 0x82843AA8, 0x82843B20, 0x82843B78, 0x82843BF0, 0x82843D30, 0x82844138, 0x82844210, 0x82844DC0, 0x82844E40, 0x82844EC8, 0x82844F50, 0x82845008, 0x828450C0, 0x82845178, 0x82845238, 0x82845428, 0x828456D8, 0x82845788, 0x82845988, 0x82845A38, 0x82845AE8, 0x82845B98, 0x82845C48, 0x82845CF8, 0x82845DA8, 0x82845E58, 0x82845F08, 0x82845FB8, 0x82846068, 0x82846118, 0x828463D8, 0x82846498, 0x82846868, 0x82846928, 0x828469D8, 0x82846A88, 0x82846B38, 0x82846BE8, 0x82846C98, 0x82846D48, 0x82846DF8, 0x82846EB0, 0x82846F68, 0x828471A8, 0x82848090, 0x82848488, 0x82848BA0, 0x8284D978, 0x8284EF40, 0x8284F548, 0x82850030, 0x828665B0, 0x82866668, 0x82866920, 0x828672C8, 0x828681E0, 0x82869760, 0x8286E098, 0x8286E238, 0x8286E2F0, 0x8286E438, 0x8286F6B8, 0x8286F768, 0x8286F958, 0x8286FA08, 0x8286FBE0, 0x8286FC90, 0x8286FE68, 0x8286FF30
