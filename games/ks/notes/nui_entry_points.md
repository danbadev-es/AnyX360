# Entry points of 0x829B0000-0x829F0000 called from outside it

81 entry points; 27 more functions of the range are only referenced from data.

## 0x829B0D78 (size 0x1C0)
- reaches: pure (no imports reached)
- call 0x829AEB84 in 0x829AE7D8: strings -
  - args: r3=copy of r31
  - result: unused

## 0x829B0F38 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x829AE4A0 in 0x829AE470: strings -
  - args: r4=r11+32, r5=copy of r30
  - result: unused
- call 0x829AE51C in 0x829AE470: strings "tRNS"; "PNG file corrupted by ASCII conversion"
  - args: r3=copy of r28, r4=stack buffer r1+80, r5=constant 4 (0x4)
  - result: unused
- call 0x829AE8CC in 0x829AE7D8: strings -
  - args: r3=copy of r31, r4=stack buffer r1+80, r5=constant 4 (0x4)
  - result: unused

## 0x829B0F58 (size 0x6C)
- reaches: pure (no imports reached)
- call 0x8291F390 in 0x8291F2D0: strings -
  - args: r3=field 96(r1), r4=stack buffer r1+144, r5=static 0x8291C858, r9=field 324(r1), r10=constant 0x82920000
  - result: unused
- call 0x829AE450 in 0x829AE308: strings "zlib version error"
  - args: r3=copy of r31, r4=constant 0 (0x0), r5=constant 0 (0x0)
  - result: unused

## 0x829B0FC8 (size 0x2D4)
- reaches: pure (no imports reached)
- call 0x82BBF5F8 in 0x82BBF428: strings -
  - args: r3=stack buffer r1+88, r4=stack buffer r1+80, r5=stack buffer r1+96, r6=constant 6 (0x6)
  - result: unused

## 0x829B3208 (size 0xE0)
- reaches: pure (no imports reached)
- call 0x8293ED58 in 0x8293E4E8: strings -
  - args: r4=r29+16, r5=field 104(r29), r6=field 108(r29), r7=field 112(r29), r8=field 116(r29), r9=field 120(r29)
  - result: unused

## 0x829B32E8 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x8293E708 in 0x8293E4E8: strings -
  - args: r3=copy of r31, r4=field 132(r1)
  - result: unused
- call 0x8293E800 in 0x8293E4E8: strings -
  - args: r3=copy of r31, r4=field 132(r1)
  - result: unused

## 0x829B3508 (size 0x94)
- reaches: pure (no imports reached)
- call 0x8293A698 in 0x8293A1A8: strings -
  - args: r4=copy of r21, r5=copy of r29, r6=copy of r20, r7=copy of r30, r8=copy of r23, r9=constant 0 (0x0)
  - result: r31 = r3
- call 0x8293A708 in 0x8293A1A8: strings -
  - args: r4=copy of r21, r5=constant -1 (0xFFFF), r6=constant 0 (0x0), r7=constant 0 (0x0), r8=constant 0 (0x0), r9=constant 0 (0x0)
  - result: unused
- call 0x8293EBA0 in 0x8293E4E8: strings -
  - args: r4=r29+16, r5=constant 0 (0x0), r6=constant 0 (0x0), r7=constant 0 (0x0), r8=constant 0 (0x0), r9=field 128(r1)
  - result: unused

## 0x829B36B8 (size 0x50)
- reaches: pure (no imports reached)
- call 0x82936250 in 0x82936220: strings -
  - args: r4=field 48(r31)
  - result: unused

## 0x829B3780 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x829645F0 in 0x82964588: strings -
  - args: r3=copy of r31, r4=copy of r30, r5=copy of r29, r6=copy of r28, r7=copy of r27
  - result: cmpwi r3,0

## 0x829B37D8 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x829647B0 in 0x82964760: strings -
  - args: r3=copy of r31, r4=copy of r30
  - result: cmpwi r3,0

## 0x829B4C18 (size 0x11C)
- reaches: pure (no imports reached)
- call 0x82990F40 in 0x8298EF20: strings -
  - args: r3=copy of r28, r4=field 176(r1), r5=stack buffer r1+128
  - result: cmpwi r3,0
- call 0x82990F80 in 0x8298EF20: strings -
  - args: r3=copy of r28, r4=field 180(r1), r5=stack buffer r1+128
  - result: cmpwi r3,0

