# Entry points of 0x826DB000-0x82760000 called from outside it

111 entry points; 327 more functions of the range are only referenced from data.

## 0x826DB098 (size 0x8C)
- reaches: ExGetXConfigSetting, XGetGameRegion
- call 0x824912F0 in 0x82490FE8: strings -
  - args: -
  - result: r28 = r3

## 0x826DB128 (size 0xB0)
- reaches: RtlInitAnsiString, RtlNtStatusToDosError
- call 0x82494674 in 0x82494638: strings -
  - args: r3=field 84(r1), r4=constant 0 (0x0), r5=stack buffer r1+96
  - result: r31 = r3

## 0x826DB1D8 (size 0xB4)
- reaches: NtClose, NtOpenFile, NtSetInformationFile, RtlInitAnsiString, RtlNtStatusToDosError
- call 0x8249470C in 0x824946D0: strings -
  - args: r3=field 84(r1)
  - result: unused
- call 0x8249DD00 in 0x8249DCC8: strings -
  - args: r3=field 84(r31)
  - result: cmpwi r3,0
- call 0x826784B8 in 0x82678448: strings -
  - args: r3=field 84(r31)
  - result: unused
- call 0x826785B0 in 0x82678580: strings -
  - args: r3=field 340(r31)
  - result: low byte (BOOL)

## 0x826DB290 (size 0x90)
- reaches: NtClose, NtCreateFile, RtlInitAnsiString, RtlNtStatusToDosError
- call 0x82494780 in 0x82494740: strings -
  - args: r3=field 84(r1), r4=constant 0 (0x0)
  - result: r31 = r3
- call 0x8249DE64 in 0x8249DE28: strings -
  - args: r3=field 84(r31), r4=constant 0 (0x0)
  - result: cmpwi r3,0
- call 0x82677FFC in 0x82677F70: strings -
  - args: r3=field 92(r31), r4=constant 0 (0x0)
  - result: r26 = r3

## 0x826DB320 (size 0x58)
- reaches: RtlNtStatusToDosError
- call 0x82494A0C in 0x82494848: strings -
  - args: r3=copy of r23, r4=stack buffer r1+80
  - result: cmpwi r3,0
- call 0x8249E824 in 0x8249E658: strings -
  - args: r3=copy of r27, r4=stack buffer r31+176
  - result: cmpwi r3,0
- call 0x82679264 in 0x82679080: strings -
  - args: r3=copy of r23, r4=stack buffer r31+176
  - result: cmpwi r3,0

## 0x826DB378 (size 0x8C)
- reaches: NtClose, NtOpenFile, RtlInitAnsiString, RtlNtStatusToDosError
- call 0x824948B8 in 0x82494848: strings "%s\*"
  - args: r4=stack buffer r1+80
  - result: r23 = r3, cmpwi r3,-1
- call 0x8249E6C4 in 0x8249E658: strings -
  - args: r3=field 116(r31), r4=stack buffer r31+176
  - result: r27 = r3, cmpwi r3,-1
- call 0x826790C0 in 0x82679080: strings -
  - args: r3=field 4(r29), r4=stack buffer r31+176
  - result: r23 = r3

## 0x826DB408 (size 0x98)
- reaches: NtCreateSemaphore, RtlInitAnsiString
- call 0x82497AA8 in 0x82497A60: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 1 (0x1), r6=constant 0 (0x0), r8=constant 0x82D00000, r9=constant 0x82D00000, r10=constant 0 (0x0)
  - result: cmplwi r3,0, stores r3 to -12876(r11)

## 0x826DB4A0 (size 0x38)
- reaches: NtReleaseSemaphore
- call 0x82497958 in 0x82497868: strings -
  - args: r3=field -12876(r10), r4=constant 1 (0x1), r5=constant 0 (0x0), r10=constant 0x82D00000
  - result: unused

## 0x826DB590 (size 0x38)
- reaches: NtClearEvent, RtlNtStatusToDosError
- call 0x825A9B6C in 0x825A9B28: strings -
  - args: r3=field 10112(r11)
  - result: unused
- call 0x825AA6C0 in 0x825AA5F0: strings -
  - args: r3=field 10112(r30)
  - result: unused

## 0x826DB7C8 (size 0x148)
- reaches: RtlImageXexHeaderField, RtlNtStatusToDosError, XamGetExecutionId, XamUserGetXUID, XexGetModuleHandle, XexGetProcedureAddress
- call 0x82498D68 in 0x82498CC8: strings -
  - args: r3=copy of r30, r4=field 332(r31), r5=stack buffer r31+8, r6=constant 3 (0x3), r7=constant 0 (0x0), r8=stack buffer r31+320, r9=constant 0 (0x0)
  - result: cmplwi r3,997

## 0x826DB910 (size 0x114)
- reaches: RtlImageXexHeaderField, RtlNtStatusToDosError, XamGetExecutionId, XamUserGetXUID, XexGetModuleHandle, XexGetProcedureAddress
- call 0x8249878C in 0x82498730: strings -
  - args: r3=constant 255 (0xFF), r4=stack buffer r31+8, r5=field 4(r31)
  - result: cmplwi r3,997

## 0x826DBA28 (size 0xCC)
- reaches: RtlNtStatusToDosError, XamUserGetXUID, XexGetModuleHandle, XexGetProcedureAddress
- call 0x8249A1E0 in 0x82499EB8: strings -
  - args: r3=copy of r28, r4=constant 0 (0x0), r5=constant 2 (0x2), r6=constant 0 (0x0), r7=constant 1 (0x1), r8=constant 0 (0x0)
  - result: cmplwi r3,0

## 0x826DBDF8 (size 0x128)
- reaches: NtClose, NtCreateEvent, ObDereferenceObject, ObReferenceObjectByHandle, XamAlloc, XamFree, XamGetOverlappedResult, XamGetPrivateEnumStructureFromHandle, XamTaskSchedule
- call 0x82499CDC in 0x82499B40: strings -
  - args: r3=field -1896(r24), r4=copy of r21, r5=constant 312 (0x138), r6=constant 0 (0x0), r7=field 0(r24)
  - result: cmplwi r3,997
- call 0x8249A230 in 0x82499EB8: strings -
  - args: r4=r11+136, r5=constant 312 (0x138), r6=constant 0 (0x0), r10=r28+28
  - result: cmplwi r3,997

## 0x826DBF20 (size 0x84)
- reaches: XamContentCreateEx
- call 0x8249DAF0 in 0x8249DA28: strings -
  - args: r3=field 4(r30), r4=field 24(r30), r5=stack buffer r31+112, r6=constant 3 (0x3), r7=constant 0 (0x0), r8=constant 0 (0x0), r9=constant 0 (0x0), r10=field 12(r30)
  - result: cmplwi r3,0
- call 0x82675918 in 0x82675858: strings -
  - args: r3=constant 255 (0xFF), r4=field 8(r30), r5=copy of r28, r6=constant 4 (0x4), r7=constant 0 (0x0), r8=constant 0 (0x0), r9=constant 0 (0x0)
  - result: cmplwi r3,997

## 0x826DBFA8 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x8249D4B0 in 0x8249D458: strings -
  - args: r3=field 0(r30), r4=stack buffer r1+80, r5=constant 0 (0x0)
  - result: cmplwi r3,0
- call 0x82675AF4 in 0x82675A70: strings -
  - args: r3=constant 255 (0xFF), r4=r29+12, r5=copy of r30
  - result: cmplwi r3,997

## 0x826DBFB0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82498C3C in 0x82498BD0: strings -
  - args: r3=field 332(r30), r4=constant 0 (0x0)
  - result: unused
- call 0x82498F08 in 0x82498EC8: strings -
  - args: r3=field 332(r31), r4=field 4(r31)
  - result: cmplwi r3,997
- call 0x8249DB34 in 0x8249DA28: strings -
  - args: r3=field 24(r30), r4=constant 0 (0x0)
  - result: cmplwi r3,3, cmplwi r3,183
- call 0x8249DBBC in 0x8249DBA0: strings -
  - args: r3=field 24(r3), r4=constant 0 (0x0)
  - result: unused
- call 0x826759FC in 0x82675988: strings -
  - args: r4=copy of r29
  - result: cmplwi r3,997

## 0x826DBFB8 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x826780D0 in 0x82677F70: strings -
  - args: r3=field 24(r28), r4=constant 0 (0x0)
  - result: unused
- call 0x826780F0 in 0x82677F70: strings -
  - args: r3=field 24(r28), r4=constant 0 (0x0)
  - result: unused
- call 0x82678508 in 0x82678448: strings -
  - args: r3=field 24(r30), r4=constant 0 (0x0)
  - result: unused

