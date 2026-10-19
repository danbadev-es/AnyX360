# Entry points of 0x82D00000-0x82D50000 called from outside it

60 entry points; 32 more functions of the range are only referenced from data.

## 0x82D08D88 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82CFC91C in 0x82CFC898: strings -
  - args: -; f1=copy of f31
  - result: unused

## 0x82D097B0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82CFC98C in 0x82CFC958: strings -
  - args: -
  - result: unused

## 0x82D09A38 (size 0x68)
- reaches: pure (no imports reached)
- call 0x82CFC97C in 0x82CFC958: strings -
  - args: -
  - result: cmplwi r3,0

## 0x82D09AA0 (size 0x58)
- reaches: pure (no imports reached)
- call 0x82CFC75C in 0x82CFC740: strings -
  - args: -
  - result: unused

## 0x82D09AF8 (size 0xB0)
- reaches: pure (no imports reached)
- call 0x82CFC8D4 in 0x82CFC898: strings -
  - args: -
  - result: unused

## 0x82D09D28 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82CFC9D0 in 0x82CFC958: strings -
  - args: -
  - result: unused

## 0x82D09FC0 (size 0x68)
- reaches: pure (no imports reached)
- call 0x82CFC9C0 in 0x82CFC958: strings -
  - args: r4=copy of r30
  - result: cmplwi r3,0

## 0x82D0A028 (size 0x58)
- reaches: pure (no imports reached)
- call 0x82CFC770 in 0x82CFC740: strings -
  - args: -
  - result: unused

## 0x82D0A080 (size 0xB0)
- reaches: pure (no imports reached)
- call 0x82CFC8E0 in 0x82CFC898: strings -
  - args: r3=field 12(r31)
  - result: unused

## 0x82D23E30 (size 0x98)
- reaches: E#483, E#686, EtxProducerUnregister, KeSetBasePriorityThread, KeSetEvent, KeWaitForSingleObject, NtClose, NtSetEvent, ObDereferenceObject, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x82A53BC8 in 0x82A53AF0: strings -
  - args: -
  - result: unused

## 0x82D25C88 (size 0x158)
- reaches: E#1CF, E#25A, E#269, E#3D1, E#482, E#483, E#686, E#869, E#86D, E#873, EtxProducerRegister, EtxProducerUnregister, ExCreateThread, KeResumeThread, KeSetAffinityThread, KeSetBasePriorityThread, KeSetEvent, KeWaitForSingleObject, KfAcquireSpinLock, KfReleaseSpinLock, NtClose, NtCreateEvent, NtSetEvent, ObCreateObject, ObDereferenceObject, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlInitializeCriticalSection, RtlLeaveCriticalSection, RtlNtStatusToDosError
- call 0x82A561A4 in 0x82A56130: strings -
  - args: r3=constant 2121 (0x849), r4=constant 5 (0x5), r10=constant 0x00270000
  - result: r30 = r3

## 0x82D25DE0 (size 0xB0)
- reaches: KeSetEvent, ObDereferenceObject, RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x82A54298 in 0x82A54218: strings -
  - args: -
  - result: unused

## 0x82D25EF8 (size 0xEC)
- reaches: RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x82B6D838 in 0x82B6D7D8: strings -
  - args: r3=stack buffer r1+80, r8=static 0x83AB9F90, r10=field 744(r31)
  - result: unused
- call 0x82B6D8B0 in 0x82B6D888: strings -
  - args: r3=stack buffer r1+80, r10=copy of r3
  - result: unused

## 0x82D26190 (size 0x118)
- reaches: pure (no imports reached)
- call 0x82A558C0 in 0x82A55830: strings -
  - args: r3=copy of r19, r4=constant 0 (0x0)
  - result: unused

## 0x82D26620 (size 0x148)
- reaches: KeSetEvent, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x82A54288 in 0x82A54218: strings "SpringfieldFrameEvent"
  - args: r4=constant 3 (0x3)
  - result: cmpwi r3,0