## 0x829B4D38 (size 0xF4)
- reaches: pure (no imports reached)
- call 0x8299093C in 0x8298EF20: strings -
  - args: r3=copy of r28, r4=stack buffer r1+176
  - result: unused

## 0x829B4E30 (size 0x1C4)
- reaches: pure (no imports reached)
- call 0x8298DF98 in 0x8298DDE0: strings -
  - args: r3=copy of r26, r4=copy of r29, r5=copy of r17, r6=stack buffer r1+164
  - result: cmpwi r3,0, cmpwi r3,0

## 0x829B4FF8 (size 0x78)
- reaches: pure (no imports reached)
- call 0x829AB394 in 0x829AB378: strings -
  - args: r3=constant 2 (0x2)
  - result: r31 = r3
- call 0x829AE32C in 0x829AE308: strings -
  - args: r3=constant 1 (0x1)
  - result: stores r3 to 80(r1), cmplwi r3,0

## 0x829B5070 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x829AE364 in 0x829AE308: strings -
  - args: r3=copy of r31
  - result: unused
- call 0x829AEE88 in 0x829AEE18: strings -
  - args: r3=copy of r31
  - result: unused
- call 0x829AEE9C in 0x829AEE18: strings -
  - args: r3=copy of r30
  - result: unused
- call 0x829AEEB0 in 0x829AEE18: strings -
  - args: r3=copy of r29
  - result: unused

## 0x829B5088 (size 0x70)
- reaches: pure (no imports reached)
- call 0x829AB264 in 0x829AB250: strings -
  - args: r4=copy of r29
  - result: r30 = r3
- call 0x829AE3B4 in 0x829AE308: strings "Incompatible libpng version in application and library"
  - args: r3=copy of r31, r4=constant 8192 (0x2000)
  - result: stores r3 to 1436(r31)

## 0x829B50F8 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x829AE35C in 0x829AE308: strings -
  - args: r4=field 1436(r31)
  - result: unused
- call 0x829AED04 in 0x829AECC8: strings -
  - args: r3=copy of r31, r4=field 1436(r31)
  - result: unused
- call 0x829AED10 in 0x829AECC8: strings -
  - args: r3=copy of r31, r4=field 1500(r31)
  - result: unused
- call 0x829AED1C in 0x829AECC8: strings -
  - args: r3=copy of r31, r4=field 1496(r31)
  - result: unused
- call 0x829AED28 in 0x829AECC8: strings -
  - args: r3=copy of r31, r4=field 1652(r31)
  - result: unused
- call 0x829AED34 in 0x829AECC8: strings -
  - args: r3=copy of r31, r4=field 1656(r31)
  - result: unused
- call 0x829AED40 in 0x829AECC8: strings -
  - args: r3=copy of r31, r4=field 1592(r31)
  - result: unused
- call 0x829AED70 in 0x829AECC8: strings -
  - args: r3=copy of r31, r4=field 1628(r31)
  - result: unused
- call 0x829AEDA4 in 0x829AECC8: strings -
  - args: r3=copy of r31
  - result: unused
- call 0x829AEDBC in 0x829AECC8: strings -
  - args: r3=copy of r31, r4=field 1604(r31)
  - result: unused

## 0x829B5120 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x829AEB70 in 0x829AE7D8: strings -
  - args: r3=copy of r31, r4=field 1496(r31), r5=field 1500(r31), r6=r11+1
  - result: unused

## 0x829B5140 (size 0x134)
- reaches: pure (no imports reached)
- call 0x829AB2E8 in 0x829AB2C8: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 0 (0x0)
  - result: stores r3 to 1536(r31)
- call 0x829AB348 in 0x829AB308: strings -
  - args: r3=field 1536(r31)
  - result: stores r3 to 1536(r31)

## 0x829B59F8 (size 0x88)
- reaches: pure (no imports reached)
- call 0x829AB4CC in 0x829AB3C8: strings -
  - args: r3=copy of r31
  - result: unused

## 0x829B6B80 (size 0x128)
- reaches: pure (no imports reached)
- call 0x829AB484 in 0x829AB3C8: strings -
  - args: r3=copy of r31
  - result: unused

## 0x829B6CA8 (size 0x68)
- reaches: pure (no imports reached)
- call 0x829AB970 in 0x829AB850: strings -
  - args: r3=copy of r31
  - result: unused