## 0x826DBFC0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x8249DB14 in 0x8249DA28: strings -
  - args: r3=field 4(r30), r4=stack buffer r31+112, r5=stack buffer r31+96, r6=constant 0 (0x0), r7=constant 0 (0x0)
  - result: cmplwi r3,0

## 0x826DBFC8 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x8249D5CC in 0x8249D568: strings -
  - args: r3=field 0(r29), r4=field 4(r29), r5=constant 1 (0x1), r6=constant 4096 (0x1000), r7=constant 1 (0x1), r8=constant 0 (0x0), r9=stack buffer r1+80
  - result: cmplwi r3,0, cmplwi r3,18
- call 0x82675614 in 0x82675590: strings -
  - args: r3=constant 254 (0xFE), r4=constant 0 (0x0), r5=constant 1 (0x1), r6=constant 0 (0x0), r7=constant 1 (0x1), r8=stack buffer r1+80, r9=copy of r30
  - result: cmplwi r3,18

## 0x826DBFD0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x8249D6A8 in 0x8249D690: strings -
  - args: r3=field 4(r11), r4=constant 0 (0x0)
  - result: unused
- call 0x82675320 in 0x82675310: strings -
  - args: r4=constant 0 (0x0)
  - result: unused
- call 0x8267592C in 0x82675858: strings -
  - args: r3=field 0(r28)
  - result: cmplwi r3,0
- call 0x82675A14 in 0x82675988: strings -
  - args: r3=field 12(r11)
  - result: cmplwi r3,0
- call 0x82675B0C in 0x82675A70: strings -
  - args: r3=field 12(r11)
  - result: cmplwi r3,0
- call 0x82675E64 in 0x82675E00: strings -
  - args: r3=field 12(r11), r4=constant 0 (0x0)
  - result: cmplwi r3,0
- call 0x8267603C in 0x82675FA0: strings -
  - args: r3=field 12(r11), r4=constant 0 (0x0)
  - result: cmplwi r3,0
- call 0x826762AC in 0x82676248: strings -
  - args: r3=field 12(r11), r4=constant 0 (0x0)
  - result: cmplwi r3,0
- call 0x82677494 in 0x82677480: strings -
  - args: r3=field 28(r3), r4=constant 0 (0x0)
  - result: unused

## 0x826DBFD8 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82498E9C in 0x82498E88: strings -
  - args: r3=field 8(r3), r4=stack buffer r1+80
  - result: unused
- call 0x8249D594 in 0x8249D568: strings -
  - args: r4=stack buffer r1+96
  - result: unused
- call 0x8249D6E4 in 0x8249D6C8: strings -
  - args: r3=field 8(r3), r4=stack buffer r1+80
  - result: cmplwi r3,0
- call 0x82677CB0 in 0x82677BE0: strings -
  - args: r3=field 28(r31), r4=stack buffer r1+96
  - result: cmplwi r3,0, cmplwi r3,5

## 0x826DBFE0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x8264335C in 0x82643330: strings -
  - args: r4=constant 0 (0x0)
  - result: r29 = r3
- call 0x826757D0 in 0x826756E0: strings -
  - args: r3=copy of r28, r4=constant 1 (0x1)
  - result: r7 = r3

## 0x826DC060 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x8249C814 in 0x8249C7D8: strings -
  - args: -
  - result: unused
- call 0x8249C898 in 0x8249C7D8: strings -
  - args: -
  - result: unused
- call 0x8249D754 in 0x8249D720: strings -
  - args: -
  - result: unused
- call 0x8249D7C4 in 0x8249D790: strings -
  - args: -
  - result: unused
- call 0x8249D850 in 0x8249D800: strings -
  - args: -
  - result: unused
- call 0x8249D894 in 0x8249D870: strings -
  - args: -
  - result: unused
- call 0x8249D910 in 0x8249D8D8: strings -
  - args: -
  - result: cmplwi r3,0
- call 0x8249DC64 in 0x8249DBE8: strings -
  - args: -
  - result: unused
- call 0x8249DD0C in 0x8249DCC8: strings -
  - args: -
  - result: unused
- call 0x8249DDBC in 0x8249DD78: strings -
  - args: -
  - result: unused
- call 0x8249DE70 in 0x8249DE28: strings -
  - args: -
  - result: unused
- call 0x8249E230 in 0x8249E180: strings -
  - args: -
  - result: cmplwi r3,2
- call 0x8249E6D4 in 0x8249E658: strings -
  - args: -
  - result: cmplwi r3,2
- call 0x8249E830 in 0x8249E658: strings -
  - args: -
  - result: cmplwi r3,18
- call 0x824A6328 in 0x824A62E8: strings -
  - args: -
  - result: cmpwi r3,997, stores r3 to -4200(r11)
- call 0x824A6540 in 0x824A6378: strings -
  - args: -
  - result: r11 = r3
- call 0x824A6570 in 0x824A6378: strings -
  - args: -
  - result: r11 = r3
- call 0x824A6630 in 0x824A65C0: strings -
  - args: -
  - result: cmplwi r3,997
- call 0x824A663C in 0x824A65C0: strings -
  - args: -
  - result: r11 = r3
- call 0x824A807C in 0x824A8068: strings -
  - args: -
  - result: r28 = r3
- call 0x824A8354 in 0x824A8268: strings -
  - args: -
  - result: cmplwi r3,2, cmplwi r3,3
- call 0x824A8478 in 0x824A83B0: strings -
  - args: -
  - result: cmplwi r3,997
- call 0x824A863C in 0x824A8490: strings -
  - args: -
  - result: cmplwi r3,997
- call 0x82677C60 in 0x82677BE0: strings -
  - args: -
  - result: cmplwi r3,21, cmplwi r3,1110
- call 0x82677DF4 in 0x82677DB8: strings -
  - args: -
  - result: unused
- call 0x82677E38 in 0x82677DB8: strings -
  - args: -
  - result: cmplwi r3,0
- call 0x82677EC4 in 0x82677E80: strings -
  - args: -
  - result: cmplwi r3,2, cmplwi r3,3
- call 0x82677F24 in 0x82677E80: strings -
  - args: -
  - result: unused
- call 0x82678074 in 0x82677F70: strings -
  - args: -
  - result: cmplwi r3,2, cmplwi r3,3
- call 0x826780DC in 0x82677F70: strings -
  - args: -
  - result: unused
- call 0x8267821C in 0x82678148: strings -
  - args: -
  - result: cmplwi r3,145
- call 0x82678514 in 0x82678448: strings -
  - args: -
  - result: unused
- call 0x826785FC in 0x82678580: strings -
  - args: -
  - result: unused
- call 0x826790E8 in 0x82679080: strings -
  - args: -
  - result: r30 = r3
- call 0x82679270 in 0x82679080: strings -
  - args: -
  - result: r30 = r3
- call 0x826C069C in 0x826C0660: strings -
  - args: -
  - result: unused
- call 0x826C422C in 0x826C41F0: strings -
  - args: -
  - result: unused
- call 0x826C432C in 0x826C4280: strings -
  - args: -
  - result: r28 = r3
- call 0x826C4460 in 0x826C4408: strings -
  - args: -
  - result: unused
- call 0x826C46C8 in 0x826C4648: strings -
  - args: -
  - result: r30 = r3
- call 0x826C48F0 in 0x826C4890: strings -
  - args: -
  - result: cmplwi r3,0
- call 0x826C4E1C in 0x826C4C70: strings -
  - args: -
  - result: r25 = r3
- call 0x826C5F84 in 0x826C5EC0: strings -
  - args: -
  - result: unused
- call 0x826C5F9C in 0x826C5EC0: strings -
  - args: -
  - result: unused
- call 0x826C68AC in 0x826C68A0: strings -
  - args: -
  - result: r29 = r3
- call 0x826CD448 in 0x826CD0C0: strings -
  - args: -
  - result: cmplwi r3,0
- call 0x826CD648 in 0x826CD0C0: strings -
  - args: -
  - result: unused
- call 0x826CD698 in 0x826CD0C0: strings -
  - args: -
  - result: cmplwi r3,5
- call 0x826D0E70 in 0x826D0BC0: strings -
  - args: r10=static 0x82ED7F60
  - result: unused
- call 0x826D1060 in 0x826D0BC0: strings -
  - args: -
  - result: unused
- call 0x826D1520 in 0x826D1270: strings -
  - args: r10=static 0x82ED7F60
  - result: unused
- call 0x826D1710 in 0x826D1270: strings -
  - args: -
  - result: unused
- call 0x826D655C in 0x826D63D8: strings -
  - args: -
  - result: cmplwi r3,122