## 0x82D26768 (size 0x1F0)
- reaches: E#686, KeResetEvent, KeWaitForSingleObject, RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x82A5531C in 0x82A55118: strings -
  - args: r3=copy of r9, r4=static 0x00277D00, r10=constant 0x00010000
  - result: r28 = r3

## 0x82D270F8 (size 0xF4)
- reaches: pure (no imports reached)
- call 0x82A50608 in 0x82A50500: strings -
  - args: -
  - result: unused
- call 0x82B645D4 in 0x82B64358: strings -
  - args: r3=stack buffer r1+224, r6=static 0x821911C0, r7=constant 0x82190000
  - result: unused
- call 0x82B716AC in 0x82B71530: strings -
  - args: r3=stack buffer r1+240, r8=static 0x821911C0, r9=constant 0x82190000
  - result: unused

## 0x82D27608 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82B763CC in 0x82B76338: strings -
  - args: r3=stack buffer r1+96
  - result: cmpwi r3,0

## 0x82D27688 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82A590B0 in 0x82A59070: strings -
  - args: r3=constant 0 (0x0), r4=stack buffer r1+96, r9=static 0x821328F0
  - result: unused

## 0x82D27F70 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82A590C0 in 0x82A59070: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=stack buffer r1+80
  - result: unused
- call 0x82A590F0 in 0x82A59070: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2), r5=stack buffer r1+84
  - result: unused
- call 0x82A84674 in 0x82A84650: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=stack buffer r1+80
  - result: unused
- call 0x82A84734 in 0x82A84650: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2), r5=stack buffer r1+84
  - result: unused
- call 0x82A847AC in 0x82A84780: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=stack buffer r1+80
  - result: unused

## 0x82D27F80 (size 0x1DC)
- reaches: KeWaitForSingleObject, RtlNtStatusToDosError
- call 0x82A59100 in 0x82A59070: strings -
  - args: r3=constant 0 (0x0), r4=constant 5 (0x5), r5=stack buffer r1+88
  - result: unused
- call 0x82A84600 in 0x82A845D0: strings -
  - args: r3=constant 0 (0x0), r4=copy of r3, r5=stack buffer r1+80
  - result: unused

## 0x82D28A08 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82A84874 in 0x82A84780: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2), r5=constant 0 (0x0)
  - result: r31 = r3
- call 0x82A84980 in 0x82A848C8: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2), r5=field 20(r31)
  - result: r30 = r3
- call 0x82A84AE0 in 0x82A84A30: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 1 (0x1)
  - result: r30 = r3
- call 0x82A84AF4 in 0x82A84A30: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2), r5=constant 0 (0x0)
  - result: r29 = r3
- call 0x82A84B54 in 0x82A84A30: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=field 64(r31)
  - result: r29 = r3
- call 0x82A84BA8 in 0x82A84A30: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=field 20(r31)
  - result: r29 = r3
- call 0x82A84C00 in 0x82A84A30: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 0 (0x0)
  - result: unused
- call 0x82A84C18 in 0x82A84A30: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 1 (0x1)
  - result: unused
- call 0x82A84C30 in 0x82A84A30: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 0 (0x0)
  - result: unused

## 0x82D28A18 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82A847DC in 0x82A84780: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r10=static 0x821328F0; f1=loaded from 312(r10)
  - result: r31 = r3
- call 0x82A847F8 in 0x82A84780: strings -
  - args: r3=constant 0 (0x0), r4=constant 6 (0x6), r9=constant 0x82130000; f1=copy of f31
  - result: r30 = r3
- call 0x82A8480C in 0x82A84780: strings -
  - args: r3=constant 0 (0x0), r4=constant 7 (0x7); f1=copy of f31
  - result: r29 = r3
- call 0x82A84820 in 0x82A84780: strings -
  - args: r3=constant 0 (0x0), r4=constant 8 (0x8); f1=copy of f31
  - result: unused
- call 0x82A8488C in 0x82A84780: strings -
  - args: r3=constant 0 (0x0), r4=constant 5 (0x5); f1=loaded from 17484(r11)
  - result: unused
- call 0x82A848F4 in 0x82A848C8: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0); f1=loaded from 4(r31)
  - result: r30 = r3