- call 0x829ABA44 in 0x829AB9A8: strings -
  - args: r3=copy of r31
  - result: unused

## 0x829B6D10 (size 0x4C)
- reaches: pure (no imports reached)
- call 0x829ADE48 in 0x829ADE20: strings -
  - args: r3=copy of r31
  - result: unused

## 0x829B6D60 (size 0x3C)
- reaches: pure (no imports reached)
- call 0x829ACB8C in 0x829ACA30: strings -
  - args: r3=copy of r28
  - result: unused

## 0x829B6DA0 (size 0x3C)
- reaches: pure (no imports reached)
- call 0x829AC9BC in 0x829AC690: strings -
  - args: r3=copy of r30
  - result: stores r3 to 0(r29)

## 0x829B76F0 (size 0x70)
- reaches: pure (no imports reached)
- call 0x829ABC48 in 0x829ABC28: strings -
  - args: -
  - result: unused

## 0x829B8840 (size 0x84)
- reaches: pure (no imports reached)
- call 0x829AEDC4 in 0x829AECC8: strings -
  - args: r3=stack buffer r31+1380
  - result: unused

## 0x829B8A20 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x829AE3E8 in 0x829AE308: strings "1.1.4"
  - args: r3=stack buffer r31+1380, r4=static 0x8205247C "1.1.4", r5=constant 56 (0x38), r10=static 0x829AB2C0
  - result: cmpwi r3,-6, cmpwi r3,-4

## 0x829B8A30 (size 0x550)
- reaches: pure (no imports reached)
- call 0x829AE988 in 0x829AE7D8: strings -
  - args: r3=stack buffer r31+1380, r4=constant 1 (0x1), r10=field 1384(r31)
  - result: cmpwi r3,1, cmpwi r3,0

## 0x829B8F80 (size 0x34)
- reaches: pure (no imports reached)
- call 0x829AE544 in 0x829AE470: strings -
  - args: r3=copy of r28, r4=copy of r31, r5=constant 4 (0x4)
  - result: unused
- call 0x829AE8F4 in 0x829AE7D8: strings -
  - args: r3=copy of r31, r4=copy of r30, r5=constant 4 (0x4)
  - result: unused
- call 0x829AE96C in 0x829AE7D8: strings -
  - args: r3=copy of r31, r5=field 1384(r31)
  - result: unused

## 0x829B9120 (size 0x25C)
- reaches: pure (no imports reached)
- call 0x829AEAA0 in 0x829AE7D8: strings -
  - args: r3=copy of r31, r4=copy of r26, r5=constant 255 (0xFF)
  - result: unused
- call 0x829AEBDC in 0x829AE7D8: strings -
  - args: r3=copy of r31, r4=copy of r26, r10=r27+28
  - result: unused
- call 0x829AEC10 in 0x829AE7D8: strings -
  - args: r3=copy of r31, r4=copy of r25, r5=constant 255 (0xFF)
  - result: unused
- call 0x829AEC28 in 0x829AE7D8: strings -
  - args: r3=copy of r31, r4=copy of r26, r5=constant 255 (0xFF)
  - result: unused

## 0x829B9380 (size 0x33C)
- reaches: pure (no imports reached)
- call 0x829AEBB8 in 0x829AE7D8: strings -
  - args: r3=copy of r30, r4=r11+1
  - result: unused

## 0x829B96C0 (size 0x220)
- reaches: pure (no imports reached)
- call 0x829AEB58 in 0x829AE7D8: strings -
  - args: r5=r11+1, r6=r9+1, r9=field 1496(r31), r10=r10+7
  - result: unused

## 0x829B98E0 (size 0x20C)
- reaches: pure (no imports reached)
- call 0x829AE7B0 in 0x829AE788: strings -
  - args: -
  - result: unused
- call 0x829AE7FC in 0x829AE7D8: strings -
  - args: -
  - result: unused

## 0x829B9AF0 (size 0x6C)
- reaches: pure (no imports reached)
- call 0x829AE528 in 0x829AE470: strings "tRNS"
  - args: r3=copy of r28, r4=stack buffer r1+80
  - result: r29 = r3
- call 0x829AE8D8 in 0x829AE7D8: strings -
  - args: r3=copy of r31, r4=stack buffer r1+80
  - result: stores r3 to 1532(r31)