- call 0x826D7AA8 in 0x826D79E0: strings -
  - args: -
  - result: r30 = r3
- call 0x826D83FC in 0x826D8298: strings -
  - args: -
  - result: stores r3 to 0(r30)
- call 0x826DA9EC in 0x826DA988: strings -
  - args: -
  - result: unused

## 0x826DC068 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x824A26C8 in 0x824A2658: strings -
  - args: r3=copy of r30, r4=constant 0 (0x0), r5=copy of r29
  - result: cmplwi r3,0

## 0x826DC070 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x8249CCDC in 0x8249CC50: strings -
  - args: r3=copy of r28, r4=stack buffer r31+112
  - result: cmplwi r3,0

## 0x826DC148 (size 0x17C)
- reaches: NtWaitForSingleObjectEx, RtlNtStatusToDosError
- call 0x8249D748 in 0x8249D720: strings -
  - args: r3=field 8(r3), r6=stack buffer r1+80, r7=constant 0 (0x0)
  - result: cmplwi r3,0
- call 0x824A6624 in 0x824A65C0: strings -
  - args: r4=copy of r28, r6=constant 0 (0x0), r8=constant 0x82E30000, r10=copy of r9
  - result: cmpwi r3,0
- call 0x824A8630 in 0x824A8490: strings -
  - args: r3=field 56(r31), r4=field 96(r31), r5=copy of r29, r6=constant 0 (0x0), r7=copy of r28
  - result: cmpwi r3,0
- call 0x82677F04 in 0x82677E80: strings -
  - args: r3=copy of r30, r4=field 344(r31), r5=field 348(r31), r6=stack buffer r1+80, r7=constant 0 (0x0)
  - result: r11 = r3
- call 0x826CD348 in 0x826CD0C0: strings -
  - args: r4=r4+1, r5=copy of r31, r6=stack buffer r1+84, r7=constant 0 (0x0)
  - result: cmpwi r3,0
- call 0x826CD43C in 0x826CD0C0: strings -
  - args: r4=stack buffer r1+80, r5=constant 1 (0x1), r6=stack buffer r1+84, r7=constant 0 (0x0)
  - result: cmpwi r3,0

## 0x826DC2C8 (size 0x124)
- reaches: NtWaitForSingleObjectEx, NtWriteFile, RtlNtStatusToDosError
- call 0x8249D7B8 in 0x8249D790: strings -
  - args: r3=field 8(r3), r6=stack buffer r1+80, r7=constant 0 (0x0)
  - result: cmplwi r3,0
- call 0x824A846C in 0x824A83B0: strings -
  - args: r3=field 56(r30), r4=copy of r29, r5=copy of r28, r6=constant 0 (0x0), r7=copy of r31
  - result: cmpwi r3,0
- call 0x826780B4 in 0x82677F70: strings -
  - args: r3=copy of r30, r4=field 344(r28), r5=field 348(r28), r6=stack buffer r31+80, r7=constant 0 (0x0)
  - result: cmpwi r3,0
- call 0x826C4DC4 in 0x826C4C70: strings -
  - args: r4=stack buffer r1+96, r5=copy of r31, r6=stack buffer r1+80, r7=constant 0 (0x0), r10=stack buffer r1+96
  - result: cmpwi r3,0
- call 0x826C4E04 in 0x826C4C70: strings -
  - args: r3=field 0(r11), r4=copy of r24, r5=copy of r29, r6=stack buffer r1+80, r7=constant 0 (0x0)
  - result: cmpwi r3,0

## 0x826DC3F0 (size 0x170)
- reaches: RtlNtStatusToDosError
- call 0x8249D844 in 0x8249D800: strings -
  - args: r3=field 8(r3), r5=constant 0 (0x0), r6=field 80(r1)
  - result: cmpwi r3,-1
- call 0x824A66D0 in 0x824A6678: strings -
  - args: r4=field 164(r1), r5=stack buffer r1+80, r6=constant 0 (0x0)
  - result: unused
- call 0x824A8038 in 0x824A8018: strings -
  - args: r3=field 56(r3), r5=constant 0 (0x0), r6=constant 0 (0x0)
  - result: unused
- call 0x826C4450 in 0x826C4408: strings -
  - args: r4=copy of r30, r5=constant 0 (0x0), r6=copy of r29
  - result: r30 = r3, cmpwi r3,-1
- call 0x826C48E0 in 0x826C4890: strings -
  - args: r4=field 84(r1), r5=stack buffer r1+80, r6=copy of r30
  - result: stores r3 to 84(r1), cmpwi r3,-1

## 0x826DC560 (size 0x68)
- reaches: RtlNtStatusToDosError
- call 0x8249D900 in 0x8249D8D8: strings -
  - args: r3=field 8(r3), r4=stack buffer r1+80
  - result: r31 = r3, cmpwi r3,-1
- call 0x824A838C in 0x824A8268: strings -
  - args: r4=constant 0 (0x0)
  - result: stores r3 to 28(r31)
- call 0x82677E28 in 0x82677DB8: strings -
  - args: r3=copy of r28, r4=stack buffer r1+80
  - result: r31 = r3, cmpwi r3,-1

## 0x826DC5C8 (size 0x110)
- reaches: NtClose, NtOpenFile, NtQueryVolumeInformationFile, RtlInitAnsiString, RtlNtStatusToDosError
- call 0x8249DC50 in 0x8249DBE8: strings -
  - args: r3=field 108(r31), r4=stack buffer r31+80, r5=stack buffer r31+96, r6=stack buffer r31+88
  - result: unused
- call 0x82677C54 in 0x82677BE0: strings -
  - args: r4=stack buffer r1+80, r5=constant 0 (0x0), r6=constant 0 (0x0)
  - result: cmplwi r3,0

## 0x826DC6D8 (size 0x58)
- reaches: NtClose, NtOpenFile, NtSetInformationFile, RtlInitAnsiString
- call 0x8249DDB0 in 0x8249DD78: strings -
  - args: r3=field 84(r31)
  - result: cmpwi r3,0
- call 0x82678210 in 0x82678148: strings -
  - args: -
  - result: cmpwi r3,0

## 0x826DC730 (size 0x1F4)
- reaches: RtlInitAnsiString, RtlNtStatusToDosError
- call 0x8249E210 in 0x8249E180: strings -
  - args: r3=field 84(r31), r4=copy of r28, r5=constant 0 (0x0), r6=constant 0 (0x0), r7=copy of r30, r8=constant 128 (0x80), r9=constant 0 (0x0)
  - result: stores r3 to 8(r29)
- call 0x824A64B4 in 0x824A6378: strings -
  - args: r3=field 84(r31), r4=constant 0x80000000, r5=constant 1 (0x1), r6=constant 0 (0x0), r7=constant 3 (0x3), r8=constant 0x60000000, r9=constant 0 (0x0)
  - result: unused
- call 0x824A64F4 in 0x824A6378: strings -
  - args: r3=field 84(r31), r4=constant 0x80000000, r5=constant 1 (0x1), r6=constant 0 (0x0), r7=constant 3 (0x3), r8=constant 0x08000000, r9=constant 0 (0x0)
  - result: unused
- call 0x824A8344 in 0x824A8268: strings -
  - args: r3=field 16(r31), r5=constant 3 (0x3), r6=constant 0 (0x0), r7=r11+3, r8=constant 0x60000000, r9=constant 0 (0x0)
  - result: stores r3 to 56(r31), cmpwi r3,-1
- call 0x82677DE4 in 0x82677DB8: strings -
  - args: r3=field 340(r3), r4=constant 0 (0x0), r5=constant 1 (0x1), r6=constant 0 (0x0), r7=constant 3 (0x3), r8=constant 128 (0x80), r9=constant 0 (0x0)
  - result: r28 = r3, cmpwi r3,-1
- call 0x82677EB4 in 0x82677E80: strings -
  - args: r3=field 340(r3), r4=constant 0x80000000, r5=constant 1 (0x1), r6=constant 0 (0x0), r7=constant 3 (0x3), r8=constant 128 (0x80), r9=constant 0 (0x0)
  - result: r30 = r3, cmpwi r3,-1
- call 0x82678064 in 0x82677F70: strings -
  - args: r3=field 0(r29), r4=constant 0x40000000, r5=constant 0 (0x0), r6=constant 0 (0x0), r7=constant 2 (0x2), r8=constant 128 (0x80), r9=constant 0 (0x0)
  - result: r30 = r3, cmpwi r3,-1
- call 0x826D0DE8 in 0x826D0BC0: strings -
  - args: r3=copy of r22, r4=copy of r25, r5=copy of r24, r6=constant 0 (0x0), r7=copy of r31, r8=copy of r26, r9=constant 0 (0x0)
  - result: r28 = r3, cmpwi r3,-1
