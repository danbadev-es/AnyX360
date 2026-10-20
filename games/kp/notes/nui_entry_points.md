# Entry points of 0x826A0000-0x826C0000 called from outside it

124 entry points; 28 more functions of the range are only referenced from data.

## 0x826A0220 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x8267CCC0 in 0x8267CC50: strings -
  - args: r3=copy of r31
  - result: unused
- call 0x8267CD1C in 0x8267CCF0: strings -
  - args: r3=copy of r31
  - result: unused
- call 0x8267FD28 in 0x8267FD00: strings -
  - args: r3=copy of r30
  - result: unused
- call 0x8268166C in 0x82681608: strings -
  - args: r3=copy of r30
  - result: unused
- call 0x8268179C in 0x82681608: strings -
  - args: r3=copy of r31
  - result: unused

## 0x826A0228 (size 0x1A4)
- reaches: pure (no imports reached)
- call 0x8267CCB0 in 0x8267CC50: strings -
  - args: r3=copy of r31, r4=copy of r30
  - result: r30 = r3

## 0x826A0810 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82678BDC in 0x82678BB0: strings -
  - args: r4=copy of r27
  - result: unused
- call 0x82678CE0 in 0x82678BB0: strings -
  - args: r4=copy of r27
  - result: r31 = r3
- call 0x82678D44 in 0x82678BB0: strings -
  - args: r4=copy of r27
  - result: r31 = r3

## 0x826A0820 (size 0x58)
- reaches: pure (no imports reached)
- call 0x82678C90 in 0x82678BB0: strings -
  - args: r3=copy of r30, r4=field 80(r1), r5=constant 0 (0x0)
  - result: r31 = r3

## 0x826A0878 (size 0xC0)
- reaches: pure (no imports reached)
- call 0x82679E60 in 0x82679B28: strings -
  - args: -
  - result: r31 = r3

## 0x826A0938 (size 0x64)
- reaches: pure (no imports reached)
- call 0x826790A0 in 0x82678D60: strings -
  - args: -
  - result: unused

## 0x826A0BE0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x8267ABA4 in 0x8267A9C0: strings -
  - args: r3=copy of r31, r4=copy of r30
  - result: cmpwi r3,0

## 0x826A13F8 (size 0x88)
- reaches: pure (no imports reached)
- call 0x82669BC4 in 0x82669B68: strings -
  - args: r3=copy of r31
  - result: cmplwi r3,0
- call 0x8267A9A8 in 0x8267A940: strings -
  - args: -
  - result: unused
- call 0x8267AFE4 in 0x8267AF98: strings -
  - args: r3=copy of r30
  - result: unused
- call 0x8267B310 in 0x8267B1F8: strings -
  - args: r3=copy of r30
  - result: unused
- call 0x82680F08 in 0x82680EA8: strings -
  - args: r3=copy of r30
  - result: unused
- call 0x8268184C in 0x826817D0: strings -
  - args: r3=copy of r30
  - result: unused

## 0x826A1AA8 (size 0xA0)
- reaches: RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x8267B204 in 0x8267B1F8: strings -
  - args: -
  - result: cmpwi r3,0

## 0x826A1C90 (size 0x68)
- reaches: RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x8267AFC8 in 0x8267AF98: strings -
  - args: -
  - result: unused
- call 0x8267B328 in 0x8267B1F8: strings -
  - args: -
  - result: unused

## 0x826A1CF8 (size 0x60)
- reaches: RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x8267A1D8 in 0x8267A198: strings -
  - args: r3=copy of r29
  - result: stores r3 to 0(r31)

## 0x826A1FA8 (size 0x50)
- reaches: RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x8267ABB8 in 0x8267A9C0: strings -
  - args: r3=copy of r30, r4=copy of r31
  - result: unused

## 0x826A2870 (size 0xD8)
- reaches: pure (no imports reached)
- call 0x8267DC28 in 0x8267DB28: strings -
  - args: r4=copy of r30
  - result: r31 = r3
- call 0x82696360 in 0x82695F98: strings -
  - args: -
  - result: cmplwi r3,0

## 0x826A2948 (size 0x12C)
- reaches: pure (no imports reached)
- call 0x8267ADF0 in 0x8267AD48: strings -
  - args: r3=stack buffer r1+80, r4=copy of r27, r5=copy of r28
  - result: r31 = r3
- call 0x8267AE64 in 0x8267AD48: strings -
  - args: r3=stack buffer r1+80, r4=copy of r26, r5=copy of r28
  - result: r31 = r3

## 0x826A2D68 (size 0x9C)
- reaches: pure (no imports reached)
- call 0x8267B2D8 in 0x8267B1F8: strings -
  - args: -
  - result: r29 = r3

## 0x826A3308 (size 0x9C)
- reaches: pure (no imports reached)
- call 0x8267B2E4 in 0x8267B1F8: strings -
  - args: -
  - result: r29 = r3

## 0x826A33A8 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x8267E464 in 0x8267E428: strings -
  - args: r3=copy of r30
  - result: unused
- call 0x8267E48C in 0x8267E428: strings -
  - args: r3=copy of r30
  - result: unused

## 0x826A33B0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x8267FC2C in 0x8267F9F0: strings -
  - args: r3=copy of r30
  - result: unused
- call 0x8267FFF0 in 0x8267FD68: strings -
  - args: r3=stack buffer r31+4
  - result: unused
- call 0x82680438 in 0x826800A8: strings -
  - args: r3=copy of r31
  - result: unused
- call 0x82680498 in 0x826800A8: strings -
  - args: r3=stack buffer r31+4
  - result: unused
- call 0x8269B5E4 in 0x8269B5C0: strings -
  - args: r3=r3+4
  - result: unused
- call 0x8269B798 in 0x8269B770: strings -
  - args: -
  - result: unused
- call 0x8269BC38 in 0x8269BBD8: strings -
  - args: r3=copy of r31
  - result: unused
- call 0x8269C358 in 0x8269C2F8: strings -
  - args: r3=stack buffer r31+4
  - result: unused

## 0x826A33D0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x8269D850 in 0x8269D7B8: strings -
  - args: r3=r11+8
  - result: unused

## 0x826A33F0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x8267D76C in 0x8267D558: strings -
  - args: r3=copy of r30, r4=copy of r28
  - result: unused
- call 0x8269D05C in 0x8269CF58: strings -
  - args: r3=copy of r29, r4=stack buffer r1+80
  - result: unused

## 0x826A3440 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x8267DA70 in 0x8267D978: strings -
  - args: r3=copy of r31, r4=copy of r26
  - result: unused

## 0x826A3468 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x8267D6C8 in 0x8267D558: strings -
  - args: r3=copy of r30, r4=stack buffer r1+128
  - result: unused
- call 0x82689A70 in 0x826899F8: strings -
  - args: r3=copy of r31, r4=stack buffer r1+80
  - result: unused

## 0x826A3548 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x8267DA24 in 0x8267D978: strings -
  - args: r3=copy of r31, r4=copy of r22
  - result: unused

## 0x826A3590 (size 0xFC)
- reaches: pure (no imports reached)
- call 0x8269DDDC in 0x8269DDA0: strings -
  - args: -
  - result: unused
- call 0x8269F608 in 0x8269F590: strings -
  - args: r3=r11+8, r10=field 4(r11)
  - result: unused

## 0x826A3690 (size 0x7C)
- reaches: pure (no imports reached)
- call 0x8267ED74 in 0x8267EA40: strings -
  - args: r3=copy of r24, r4=copy of r25, r5=copy of r20
  - result: unused
- call 0x8269B81C in 0x8269B7E0: strings -
  - args: r3=field 32(r3), r4=field 28(r3), r5=field 52(r3)
  - result: unused
- call 0x8269BC0C in 0x8269BBD8: strings -
  - args: r3=field 32(r3), r4=field 28(r3), r5=field 52(r3)
  - result: unused

## 0x826A3710 (size 0x10C)
- reaches: pure (no imports reached)
- call 0x8267EC54 in 0x8267EA40: strings -
  - args: r3=copy of r25, r4=copy of r20, r5=stack buffer r1+80
  - result: r24 = r3

## 0x826A38E0 (size 0x2C)
- reaches: pure (no imports reached)
- call 0x82692F70 in 0x82692F30: strings -
  - args: r3=copy of r31, r4=field 4(r29)
  - result: unused