## 0x829B9B60 (size 0xF4)
- reaches: pure (no imports reached)
- call 0x829AE8BC in 0x829AE7D8: strings "Decompression error"
  - args: r3=copy of r31, r4=constant 0 (0x0)
  - result: unused

## 0x829B9C58 (size 0x2C4)
- reaches: pure (no imports reached)
- call 0x829AE588 in 0x829AE470: strings -
  - args: r3=copy of r28, r4=copy of r27, r5=copy of r29
  - result: unused

## 0x829B9F20 (size 0x1B8)
- reaches: pure (no imports reached)
- call 0x829AE5D0 in 0x829AE470: strings -
  - args: r3=copy of r28, r4=copy of r27, r5=copy of r29
  - result: unused

## 0x829BA0D8 (size 0x7C)
- reaches: pure (no imports reached)
- call 0x829AE618 in 0x829AE470: strings -
  - args: r3=copy of r28, r4=copy of r27, r5=copy of r29
  - result: unused

## 0x829BA158 (size 0x19C)
- reaches: pure (no imports reached)
- call 0x829AE694 in 0x829AE470: strings -
  - args: r3=copy of r28, r4=copy of r27, r5=copy of r29
  - result: unused

## 0x829BA2F8 (size 0x16C)
- reaches: pure (no imports reached)
- call 0x829AE6DC in 0x829AE470: strings -
  - args: r3=copy of r28, r4=copy of r27, r5=copy of r29
  - result: unused

## 0x829BA468 (size 0x234)
- reaches: pure (no imports reached)
- call 0x829AE724 in 0x829AE470: strings -
  - args: -
  - result: unused

## 0x829BA6A0 (size 0x6C)
- reaches: pure (no imports reached)
- call 0x829AE72C in 0x829AE470: strings -
  - args: -
  - result: unused

## 0x829BA710 (size 0x37C)
- reaches: pure (no imports reached)
- call 0x829AEAA8 in 0x829AE7D8: strings -
  - args: r3=copy of r31
  - result: unused
- call 0x829AEC30 in 0x829AE7D8: strings -
  - args: r3=copy of r31
  - result: unused

## 0x829C5988 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82260520 in 0x82260458: strings "Team1IsHuman"; "Team0IsHuman"
  - args: r3=stack buffer r1+80, r4=stack buffer r1+84
  - result: cmpwi r3,0
- call 0x82260668 in 0x82260560: strings "Team3GameDurationSecs"; "Team3Score"
  - args: r3=stack buffer r1+80, r4=stack buffer r1+84
  - result: cmpwi r3,0
- call 0x82261CE0 in 0x82261BE0: strings "Player3Score"; "Player2Score"
  - args: r3=stack buffer r1+80, r4=stack buffer r1+84
  - result: cmpwi r3,0
- call 0x822623FC in 0x822622E8: strings "Duration"; "Player3Score"
  - args: r3=stack buffer r1+80, r4=stack buffer r1+84
  - result: cmpwi r3,0
- call 0x822636A0 in 0x822635D8: strings "Team1IsHuman"; "Team0IsHuman"
  - args: r3=stack buffer r1+80, r4=stack buffer r1+84
  - result: cmpwi r3,0
- call 0x822637E8 in 0x822636E0: strings "Team3GameDurationSecs"; "Team3Score"
  - args: r3=stack buffer r1+80, r4=stack buffer r1+84
  - result: cmpwi r3,0
- call 0x822643BC in 0x82264238: strings "Team1NumberOfShotsOnTarget"; "Team1NumberOfShots"
  - args: r3=stack buffer r1+80, r4=stack buffer r1+84
  - result: cmpwi r3,0
- call 0x82264E28 in 0x82264CE0: strings "Player1TotalStunned"; "Player1TotalKnockedDown"
  - args: r3=stack buffer r1+80, r4=stack buffer r1+84
  - result: cmpwi r3,0
- call 0x8249C018 in 0x8249BF48: strings -
  - args: r3=stack buffer r1+88, r4=stack buffer r1+84
  - result: cmpwi r3,0
- call 0x82F4E8C0 in 0x82F4E770: strings -
  - args: r3=stack buffer r1+84, r4=stack buffer r1+80
  - result: cmpwi r3,0

## 0x829C62A8 (size 0x1E4)
- reaches: ExGetXConfigSetting, KfAcquireSpinLock, KfReleaseSpinLock, XamNuiCameraElevationGetAngle, XamNuiCameraElevationSetAngle
- call 0x82F4E3B4 in 0x82F4E330: strings -
  - args: r7=r11+11424, r8=r11+11384; f1=copy of f31, f2=copy of f29, f3=copy of f30
  - result: unused