- call 0x826D0E2C in 0x826D0BC0: strings -
  - args: r3=copy of r22, r4=copy of r25, r5=copy of r24, r6=constant 0 (0x0), r7=copy of r31, r8=copy of r26, r9=constant 0 (0x0)
  - result: r28 = r3, cmpwi r3,-1
- call 0x826D1054 in 0x826D0BC0: strings -
  - args: r3=copy of r22, r5=copy of r24, r6=constant 0 (0x0), r7=constant 3 (0x3), r8=copy of r26, r9=constant 0 (0x0)
  - result: cmpwi r3,-1
- call 0x826D856C in 0x826D8510: strings -
  - args: r3=stack buffer r1+80, r4=copy of r30, r5=copy of r29, r6=copy of r28, r7=copy of r27, r8=copy of r26, r9=copy of r25
  - result: unused

## 0x826DC928 (size 0x88)
- reaches: RtlTimeToTimeFields
- call 0x8249E77C in 0x8249E658: strings -
  - args: r3=stack buffer r31+160, r4=stack buffer r31+144
  - result: unused
- call 0x8249ECC4 in 0x8249ECA8: strings -
  - args: r3=copy of r4, r4=stack buffer r1+80
  - result: unused

## 0x826DC9B0 (size 0x70)
- reaches: ExGetXConfigSetting, KeQuerySystemTime, RtlNtStatusToDosError, RtlTimeFieldsToTime, RtlTimeToTimeFields
- call 0x8249E770 in 0x8249E658: strings -
  - args: r3=stack buffer r31+196, r4=stack buffer r31+160
  - result: unused

## 0x826DCAC0 (size 0xC0)
- reaches: RtlNtStatusToDosError, RtlUnicodeToMultiByteN
- call 0x824A58C8 in 0x824A5868: strings -
  - args: r3=constant 0x00000000, r4=constant 0 (0x0), r5=field -4228(r30), r6=constant -1 (0xFFFF), r7=stack buffer r1+80, r8=constant 512 (0x200), r9=constant 0 (0x0), r10=constant 0 (0x0)
  - result: unused
- call 0x825B0E50 in 0x825B0E18: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=field 0(r3), r6=constant -1 (0xFFFF), r7=copy of r31, r8=constant 1024 (0x400), r9=constant 0 (0x0), r10=constant 0 (0x0)
  - result: unused
- call 0x82652D70 in 0x82652CD8: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=field 20(r29), r6=field 16(r29), r7=stack buffer r31+128, r8=constant 255 (0xFF), r9=constant 0 (0x0), r10=constant 0 (0x0)
  - result: unused
- call 0x826D64F0 in 0x826D63D8: strings -
  - args: r3=field 4(r9), r4=constant 0 (0x0), r5=copy of r31, r6=copy of r29, r7=copy of r28, r8=copy of r29, r9=constant 0 (0x0), r10=stack buffer r1+80
  - result: cmplwi r3,0
- call 0x826D653C in 0x826D63D8: strings -
  - args: r3=field 4(r9), r4=constant 0 (0x0), r5=copy of r31, r6=constant -1 (0xFFFF), r7=copy of r28, r8=copy of r29, r9=constant 0 (0x0), r10=stack buffer r1+80
  - result: r30 = r3
- call 0x826D6594 in 0x826D63D8: strings -
  - args: r3=field 4(r11), r4=constant 0 (0x0), r5=copy of r31, r6=constant 1 (0x1), r7=stack buffer r1+84, r8=field 172(r11), r9=constant 0 (0x0), r10=stack buffer r1+80
  - result: cmpwi r3,0
- call 0x826D6668 in 0x826D63D8: strings -
  - args: r3=field 4(r11), r4=constant 0 (0x0), r5=copy of r31, r6=constant -1 (0xFFFF), r7=constant 0 (0x0), r8=constant 0 (0x0), r9=constant 0 (0x0), r10=stack buffer r1+80
  - result: cmplwi r3,0

## 0x826DCB80 (size 0xA8)
- reaches: RtlNtStatusToDosError
- call 0x824A631C in 0x824A62E8: strings -
  - args: r4=static 0x82E2EF80, r5=stack buffer r1+80, r6=constant 0 (0x0), r9=constant 0x82E30000, r10=field -4196(r10)
  - result: cmpwi r3,0
- call 0x824A7EA4 in 0x824A7E58: strings -
  - args: r3=field 56(r31), r5=stack buffer r1+80, r6=constant 0 (0x0)
  - result: cmpwi r3,0
- call 0x824A86A4 in 0x824A8660: strings -
  - args: r3=field 56(r31), r5=stack buffer r1+80, r6=constant 0 (0x0)
  - result: cmpwi r3,0

## 0x826DCC28 (size 0xA4)
- reaches: NtQueryInformationFile, NtSetInformationFile, RtlNtStatusToDosError
- call 0x824A8040 in 0x824A8018: strings -
  - args: r3=field 56(r31)
  - result: unused
- call 0x826D83D0 in 0x826D8298: strings -
  - args: -
  - result: unused

## 0x826DCCD0 (size 0x94)
- reaches: XeKeysSetKey
- call 0x825FC064 in 0x825FC028: strings -
  - args: r3=constant 0 (0x0), r5=constant 16 (0x10)
  - result: unused

## 0x826DCD68 (size 0xA4)
- reaches: XeKeysAesCbc
- call 0x825FC078 in 0x825FC028: strings -
  - args: r3=constant 0 (0x0), r4=copy of r31, r5=constant 16 (0x10), r6=copy of r30
  - result: unused

## 0x826DD040 (size 0xAC)
- reaches: pure (no imports reached)
- call 0x82607A44 in 0x82607958: strings -
  - args: r3=field 96(r1), r4=field 244(r11), r9=copy of r4, r10=field 248(r11)
  - result: unused
- call 0x82607A5C in 0x82607958: strings -
  - args: r3=copy of r28, r4=copy of r27
  - result: unused

## 0x826DD0F0 (size 0x2F4)
- reaches: pure (no imports reached)
- call 0x825FFD94 in 0x825FFD68: strings -
  - args: r3=r11+12
  - result: unused
- call 0x826001D0 in 0x825FFFC0: strings -
  - args: r3=stack buffer r31+12, r4=stack buffer r31+16396
  - result: unused

## 0x826DD570 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x826093AC in 0x826092F8: strings -
  - args: -
  - result: unused

## 0x826DD5A0 (size 0xD4)
- reaches: RtlMultiByteToUnicodeN, RtlNtStatusToDosError
- call 0x826432BC in 0x826431C8: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=copy of r3, r6=constant -1 (0xFFFF), r7=stack buffer r31+96, r8=constant 128 (0x80)
  - result: unused
- call 0x826CD63C in 0x826CD0C0: strings -
  - args: r3=constant 0x00000000, r4=constant 0 (0x0), r5=copy of r23, r6=copy of r31, r7=copy of r19
  - result: r30 = r3

## 0x826DD678 (size 0x64)
- reaches: NtQueryFullAttributesFile, RtlInitAnsiString, RtlNtStatusToDosError
- call 0x82677FE8 in 0x82677F70: strings -
  - args: r3=field 92(r31)
  - result: cmpwi r3,-1

## 0x826DD6E0 (size 0x80)
- reaches: RtlRaiseException
- call 0x826C0578 in 0x826C0510: strings -
  - args: r3=field 80(r1), r4=field 84(r1), r5=field 96(r1), r6=stack buffer r1+100
  - result: unused
- call 0x826C95DC in 0x826C92C8: strings -
  - args: r3=copy of r27, r4=constant 0 (0x0), r5=constant 1 (0x1), r6=stack buffer r1+180
  - result: unused

## 0x826DE918 (size 0xA8)
- reaches: KeBugCheckEx, KeGetCurrentProcessType
- call 0x826C8BB0 in 0x826C8B70: strings -
  - args: r4=constant 0 (0x0), r5=copy of r31
  - result: unused

## 0x826DF6A0 (size 0x880)
- reaches: KeBugCheckEx, KeGetCurrentProcessType, NtAllocateVirtualMemory, NtFreeVirtualMemory, RtlCompareMemoryUlong, RtlEnterCriticalSection, RtlLeaveCriticalSection, RtlRaiseException
- call 0x826C0D04 in 0x826C0CA8: strings -
  - args: r4=constant 0 (0x0), r5=copy of r31
  - result: r29 = r3
- call 0x826D56AC in 0x826D5640: strings -
  - args: r4=constant 8 (0x8), r5=copy of r31
  - result: cmplwi r3,0