## 0x826A3910 (size 0x34)
- reaches: pure (no imports reached)
- call 0x82692F58 in 0x82692F30: strings -
  - args: r3=copy of r31; f1=loaded from 80(r1)
  - result: unused
- call 0x82692F64 in 0x82692F30: strings -
  - args: r3=copy of r31; f1=loaded from 84(r1)
  - result: unused
- call 0x82692F98 in 0x82692F30: strings -
  - args: r3=copy of r31
  - result: unused
- call 0x82692FA4 in 0x82692F30: strings -
  - args: r3=copy of r31; f1=loaded from 4(r30)
  - result: unused
- call 0x82692FB0 in 0x82692F30: strings -
  - args: r3=copy of r31; f1=loaded from 8(r30)
  - result: unused
- call 0x82692FBC in 0x82692F30: strings -
  - args: r3=copy of r31; f1=loaded from 12(r30)
  - result: unused
- call 0x82692FC8 in 0x82692F30: strings -
  - args: r3=copy of r31; f1=loaded from 16(r30)
  - result: unused
- call 0x82692FD4 in 0x82692F30: strings -
  - args: r3=copy of r31; f1=loaded from 20(r30)
  - result: unused

## 0x826A3948 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x8269CCE4 in 0x8269CC80: strings -
  - args: r3=copy of r28
  - result: unused

## 0x826A39E0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x8269CD7C in 0x8269CD18: strings -
  - args: r3=copy of r28
  - result: unused

## 0x826A3B88 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x8269F4BC in 0x8269F480: strings -
  - args: r3=copy of r30
  - result: unused
- call 0x8269F4E4 in 0x8269F480: strings -
  - args: r3=copy of r30
  - result: unused
- call 0x8269F79C in 0x8269F590: strings -
  - args: -
  - result: unused

## 0x826A3BE0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x8267FA90 in 0x8267F9F0: strings -
  - args: r3=copy of r30
  - result: unused
- call 0x8269CE14 in 0x8269CDB0: strings -
  - args: r3=copy of r28
  - result: unused

## 0x826A3BF8 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x8269F434 in 0x8269F3F8: strings -
  - args: r3=copy of r30
  - result: unused
- call 0x8269F45C in 0x8269F3F8: strings -
  - args: r3=copy of r30
  - result: unused
- call 0x8269F7CC in 0x8269F590: strings -
  - args: -
  - result: unused

## 0x826A3C00 (size 0x6C)
- reaches: pure (no imports reached)
- call 0x8269F5C4 in 0x8269F590: strings -
  - args: r4=static 0x8269F590
  - result: unused
- call 0x8269F688 in 0x8269F590: strings -
  - args: r4=static 0x826A3590
  - result: cmplwi r3,0

## 0x826A3C70 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82A4CAE8 in 0x82A4CA30: strings -
  - args: -
  - result: unused

## 0x826A3CC8 (size 0x1BC)
- reaches: KeSetEvent, KeWaitForSingleObject
- call 0x82A4CACC in 0x82A4CA30: strings -
  - args: r3=field 8(r31), r4=constant 0 (0x0), r5=constant 1 (0x1), r6=static 0x82A4B898, r7=copy of r30
  - result: unused
- call 0x82A4CB04 in 0x82A4CA30: strings -
  - args: r3=field 8(r31), r4=constant 0 (0x0), r5=constant 1 (0x1), r6=static 0x82A4B898, r7=copy of r30
  - result: unused

## 0x826A3E88 (size 0x128)
- reaches: KeSetEvent, KeWaitForSingleObject
- call 0x82A4E338 in 0x82A4D288: strings -
  - args: r3=field 4(r31), r4=constant 0 (0x0), r5=static 0x82A4CA30, r6=copy of r29
  - result: unused

## 0x826A3FB0 (size 0x100)
- reaches: KeWaitForSingleObject
- call 0x82A49E18 in 0x82A49C78: strings -
  - args: r4=stack buffer r1+112
  - result: cmpwi r3,4, stores r3 to 12(r30)

## 0x826A41C8 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x828E7624 in 0x828E75A8: strings -
  - args: -
  - result: unused
- call 0x828EC83C in 0x828EC210: strings -
  - args: -
  - result: unused
- call 0x8291C4C8 in 0x8291C480: strings -
  - args: -
  - result: unused
- call 0x8291CAAC in 0x8291CA20: strings -
  - args: -
  - result: unused
- call 0x82954C30 in 0x82954A80: strings -
  - args: -
  - result: unused
- call 0x82958BB8 in 0x82958B30: strings -
  - args: -
  - result: unused
- call 0x82A4B01C in 0x82A4AFF0: strings -
  - args: -
  - result: unused

## 0x826A65D8 (size 0x108)
- reaches: KeResetEvent, KeWaitForSingleObject, RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x826C91D8 in 0x826C9180: strings -
  - args: r3=stack buffer r31+4236, r4=stack buffer r1+80, r5=stack buffer r1+84, r6=stack buffer r1+88
  - result: r29 = r3

## 0x826ABB48 (size 0x2EC)
- reaches: DbgPrint, EtxProducerLog, EtxProducerRegister, EtxProducerUnregister, ExCreateThread, KeInitializeMutant, KeReleaseMutant, KeResumeThread, KeSetAffinityThread, KeSetBasePriorityThread, KeSetEvent, KeWaitForSingleObject, KfAcquireSpinLock, KfReleaseSpinLock, NtClose, NtCreateEvent, NtSetEvent, ObDereferenceObject, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlInitializeCriticalSection, RtlLeaveCriticalSection, RtlNtStatusToDosError, XGetVideoMode, XamContentClose, XamContentOpenFile, XamGetActiveDashAppInfo, XamGetCurrentTitleId, XamNuiCameraTiltSetCallback, XamNuiIdentityGetSessionId, XamUserNuiGetUserIndex, XamXStudioRequest
- call 0x82A50218 in 0x82A501C8: strings -
  - args: r4=constant 5 (0x5)
  - result: cmpwi r3,0

## 0x826AC408 (size 0x114)
- reaches: pure (no imports reached)
- call 0x826C7A7C in 0x826C78F0: strings -
  - args: r3=constant 2 (0x2), r4=copy of r26, r5=copy of r28, r6=copy of r30, r8=stack buffer r1+80, r9=stack buffer r1+84
  - result: unused
- call 0x826C8354 in 0x826C8238: strings -
  - args: r3=constant 2 (0x2), r4=copy of r27, r5=field 80(r31), r6=field 84(r31), r8=copy of r30, r9=copy of r29
  - result: unused
- call 0x826C8378 in 0x826C8238: strings -
  - args: r3=constant 2 (0x2), r4=copy of r27, r5=field 88(r31), r6=field 92(r31), r8=stack buffer r31+56, r9=copy of r28
  - result: unused
- call 0x826D355C in 0x826D3470: strings -
  - args: r3=constant 2 (0x2), r4=copy of r30, r5=field 80(r1), r6=field 84(r1), r8=stack buffer r1+112, r9=stack buffer r1+116
  - result: r7 = r3
- call 0x826D3584 in 0x826D3470: strings -
  - args: r3=constant 2 (0x2), r4=copy of r30, r5=field 96(r1), r6=field 100(r1), r8=stack buffer r1+120, r9=stack buffer r1+124
  - result: r7 = r3

## 0x826AC520 (size 0x328)
- reaches: KeResetEvent, KeWaitForSingleObject, ObDereferenceObject, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlLeaveCriticalSection, RtlNtStatusToDosError
- call 0x82A43F08 in 0x82A43EE8: strings -
  - args: r3=field 2776(r3), r4=constant 0 (0x0), r5=copy of r29
  - result: cmpwi r3,0
- call 0x82A43F30 in 0x82A43EE8: strings -
  - args: r3=field 2780(r31), r4=constant 0 (0x0), r5=copy of r28
  - result: cmpwi r3,0
- call 0x82A45990 in 0x82A45958: strings -
  - args: r3=field 20(r27), r4=constant 33 (0x21), r5=stack buffer r1+96
  - result: cmpwi r3,0
- call 0x82A45AEC in 0x82A45AA0: strings -
  - args: r3=field 24(r27), r4=constant 33 (0x21), r5=stack buffer r1+96
  - result: cmpwi r3,0