- call 0x82F4E888 in 0x82F4E770: strings -
  - args: r7=r5+11424, r8=r5+11384; f1=loaded from 11636(r5), f2=loaded from 11640(r5), f3=loaded from 11644(r5)
  - result: unused

## 0x829C79C8 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82283CAC in 0x82283C60: strings -
  - args: -
  - result: unused
- call 0x8249B1F0 in 0x8249AC58: strings -
  - args: -
  - result: unused

## 0x829C7D00 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82F5D910 in 0x82F5D8C0: strings -
  - args: r3=stack buffer r1+96, r10=stack buffer r1+88
  - result: cmpwi r3,0

## 0x829C8658 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82F59288 in 0x82F59258: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=stack buffer r1+80
  - result: unused
- call 0x82F59334 in 0x82F59258: strings -
  - args: r4=constant 2 (0x2), r5=stack buffer r1+84
  - result: cmpwi r3,0
- call 0x82F5E0D4 in 0x82F5E0A0: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=stack buffer r1+80
  - result: unused
- call 0x82F5E3E0 in 0x82F5E3B8: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=stack buffer r1+80
  - result: unused
- call 0x82F5E49C in 0x82F5E3B8: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2), r5=stack buffer r1+84
  - result: unused

## 0x829C8668 (size 0x1DC)
- reaches: KeWaitForSingleObject, RtlNtStatusToDosError
- call 0x82F592A8 in 0x82F59258: strings -
  - args: r4=constant 0 (0x0), r5=copy of r30
  - result: cmpwi r3,0
- call 0x82F592D0 in 0x82F59258: strings -
  - args: r3=constant 0 (0x0), r4=constant 6 (0x6), r5=copy of r30
  - result: cmpwi r3,0
- call 0x82F592F8 in 0x82F59258: strings -
  - args: r3=constant 0 (0x0), r4=constant 7 (0x7), r5=copy of r30
  - result: cmpwi r3,0
- call 0x82F59318 in 0x82F59258: strings -
  - args: r3=constant 0 (0x0), r4=constant 8 (0x8), r5=copy of r31
  - result: cmpwi r3,0
- call 0x82F5935C in 0x82F59258: strings -
  - args: r3=constant 0 (0x0), r4=constant 5 (0x5), r5=copy of r31
  - result: cmpwi r3,0
- call 0x82F5E368 in 0x82F5E338: strings -
  - args: r3=constant 0 (0x0), r4=copy of r3, r5=stack buffer r1+80
  - result: unused

## 0x829C90F0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x824A1538 in 0x824A14F0: strings "WhiteBalanceMode"
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=copy of r3
  - result: unused
- call 0x82F5E16C in 0x82F5E0A0: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2), r5=constant 0 (0x0)
  - result: unused
- call 0x82F5E250 in 0x82F5E1B8: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2), r5=field 20(r31)
  - result: unused
- call 0x82F5E2B8 in 0x82F5E290: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=field 0(r30)
  - result: unused
- call 0x82F5E554 in 0x82F5E4D8: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 1 (0x1)
  - result: unused
- call 0x82F5E56C in 0x82F5E4D8: strings -
  - args: r3=constant 0 (0x0), r4=constant 2 (0x2), r5=constant 0 (0x0)
  - result: unused
- call 0x82F5E620 in 0x82F5E4D8: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 0 (0x0)
  - result: unused
- call 0x82F5E638 in 0x82F5E4D8: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 1 (0x1)
  - result: unused
- call 0x82F5E650 in 0x82F5E4D8: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0), r5=constant 0 (0x0)
  - result: unused

## 0x829C9100 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x824A1560 in 0x824A14F0: strings "ExposureTime"
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0)
  - result: unused
- call 0x824A1584 in 0x824A14F0: strings "AutoExposureTargetStopAdjust"
  - args: r3=constant 0 (0x0), r4=constant 5 (0x5)
  - result: unused
- call 0x824A15AC in 0x824A14F0: strings "ColorGainRed"
  - args: r3=constant 0 (0x0), r4=constant 6 (0x6)
  - result: unused
- call 0x824A15CC in 0x824A14F0: strings "ColorGainGreen"
  - args: r3=constant 0 (0x0), r4=constant 7 (0x7)
  - result: unused