- call 0x82A84908 in 0x82A848C8: strings -
  - args: r3=constant 0 (0x0), r4=constant 6 (0x6); f1=loaded from 8(r31)
  - result: r29 = r3
- call 0x82A8491C in 0x82A848C8: strings -
  - args: r3=constant 0 (0x0), r4=constant 7 (0x7); f1=loaded from 12(r31)
  - result: r28 = r3
- call 0x82A84930 in 0x82A848C8: strings -
  - args: r3=constant 0 (0x0), r4=constant 8 (0x8); f1=loaded from 16(r31)
  - result: unused
- call 0x82A84994 in 0x82A848C8: strings -
  - args: r3=constant 0 (0x0), r4=constant 5 (0x5); f1=loaded from 24(r31)
  - result: unused
- call 0x82A84B0C in 0x82A84A30: strings -
  - args: r3=constant 0 (0x0), r4=constant 5 (0x5); f1=loaded from 17484(r11)
  - result: unused

## 0x82D28A28 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82A84B84 in 0x82A84A30: strings -
  - args: r3=constant 0 (0x0), r4=stack buffer r31+92
  - result: unused
- call 0x82A84BD8 in 0x82A84A30: strings -
  - args: r3=constant 0 (0x0), r4=stack buffer r31+48
  - result: unused

## 0x82D28E28 (size 0x190)
- reaches: RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x82B761D0 in 0x82B760F8: strings -
  - args: r3=constant 2 (0x2), r4=copy of r23, r5=field 80(r1), r6=field 84(r1), r8=copy of r31, r9=copy of r25
  - result: unused
- call 0x82B761F8 in 0x82B760F8: strings -
  - args: r3=constant 2 (0x2), r4=copy of r23, r5=field 96(r1), r6=field 100(r1), r8=copy of r29, r9=copy of r27
  - result: unused

## 0x82D28FB8 (size 0x310)
- reaches: KeResetEvent, KeWaitForSingleObject, ObDereferenceObject, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlLeaveCriticalSection, RtlNtStatusToDosError
- call 0x82A54308 in 0x82A542C8: strings -
  - args: r4=constant 0 (0x0), r5=stack buffer r1+80, r10=constant 0x00010000
  - result: cmpwi r3,0
- call 0x82A54624 in 0x82A545B8: strings -
  - args: r4=constant 0 (0x0), r5=copy of r29
  - result: low byte (BOOL)

## 0x82D292C8 (size 0x14C)
- reaches: ObDereferenceObject, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlLeaveCriticalSection, RtlNtStatusToDosError
- call 0x82A54458 in 0x82A542C8: strings -
  - args: r4=field 80(r1), r7=constant 1 (0x1), r8=r11-28092
  - result: unused
- call 0x82A55914 in 0x82A55830: strings -
  - args: r3=field -12(r30)
  - result: unused

## 0x82D2AAB0 (size 0x8C4)
- reaches: KeSetEvent, KfAcquireSpinLock, KfReleaseSpinLock, ObCreateObject, ObDereferenceObject, ObOpenObjectByPointer, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlLeaveCriticalSection, RtlNtStatusToDosError
- call 0x82A53CFC in 0x82A53BF8: strings -
  - args: r3=constant 1 (0x1), r4=constant 2 (0x2), r5=constant 0 (0x0), r6=constant 1 (0x1), r7=constant 0 (0x0), r8=static 0x00009224
  - result: cmpwi r3,0
- call 0x82A53FA8 in 0x82A53E90: strings -
  - args: r3=constant 0 (0x0), r4=constant 1 (0x1), r5=constant 0 (0x0), r6=constant 1 (0x1), r7=constant 0 (0x0), r8=static 0x00009220
  - result: cmpwi r3,0
- call 0x82A53FF8 in 0x82A53E90: strings -
  - args: r3=constant 7 (0x7), r4=constant 0 (0x0), r5=constant 0 (0x0), r6=constant 1 (0x1), r7=constant 0 (0x0), r8=static 0x0000921C
  - result: cmpwi r3,0