## 0x826AC848 (size 0x14C)
- reaches: ObDereferenceObject, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlLeaveCriticalSection, RtlNtStatusToDosError
- call 0x82A43F44 in 0x82A43EE8: strings -
  - args: r3=field 2776(r31), r4=field 0(r29)
  - result: unused
- call 0x82A43FB0 in 0x82A43EE8: strings -
  - args: r3=field 2776(r31), r4=field 0(r29)
  - result: unused
- call 0x82A43FC4 in 0x82A43EE8: strings -
  - args: r3=field 2780(r31), r4=field 0(r28)
  - result: unused
- call 0x82A45A8C in 0x82A45958: strings -
  - args: r3=field 20(r27), r4=field 96(r1)
  - result: unused
- call 0x82A45BE8 in 0x82A45AA0: strings -
  - args: r3=field 24(r27), r4=field 96(r1)
  - result: unused

## 0x826ACFF8 (size 0xB0)
- reaches: RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x826C81D8 in 0x826C8190: strings -
  - args: r3=constant 2 (0x2), r4=copy of r30, r5=field 88(r1), r6=field 84(r1), r8=stack buffer r1+92, r9=stack buffer r1+96
  - result: unused
- call 0x82A4CF7C in 0x82A4CF18: strings -
  - args: r3=constant 2 (0x2), r4=constant 0 (0x0), r5=field 96(r1), r6=field 100(r1), r8=stack buffer r1+80, r9=stack buffer r1+84
  - result: cmpwi r3,0

## 0x826AD160 (size 0x21C)
- reaches: KeSetEvent, ObCreateObject, ObDereferenceObject, ObOpenObjectByPointer, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlLeaveCriticalSection, RtlNtStatusToDosError
- call 0x82A470A0 in 0x82A47078: strings -
  - args: r3=constant 1 (0x1), r4=constant 2 (0x2), r5=constant 0 (0x0), r6=constant 1 (0x1), r7=constant 0 (0x0), r8=r3+24
  - result: unused
- call 0x82A470BC in 0x82A47078: strings -
  - args: r3=constant 4 (0x4), r4=constant 1 (0x1), r5=constant 0 (0x0), r6=constant 1 (0x1), r7=constant 0 (0x0), r8=r28+20
  - result: unused
- call 0x82A4CBCC in 0x82A4CB80: strings -
  - args: r3=constant 0 (0x0), r4=constant 1 (0x1), r5=constant 0 (0x0), r6=constant 1 (0x1), r7=constant 0 (0x0), r8=r30+2776
  - result: unused
- call 0x82A4CBE8 in 0x82A4CB80: strings -
  - args: r3=constant 7 (0x7), r4=constant 0 (0x0), r5=constant 0 (0x0), r6=constant 1 (0x1), r7=constant 0 (0x0), r8=r30+2780
  - result: unused

## 0x826AD380 (size 0xB0)
- reaches: KeSetEvent, ObDereferenceObject, RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x8288765C in 0x82887638: strings -
  - args: -
  - result: unused
- call 0x828876A8 in 0x82887680: strings -
  - args: -
  - result: unused
- call 0x8289AB4C in 0x8289AB40: strings -
  - args: -
  - result: unused
- call 0x8289ABB0 in 0x8289AB70: strings -
  - args: r8=constant 504 (0x1F8), r9=static 0x821821B0, r10=constant 468 (0x1D4)
  - result: unused
- call 0x828A50C0 in 0x828A5058: strings -
  - args: -
  - result: unused
- call 0x828A6428 in 0x828A6338: strings -
  - args: -
  - result: unused
- call 0x828C73B8 in 0x828C7398: strings -
  - args: -
  - result: unused
- call 0x828C8910 in 0x828C87E8: strings -
  - args: -
  - result: unused
- call 0x828C9140 in 0x828C90F0: strings -
  - args: -
  - result: unused
- call 0x828C9188 in 0x828C9168: strings -
  - args: -
  - result: unused
- call 0x828CCA48 in 0x828CC9A8: strings -
  - args: -
  - result: unused
- call 0x828CDFFC in 0x828CDF70: strings -
  - args: -
  - result: unused
- call 0x828D9548 in 0x828D9538: strings -
  - args: -
  - result: unused
- call 0x828D9800 in 0x828D97E8: strings -
  - args: -
  - result: unused
- call 0x828DA288 in 0x828DA278: strings -
  - args: -
  - result: unused
- call 0x828DCDB0 in 0x828DCD98: strings -
  - args: -
  - result: unused
- call 0x828E75D0 in 0x828E75A8: strings -
  - args: -
  - result: unused
- call 0x828EC854 in 0x828EC210: strings -
  - args: -
  - result: unused
- call 0x828F13B8 in 0x828F1318: strings -
  - args: -
  - result: unused
- call 0x828F3134 in 0x828F30A8: strings -
  - args: -
  - result: unused
- call 0x828F3B6C in 0x828F3B60: strings -
  - args: -
  - result: unused
- call 0x828F73C8 in 0x828F73B8: strings -
  - args: -
  - result: unused
- call 0x828FA2D4 in 0x828FA2C8: strings -
  - args: -
  - result: unused
- call 0x828FA588 in 0x828FA550: strings -
  - args: -
  - result: unused
- call 0x82906728 in 0x829066D0: strings -
  - args: -
  - result: unused
- call 0x8290692C in 0x829068B8: strings -
  - args: r9=static 0x821821B0, r10=constant 812 (0x32C)
  - result: unused
- call 0x8290DC84 in 0x8290DC00: strings -
  - args: -
  - result: unused
- call 0x8290E674 in 0x8290E560: strings -
  - args: r6=constant 864 (0x360), r7=static 0x821821B0, r8=constant 0x82180000
  - result: unused
- call 0x82911F28 in 0x82911E90: strings -
  - args: -
  - result: unused
- call 0x8291284C in 0x82912428: strings -
  - args: -
  - result: unused
- call 0x829269D0 in 0x829269C0: strings -
  - args: -
  - result: unused
- call 0x829278E4 in 0x829278D0: strings -
  - args: -
  - result: unused
- call 0x8292EC84 in 0x8292EC70: strings -
  - args: -
  - result: unused
- call 0x8292F06C in 0x8292EFF0: strings -
  - args: -
  - result: unused
- call 0x82937F8C in 0x82937E78: strings -
  - args: -
  - result: unused
- call 0x82938BBC in 0x82938A88: strings -
  - args: -
  - result: unused
- call 0x82943110 in 0x829430A8: strings -
  - args: -
  - result: unused
- call 0x8294452C in 0x829444C8: strings -
  - args: -
  - result: unused
- call 0x82954CA0 in 0x82954A80: strings -
  - args: -
  - result: unused
- call 0x82A4F4FC in 0x82A4F460: strings -
  - args: -
  - result: unused

## 0x826AD4F8 (size 0x104)
- reaches: RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x82886E74 in 0x82886D90: strings -
  - args: r3=stack buffer r1+80, r8=field 32(r3), r9=stack buffer r1+84, r10=r11+1
  - result: unused
- call 0x8289CB44 in 0x8289C9A0: strings -
  - args: r3=stack buffer r1+80, r9=field 4(r10), r10=field 8(r10)
  - result: unused
- call 0x828A9C80 in 0x828A9B28: strings -
  - args: r3=stack buffer r1+264
  - result: unused
- call 0x828CD974 in 0x828CD878: strings -
  - args: r3=stack buffer r1+112
  - result: unused
- call 0x828D9120 in 0x828D8FB8: strings -
  - args: r3=stack buffer r1+112, r9=field 4(r10), r10=field 8(r10)
  - result: unused
- call 0x828E017C in 0x828E0018: strings -
  - args: r3=stack buffer r1+80, r9=field 4(r10), r10=field 8(r10)
  - result: unused
- call 0x828F26BC in 0x828F2540: strings -
  - args: r3=stack buffer r1+80, r9=field 4(r10), r10=field 8(r10)
  - result: unused
- call 0x828F96C0 in 0x828F9560: strings -
  - args: r3=stack buffer r1+80, r9=field 4(r10), r10=field 8(r10)
  - result: unused
- call 0x82900414 in 0x828FFD08: strings -
  - args: r3=stack buffer r1+112, r10=field 4(r11)
  - result: unused