- call 0x824A15EC in 0x824A14F0: strings "ColorGainBlue"
  - args: r3=constant 0 (0x0), r4=constant 8 (0x8)
  - result: unused
- call 0x82F5E100 in 0x82F5E0A0: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0); f1=loaded from 23352(r11)
  - result: unused
- call 0x82F5E120 in 0x82F5E0A0: strings -
  - args: r3=constant 0 (0x0), r4=constant 6 (0x6); f1=copy of f31
  - result: unused
- call 0x82F5E13C in 0x82F5E0A0: strings -
  - args: r3=constant 0 (0x0), r4=constant 7 (0x7); f1=copy of f31
  - result: unused
- call 0x82F5E188 in 0x82F5E0A0: strings -
  - args: r3=constant 0 (0x0), r4=constant 5 (0x5); f1=loaded from -21216(r11)
  - result: unused
- call 0x82F5E1EC in 0x82F5E1B8: strings -
  - args: r3=constant 0 (0x0), r4=constant 0 (0x0); f1=loaded from 4(r31)
  - result: unused
- call 0x82F5E204 in 0x82F5E1B8: strings -
  - args: r3=constant 0 (0x0), r4=constant 6 (0x6); f1=loaded from 8(r31)
  - result: unused
- call 0x82F5E220 in 0x82F5E1B8: strings -
  - args: r3=constant 0 (0x0), r4=constant 7 (0x7); f1=loaded from 12(r31)
  - result: unused
- call 0x82F5E268 in 0x82F5E1B8: strings -
  - args: r3=constant 0 (0x0), r4=constant 5 (0x5); f1=loaded from 24(r31)
  - result: unused
- call 0x82F5E58C in 0x82F5E4D8: strings -
  - args: r3=constant 0 (0x0), r4=constant 5 (0x5); f1=loaded from -21216(r11)
  - result: unused

## 0x829C9110 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82F5E5C4 in 0x82F5E4D8: strings -
  - args: r3=constant 0 (0x0), r4=stack buffer r31+92
  - result: unused
- call 0x82F5E5F0 in 0x82F5E4D8: strings -
  - args: r3=constant 0 (0x0), r4=stack buffer r31+48
  - result: unused

## 0x829CF908 (size 0x158)
- reaches: EtxProducerRegister, EtxProducerUnregister, ExCreateThread, KeResumeThread, KeSetAffinityThread, KeSetBasePriorityThread, KeSetEvent, KeWaitForSingleObject, KfAcquireSpinLock, KfReleaseSpinLock, NtClose, NtCreateEvent, NtSetEvent, ObCreateObject, ObDereferenceObject, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlInitializeCriticalSection, RtlLeaveCriticalSection, RtlNtStatusToDosError, XGetVideoMode, XamContentClose, XamContentOpenFile, XamGetCurrentTitleId, XamNuiCameraTiltSetCallback, XamNuiIdentityGetSessionId, XamUserNuiGetUserIndex, XamXStudioRequest, XamXlfsInitializeUploadQueue, XamXlfsUninitializeUploadQueue
- call 0x82F4E460 in 0x82F4E3F0: strings -
  - args: r4=constant 3 (0x3)
  - result: cmpwi r3,0

## 0x829CFE50 (size 0x190)
- reaches: RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x82F5D73C in 0x82F5D668: strings -
  - args: r3=constant 2 (0x2), r4=copy of r24, r5=field 80(r1), r6=field 84(r1), r8=copy of r31, r9=copy of r29
  - result: unused
- call 0x82F5D764 in 0x82F5D668: strings -
  - args: r3=constant 2 (0x2), r4=copy of r24, r5=field 96(r1), r6=field 100(r1), r8=copy of r28, r9=copy of r30
  - result: unused

## 0x829CFFE0 (size 0x310)
- reaches: KeResetEvent, KeWaitForSingleObject, ObDereferenceObject, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlLeaveCriticalSection, RtlNtStatusToDosError
- call 0x82F4D77C in 0x82F4D720: strings -
  - args: r3=field 4(r10), r4=constant 0 (0x0), r10=r10+2
  - result: cmpwi r3,0

## 0x829D02F0 (size 0x14C)
- reaches: ObDereferenceObject, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlLeaveCriticalSection, RtlNtStatusToDosError
- call 0x82F4D7B0 in 0x82F4D720: strings -
  - args: r3=field 4(r10), r10=r9+2
  - result: unused