## 0x82D2B678 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82A59C6C in 0x82A59C10: strings -
  - args: r3=stack buffer r1+84, r4=stack buffer r1+80
  - result: cmpwi r3,0
- call 0x82A5FE1C in 0x82A5FB38: strings -
  - args: r3=stack buffer r1+84, r4=stack buffer r1+80
  - result: unused
- call 0x82A60190 in 0x82A60068: strings -
  - args: r3=stack buffer r1+80, r4=stack buffer r1+88
  - result: unused

## 0x82D2BF98 (size 0x1E4)
- reaches: E#880, E#881, ExGetXConfigSetting, KfAcquireSpinLock, KfReleaseSpinLock
- call 0x82A603E8 in 0x82A60370: strings -
  - args: r3=constant 32 (0x20), r7=copy of r30, r8=copy of r29, r9=static 0x82130844, r10=constant 0x82130000; f1=copy of f31, f2=loaded from 7792(r9), f3=loaded from 2116(r11)
  - result: stores r3 to 76(r31), cmplwi r3,0
- call 0x82A6078C in 0x82A60620: strings -
  - args: r3=copy of r27, r7=copy of r29, r8=copy of r28; f1=copy of f28, f2=copy of f31, f3=copy of f30
  - result: stores r3 to 76(r31), cmplwi r3,170

## 0x82D2D6B8 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82B68298 in 0x82B68270: strings -
  - args: -
  - result: cmpwi r3,0
- call 0x82B6E394 in 0x82B6E208: strings -
  - args: r3=copy of r27
  - result: r31 = r3, cmpwi r3,0

## 0x82D2D6C0 (size 0x17C)
- reaches: RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x82B695A8 in 0x82B68630: strings -
  - args: r3=field 68(r31), r4=copy of r29, r5=copy of r30, r6=static 0x82B67F18, r7=copy of r3
  - result: r5 = r3

## 0x82D2D840 (size 0x104)
- reaches: RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x82B69228 in 0x82B68630: strings -
  - args: r3=field 68(r31), r4=constant 0 (0x0), r5=static 0x82B67E38, r6=copy of r3
  - result: r5 = r3

## 0x82D2D948 (size 0xDC)
- reaches: RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x82B6A730 in 0x82B6A1F0: strings -
  - args: r3=copy of r29, r4=stack buffer r1+88
  - result: cmpwi r3,0
- call 0x82B6ADDC in 0x82B6AD50: strings -
  - args: r3=copy of r29, r4=stack buffer r1+80
  - result: cmpwi r3,0
- call 0x82B6B00C in 0x82B6AD50: strings -
  - args: r3=copy of r31, r4=stack buffer r1+88
  - result: cmpwi r3,0
- call 0x82B6B110 in 0x82B6B070: strings -
  - args: r3=copy of r29, r4=stack buffer r1+80, r6=field 124(r1)
  - result: unused
- call 0x82B6F024 in 0x82B6F010: strings -
  - args: r3=copy of r4, r4=stack buffer r1+80
  - result: cmplwi r3,254
- call 0x82B6F0CC in 0x82B6F0A8: strings -
  - args: r3=copy of r31, r4=stack buffer r1+80
  - result: unused

## 0x82D2DB58 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82B6D070 in 0x82B6BBE8: strings -
  - args: -
  - result: unused

## 0x82D2DB60 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82B79B70 in 0x82B79AE8: strings -
  - args: -
  - result: cmplwi r3,0

## 0x82D3DBB0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x835A492C in 0x835A4920: strings -
  - args: -
  - result: stores f1 (float result)

## 0x82D3F920 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x835A4954 in 0x835A4948: strings -
  - args: -
  - result: stores f1 (float result)

## 0x82D40388 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82F72D08 in 0x82F72CD0: strings -
  - args: r3=stack buffer r1+124
  - result: unused
- call 0x82F73CC8 in 0x82F73C98: strings -
  - args: r3=stack buffer r1+144
  - result: unused
- call 0x82F7403C in 0x82F73DC8: strings -
  - args: r3=copy of r30
  - result: unused
- call 0x831D4960 in 0x831D4948: strings -
  - args: r3=stack buffer r31+8
  - result: unused
