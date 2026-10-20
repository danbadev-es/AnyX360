# Entry points of 0x821B2700-0x821D3000 called from outside it

18 entry points; 4 more functions of the range are only referenced from data.

## 0x821B2788 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x823D888C in 0x823D8840: strings -
  - args: r3=stack buffer r1+80, r4=copy of r29
  - result: r31 = r3

## 0x821B27A8 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x823B6F3C in 0x823B6F20: strings -
  - args: -
  - result: unused
- call 0x823B7058 in 0x823B7038: strings -
  - args: -
  - result: cmplwi r3,0
- call 0x823B7088 in 0x823B7038: strings -
  - args: -
  - result: unused
- call 0x823BDBB8 in 0x823BDB60: strings -
  - args: -
  - result: unused
- call 0x823C101C in 0x823C0FE8: strings -
  - args: -
  - result: unused
- call 0x823C6E8C in 0x823C6E70: strings -
  - args: -
  - result: cmplwi r3,0
- call 0x823CE288 in 0x823CE228: strings -
  - args: -
  - result: unused
- call 0x823D0AC4 in 0x823D0A70: strings -
  - args: -
  - result: unused
- call 0x823D0B70 in 0x823D0A70: strings -
  - args: -
  - result: unused
- call 0x823D8ECC in 0x823D8EB0: strings -
  - args: -
  - result: unused
- call 0x823D8F0C in 0x823D8EF8: strings -
  - args: -
  - result: unused
- call 0x824542A0 in 0x82454228: strings -
  - args: -
  - result: r29 = r3
- call 0x82454340 in 0x82454308: strings -
  - args: -
  - result: unused
- call 0x82454380 in 0x82454308: strings -
  - args: -
  - result: unused
- call 0x824545D8 in 0x82454538: strings -
  - args: -
  - result: unused
- call 0x82454618 in 0x82454538: strings -
  - args: -
  - result: unused
- call 0x82454980 in 0x824547E8: strings -
  - args: -
  - result: unused
- call 0x82454984 in 0x824547E8: strings -
  - args: -
  - result: unused
- call 0x82454DC8 in 0x82454C18: strings -
  - args: -
  - result: unused
- call 0x82454DCC in 0x82454C18: strings -
  - args: -
  - result: unused
- call 0x8245ABA0 in 0x8245AB80: strings -
  - args: -
  - result: unused
- call 0x8245AC1C in 0x8245AB80: strings -
  - args: -
  - result: unused
- call 0x8245AF20 in 0x8245AF00: strings -
  - args: -
  - result: unused
- call 0x8245AF9C in 0x8245AF00: strings -
  - args: -
  - result: unused

## 0x821B2808 (size 0x1BC)
- reaches: KeSetEvent, KeWaitForSingleObject
- call 0x826CB5A0 in 0x826CB548: strings -
  - args: r5=constant 33 (0x21), r6=constant 0 (0x0), r7=constant 0 (0x0)
  - result: unused
- call 0x826CC2D8 in 0x826CC270: strings -
  - args: r3=field 748(r31), r5=constant 1 (0x1), r6=constant 0 (0x0), r7=constant 0 (0x0)
  - result: unused

## 0x821B29C8 (size 0x128)
- reaches: KeSetEvent, KeWaitForSingleObject
- call 0x826CD6B4 in 0x826CD068: strings -
  - args: r3=field 0(r20), r4=constant 0 (0x0), r5=constant 0 (0x0), r6=constant 0 (0x0)
  - result: unused

## 0x821B2AF0 (size 0x100)
- reaches: KeWaitForSingleObject
- call 0x82685394 in 0x82685370: strings -
  - args: r3=copy of r30, r4=stack buffer r1+80
  - result: unused

## 0x821B2D08 (size 0x130)
- reaches: KeWaitForSingleObject
- call 0x826CD294 in 0x826CD068: strings "list<T> too long"
  - args: r3=stack buffer r1+144
  - result: cmpwi r3,0
- call 0x826CD320 in 0x826CD068: strings -
  - args: r3=stack buffer r1+144
  - result: cmpwi r3,0
- call 0x826CD508 in 0x826CD068: strings -
  - args: r3=stack buffer r1+176, r10=constant 4 (0x4)
  - result: cmpwi r3,0
- call 0x826CD538 in 0x826CD068: strings -
  - args: r3=stack buffer r1+176
  - result: cmpwi r3,0
- call 0x826CD580 in 0x826CD068: strings -
  - args: r3=stack buffer r1+176
  - result: cmpwi r3,0
- call 0x826CD5B0 in 0x826CD068: strings -
  - args: r3=stack buffer r1+176
  - result: cmpwi r3,0
- call 0x826CD628 in 0x826CD068: strings -
  - args: r3=stack buffer r1+112
  - result: cmpwi r3,0
- call 0x826CD694 in 0x826CD068: strings -
  - args: r3=stack buffer r1+112
  - result: cmpwi r3,0
- call 0x826CD740 in 0x826CD068: strings -
  - args: r3=stack buffer r1+176
  - result: cmpwi r3,0
- call 0x826CD770 in 0x826CD068: strings -
  - args: r3=stack buffer r1+176
  - result: cmpwi r3,0
- call 0x826CD834 in 0x826CD068: strings -
  - args: r3=stack buffer r1+176
  - result: cmpwi r3,0
- call 0x826CD864 in 0x826CD068: strings -
  - args: r3=stack buffer r1+176
  - result: cmpwi r3,0
- call 0x826CD8B8 in 0x826CD068: strings -
  - args: r3=stack buffer r1+176, r10=constant 1 (0x1)
  - result: cmpwi r3,0