- call 0x826D82F8 in 0x826D8298: strings -
  - args: r4=constant 8 (0x8), r5=constant 4096 (0x1000)
  - result: r29 = r3

## 0x826DFF98 (size 0x27C)
- reaches: KeBugCheckEx, KeGetCurrentProcessType, NtAllocateVirtualMemory, NtFreeVirtualMemory, RtlCompareMemoryUlong, RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x826C0688 in 0x826C0660: strings -
  - args: r4=constant 0 (0x0), r5=copy of r31
  - result: cmpwi r3,0
- call 0x826D83A4 in 0x826D8298: strings -
  - args: r4=constant 0 (0x0), r5=copy of r29
  - result: unused

## 0x826E0290 (size 0x848)
- reaches: KeBugCheckEx, KeGetCurrentProcessType, NtAllocateVirtualMemory, NtFreeVirtualMemory, RtlCompareMemoryUlong, RtlEnterCriticalSection, RtlLeaveCriticalSection, RtlRaiseException
- call 0x826C5F28 in 0x826C5EC0: strings -
  - args: r4=constant 0 (0x0), r5=copy of r28, r6=copy of r31
  - result: r30 = r3

## 0x826E0B70 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x826C067C in 0x826C0660: strings -
  - args: -
  - result: unused
- call 0x826C0CC8 in 0x826C0CA8: strings -
  - args: -
  - result: cmplwi r3,0
- call 0x826C0CF8 in 0x826C0CA8: strings -
  - args: -
  - result: unused
- call 0x826C5F18 in 0x826C5EC0: strings -
  - args: -
  - result: unused
- call 0x826C8BA4 in 0x826C8B70: strings -
  - args: -
  - result: unused
- call 0x826D2654 in 0x826D2638: strings -
  - args: -
  - result: cmplwi r3,0
- call 0x826D56A0 in 0x826D5640: strings -
  - args: -
  - result: unused
- call 0x826D82EC in 0x826D8298: strings -
  - args: -
  - result: unused
- call 0x826D8398 in 0x826D8298: strings -
  - args: -
  - result: unused

## 0x826E0DA0 (size 0x40)
- reaches: RtlInitAnsiString
- call 0x826C4058 in 0x826C3E08: strings -
  - args: r3=stack buffer r1+80, r10=constant 0 (0x0)
  - result: unused
- call 0x826CEAD8 in 0x826CDFB8: strings -
  - args: r3=stack buffer r1+656
  - result: unused
- call 0x826D2478 in 0x826D1940: strings -
  - args: r3=stack buffer r1+656
  - result: unused

## 0x826E0DE0 (size 0x24)
- reaches: ExTerminateThread, RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x826C41A0 in 0x826C4170: strings -
  - args: -
  - result: unused
- call 0x826C4230 in 0x826C41F0: strings -
  - args: -
  - result: unused

## 0x826E0E08 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x826C6964 in 0x826C68A0: strings -
  - args: r3=copy of r29
  - result: unused
- call 0x826D2568 in 0x826D2518: strings -
  - args: r3=constant 8 (0x8)
  - result: stores r3 to 80(r31)
- call 0x826D9FB0 in 0x826D9F48: strings -
  - args: r3=constant 8 (0x8)
  - result: unused
- call 0x826DA020 in 0x826D9FD0: strings -
  - args: r3=constant 8 (0x8)
  - result: unused

## 0x826E0E10 (size 0x68)
- reaches: RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x826DA43C in 0x826DA400: strings -
  - args: r3=constant 1 (0x1)
  - result: unused

## 0x826E0E78 (size 0x6C)
- reaches: RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x826C6B94 in 0x826C6AE8: strings -
  - args: r3=copy of r10, r4=constant 1 (0x1), r9=constant -1 (0xFFFF), r10=static 0x82E38F84
  - result: unused

## 0x826E0EE8 (size 0x6C)
- reaches: RtlFreeAnsiString, RtlInitAnsiString, RtlInitUnicodeString, RtlUnicodeStringToAnsiString
- call 0x826C8D00 in 0x826C8CE8: strings -
  - args: -
  - result: unused
- call 0x826C8D34 in 0x826C8D18: strings -
  - args: -
  - result: unused
- call 0x826C8D48 in 0x826C8D18: strings -
  - args: -
  - result: unused
- call 0x826CF6DC in 0x826CEC90: strings -
  - args: r3=stack buffer r1+1152
  - result: unused
- call 0x826D01BC in 0x826CF758: strings -
  - args: r3=stack buffer r1+1152
  - result: unused
- call 0x826D7E78 in 0x826D7C30: strings -
  - args: r3=stack buffer r1+80
  - result: unused

## 0x826E10E0 (size 0x3C)
- reaches: NtFlushBuffersFile, RtlNtStatusToDosError
- call 0x826D7A9C in 0x826D79E0: strings -
  - args: -
  - result: cmpwi r3,0

## 0x826E1120 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x826DAAD0 in 0x826DAA60: strings -
  - args: r3=constant 0 (0x0)
  - result: unused

## 0x826E1138 (size 0x34)
- reaches: RtlNtStatusToDosError
- call 0x826D99B0 in 0x826D9980: strings -
  - args: -
  - result: unused
- call 0x826D9DAC in 0x826D9D90: strings -
  - args: -
  - result: unused
- call 0x826DAA44 in 0x826DAA20: strings -
  - args: -
  - result: unused
- call 0x826DAADC in 0x826DAA60: strings -
  - args: -
  - result: unused

## 0x826E1170 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x826DA62C in 0x826DA5E0: strings -
  - args: -
  - result: unused
- call 0x826DA6A4 in 0x826DA658: strings -
  - args: -
  - result: unused
- call 0x826DA72C in 0x826DA6D0: strings -
  - args: -
  - result: unused
- call 0x826DA7F0 in 0x826DA768: strings -
  - args: -
  - result: unused

## 0x826E1190 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x826D9A90 in 0x826D9A40: strings -
  - args: r3=copy of r31
  - result: unused
- call 0x826D9B14 in 0x826D9AB0: strings -
  - args: -
  - result: unused
- call 0x826D9B90 in 0x826D9B30: strings -
  - args: -
  - result: unused
- call 0x826D9C08 in 0x826D9BB0: strings -
  - args: -
  - result: unused
- call 0x826D9CD0 in 0x826D9C40: strings -
  - args: r3=copy of r31
  - result: unused

## 0x826E1270 (size 0x44)
- reaches: KeBugCheckEx, KeGetCurrentProcessType, NtAllocateVirtualMemory, NtFreeVirtualMemory, RtlCompareMemoryUlong, RtlEnterCriticalSection, RtlLeaveCriticalSection, RtlRaiseException
- call 0x826D9F00 in 0x826D9DE0: strings -
  - args: r4=copy of r27
  - result: r30 = r3

## 0x826E14B0 (size 0xE0)
- reaches: pure (no imports reached)
- call 0x826DA460 in 0x826DA400: strings -
  - args: r3=constant 1 (0x1)
  - result: unused

## 0x826E1590 (size 0x74)
- reaches: pure (no imports reached)
- call 0x826DA458 in 0x826DA400: strings -
  - args: -
  - result: unused

## 0x826E16E0 (size 0x58)
- reaches: HalReturnToFirmware, KeGetCurrentProcessType, NtAllocateVirtualMemory, NtFreeVirtualMemory, NtQueryVirtualMemory, RtlImageXexHeaderField, RtlInitializeCriticalSection
- call 0x826DA434 in 0x826DA400: strings -
  - args: r8=constant 0x82EE0000, r9=constant 0x82EE0000, r10=constant -1 (0xFFFF)
  - result: unused

## 0x826E1828 (size 0x58)
- reaches: RtlInitAnsiString
- call 0x826DAA90 in 0x826DAA60: strings -
  - args: r3=stack buffer r1+96, r4=stack buffer r1+88, r5=copy of r6
  - result: r4 = r3

## 0x826E1880 (size 0x80)
- reaches: ExGetXConfigSetting, KeQuerySystemTime, RtlNtStatusToDosError, RtlTimeFieldsToTime, RtlTimeToTimeFields
- call 0x826DAC00 in 0x826DABE0: strings -
  - args: r3=stack buffer r1+88
  - result: unused

## 0x826E1B80 (size 0x90)
- reaches: KeDelayExecutionThread
- call 0x826DAC88 in 0x826DAC78: strings -
  - args: -
  - result: unused

## 0x826E3C08 (size 0xB0)
- reaches: KeSetEvent, ObDereferenceObject, RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x822A009C in 0x822A0088: strings -
  - args: -
  - result: unused