- call 0x831DA61C in 0x831DA600: strings -
  - args: r3=stack buffer r31+8
  - result: unused
- call 0x831E0CA4 in 0x831E0C88: strings -
  - args: r3=stack buffer r31+8
  - result: unused
- call 0x831E3BA0 in 0x831E3B88: strings -
  - args: r3=stack buffer r31+4
  - result: unused

## 0x82D48698 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82D61D24 in 0x82D61CD0: strings -
  - args: r3=copy of r30
  - result: cmpwi r3,0
- call 0x82D61DA8 in 0x82D61D60: strings -
  - args: -
  - result: stores r3 to 112(r1)
- call 0x82D62024 in 0x82D61D60: strings -
  - args: r3=field 404(r1), r10=constant 0x00010000
  - result: unused

## 0x82D486B0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82D5F4E4 in 0x82D5F4B0: strings -
  - args: -
  - result: stores r3 to 88(r1)

## 0x82D486C0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82D56DC8 in 0x82D56CF0: strings -
  - args: -
  - result: unused
- call 0x82D570C8 in 0x82D570A8: strings -
  - args: -
  - result: unused
- call 0x82D577CC in 0x82D57768: strings -
  - args: -
  - result: unused
- call 0x82D5C590 in 0x82D5C450: strings -
  - args: -
  - result: unused
- call 0x82D5D290 in 0x82D5CDE8: strings -
  - args: -
  - result: unused
- call 0x82D5E0F8 in 0x82D5DB18: strings -
  - args: r8=stack buffer r1+384, r9=stack buffer r1+368, r10=constant 16064 (0x3EC0)
  - result: unused
- call 0x82D62138 in 0x82D61D60: strings -
  - args: -
  - result: unused

## 0x82D486D8 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82D60584 in 0x82D5F4B0: strings -
  - args: r3=field 21664(r31)
  - result: unused

## 0x82D486F0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82D60ED4 in 0x82D605E8: strings -
  - args: r3=constant 3 (0x3), r4=stack buffer r1+352
  - result: unused

## 0x82D48708 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82D5C25C in 0x82D5C230: strings -
  - args: r3=field 16708(r27)
  - result: unused

## 0x82D48718 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82D5C2BC in 0x82D5C230: strings -
  - args: r3=copy of r21, r4=stack buffer r1+80
  - result: unused
- call 0x82D5C3FC in 0x82D5C230: strings -
  - args: r3=copy of r21, r4=stack buffer r1+112
  - result: unused

## 0x82D48730 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82D5C43C in 0x82D5C230: strings -
  - args: -
  - result: unused

## 0x82D48740 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82D5C9F0 in 0x82D5C450: strings -
  - args: r3=field 16708(r31), r4=field 21664(r31); f1=copy of f29
  - result: unused
- call 0x82D60658 in 0x82D605E8: strings -
  - args: r3=field -4956(r30); f1=copy of f31
  - result: unused
- call 0x82D606B4 in 0x82D605E8: strings -
  - args: r3=field -4956(r30), r4=field 0(r30); f1=copy of f31
  - result: unused

## 0x82D489C0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82D676C8 in 0x82D67530: strings -
  - args: r3=constant 2 (0x2), r4=stack buffer r1+96, r5=constant 76 (0x4C)
  - result: unused
- call 0x82D6770C in 0x82D676D8: strings -
  - args: r3=constant 3 (0x3), r4=stack buffer r1+80, r5=constant 8 (0x8), r10=field 6836(r3)
  - result: unused
- call 0x82D6BFE8 in 0x82D6BF98: strings -
  - args: r3=constant 4 (0x4), r4=stack buffer r1+176, r5=constant 8 (0x8), r8=field 3552(r10)
  - result: unused
- call 0x82D6D6E8 in 0x82D6BF98: strings -
  - args: r3=constant 5 (0x5), r4=stack buffer r1+176, r5=constant 8 (0x8), r9=field 852(r1), r10=stack buffer r1+176
  - result: unused