- call 0x82907634 in 0x82906FE0: strings -
  - args: r3=stack buffer r1+80, r9=field 4(r10), r10=field 8(r10)
  - result: unused
- call 0x8290DBC8 in 0x8290D9B8: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x8290DBEC in 0x8290D9B8: strings -
  - args: r3=stack buffer r1+80, r9=field 4(r10), r10=field 8(r10)
  - result: unused
- call 0x82916504 in 0x82916118: strings -
  - args: r3=stack buffer r1+80, r9=field 4(r10), r10=field 8(r10)
  - result: unused
- call 0x82926F84 in 0x82926DB0: strings -
  - args: r3=stack buffer r1+80, r9=field 4(r10), r10=field 8(r10)
  - result: unused
- call 0x8292E83C in 0x8292E648: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x82937A80 in 0x82937918: strings -
  - args: r3=stack buffer r1+80, r9=field 4(r10), r10=field 8(r10)
  - result: unused
- call 0x82947FF8 in 0x82947E88: strings -
  - args: r3=stack buffer r1+80, r9=field 4(r10), r10=field 8(r10)
  - result: unused

## 0x826ADA00 (size 0x1A0)
- reaches: KeReleaseMutant, KeResetEvent, KeSetEvent, KeWaitForSingleObject, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x82887668 in 0x82887638: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0)
  - result: unused
- call 0x828876B4 in 0x82887680: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2)
  - result: unused
- call 0x8289AB58 in 0x8289AB40: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0)
  - result: unused
- call 0x8289ABBC in 0x8289AB70: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2)
  - result: unused
- call 0x828A50CC in 0x828A5058: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0)
  - result: unused
- call 0x828A6434 in 0x828A6338: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2)
  - result: unused
- call 0x828C73C4 in 0x828C7398: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2)
  - result: unused
- call 0x828C891C in 0x828C87E8: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0)
  - result: unused
- call 0x828C914C in 0x828C90F0: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0)
  - result: unused
- call 0x828C9194 in 0x828C9168: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2)
  - result: unused
- call 0x828CCA54 in 0x828CC9A8: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0)
  - result: unused
- call 0x828CE008 in 0x828CDF70: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2)
  - result: unused
- call 0x828D9554 in 0x828D9538: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0)
  - result: unused
- call 0x828D980C in 0x828D97E8: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2)
  - result: unused
- call 0x828DA294 in 0x828DA278: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0)
  - result: unused
- call 0x828DCDBC in 0x828DCD98: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2)
  - result: unused
- call 0x828E75DC in 0x828E75A8: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2)
  - result: unused
- call 0x828EC860 in 0x828EC210: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0)
  - result: unused
- call 0x828F13C4 in 0x828F1318: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0)
  - result: unused
- call 0x828F3140 in 0x828F30A8: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2)
  - result: unused
- call 0x828F3B78 in 0x828F3B60: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0)
  - result: unused
- call 0x828F73D4 in 0x828F73B8: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2)
  - result: unused
- call 0x828FA2E0 in 0x828FA2C8: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0)
  - result: unused
- call 0x828FA594 in 0x828FA550: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2)
  - result: unused
- call 0x82906734 in 0x829066D0: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0)
  - result: unused
- call 0x82906938 in 0x829068B8: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2)
  - result: unused
- call 0x8290DC90 in 0x8290DC00: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0)
  - result: unused
- call 0x8290E680 in 0x8290E560: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2)
  - result: unused
- call 0x82911F34 in 0x82911E90: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0)
  - result: unused
- call 0x82912858 in 0x82912428: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2)
  - result: unused
- call 0x829269DC in 0x829269C0: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0)
  - result: unused
- call 0x829278F0 in 0x829278D0: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2)
  - result: unused
- call 0x8292EC90 in 0x8292EC70: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0)
  - result: unused
- call 0x8292F078 in 0x8292EFF0: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2)
  - result: unused
- call 0x82937F98 in 0x82937E78: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0)
  - result: unused
- call 0x82938BC8 in 0x82938A88: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2)
  - result: unused
- call 0x8294311C in 0x829430A8: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0)
  - result: unused
- call 0x82944538 in 0x829444C8: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2)
  - result: unused
- call 0x82954CAC in 0x82954A80: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0)
  - result: unused
- call 0x82A4F4F4 in 0x82A4F460: strings -
  - args: r3=constant 0 (0x0)
  - result: unused
- call 0x82A50238 in 0x82A501C8: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0)
  - result: cmpwi r3,0

## 0x826ADBA0 (size 0x27C)
- reaches: KeReleaseMutant, KeResetEvent, KeSetEvent, KeWaitForSingleObject, RtlEnterCriticalSection, RtlLeaveCriticalSection, XamIsUIActive, XamXStudioRequest
- call 0x82A4D2F4 in 0x82A4D288: strings -
  - args: r4=r27+32, r9=field 16(r11), r10=constant 10 (0xA)
  - result: r14 = r3, stores r3 to 120(r1), cmpwi r3,0

## 0x826AE820 (size 0x154)
- reaches: pure (no imports reached)
- call 0x826CCD70 in 0x826CCCA0: strings -
  - args: r3=copy of r31
  - result: unused
- call 0x826D0DCC in 0x826D0C10: strings -
  - args: r3=stack buffer r1+96
  - result: unused
- call 0x826DCB28 in 0x826DC998: strings -
  - args: r3=stack buffer r1+96
  - result: unused
- call 0x826EA14C in 0x826EA088: strings -
  - args: r3=copy of r31
  - result: unused
- call 0x828227A4 in 0x82822718: strings -
  - args: -
  - result: unused

## 0x826AE978 (size 0xF4)
- reaches: pure (no imports reached)
- call 0x82A49404 in 0x82A48EC8: strings -
  - args: r3=stack buffer r1+160
  - result: unused
- call 0x82A4BDF4 in 0x82A4BDA0: strings -
  - args: r3=stack buffer r1+144, r9=static 0x821821B0
  - result: unused
- call 0x82A4BF28 in 0x82A4BED0: strings -
  - args: r3=stack buffer r1+112
  - result: unused
- call 0x82A4DC18 in 0x82A4D288: strings -
  - args: r3=stack buffer r1+144
  - result: unused

## 0x826AEDE8 (size 0x21C)
- reaches: DbgPrint, EtxProducerLog, RtlEnterCriticalSection, RtlLeaveCriticalSection, XamGetCurrentTitleId
- call 0x82A4B888 in 0x82A4B818: strings -
  - args: r3=constant 0 (0x0), r4=copy of r29
  - result: unused

## 0x826AF008 (size 0x22C)
- reaches: DbgPrint, EtxProducerLog, RtlEnterCriticalSection, RtlLeaveCriticalSection, XamGetCurrentTitleId
- call 0x82A4B3C8 in 0x82A4B3A0: strings -
  - args: -
  - result: unused
- call 0x82A4C3C4 in 0x82A4C380: strings -
  - args: -
  - result: unused
- call 0x82A4E588 in 0x82A4D288: strings -
  - args: -
  - result: unused
- call 0x82BF423C in 0x82BF41E0: strings -
  - args: -
  - result: unused

## 0x826AF238 (size 0x94)
- reaches: RtlInitializeCriticalSection
- call 0x82CACA3C in 0x82CACA28: strings -
  - args: r3=static 0x82D72788
  - result: unused

## 0x826AF6F0 (size 0x5C)
- reaches: XamNuiCameraElevationGetAngle
- call 0x82A4B1A0 in 0x82A4B180: strings -
  - args: r3=stack buffer r1+84, r4=stack buffer r1+80
  - result: cmpwi r3,0
- call 0x82A4B220 in 0x82A4B1F8: strings -
  - args: r3=stack buffer r1+80, r4=stack buffer r1+88
  - result: cmpwi r3,0

## 0x826AF7D8 (size 0xA4)
- reaches: XamNuiCameraElevationSetAngle, XexCheckExecutablePrivilege
- call 0x82A4B2F8 in 0x82A4B2D8: strings -
  - args: r3=field 84(r1)
  - result: cmpwi r3,0

## 0x826AF880 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x826CBD0C in 0x826CBC90: strings -
  - args: -
  - result: unused
- call 0x826CBEF0 in 0x826CBE10: strings -
  - args: -
  - result: cmpwi r3,0