- call 0x822A010C in 0x822A0088: strings -
  - args: -
  - result: unused

## 0x826E3D80 (size 0x104)
- reaches: RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x825AB178 in 0x825AB160: strings -
  - args: r3=stack buffer r1+80
  - result: unused

## 0x826E43E0 (size 0x1A0)
- reaches: KeReleaseMutant, KeResetEvent, KeSetEvent, KeWaitForSingleObject, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x822A00A8 in 0x822A0088: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0)
  - result: unused
- call 0x822A0118 in 0x822A0088: strings -
  - args: r4=constant 2 (0x2)
  - result: unused
- call 0x825ABC20 in 0x825AB9E8: strings -
  - args: r10=constant 2 (0x2)
  - result: unused

## 0x826E4580 (size 0x27C)
- reaches: KeReleaseMutant, KeResetEvent, KeSetEvent, KeWaitForSingleObject, RtlEnterCriticalSection, RtlLeaveCriticalSection, XamIsUIActive, XamXStudioRequest
- call 0x825AA594 in 0x825AA570: strings -
  - args: r3=constant 0 (0x0), r4=field 20880(r3)
  - result: cmpwi r3,0

## 0x826E5358 (size 0xF4)
- reaches: pure (no imports reached)
- call 0x825AE208 in 0x825AE110: strings -
  - args: -
  - result: unused

## 0x826E54A0 (size 0x1BC)
- reaches: KeSetEvent, KeWaitForSingleObject
- call 0x825AECC4 in 0x825AEC60: strings -
  - args: r3=field 2656(r30), r4=copy of r31, r6=static 0x825AEA40, r7=copy of r29
  - result: cmpwi r3,0

## 0x826E5660 (size 0x128)
- reaches: KeSetEvent, KeWaitForSingleObject
- call 0x825AEAEC in 0x825AEAA8: strings -
  - args: r3=field 2656(r31), r4=constant 0 (0x0), r5=static 0x825AEA40, r6=copy of r30
  - result: cmpwi r3,0

## 0x826E5788 (size 0x100)
- reaches: KeWaitForSingleObject
- call 0x82544A1C in 0x825449F8: strings -
  - args: r3=copy of r29, r4=stack buffer r1+80
  - result: unused

## 0x826E59A0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x825A8184 in 0x825A8150: strings -
  - args: -
  - result: unused

## 0x826E7C50 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x825A92C4 in 0x825A8978: strings "show_gesture_troubleshooter"
  - args: -
  - result: unused

## 0x826E82A8 (size 0x328)
- reaches: KeResetEvent, KeWaitForSingleObject, ObDereferenceObject, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlLeaveCriticalSection, RtlNtStatusToDosError
- call 0x825AB028 in 0x825AAFE8: strings -
  - args: r4=constant 0 (0x0)
  - result: unused

## 0x826E85D0 (size 0x14C)
- reaches: ObDereferenceObject, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlLeaveCriticalSection, RtlNtStatusToDosError
- call 0x825AB09C in 0x825AAFE8: strings -
  - args: r3=field 0(r31)
  - result: unused

## 0x826E8E38 (size 0x21C)
- reaches: KeSetEvent, ObCreateObject, ObDereferenceObject, ObOpenObjectByPointer, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlLeaveCriticalSection, RtlNtStatusToDosError
- call 0x825ABC3C in 0x825AB9E8: strings -
  - args: r3=constant 2 (0x2), r4=constant 2 (0x2), r5=constant 0 (0x0), r6=constant 2 (0x2), r7=constant 0 (0x0), r8=r30+4608
  - result: unused
- call 0x825ABC6C in 0x825AB9E8: strings -
  - args: r3=constant 4 (0x4), r4=constant 1 (0x1), r5=constant 0 (0x0), r6=constant 2 (0x2), r7=constant 0 (0x0), r8=r30+4632
  - result: unused

## 0x826E96D8 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x825AB550 in 0x825AB518: strings -
  - args: r3=constant 0 (0x0), r4=stack buffer r1+96
  - result: unused

## 0x826E9EC8 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x825AB460 in 0x825AB448: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=stack buffer r1+80
  - result: unused
- call 0x825AB534 in 0x825AB518: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2), r5=stack buffer r1+80
  - result: unused

## 0x826EA950 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x825AB428 in 0x825AB400: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0)
  - result: unused
- call 0x825AB4BC in 0x825AB488: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2), r10=constant 2 (0x2)
  - result: unused
- call 0x825ABD8C in 0x825AB9E8: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 1 (0x1)
  - result: unused

## 0x826EA960 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x825AB4F8 in 0x825AB488: strings -
  - args: r3=constant 0 (0x0), r4=stack buffer r1+80
  - result: unused
- call 0x825AB664 in 0x825AB640: strings -
  - args: r3=constant 0 (0x0), r4=stack buffer r1+80
  - result: unused

## 0x826EFCA0 (size 0x310)
- reaches: DbgPrint, EtxProducerLog, EtxProducerUnregister, KeBugCheckEx, KeGetCurrentProcessType, KeReleaseMutant, KeResetEvent, KeSetBasePriorityThread, KeSetEvent, KeWaitForSingleObject, NtAllocateVirtualMemory, NtClose, NtFreeVirtualMemory, NtSetEvent, ObDereferenceObject, ObReferenceObjectByHandle, RtlCompareMemoryUlong, RtlEnterCriticalSection, RtlLeaveCriticalSection, XamGetActiveDashAppInfo, XamGetCurrentTitleId, XamXStudioRequest, XamXlfsUninitializeUploadQueue, XexGetProcedureAddress
- call 0x825AB8C8 in 0x825AB7D8: strings -
  - args: -
  - result: unused

## 0x826F2150 (size 0x2AC)
- reaches: DbgPrint, EtxProducerLog, EtxProducerRegister, EtxProducerUnregister, ExCreateThread, KeBugCheckEx, KeGetCurrentProcessType, KeInitializeMutant, KeReleaseMutant, KeResumeThread, KeSetAffinityThread, KeSetBasePriorityThread, KeSetEvent, KeWaitForSingleObject, KfAcquireSpinLock, KfReleaseSpinLock, NtAllocateVirtualMemory, NtClose, NtCreateEvent, NtFreeVirtualMemory, NtQueryFullAttributesFile, NtQueryVirtualMemory, NtSetEvent, NtWaitForSingleObjectEx, ObDereferenceObject, ObReferenceObjectByHandle, RtlCompareMemoryUlong, RtlEnterCriticalSection, RtlInitAnsiString, RtlInitializeCriticalSection, RtlLeaveCriticalSection, RtlNtStatusToDosError, RtlRaiseException, XGetVideoMode, XamContentClose, XamContentOpenFile, XamGetActiveDashAppInfo, XamGetCurrentTitleId, XamNuiCameraTiltSetCallback, XamNuiIdentityGetSessionId, XamUserNuiGetUserIndex, XamXStudioRequest, XamXlfsInitializeUploadQueue, XamXlfsUninitializeUploadQueue, XexGetProcedureAddress
- call 0x825ABB94 in 0x825AB9E8: strings -
  - args: r3=copy of r25, r4=constant 5 (0x5)
  - result: unused

## 0x8270EF70 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x8234F8A4 in 0x8234F858: strings -
  - args: r3=copy of r30
  - result: cmplwi r3,0
- call 0x8234F8BC in 0x8234F858: strings -
  - args: r3=copy of r30
  - result: unused
- call 0x82350104 in 0x82350000: strings -
  - args: r3=copy of r25
  - result: r24 = r3
- call 0x82350110 in 0x82350000: strings -
  - args: r3=copy of r25
  - result: r25 = r3
- call 0x82350700 in 0x82350688: strings -
  - args: r3=static 0x82CF07E0
  - result: unused
- call 0x82350DAC in 0x82350C90: strings -
  - args: r3=static 0x82CF07E0
  - result: unused
- call 0x82351E80 in 0x82351E40: strings -
  - args: r3=static 0x82CF07E0
  - result: unused
- call 0x82356554 in 0x82356538: strings -
  - args: r3=static 0x82CF07E0
  - result: unused
- call 0x8235AE68 in 0x8235ACF0: strings -
  - args: r3=static 0x82CF07E0
  - result: unused
- call 0x8235F094 in 0x8235EFF8: strings -
  - args: r3=static 0x82CF07E0
  - result: unused
- call 0x8235FB34 in 0x8235FA40: strings -
  - args: r3=static 0x82CF07E0
  - result: unused

## 0x8270EF88 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x8263E250 in 0x8263E238: strings -
  - args: -
  - result: r30 = r3