## 0x82D49600 (size 0x68)
- reaches: pure (no imports reached)
- call 0x82D66DF0 in 0x82D66078: strings -
  - args: r3=field 0(r31), r4=constant 3 (0x3)
  - result: unused
- call 0x82D674FC in 0x82D66078: strings -
  - args: r3=field 0(r31), r4=constant 4 (0x4)
  - result: unused
- call 0x82D68254 in 0x82D67830: strings -
  - args: r3=field 0(r31), r4=r28-1, r8=field 0(r31)
  - result: unused
- call 0x82D69C60 in 0x82D67830: strings -
  - args: r3=field 0(r31), r4=copy of r28
  - result: unused

## 0x82D4B478 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82D6C2C8 in 0x82D6BF98: strings -
  - args: r5=constant 1 (0x1), r6=constant 8 (0x8)
  - result: unused
- call 0x82D6D464 in 0x82D6BF98: strings -
  - args: r5=constant 1 (0x1), r6=constant 8 (0x8)
  - result: unused
- call 0x82D6D4D8 in 0x82D6BF98: strings -
  - args: r5=constant 1 (0x1), r6=constant 8 (0x8)
  - result: unused
- call 0x82D6D55C in 0x82D6BF98: strings -
  - args: r5=constant 1 (0x1), r6=constant 8 (0x8)
  - result: unused
- call 0x82D6E3D4 in 0x82D6D708: strings -
  - args: r3=copy of r29, r4=copy of r24
  - result: unused
- call 0x82D6E850 in 0x82D6D708: strings -
  - args: r3=copy of r31, r4=copy of r28, r5=constant 8 (0x8), r6=constant 8 (0x8)
  - result: unused
- call 0x82D6E910 in 0x82D6D708: strings -
  - args: -
  - result: unused
- call 0x82D6EA04 in 0x82D6D708: strings -
  - args: r3=copy of r31, r4=copy of r30, r5=constant 8 (0x8), r6=constant 8 (0x8)
  - result: unused
- call 0x82D6EDFC in 0x82D6D708: strings -
  - args: r3=copy of r31, r4=copy of r30, r5=copy of r29, r6=constant 8 (0x8)
  - result: unused
- call 0x82D6EF08 in 0x82D6D708: strings -
  - args: r3=copy of r31, r4=copy of r28, r5=constant 1 (0x1), r6=constant 8 (0x8)
  - result: unused
- call 0x82D6F1E8 in 0x82D6D708: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 1 (0x1), r6=constant 8 (0x8)
  - result: unused
- call 0x82D701D0 in 0x82D700F8: strings -
  - args: r4=copy of r23, r5=copy of r22, r6=copy of r27
  - result: r24 = r3

## 0x82D4D278 (size 0xC4)
- reaches: pure (no imports reached)
- call 0x82D555B4 in 0x82D54FF8: strings -
  - args: r3=copy of r28, r4=constant 1000 (0x3E8)
  - result: stores f1 (float result)
- call 0x82D5562C in 0x82D54FF8: strings -
  - args: r3=copy of r27, r4=constant 1000 (0x3E8)
  - result: stores f1 (float result)
- call 0x82D556CC in 0x82D54FF8: strings -
  - args: r3=copy of r30, r4=constant 1000 (0x3E8)
  - result: stores f1 (float result)
- call 0x82D55820 in 0x82D54FF8: strings -
  - args: r3=copy of r29, r4=constant 1000 (0x3E8)
  - result: stores f1 (float result)
- call 0x82D558B8 in 0x82D54FF8: strings -
  - args: r3=copy of r31, r4=constant 1000 (0x3E8)
  - result: stores f1 (float result)
- call 0x82D558F0 in 0x82D54FF8: strings -
  - args: r3=copy of r19, r4=constant 1000 (0x3E8)
  - result: stores f1 (float result)

## 0x82D4D340 (size 0x1F4)
- reaches: pure (no imports reached)
- call 0x82D53B80 in 0x82D52F40: strings -
  - args: r3=copy of r31, r5=copy of r28
  - result: unused
- call 0x82D540F0 in 0x82D52F40: strings -
  - args: r3=copy of r31, r4=copy of r27, r5=copy of r26
  - result: unused