## 0x826B0138 (size 0x1E4)
- reaches: ExGetXConfigSetting, KfAcquireSpinLock, KfReleaseSpinLock, XamNuiCameraElevationGetAngle, XamNuiCameraElevationSetAngle
- call 0x82A4B374 in 0x82A4B340: strings -
  - args: r3=constant 8 (0x8), r7=field -4432(r10), r8=field -4(r8), r9=static 0x821821B0, r10=constant 0x82DB0000; f1=loaded from 12(r9), f2=copy of f3, f3=loaded from 212(r9)
  - result: cmpwi r3,0, cmpwi r3,997

## 0x826B1980 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x826C7DA0 in 0x826C7D80: strings -
  - args: r3=constant 0 (0x0), r4=stack buffer r31+4128
  - result: unused

## 0x826B1A08 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x826C7DB0 in 0x826C7D80: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=stack buffer r31+4140
  - result: unused

## 0x826B1B00 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x826CAE84 in 0x826CAE58: strings -
  - args: r3=constant 0 (0x0), r4=copy of r26
  - result: r31 = r3
- call 0x826CAF44 in 0x826CAF18: strings -
  - args: r3=constant 0 (0x0), r4=copy of r26
  - result: r31 = r3
- call 0x826D24D4 in 0x826D2488: strings -
  - args: r3=constant 0 (0x0), r4=copy of r29
  - result: r30 = r3
- call 0x826D2720 in 0x826D25C0: strings -
  - args: r3=constant 0 (0x0), r4=copy of r29
  - result: r31 = r3

## 0x826B2018 (size 0x308)
- reaches: DbgPrint, KeWaitForSingleObject, RtlNtStatusToDosError
- call 0x826C7DC4 in 0x826C7D80: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 0 (0x0), r6=stack buffer r31+4156
  - result: unused
- call 0x826C7DD8 in 0x826C7D80: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 1 (0x1), r6=stack buffer r31+4164
  - result: unused
- call 0x826C7DEC in 0x826C7D80: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 2 (0x2), r6=stack buffer r31+4160
  - result: unused
- call 0x826C7E00 in 0x826C7D80: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 4 (0x4), r6=stack buffer r31+4168
  - result: unused
- call 0x826C7EA8 in 0x826C7D80: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 5 (0x5), r6=stack buffer r31+4172
  - result: unused
- call 0x826CAFB4 in 0x826CAF18: strings -
  - args: r3=constant 0 (0x0), r4=field 68(r30), r5=constant 2 (0x2), r6=copy of r25
  - result: r31 = r3
- call 0x826CBD38 in 0x826CBC90: strings -
  - args: r3=constant 0 (0x0), r4=field 68(r30), r5=constant 3 (0x3), r6=stack buffer r1+80
  - result: r31 = r3

## 0x826B2320 (size 0x274)
- reaches: KeWaitForSingleObject, RtlNtStatusToDosError
- call 0x826C7E14 in 0x826C7D80: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 5 (0x5), r6=stack buffer r31+4196
  - result: unused
- call 0x826C7E44 in 0x826C7D80: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 0 (0x0), r6=stack buffer r31+4188
  - result: unused
- call 0x826C7E58 in 0x826C7D80: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 6 (0x6), r6=stack buffer r31+4176
  - result: unused
- call 0x826C7E6C in 0x826C7D80: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 7 (0x7), r6=stack buffer r31+4180
  - result: unused
- call 0x826C7E80 in 0x826C7D80: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 8 (0x8), r6=stack buffer r31+4184
  - result: unused
- call 0x826CAEA0 in 0x826CAE58: strings -
  - args: r3=constant 0 (0x0), r4=field 68(r30), r5=constant 0 (0x0), r6=copy of r29
  - result: r31 = r3
- call 0x826CAEBC in 0x826CAE58: strings -
  - args: r3=constant 0 (0x0), r4=field 68(r30), r5=constant 6 (0x6), r6=copy of r28
  - result: r31 = r3
- call 0x826CAED8 in 0x826CAE58: strings -
  - args: r3=constant 0 (0x0), r4=field 68(r30), r5=constant 7 (0x7), r6=copy of r27
  - result: r31 = r3
- call 0x826CAEF4 in 0x826CAE58: strings -
  - args: r3=constant 0 (0x0), r4=field 68(r30), r5=constant 8 (0x8), r6=copy of r25
  - result: r31 = r3
- call 0x826CAF60 in 0x826CAF18: strings -
  - args: r3=constant 0 (0x0), r4=field 68(r30), r5=constant 6 (0x6), r6=copy of r29
  - result: r31 = r3
- call 0x826CAF7C in 0x826CAF18: strings -
  - args: r3=constant 0 (0x0), r4=field 68(r30), r5=constant 7 (0x7), r6=copy of r28
  - result: r31 = r3
- call 0x826CAF98 in 0x826CAF18: strings -
  - args: r3=constant 0 (0x0), r4=field 68(r30), r5=constant 8 (0x8), r6=copy of r27
  - result: r31 = r3
- call 0x826D24F0 in 0x826D2488: strings -
  - args: r4=field 0(r31), r5=constant 0 (0x0), r6=stack buffer r1+80
  - result: r30 = r3
- call 0x826D2530 in 0x826D2488: strings -
  - args: r3=constant 0 (0x0), r4=field 0(r31), r5=constant 7 (0x7), r6=stack buffer r1+84
  - result: r30 = r3
- call 0x826D254C in 0x826D2488: strings -
  - args: r4=field 0(r31), r5=constant 6 (0x6), r6=stack buffer r1+88
  - result: r30 = r3
- call 0x826D2568 in 0x826D2488: strings -
  - args: r4=field 0(r31), r5=constant 8 (0x8), r6=stack buffer r1+92
  - result: r30 = r3

## 0x826B2598 (size 0xDC)
- reaches: KfAcquireSpinLock, KfReleaseSpinLock
- call 0x826CAF04 in 0x826CAE58: strings -
  - args: r3=constant 0 (0x0), r4=copy of r26
  - result: unused
- call 0x826CAFC4 in 0x826CAF18: strings -
  - args: r3=constant 0 (0x0), r4=copy of r26
  - result: unused
- call 0x826D25B0 in 0x826D2488: strings -
  - args: r3=constant 0 (0x0), r4=copy of r29, r10=constant 0 (0x0)
  - result: unused
- call 0x826D2734 in 0x826D25C0: strings -
  - args: r3=constant 0 (0x0), r4=copy of r29
  - result: unused
- call 0x826D2774 in 0x826D25C0: strings -
  - args: -
  - result: unused

## 0x826B2798 (size 0x104)
- reaches: KfAcquireSpinLock, KfReleaseSpinLock
- call 0x826C7ED8 in 0x826C7EB8: strings -
  - args: r3=constant 0 (0x0), r4=stack buffer r31+4128
  - result: unused
- call 0x826C9168 in 0x826C8F70: strings -
  - args: r3=constant 0 (0x0), r4=stack buffer r1+88
  - result: unused

## 0x826B28A0 (size 0x5FC)
- reaches: DbgPrint, KeWaitForSingleObject, KfAcquireSpinLock, KfReleaseSpinLock, RtlNtStatusToDosError
- call 0x826C7CE0 in 0x826C7CC0: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 4 (0x4), r6=constant 0 (0x0)
  - result: r31 = r3
- call 0x826C7D28 in 0x826C7CC0: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 1 (0x1), r6=constant 0 (0x0)
  - result: r31 = r3
- call 0x826C7EFC in 0x826C7EB8: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 0 (0x0), r6=field 4156(r31)
  - result: unused
- call 0x826C7F10 in 0x826C7EB8: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 1 (0x1), r6=field 4164(r31)
  - result: unused
- call 0x826C7F24 in 0x826C7EB8: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 2 (0x2), r6=field 4160(r31)
  - result: unused
- call 0x826C7F38 in 0x826C7EB8: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 4 (0x4), r6=field 4168(r31)
  - result: unused
- call 0x826C7FE0 in 0x826C7EB8: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 5 (0x5), r6=field 4172(r31)
  - result: unused
- call 0x826CBCB4 in 0x826CBC90: strings -
  - args: r3=constant 0 (0x0), r4=field 68(r3), r5=constant 0 (0x0), r6=constant 1 (0x1)
  - result: r31 = r3