## 0x8270EF90 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82592C20 in 0x825929B8: strings -
  - args: r3=copy of r24
  - result: r30 = r3
- call 0x82592C2C in 0x825929B8: strings -
  - args: r3=copy of r28
  - result: unused

## 0x82727F40 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x825B2470 in 0x825B23A8: strings -
  - args: -
  - result: unused
- call 0x825B2EA0 in 0x825B2DF0: strings -
  - args: -
  - result: unused
- call 0x825B3104 in 0x825B3008: strings -
  - args: -
  - result: unused

## 0x82727F50 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x825B11C8 in 0x825B1148: strings -
  - args: r3=constant 64 (0x40)
  - result: unused

## 0x82727F60 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x825B2494 in 0x825B23A8: strings -
  - args: r3=constant 5 (0x5), r4=stack buffer r1+96, r5=stack buffer r1+84
  - result: cmpwi r3,0

## 0x82729828 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x825B0EF0 in 0x825B0ED0: strings -
  - args: -
  - result: unused
- call 0x825B10B0 in 0x825B1058: strings -
  - args: -
  - result: unused
- call 0x825B2E2C in 0x825B2DF0: strings -
  - args: -
  - result: unused
- call 0x825B310C in 0x825B3008: strings -
  - args: -
  - result: unused

## 0x82729838 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x825B24EC in 0x825B23A8: strings -
  - args: r3=copy of r30
  - result: unused

## 0x82729848 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x825B2E84 in 0x825B2DF0: strings -
  - args: r3=r30+8, r4=field 96(r1)
  - result: unused

## 0x82729868 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x825B2FC4 in 0x825B2F28: strings -
  - args: r3=stack buffer r1+104, r4=field 0(r3), r5=constant 33 (0x21), r6=constant 1 (0x1), r7=copy of r31
  - result: unused

## 0x82729890 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x825B2150 in 0x825B20E0: strings -
  - args: r3=stack buffer r1+104, r4=field 0(r28), r5=copy of r31
  - result: unused

## 0x827298B0 (size 0x48)
- reaches: pure (no imports reached)
- call 0x825B21D0 in 0x825B20E0: strings -
  - args: r3=stack buffer r1+104, r4=field 0(r28), r5=constant 0 (0x0), r6=stack buffer r1+2160, r7=constant 0 (0x0), r8=constant 1 (0x1), r10=stack buffer r1+84; f1=loaded from 3516(r11)
  - result: unused

## 0x827298F8 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x825B2230 in 0x825B21E0: strings -
  - args: r3=stack buffer r1+104, r9=field 8(r11), r10=stack buffer r1+96
  - result: unused

## 0x8272ABF8 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x825B0EF4 in 0x825B0ED0: strings -
  - args: -
  - result: unused

## 0x8272AC08 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x825B10D8 in 0x825B1058: strings -
  - args: r3=copy of r30, r4=field 32(r11), r5=field 10328(r10), r6=constant 0 (0x0), r7=stack buffer r31+8, r10=constant 0x82E30000
  - result: unused

## 0x8272AC30 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x825B0EB4 in 0x825B0E70: strings -
  - args: r3=stack buffer r31+8
  - result: unused

## 0x8272AC40 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x825B2F04 in 0x825B2ED0: strings -
  - args: r3=field 10328(r10), r4=stack buffer r1+88, r10=constant 0x82E30000
  - result: unused

## 0x8272BB40 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x825B3224 in 0x825B3008: strings -
  - args: r3=field 0(r3), r4=constant 1 (0x1)
  - result: unused

## 0x8272BC18 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x825B11A4 in 0x825B1148: strings -
  - args: r3=stack buffer r1+80, r4=constant 3 (0x3)
  - result: r30 = r3

## 0x82732248 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x825206AC in 0x82520350: strings -
  - args: r3=field 76(r30)
  - result: cmpwi r3,0
- call 0x825206F8 in 0x82520350: strings -
  - args: r3=field 76(r30)
  - result: cmpwi r3,0
- call 0x82520754 in 0x82520350: strings -
  - args: r3=field 76(r30)
  - result: stores r3 to 92(r30)
- call 0x82520A24 in 0x82520350: strings -
  - args: r3=field 76(r30)
  - result: cmpwi r3,0
- call 0x8259E07C in 0x8259E060: strings -
  - args: r3=copy of r4
  - result: r29 = r3
- call 0x825AA654 in 0x825AA5F0: strings -
  - args: r3=field 132(r31)
  - result: r4 = r3
- call 0x8266FAE0 in 0x8266FA58: strings -
  - args: -
  - result: unused
- call 0x8266FAEC in 0x8266FA58: strings -
  - args: -
  - result: unused
- call 0x826786B0 in 0x82678648: strings -
  - args: r3=copy of r30
  - result: stores r3 to 88(r31)
- call 0x82678704 in 0x82678648: strings -
  - args: r3=copy of r30
  - result: stores r3 to 96(r31)
- call 0x8267880C in 0x82678648: strings -
  - args: r3=copy of r30
  - result: stores r3 to 104(r31)
- call 0x82678860 in 0x82678648: strings -
  - args: r3=copy of r30
  - result: stores r3 to 112(r31)
- call 0x82678A78 in 0x826789C0: strings -
  - args: r3=copy of r27
  - result: stores r3 to 88(r31)
- call 0x82678ACC in 0x826789C0: strings -
  - args: r3=copy of r27
  - result: stores r3 to 96(r31)

## 0x82733748 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82646A10 in 0x826469F8: strings -
  - args: r3=r3+84
  - result: low byte (BOOL)

## 0x82733DC0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82518614 in 0x82518588: strings -
  - args: r3=field 180(r31)
  - result: unused
- call 0x82518680 in 0x82518588: strings -
  - args: r3=field 180(r31)
  - result: unused

## 0x8273D0F8 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x8251DE90 in 0x8251DB00: strings -
  - args: r3=field -32744(r28)
  - result: unused
- call 0x8251DF64 in 0x8251DB00: strings -
  - args: r3=field -32744(r29)
  - result: cmpwi r3,0
- call 0x8251E0D4 in 0x8251DB00: strings -
  - args: r3=field -32744(r11)
  - result: unused
- call 0x8251E2E8 in 0x8251DB00: strings -
  - args: r3=field -32744(r29)
  - result: cmpwi r3,0
- call 0x8251E4AC in 0x8251DB00: strings -
  - args: r3=field -32744(r29)
  - result: cmpwi r3,0
- call 0x8251E588 in 0x8251DB00: strings -
  - args: r3=field -32744(r11)
  - result: unused
- call 0x8251E5C4 in 0x8251DB00: strings -
  - args: r3=field -32744(r29)
  - result: cmpwi r3,8
- call 0x825204EC in 0x82520350: strings -
  - args: r3=field -32744(r29)
  - result: stores r3 to 92(r30), cmpwi r3,0, cmpwi r3,6
- call 0x82520558 in 0x82520350: strings -
  - args: r3=field -32744(r29)
  - result: cmpwi r3,0, cmpwi r3,4
- call 0x82520600 in 0x82520350: strings -
  - args: r3=field -32744(r29)
  - result: cmpwi r3,0, cmpwi r3,7
- call 0x82520648 in 0x82520350: strings -
  - args: r3=field -32744(r29)
  - result: cmpwi r3,0, cmpwi r3,7
- call 0x825207A8 in 0x82520350: strings -
  - args: r3=field -32744(r29)
  - result: cmpwi r3,0
- call 0x825207F0 in 0x82520350: strings -
  - args: r3=field -32744(r29)
  - result: stores r3 to 92(r30)
- call 0x82520824 in 0x82520350: strings -
  - args: r3=field -32744(r29)
  - result: stores r3 to 92(r30), cmpwi r3,0, cmpwi r3,6
- call 0x825208B0 in 0x82520350: strings -
  - args: r3=field -32744(r29)
  - result: cmpwi r3,0, cmpwi r3,4
- call 0x8252094C in 0x82520350: strings -
  - args: r3=field -32744(r29)
  - result: cmpwi r3,0, cmpwi r3,7
- call 0x8252099C in 0x82520350: strings -
  - args: r3=field -32744(r29)
  - result: cmpwi r3,0, cmpwi r3,7
- call 0x82520AA4 in 0x82520350: strings -
  - args: r3=field -32744(r29)
  - result: cmpwi r3,0, cmpwi r3,4
- call 0x82520B48 in 0x82520350: strings -
  - args: r3=field -32744(r29)
  - result: cmpwi r3,0, cmpwi r3,8
- call 0x82520B98 in 0x82520350: strings -
  - args: r3=field -32744(r29)
  - result: cmpwi r3,0