- call 0x82D54254 in 0x82D52F40: strings -
  - args: r3=copy of r31, r4=copy of r25, r5=copy of r29
  - result: unused

## 0x82D4D538 (size 0x358)
- reaches: pure (no imports reached)
- call 0x82D521A4 in 0x82D50E30: strings -
  - args: r3=field 788(r1), r7=field 164(r1), r8=field 180(r1), r9=field 124(r1), r10=field 176(r1)
  - result: unused

## 0x82D4D890 (size 0x1AC)
- reaches: pure (no imports reached)
- call 0x82D52EB4 in 0x82D52E18: strings -
  - args: r3=copy of r28, r4=copy of r25, r5=copy of r24, r6=copy of r27, r7=copy of r30, r8=copy of r31, r9=copy of r26
  - result: r29 = r3
- call 0x82D52ED8 in 0x82D52E18: strings -
  - args: r3=copy of r28, r4=copy of r25, r5=copy of r31, r6=copy of r27, r7=copy of r30, r8=copy of r31, r9=copy of r26
  - result: r29 = r3
- call 0x82D52F04 in 0x82D52E18: strings -
  - args: r3=copy of r28, r4=copy of r30, r5=copy of r24, r6=copy of r27, r7=copy of r30, r8=copy of r31, r9=copy of r26
  - result: r29 = r3

## 0x82D4DA40 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82D51E44 in 0x82D50E30: strings -
  - args: r3=copy of r24, r4=copy of r17
  - result: unused
- call 0x82D535A4 in 0x82D52F40: strings -
  - args: r3=copy of r31, r4=r27-1
  - result: unused
- call 0x82D544B0 in 0x82D52F40: strings -
  - args: r3=copy of r31, r4=copy of r26
  - result: unused
- call 0x82D544BC in 0x82D52F40: strings -
  - args: r3=copy of r31, r4=copy of r30
  - result: unused
- call 0x82D54554 in 0x82D52F40: strings -
  - args: r3=copy of r31, r4=copy of r26
  - result: unused
- call 0x82D54E0C in 0x82D52F40: strings -
  - args: r3=copy of r31, r4=copy of r29
  - result: unused
- call 0x82D54F04 in 0x82D52F40: strings -
  - args: r3=copy of r31, r4=copy of r29
  - result: unused
- call 0x82D554A8 in 0x82D54FF8: strings -
  - args: r3=copy of r26, r4=r28-1
  - result: unused

## 0x82D4DB28 (size 0x814)
- reaches: pure (no imports reached)
- call 0x82D5559C in 0x82D54FF8: strings -
  - args: r3=copy of r26, r4=field 524(r1), r5=field 532(r1)
  - result: unused

## 0x82D4F598 (size 0x5E8)
- reaches: pure (no imports reached)
- call 0x82D55614 in 0x82D54FF8: strings -
  - args: r3=copy of r26
  - result: unused

## 0x82D4FB80 (size 0x2AC)
- reaches: pure (no imports reached)
- call 0x82D55554 in 0x82D54FF8: strings -
  - args: r3=copy of r26
  - result: unused

## 0x82D4FE30 (size 0x660)
- reaches: pure (no imports reached)
- call 0x82D554D8 in 0x82D54FF8: strings -
  - args: r3=copy of r26
  - result: unused

## Referenced only from data
0x82D08498, 0x82D090D8, 0x82D09148, 0x82D09328, 0x82D09538, 0x82D13AC0, 0x82D13D88, 0x82D13FA8, 0x82D14048, 0x82D140B0, 0x82D1A500, 0x82D1A898, 0x82D28DE8, 0x82D29B30, 0x82D3EC08, 0x82D3EDE0, 0x82D3F180, 0x82D3F260, 0x82D404D0, 0x82D40568, 0x82D40730, 0x82D41698, 0x82D416F8, 0x82D417C8, 0x82D41A68, 0x82D41D38, 0x82D42BE0, 0x82D43008, 0x82D43760, 0x82D46058, 0x82D47988, 0x82D47F90