- call 0x826CBD70 in 0x826CBC90: strings -
  - args: r3=constant 0 (0x0), r4=field 68(r30), r5=constant 0 (0x0), r6=constant 0 (0x0)
  - result: r31 = r3
- call 0x826CBF20 in 0x826CBE10: strings -
  - args: r3=constant 0 (0x0), r4=field 68(r30), r5=constant 0 (0x0), r6=constant 2 (0x2)
  - result: unused

## 0x826B2EA0 (size 0xC8)
- reaches: KfAcquireSpinLock, KfReleaseSpinLock
- call 0x826D2744 in 0x826D25C0: strings -
  - args: r3=field 0(r30); f1=copy of f28
  - result: r31 = r3

## 0x826B2F68 (size 0x180)
- reaches: KfAcquireSpinLock, KfReleaseSpinLock
- call 0x826D2760 in 0x826D25C0: strings -
  - args: r3=field 0(r30); f1=copy of f30, f2=copy of f31, f3=copy of f29
  - result: r31 = r3

## 0x826B30E8 (size 0x260)
- reaches: KfAcquireSpinLock, KfReleaseSpinLock
- call 0x826C7D48 in 0x826C7CC0: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 5 (0x5); f1=loaded from -29680(r11)
  - result: r31 = r3
- call 0x826C7F4C in 0x826C7EB8: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 5 (0x5); f1=loaded from 4196(r31)
  - result: unused
- call 0x826C7F7C in 0x826C7EB8: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 0 (0x0); f1=loaded from 4188(r31)
  - result: unused
- call 0x826C7F90 in 0x826C7EB8: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 6 (0x6); f1=loaded from 4176(r31)
  - result: unused
- call 0x826C7FA4 in 0x826C7EB8: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 7 (0x7); f1=loaded from 4180(r31)
  - result: unused
- call 0x826C7FB8 in 0x826C7EB8: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 8 (0x8); f1=loaded from 4184(r31)
  - result: unused

## 0x826B3348 (size 0x124)
- reaches: KfAcquireSpinLock, KfReleaseSpinLock
- call 0x826C7EE8 in 0x826C7EB8: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=stack buffer r31+4140
  - result: unused

## 0x826B3470 (size 0x5C)
- reaches: KfAcquireSpinLock, KfReleaseSpinLock
- call 0x82A4BA78 in 0x82A4B930: strings -
  - args: r3=stack buffer r1+88
  - result: unused

## 0x826B34D0 (size 0x58)
- reaches: DbgPrint, KeWaitForSingleObject, KfAcquireSpinLock, KfReleaseSpinLock, RtlNtStatusToDosError
- call 0x828991E4 in 0x828991B8: strings -
  - args: r5=constant 1 (0x1)
  - result: unused
- call 0x82899220 in 0x828991B8: strings -
  - args: r5=constant 0 (0x0)
  - result: unused
- call 0x82899DB0 in 0x82899CD0: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 0 (0x0)
  - result: unused
- call 0x8289A3CC in 0x8289A3B0: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 1 (0x1)
  - result: unused
- call 0x8291B184 in 0x8291B158: strings -
  - args: r5=constant 1 (0x1)
  - result: unused
- call 0x8291B1C0 in 0x8291B158: strings -
  - args: r5=constant 0 (0x0)
  - result: unused
- call 0x8291BE6C in 0x8291BE50: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 1 (0x1)
  - result: unused
- call 0x8291C390 in 0x8291C2B0: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 0 (0x0)
  - result: unused
- call 0x8291D69C in 0x8291D670: strings -
  - args: r5=constant 1 (0x1)
  - result: unused
- call 0x8291D6D8 in 0x8291D670: strings -
  - args: r5=constant 0 (0x0)
  - result: unused
- call 0x8291DADC in 0x8291DAC0: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 1 (0x1)
  - result: unused
- call 0x82935A6C in 0x82935A40: strings -
  - args: r5=constant 1 (0x1)
  - result: unused
- call 0x82935AA8 in 0x82935A40: strings -
  - args: r5=constant 0 (0x0)
  - result: unused
- call 0x82937E94 in 0x82937E78: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 1 (0x1)
  - result: unused
- call 0x8293D1FC in 0x8293D1D0: strings -
  - args: r5=constant 1 (0x1)
  - result: unused
- call 0x8293D238 in 0x8293D1D0: strings -
  - args: r5=constant 0 (0x0)
  - result: unused
- call 0x8293EA0C in 0x8293E9F0: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 1 (0x1)
  - result: unused

## 0x826B3528 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x829D0524 in 0x829D04E8: strings -
  - args: -
  - result: unused

## 0x826B3538 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x826C4178 in 0x826C4088: strings -
  - args: -
  - result: r5 = r3
- call 0x826D5D3C in 0x826D5CD8: strings -
  - args: -
  - result: r5 = r3

## 0x826B3548 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x826C6148 in 0x826C6000: strings -
  - args: r3=copy of r31, r4=field 20(r24)
  - result: low byte (BOOL)
- call 0x826C63F0 in 0x826C6378: strings -
  - args: r3=copy of r31, r4=field 20(r30)
  - result: low byte (BOOL)
- call 0x826C64B0 in 0x826C6438: strings -
  - args: r3=copy of r30, r4=field 20(r31)
  - result: low byte (BOOL)
- call 0x826C6E3C in 0x826C6DC8: strings -
  - args: r4=field 20(r26)
  - result: low byte (BOOL)

## 0x826B3580 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x826C2B0C in 0x826C2AD8: strings -
  - args: r3=copy of r29
  - result: r4 = r3

## 0x826B3690 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x826C0B14 in 0x826C0A20: strings -
  - args: r3=field 8(r11), r4=field 4(r31), r6=copy of r22, r8=stack buffer r1+88, r9=stack buffer r1+80
  - result: unused
- call 0x826C0BD4 in 0x826C0A20: strings -
  - args: r3=field 8(r11), r4=field 4(r31), r6=copy of r22, r8=stack buffer r1+88, r9=stack buffer r1+80
  - result: unused
- call 0x826C0CA0 in 0x826C0A20: strings -
  - args: r3=field 8(r11), r4=field 4(r31), r6=copy of r22, r8=stack buffer r1+88, r9=stack buffer r1+80
  - result: unused
- call 0x826C0D60 in 0x826C0A20: strings -
  - args: r3=field 8(r11), r4=field 4(r31), r6=copy of r22, r8=stack buffer r1+88, r9=stack buffer r1+80
  - result: unused
- call 0x826C18A4 in 0x826C17B0: strings -
  - args: r3=constant 2 (0x2), r4=copy of r30, r5=field 128(r1), r6=field 132(r1), r8=stack buffer r1+112, r9=stack buffer r1+116
  - result: unused
- call 0x826C18C4 in 0x826C17B0: strings -
  - args: r3=constant 2 (0x2), r4=copy of r30, r5=field 136(r1), r6=field 140(r1), r8=stack buffer r1+120, r9=stack buffer r1+124
  - result: unused
- call 0x826C1AB8 in 0x826C1A20: strings -
  - args: r3=field 16(r29), r4=copy of r25, r5=field -4(r31), r6=field 0(r31), r8=copy of r30, r9=r30+4
  - result: unused

## 0x826B3DB0 (size 0x74)
- reaches: KeWaitForSingleObject
- call 0x826C23B8 in 0x826C23A0: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x826C24B4 in 0x826C2448: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x826C27F8 in 0x826C2788: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x826C2C30 in 0x826C2BC0: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x826C58E4 in 0x826C58D0: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x826C6118 in 0x826C6000: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x826C63B8 in 0x826C6378: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x826C6454 in 0x826C6438: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x826C68F8 in 0x826C67B0: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x826C69B8 in 0x826C6998: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x826C6A34 in 0x826C6998: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x826C6DF0 in 0x826C6DC8: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x826C7360 in 0x826C72F8: strings -
  - args: r3=stack buffer r1+80
  - result: unused

## 0x826B3E28 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x826C241C in 0x826C23A0: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x826C25B0 in 0x826C2448: strings -
  - args: r3=stack buffer r1+80, r10=field 0(r11)
  - result: unused
- call 0x826C2840 in 0x826C2788: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x826C2864 in 0x826C2788: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x826C2C6C in 0x826C2BC0: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x826C591C in 0x826C58D0: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x826C6184 in 0x826C6000: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x826C61E4 in 0x826C6000: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x826C6418 in 0x826C6378: strings -
  - args: r3=stack buffer r1+80, r10=constant 1 (0x1)
  - result: unused