## 0x829D1AD8 (size 0x8C4)
- reaches: KeSetEvent, KfAcquireSpinLock, KfReleaseSpinLock, ObCreateObject, ObDereferenceObject, ObOpenObjectByPointer, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlLeaveCriticalSection, RtlNtStatusToDosError
- call 0x82F4D6EC in 0x82F4D6A0: strings -
  - args: r3=field 8(r30), r5=constant 0 (0x0), r6=constant 2 (0x2), r7=constant 0 (0x0), r8=static 0x831C0004
  - result: unused

## 0x829D23A0 (size 0xB0)
- reaches: KeSetEvent, ObDereferenceObject, RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x82F4D870 in 0x82F4D7D8: strings -
  - args: -
  - result: unused
- call 0x82F4D974 in 0x82F4D7D8: strings -
  - args: -
  - result: unused

## 0x829D24B8 (size 0xEC)
- reaches: RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x82F4DBE4 in 0x82F4DBD8: strings -
  - args: -
  - result: unused

## 0x829D2750 (size 0x118)
- reaches: pure (no imports reached)
- call 0x82F4E5DC in 0x82F4E558: strings -
  - args: r3=r11+5536, r4=r11+11060
  - result: unused

## 0x829D2BE0 (size 0x148)
- reaches: KeSetEvent, ObReferenceObjectByHandle, RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x82F4D830 in 0x82F4D7D8: strings -
  - args: -
  - result: cmpwi r3,0
- call 0x82F4D88C in 0x82F4D7D8: strings -
  - args: r3=copy of r30
  - result: cmpwi r3,0

## 0x829D2D28 (size 0x1F0)
- reaches: KeResetEvent, KeWaitForSingleObject, RtlEnterCriticalSection, RtlLeaveCriticalSection, XamXStudioRequest
- call 0x82F4D8C0 in 0x82F4D7D8: strings -
  - args: r4=stack buffer r1+80
  - result: r28 = r3

## 0x829DA6D8 (size 0x17C)
- reaches: RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x82F4E298 in 0x82F4E218: strings -
  - args: r3=field 4(r4), r4=constant 0 (0x0), r5=constant 1 (0x1), r6=static 0x82F4DAA0, r7=constant 0 (0x0)
  - result: cmpwi r3,0

## 0x829DA858 (size 0x104)
- reaches: RtlEnterCriticalSection, RtlLeaveCriticalSection
- call 0x82F4E654 in 0x82F4E558: strings -
  - args: r3=field 5588(r10), r4=constant 0 (0x0), r5=static 0x82F4E218
  - result: unused

## 0x829DAB70 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82F4DE58 in 0x82F4DE48: strings -
  - args: -
  - result: unused

## 0x829E37B0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x83129614 in 0x831295C8: strings -
  - args: r3=stack buffer r31+56
  - result: unused
- call 0x8312961C in 0x831295C8: strings -
  - args: r3=stack buffer r31+72
  - result: unused

## 0x829E3BC0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x8312964C in 0x83129640: strings -
  - args: -
  - result: stores f1 (float result)

## 0x829E4B18 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82C02434 in 0x82C02288: strings -
  - args: r3=copy of r27, r4=field 140(r31)
  - result: unused
- call 0x82C03130 in 0x82C02F30: strings -
  - args: r3=copy of r30, r4=field 100(r31)
  - result: unused

## 0x829E53B0 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x82893C38 in 0x828939C0: strings -
  - args: r3=stack buffer r1+112
  - result: unused
- call 0x828A0618 in 0x828A0598: strings -
  - args: r3=stack buffer r1+96
  - result: unused
- call 0x828A06B8 in 0x828A0638: strings -
  - args: r3=stack buffer r1+96
  - result: unused
- call 0x828A5148 in 0x828A50E0: strings ".\NetworkWrapperClass.cpp"
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x828BAC48 in 0x828BABE8: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x828BACC8 in 0x828BAC68: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x828BAD48 in 0x828BACE8: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x828CB370 in 0x828CB310: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x828E53B8 in 0x828E5338: strings -
  - args: r3=stack buffer r1+96
  - result: unused
- call 0x828E8F60 in 0x828E8EF8: strings -
  - args: r3=stack buffer r1+96
  - result: unused