- call 0x82520BF4 in 0x82520350: strings -
  - args: r3=field -32744(r29)
  - result: stores r3 to 92(r30)
- call 0x82520C44 in 0x82520350: strings -
  - args: r3=field -32744(r29)
  - result: stores r3 to 92(r30)
- call 0x825390BC in 0x82539080: strings -
  - args: r3=copy of r31
  - result: cmpwi r3,1
- call 0x82539A10 in 0x825399D8: strings -
  - args: r3=copy of r30
  - result: cmpwi r3,3
- call 0x8253AE04 in 0x8253AD70: strings -
  - args: r3=copy of r27
  - result: cmpwi r3,0
- call 0x8253AF00 in 0x8253AE80: strings -
  - args: r3=copy of r28
  - result: cmpwi r3,1
- call 0x8253C870 in 0x8253B270: strings -
  - args: -
  - result: cmpwi r3,2
- call 0x826406D4 in 0x82640648: strings -
  - args: r3=field -32744(r30)
  - result: cmpwi r3,1
- call 0x82640768 in 0x82640708: strings -
  - args: r3=field -32744(r31)
  - result: unused
- call 0x826760CC in 0x82675FA0: strings -
  - args: r3=copy of r30
  - result: stores r3 to 88(r31)
- call 0x82676120 in 0x82675FA0: strings -
  - args: r3=copy of r30
  - result: stores r3 to 96(r31)
- call 0x82676350 in 0x82676248: strings -
  - args: r3=copy of r30
  - result: stores r3 to 88(r31)
- call 0x826763A4 in 0x82676248: strings -
  - args: r3=copy of r30
  - result: stores r3 to 96(r31)

## 0x8275A678 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x825257B0 in 0x82525760: strings -
  - args: r3=copy of r31, r10=field 0(r27)
  - result: r25 = r3
- call 0x826751E4 in 0x826751B0: strings -
  - args: r3=field 32740(r30)
  - result: r30 = r3

## Referenced only from data
0x826DF68C, 0x826DFF20, 0x826E0214, 0x826E0AD8, 0x826E0B3C, 0x826E0B60, 0x826E3B3C, 0x826E8150, 0x826E8C68, 0x82707280, 0x827096C8, 0x8270B228, 0x8270DB28, 0x8270DEC8, 0x8270DFA8, 0x8270F4E0, 0x82710C88, 0x8271B610, 0x8271B6B8, 0x8271B8A0, 0x8271BA28, 0x8271D2F0, 0x8271D388, 0x8271D550, 0x8271E478, 0x8271E4D8, 0x8271E5A8, 0x8271E848, 0x8271EB18, 0x82720778, 0x82720B70, 0x827212E0, 0x82723B90, 0x82725158, 0x82725760, 0x82729400, 0x827294B8, 0x827297C0, 0x8272A170, 0x8272B260, 0x8272C820, 0x82731188, 0x82731310, 0x827313C8, 0x82731510, 0x827326B0, 0x82732760, 0x82732950, 0x82732A30, 0x82732C08, 0x82732CB8, 0x82732E90, 0x82732F58, 0x82733138, 0x82733200, 0x82733408, 0x827334B8, 0x82733698, 0x82733760, 0x82733940, 0x827339F0, 0x82733B18, 0x82733DF0, 0x82734258, 0x82734368, 0x827347B0, 0x82734808, 0x827348E8, 0x827349C8, 0x82734AA8, 0x82734CB0, 0x82734D60, 0x82734E40, 0x82735020, 0x827350D0, 0x827351B0, 0x827354A8, 0x82735C30, 0x82736A18, 0x82736A80, 0x82736AE8, 0x82736B50, 0x82736BB8, 0x82736C20, 0x82736C88, 0x82736CF0, 0x82736D58, 0x82736DC0, 0x82736E28, 0x82736E90, 0x82736EF8, 0x82736F60, 0x82736FC8, 0x82737268, 0x82737570, 0x82737DA0, 0x82737E10, 0x827380B0, 0x82738498, 0x82738568, 0x827385C0, 0x82738630, 0x827387D8, 0x82738980, 0x82739480, 0x82739738, 0x82739A18, 0x82739AE8, 0x82739BC8, 0x82739C18, 0x82739C68, 0x82739CD0, 0x82739D28, 0x82739D78, 0x82739DD8, 0x82739E28, 0x82739E88, 0x82739EE8, 0x82739F60, 0x82739FB0, 0x8273A020, 0x8273A080, 0x8273A0D8, 0x8273A130, 0x8273A188, 0x8273A1D8, 0x8273A230, 0x8273A290, 0x8273A2F8, 0x8273A350, 0x8273A3B0, 0x8273A418, 0x8273A480, 0x8273A4E8, 0x8273A550, 0x8273A5D8, 0x8273A638, 0x8273A6A8, 0x8273A9D8, 0x8273AB10, 0x8273AC20, 0x8273AEE8, 0x8273AFC0, 0x8273B1B8, 0x8273B2E0, 0x8273B348, 0x8273BA50, 0x8273BCF8, 0x8273BD60, 0x8273BEE8, 0x8273C160, 0x8273C1F8, 0x8273C498, 0x8273C570, 0x8273C9A8, 0x8273CE80, 0x8273CEE8, 0x8273CF50, 0x8273CFB8, 0x8273D130, 0x8273D7D0, 0x8273D9F8, 0x8273DA60, 0x8273DE58, 0x8273DFA0, 0x8273E160, 0x8273E238, 0x8273E668, 0x8273E6F0, 0x8273E810, 0x8273EAF0, 0x8273EB50, 0x8273EBB0, 0x8273EC10, 0x8273EC70, 0x8273ECD0, 0x8273ED30, 0x8273ED90, 0x8273EE10, 0x8273F290, 0x8273F340, 0x8273F610, 0x8273FE00, 0x8273FED8, 0x8273FF40, 0x8273FFA8, 0x82740010, 0x82740078, 0x827400E0, 0x82740498, 0x82740520, 0x82740580, 0x82740638, 0x82740698, 0x82740838, 0x827409E0, 0x82740B18, 0x82741198, 0x82741328, 0x827413B0, 0x827418A0, 0x82741900, 0x82741960, 0x82741A20, 0x82741B20, 0x82741B90, 0x82742300, 0x82742368, 0x82742510, 0x82742658, 0x82742788, 0x827427F0, 0x82742858, 0x827428C0, 0x82742920, 0x82742FE8, 0x82743050, 0x82743AD0, 0x82743BC0, 0x82743C70, 0x82743E90, 0x827469C8, 0x82746D30, 0x827474F0, 0x827477E0, 0x827491E8, 0x8274A2B0, 0x8274A310, 0x8274C530, 0x8274C5F0, 0x8274C698, 0x8274C760, 0x8274C948, 0x8274CB48, 0x8274CF88, 0x8274D128, 0x8274D1E0, 0x8274D588, 0x8274DA20, 0x8274EE60, 0x8274EFE8, 0x8274F280, 0x8274F2D8, 0x8274F898, 0x8274FEF0, 0x8274FFB0, 0x82751040, 0x82751100, 0x827511A8, 0x82751330, 0x82751B88, 0x82751CC8, 0x82751D48, 0x82751DA8, 0x82751EE0, 0x82751F60, 0x82752010, 0x82752150, 0x82752270, 0x82752390, 0x82752680, 0x82752BC8, 0x82752C48, 0x82752CF8, 0x82752DA0, 0x82752E38, 0x82752EE8, 0x82752F40, 0x82752F98, 0x82753068, 0x82753250, 0x827532C0, 0x82753460, 0x827535C0, 0x827544C0, 0x82755380, 0x82756788, 0x82756AF0, 0x82756D10, 0x82756E70, 0x82757080, 0x827570A8, 0x82757218, 0x82757298, 0x82757308, 0x82757368, 0x827573B8, 0x827584C0, 0x82758520, 0x82758590, 0x827589C0, 0x82758E38, 0x82759268, 0x827596B0, 0x82759908, 0x82759A28, 0x8275A178, 0x8275A1E8, 0x8275A488, 0x8275A4F0, 0x8275A5B8, 0x8275A618, 0x8275A680, 0x8275A6E0, 0x8275AD60, 0x8275B128, 0x8275B640, 0x8275B720, 0x8275B918, 0x8275BC38, 0x8275BD48, 0x8275C058, 0x8275C0C0, 0x8275C128, 0x8275C2D0, 0x8275C380, 0x8275C540, 0x8275CF60, 0x8275D178, 0x8275DCE8, 0x8275DF48, 0x8275E0B0, 0x8275E460, 0x8275E800, 0x8275FC38, 0x8275FD90, 0x8275FEB0