- call 0x826CD8E8 in 0x826CD068: strings -
  - args: r3=stack buffer r1+176
  - result: cmpwi r3,0, cmplwi r3,0

## 0x821B2E38 (size 0x144)
- reaches: KeWaitForSingleObject
- call 0x826CC060 in 0x826CBF50: strings -
  - args: r3=field 748(r31)
  - result: unused

## 0x821B30F8 (size 0x104)
- reaches: RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x8266CA68 in 0x8266C960: strings -
  - args: r3=stack buffer r1+88
  - result: unused

## 0x821B3720 (size 0x14C)
- reaches: KeSetEvent, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x8266D8C4 in 0x8266D848: strings -
  - args: r3=field -18828(r11), r4=constant 2 (0x2)
  - result: unused

## 0x821B3870 (size 0x27C)
- reaches: KeReleaseMutant, KeResetEvent, KeSetEvent, KeWaitForSingleObject, RtlEnterCriticalSection, RtlLeaveCriticalSection, XamIsUIActive, XamXStudioRequest
- call 0x8266CBC0 in 0x8266CA80: strings -
  - args: r3=constant 0 (0x0), r4=copy of r30
  - result: cmpwi r3,0

## 0x821B4648 (size 0xF4)
- reaches: pure (no imports reached)
- call 0x8266C35C in 0x8266C2F0: strings -
  - args: r3=stack buffer r1+96, r10=static 0x82131F20
  - result: unused

## 0x821C04E8 (size 0x98)
- reaches: EtxProducerLog, EtxProducerUnregister, KeReleaseMutant, KeResetEvent, KeSetBasePriorityThread, KeSetEvent, KeWaitForSingleObject, NtClose, NtSetEvent, ObDereferenceObject, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlLeaveCriticalSection, XamGetActiveDashAppInfo, XamGetCurrentTitleId, XamXStudioRequest, XamXlfsUninitializeUploadQueue
- call 0x8266CE34 in 0x8266CD60: strings -
  - args: -
  - result: unused

## 0x821C2918 (size 0x1A8)
- reaches: EtxProducerLog, EtxProducerRegister, EtxProducerUnregister, ExCreateThread, KeInitializeMutant, KeReleaseMutant, KeResumeThread, KeSetAffinityThread, KeSetBasePriorityThread, KeSetEvent, KeWaitForSingleObject, KfAcquireSpinLock, KfReleaseSpinLock, NtClose, NtCreateEvent, NtSetEvent, ObDereferenceObject, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlInitializeCriticalSection, RtlLeaveCriticalSection, RtlNtStatusToDosError, XGetVideoMode, XamContentClose, XamContentOpenFile, XamGetActiveDashAppInfo, XamGetCurrentTitleId, XamNuiCameraTiltSetCallback, XamNuiIdentityGetSessionId, XamUserNuiGetUserIndex, XamXStudioRequest, XamXlfsInitializeUploadQueue, XamXlfsUninitializeUploadQueue
- call 0x8266D8B4 in 0x8266D848: strings -
  - args: r3=constant 3081 (0xC09)
  - result: unused

## 0x821C3120 (size 0x328)
- reaches: KeResetEvent, KeWaitForSingleObject, ObDereferenceObject, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlLeaveCriticalSection, RtlNtStatusToDosError
- call 0x8266D298 in 0x8266D1A0: strings -
  - args: r3=field -18812(r28), r4=constant 0 (0x0), r5=stack buffer r1+80
  - result: unused
- call 0x8266D440 in 0x8266D348: strings -
  - args: r3=field -18816(r28), r4=constant 0 (0x0), r5=stack buffer r1+80
  - result: unused

## 0x821C3448 (size 0x14C)
- reaches: ObDereferenceObject, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlLeaveCriticalSection, RtlNtStatusToDosError
- call 0x8266D2EC in 0x8266D1A0: strings -
  - args: r3=field -18812(r28), r4=field 80(r1)
  - result: unused
- call 0x8266D494 in 0x8266D348: strings -
  - args: r3=field -18816(r28), r4=field 80(r1)
  - result: unused

## 0x821C3CC0 (size 0x21C)
- reaches: KeSetEvent, ObCreateObject, ObDereferenceObject, ObOpenObjectByPointer, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlLeaveCriticalSection, RtlNtStatusToDosError
- call 0x8266D8F4 in 0x8266D848: strings -
  - args: r3=constant 1 (0x1), r4=constant 2 (0x2), r5=constant 0 (0x0), r6=constant 1 (0x1), r7=field -18824(r11), r8=static 0x8286B680, r10=constant 0x82870000
  - result: unused
- call 0x8266D918 in 0x8266D848: strings -
  - args: r3=constant 0 (0x0), r4=constant 1 (0x1), r5=constant 0 (0x0), r6=constant 1 (0x1), r7=field -18820(r11), r8=static 0x8286B684, r10=constant 0x82870000
  - result: unused

## 0x821D0C88 (size 0x90)
- reaches: XamNuiCameraElevationStopMovement
- call 0x8268DD80 in 0x8268DD48: strings -
  - args: -
  - result: unused

## 0x821D15D0 (size 0x1E4)
- reaches: ExGetXConfigSetting, KfAcquireSpinLock, KfReleaseSpinLock, XamNuiCameraElevationGetAngle, XamNuiCameraElevationSetAngle
- call 0x8268DEF4 in 0x8268DDF8: strings -
  - args: r3=field 24(r30), r7=r28+32; f1=loaded from 28(r30), f2=loaded from 32(r30), f3=loaded from 36(r30)
  - result: cmplwi r3,170

## Referenced only from data
0x821B2754, 0x821B2778, 0x821C2FC8, 0x821C3AF0