- call 0x828E9288 in 0x828E9220: strings -
  - args: r3=stack buffer r1+96
  - result: unused
- call 0x828E9354 in 0x828E92A8: strings -
  - args: r3=stack buffer r1+112
  - result: unused
- call 0x828E93BC in 0x828E92A8: strings -
  - args: r3=stack buffer r1+128
  - result: unused
- call 0x828EBA48 in 0x828EB920: strings -
  - args: r3=stack buffer r1+192
  - result: unused
- call 0x828EBF3C in 0x828EBB90: strings -
  - args: r3=stack buffer r1+192
  - result: unused
- call 0x828EC06C in 0x828EBF50: strings -
  - args: r3=stack buffer r1+192
  - result: unused
- call 0x82E1E9BC in 0x82E1E918: strings -
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x82E1F2AC in 0x82E1F268: strings ".\LogWriter.cpp"
  - args: r3=stack buffer r1+80
  - result: unused
- call 0x82E2243C in 0x82E22218: strings -
  - args: r3=stack buffer r1+96
  - result: unused
- call 0x82E22C90 in 0x82E22B50: strings -
  - args: r3=stack buffer r1+160
  - result: unused

## 0x829E5928 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x83129674 in 0x83129668: strings -
  - args: -
  - result: stores f1 (float result)

## 0x829EE700 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x8227D6D0 in 0x8227D6A8: strings -
  - args: r3=copy of r29
  - result: unused

## 0x829EEA88 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x829F14DC in 0x829F1358: strings -
  - args: r3=copy of r31
  - result: unused

## 0x829EEB28 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x8227D700 in 0x8227D6A8: strings -
  - args: r3=copy of r29, r4=copy of r28
  - result: unused

## 0x829EEC40 (size 0x1C0)
- reaches: pure (no imports reached)
- call 0x8227D518 in 0x8227D498: strings -
  - args: r3=copy of r29
  - result: unused

## 0x829EF778 (size 0x304)
- reaches: pure (no imports reached)
- call 0x829F0F94 in 0x829F00A0: strings -
  - args: r3=copy of r30, r4=copy of r27, r10=constant 0x82000000
  - result: unused
- call 0x829F1310 in 0x829F12C8: strings -
  - args: r3=copy of r30, r4=copy of r31
  - result: cmpwi r3,0
- call 0x829F1348 in 0x829F12C8: strings -
  - args: r3=copy of r30
  - result: unused

## 0x829EFA80 (size 0x1CC)
- reaches: pure (no imports reached)
- call 0x829F11A8 in 0x829F10C8: strings -
  - args: r3=copy of r31; f2=loaded from 208(r30)
  - result: unused
- call 0x829F122C in 0x829F10C8: strings -
  - args: r3=copy of r31; f2=loaded from 208(r30)
  - result: unused

## 0x829EFC50 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x8227D4F8 in 0x8227D498: strings -
  - args: r3=stack buffer r1+80
  - result: unused

## 0x829EFD40 (size not a .pdata start)
- reaches: pure (no imports reached)
- call 0x8227D4E4 in 0x8227D498: strings -
  - args: r3=stack buffer r1+80
  - result: unused

## 0x829EFDB0 (size 0x148)
- reaches: pure (no imports reached)
- call 0x829F0078 in 0x829EFEF8: strings -
  - args: r3=copy of r28, r9=field 444(r30), r10=field 428(r30); f1=loaded from -24788(r11)
  - result: unused

## 0x829EFEF8 (size 0x1A4)
- reaches: pure (no imports reached)
- call 0x8227D628 in 0x8227D540: strings -
  - args: r3=copy of r29, r5=copy of r26, r6=copy of r23, r7=constant 0 (0x0), r8=copy of r25, r9=constant 0 (0x0)
  - result: unused

## Referenced only from data
0x829B3308, 0x829B35A0, 0x829B3708, 0x829B44B0, 0x829B7CB8, 0x829B7E88, 0x829B82A0, 0x829CFE10, 0x829D0B58, 0x829E4C18, 0x829E4DF8, 0x829E5198, 0x829E5278, 0x829E64C8, 0x829E6560, 0x829E6728, 0x829E7690, 0x829E76F0, 0x829E7750, 0x829E7A60, 0x829E7D30, 0x829E8BD8, 0x829E9000, 0x829E9758, 0x829EC050, 0x829ED980, 0x829EDF88