- call 0x826C6488 in 0x826C6438: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x826C6508 in 0x826C6438: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x826C6944 in 0x826C67B0: strings -
  - args: -
  - result: unused
- call 0x826C696C in 0x826C67B0: strings -
  - args: -
  - result: unused
- call 0x826C6A18 in 0x826C6998: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x826C6A74 in 0x826C6998: strings -
  - args: r3=stack buffer r1+80, r10=field 220(r31)
  - result: unused
- call 0x826C6E2C in 0x826C6DC8: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x826C6E58 in 0x826C6DC8: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x826C739C in 0x826C72F8: strings -
  - args: r10=constant 1 (0x1)
  - result: unused
- call 0x826C73BC in 0x826C72F8: strings -
  - args: -
  - result: unused

## 0x826B4260 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x826C0DDC in 0x826C0D88: strings -
  - args: r3=stack buffer r1+96, r4=field 28480(r28), r5=copy of r31, r6=copy of r29
  - result: unused
- call 0x826C7214 in 0x826C7028: strings -
  - args: r3=stack buffer r1+112, r4=constant 102 (0x66), r5=copy of r20, r6=constant 0x00000000
  - result: unused

## 0x826B44B8 (size 0x50)
- reaches: pure (no imports reached)
- call 0x826C0DBC in 0x826C0D88: strings -
  - args: r3=stack buffer r1+80, r4=copy of r29
  - result: unused
- call 0x826C17E8 in 0x826C17B0: strings -
  - args: r3=stack buffer r1+80, r4=constant 0x00040000
  - result: unused
- call 0x826C28C0 in 0x826C2878: strings -
  - args: r3=stack buffer r1+84
  - result: unused
- call 0x826C2BF4 in 0x826C2BC0: strings -
  - args: r3=stack buffer r1+84, r4=constant 0x00010000
  - result: unused
- call 0x826C5C2C in 0x826C5BD8: strings -
  - args: r3=stack buffer r1+80, r10=r11-1
  - result: unused
- call 0x826C6244 in 0x826C61F0: strings -
  - args: r3=stack buffer r1+92, r4=constant 0x00060000
  - result: unused
- call 0x826C66D0 in 0x826C66B8: strings -
  - args: r3=stack buffer r1+80, r4=constant 0x00020000
  - result: unused
- call 0x826C67F8 in 0x826C67B0: strings -
  - args: r3=stack buffer r1+88, r4=copy of r26
  - result: unused
- call 0x826C6E6C in 0x826C6DC8: strings -
  - args: r3=stack buffer r1+100, r4=copy of r31
  - result: unused
- call 0x826C6E78 in 0x826C6DC8: strings -
  - args: r3=stack buffer r1+96, r4=copy of r31
  - result: unused
- call 0x826C707C in 0x826C7028: strings -
  - args: r3=stack buffer r1+96, r4=copy of r31
  - result: unused
- call 0x826C708C in 0x826C7028: strings -
  - args: r3=stack buffer r1+92, r4=constant 0x00030000
  - result: unused
- call 0x826C709C in 0x826C7028: strings -
  - args: r3=stack buffer r1+80, r4=constant 0x00000000
  - result: unused
- call 0x826D5BF0 in 0x826D5B98: strings -
  - args: r3=stack buffer r1+80, r4=constant 0x00010000
  - result: unused

## 0x826B4508 (size 0x90)
- reaches: pure (no imports reached)
- call 0x826C1B0C in 0x826C1A20: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x826C1B3C in 0x826C1A20: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x826C1B84 in 0x826C1A20: strings -
  - args: r3=stack buffer r1+80
  - result: unused

## 0x826B4728 (size 0xA0)
- reaches: pure (no imports reached)
- call 0x826C02F4 in 0x826C0280: strings -
  - args: r3=copy of r30, r4=stack buffer r1+80
  - result: unused

## 0x826B49E8 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x826C6934 in 0x826C67B0: strings -
  - args: -
  - result: low byte (BOOL)

## 0x826B5CC0 (size 0x84)
- reaches: pure (no imports reached)
- call 0x826C2560 in 0x826C2448: strings -
  - args: r3=copy of r31, r4=constant 1 (0x1), r5=constant 4 (0x4), r10=constant 0 (0x0)
  - result: unused
- call 0x826C2CF0 in 0x826C2BC0: strings -
  - args: r4=constant 1 (0x1), r5=constant 2 (0x2)
  - result: r31 = r3

## 0x826B5D48 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x826C2B30 in 0x826C2AD8: strings -
  - args: r3=copy of r29, r5=copy of r31
  - result: unused

## 0x826B7BB8 (size 0x3C)
- reaches: RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x826C65D8 in 0x826C6588: strings -
  - args: r3=copy of r29, r10=constant 0x82D70000
  - result: unused
- call 0x826C6708 in 0x826C66B8: strings -
  - args: r3=copy of r31
  - result: unused
- call 0x826C67D8 in 0x826C67B0: strings -
  - args: r3=copy of r29
  - result: unused
- call 0x826C7318 in 0x826C72F8: strings -
  - args: -
  - result: unused

## 0x826B7BF8 (size 0x80)
- reaches: RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x826C65E0 in 0x826C6588: strings -
  - args: r3=copy of r29
  - result: unused
- call 0x826C664C in 0x826C6588: strings -
  - args: r3=copy of r29
  - result: r30 = r3, cmplwi r3,0
- call 0x826C6710 in 0x826C66B8: strings -
  - args: r3=copy of r31
  - result: cmplwi r3,0
- call 0x826C674C in 0x826C66B8: strings -
  - args: r3=copy of r31
  - result: cmplwi r3,0
- call 0x826C67E0 in 0x826C67B0: strings -
  - args: r3=copy of r29
  - result: r30 = r3
- call 0x826C68B8 in 0x826C67B0: strings -
  - args: r3=copy of r29
  - result: r30 = r3
- call 0x826C7324 in 0x826C72F8: strings -
  - args: -
  - result: r31 = r3, cmplwi r3,0
- call 0x826C73A8 in 0x826C72F8: strings -
  - args: -
  - result: r31 = r3, cmplwi r3,0

## 0x826B84E0 (size 0xC80)
- reaches: EtxProducerLog, KeReleaseMutant, KeWaitForSingleObject, XamGetActiveDashAppInfo, XamGetCurrentTitleId, XamNuiIdentityGetSessionId
- call 0x826C2BAC in 0x826C2AD8: strings -
  - args: r3=constant 0 (0x0), r4=copy of r31
  - result: unused

## 0x826BB540 (size 0x6C)
- reaches: pure (no imports reached)
- call 0x826CB9CC in 0x826CB940: strings -
  - args: -
  - result: cmplwi r3,0
- call 0x826CB9E0 in 0x826CB940: strings -
  - args: -
  - result: unused
- call 0x826CBFE4 in 0x826CBF58: strings -
  - args: r3=copy of r31
  - result: unused
- call 0x826D2D40 in 0x826D2D20: strings -
  - args: -
  - result: unused
- call 0x826D2E1C in 0x826D2DD8: strings -
  - args: -
  - result: unused
- call 0x826D36C8 in 0x826D3638: strings -
  - args: r3=copy of r31
  - result: unused

## 0x826BB778 (size 0x60)
- reaches: pure (no imports reached)
- call 0x826C7D64 in 0x826C7CC0: strings -
  - args: r3=constant 0 (0x0)
  - result: unused
- call 0x826C8FA4 in 0x826C8F70: strings -
  - args: r3=constant 0 (0x0)
  - result: cmpwi r3,0

## 0x826BB808 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x826C9010 in 0x826C8F70: strings -
  - args: r3=constant 0 (0x0), r6=r30+4124; f1=loaded from 12(r28), f2=loaded from 20(r28)
  - result: cmpwi r3,0

## 0x826BB838 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x826C902C in 0x826C8F70: strings -
  - args: r3=constant 0 (0x0)
  - result: cmpwi r3,0

## 0x826BB868 (size 0x68)
- reaches: pure (no imports reached)
- call 0x826C8B70 in 0x826C88D8: strings -
  - args: r3=constant 0 (0x0), r4=copy of r21, r5=copy of r26, r6=copy of r28, r7=copy of r27, r8=constant 640 (0x280), r9=copy of r29; f1=loaded from 4120(r31)
  - result: unused

## 0x826BB960 (size 0x4C)
- reaches: pure (no imports reached)
- call 0x826C22A8 in 0x826C2208: strings -
  - args: -
  - result: unused

## 0x826BB9B0 (size 0x4C)
- reaches: pure (no imports reached)
- call 0x826C22DC in 0x826C2208: strings -
  - args: r3=constant 1 (0x1), r4=stack buffer r31+96, r5=stack buffer r31+84, r6=stack buffer r31+88, r7=stack buffer r31+92
  - result: unused

## 0x826BBA00 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x826C8D40 in 0x826C88D8: strings -
  - args: r3=constant 0 (0x0)
  - result: low byte (BOOL)

## 0x826BBAF0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x826C7D5C in 0x826C7CC0: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2)
  - result: unused

## 0x826BBB60 (size 0x94)
- reaches: pure (no imports reached)
- call 0x826D4EAC in 0x826D4E60: strings -
  - args: r3=field 12(r3), r4=constant 0 (0x0), r5=stack buffer r1+84, r6=constant 4 (0x4), r7=stack buffer r1+80
  - result: cmpwi r3,0
- call 0x826D4EE0 in 0x826D4E60: strings -
  - args: r3=field 12(r31), r4=constant 1 (0x1), r5=stack buffer r31+32, r6=constant 120 (0x78), r7=stack buffer r1+80
  - result: cmpwi r3,0

## 0x826BCE70 (size 0x9C)
- reaches: pure (no imports reached)
- call 0x826C6574 in 0x826C6438: strings -
  - args: r3=static 0x00010A90, r10=field 16(r31)
  - result: cmpwi r3,0

## 0x826BD088 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x826C2700 in 0x826C2618: strings -
  - args: r3=copy of r26, r10=field 16(r30)
  - result: stores r3 to 64(r31)

## 0x826BD440 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x826C6AA8 in 0x826C6A80: strings -
  - args: -
  - result: unused
- call 0x826C6ABC in 0x826C6A80: strings -
  - args: -
  - result: unused
- call 0x826E455C in 0x826E4520: strings -
  - args: -
  - result: unused

## 0x826BD508 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x826C6664 in 0x826C6588: strings -
  - args: r3=copy of r26, r4=stack buffer r1+88
  - result: unused

## 0x826BD650 (size 0xB4)
- reaches: pure (no imports reached)
- call 0x826C6B28 in 0x826C6A80: strings -
  - args: r3=copy of r27, r4=stack buffer r1+80
  - result: unused

## 0x826BDC00 (size 0x48)
- reaches: pure (no imports reached)
- call 0x826C6178 in 0x826C6000: strings -
  - args: r3=r3-29424, r4=stack buffer r1+96, r5=stack buffer r1+84, r10=field 16(r24)
  - result: unused

## 0x826BDC48 (size 0xE8)
- reaches: pure (no imports reached)
- call 0x826C1E08 in 0x826C1D38: strings -
  - args: r3=copy of r28, r4=field 16(r30), r5=field 12(r30)
  - result: unused

## 0x826BDD30 (size 0xA0)
- reaches: pure (no imports reached)
- call 0x826C2730 in 0x826C2618: strings -
  - args: r3=copy of r26, r4=copy of r28, r5=copy of r27
  - result: unused

## 0x826BDDD0 (size 0xB4)
- reaches: pure (no imports reached)
- call 0x826C2A90 in 0x826C2A08: strings -
  - args: r3=r3-32128, r4=copy of r30, r5=copy of r11, r9=field 16(r6), r10=constant 0x00010000
  - result: cmpwi r3,0

## 0x826BDE88 (size 0xE4)
- reaches: pure (no imports reached)
- call 0x826C6FA0 in 0x826C6DC8: strings -
  - args: r3=copy of r24, r4=constant 0 (0x0)
  - result: unused

## 0x826BDFB8 (size 0x44)
- reaches: pure (no imports reached)
- call 0x826C6108 in 0x826C6000: strings -
  - args: r3=stack buffer r1+96, r4=r4-32000, r5=stack buffer r1+84, r9=constant 0x00000000, r10=field 11324(r27)
  - result: unused

## 0x826BE248 (size 0x29C)
- reaches: pure (no imports reached)
- call 0x826C7230 in 0x826C7028: strings -
  - args: r3=stack buffer r1+112, r4=field 0(r25), r5=constant 20 (0x14), r6=stack buffer r1+80, r7=stack buffer r1+144
  - result: unused

## 0x826BE660 (size 0x120)
- reaches: pure (no imports reached)
- call 0x826C6C5C in 0x826C6B60: strings -
  - args: r3=copy of r24, r4=stack buffer r1+80, r5=stack buffer r1+512, r10=constant 2004 (0x7D4)
  - result: r21 = r3

## 0x826BEA50 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x826C01CC in 0x826C00E8: strings -
  - args: r9=field 20(r3), r10=field 84(r1)
  - result: unused

## 0x826BF098 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x826C271C in 0x826C2618: strings -
  - args: r3=copy of r30
  - result: stores r3 to 72(r31)

## 0x826BF858 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x826C2760 in 0x826C2618: strings -
  - args: r3=copy of r30
  - result: unused

## 0x826BF938 (size 0xC4)
- reaches: pure (no imports reached)
- call 0x826C2ACC in 0x826C2A08: strings -
  - args: r3=copy of r29, r5=field 8(r31)
  - result: unused

## 0x826BFA58 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x826C0B24 in 0x826C0A20: strings -
  - args: r3=field 0(r31), r4=field 88(r1), r5=field 80(r1)
  - result: unused
- call 0x826C0BE4 in 0x826C0A20: strings -
  - args: r3=field 0(r31), r4=field 88(r1), r5=field 80(r1)
  - result: unused
- call 0x826C0CB0 in 0x826C0A20: strings -
  - args: r3=field 0(r31), r4=field 88(r1), r5=field 80(r1)
  - result: unused
- call 0x826C0D70 in 0x826C0A20: strings -
  - args: r3=field 0(r31), r4=field 88(r1), r5=field 80(r1)
  - result: unused

## 0x826BFAD0 (size 0x6C)
- reaches: pure (no imports reached)
- call 0x826C0258 in 0x826C01F0: strings -
  - args: r4=copy of r28, r5=copy of r30, r10=constant 20 (0x14)
  - result: unused

## 0x826BFB40 (size 0x74)
- reaches: pure (no imports reached)
- call 0x826C02E8 in 0x826C0280: strings -
  - args: r3=stack buffer r1+80, r4=copy of r29, r5=copy of r31
  - result: unused

## 0x826BFBD8 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x826C2710 in 0x826C2618: strings -
  - args: r3=copy of r29
  - result: stores r3 to 68(r31)

## 0x826BFD40 (size 0xF4)
- reaches: pure (no imports reached)
- call 0x826C01E0 in 0x826C00E8: strings -
  - args: r3=copy of r31, r4=copy of r30, r5=field 32(r3), r6=copy of r29
  - result: unused

## 0x826BFE38 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x826C0038 in 0x826BFFB8: strings -
  - args: r3=copy of r20
  - result: unused
- call 0x826C00B8 in 0x826BFFB8: strings -
  - args: r3=copy of r20
  - result: unused

## 0x826BFFB8 (size 0x130)
- reaches: pure (no imports reached)
- call 0x826C0434 in 0x826C03A0: strings -
  - args: r3=copy of r30, r4=copy of r29, r5=copy of r31, r6=copy of r28, r7=copy of r27, r8=stack buffer r1+80
  - result: unused

## Referenced only from data
0x826A0C50, 0x826A0D10, 0x826A0E70, 0x826A0F48, 0x826A10A8, 0x826A1170, 0x826A1480, 0x826A1938, 0x826A2048, 0x826A2098, 0x826A2110, 0x826A21E8, 0x826A22C8, 0x826A2338, 0x826A2528, 0x826A2638, 0x826A27A8, 0x826A2C98, 0x826A2E18, 0x826A2E88, 0x826A2EF8, 0x826A2F48, 0x826A2FE0, 0x826A3050, 0x826A3170, 0x826A3268, 0x826AC3C8, 0x826ACEE0
