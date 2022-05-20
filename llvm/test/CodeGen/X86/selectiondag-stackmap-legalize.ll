; RUN: llc < %s -mtriple=x86_64-apple-darwin -mcpu=corei7-avx -enable-patchpoint-liveness=false | FileCheck %s

; CHECK-LABEL:  .section  __LLVM_STACKMAPS,__llvm_stackmaps
; CHECK-NEXT:   __LLVM_StackMaps:

; Header
; CHECK-NEXT:   .byte 3
; CHECK-NEXT:   .byte 0
; CHECK-NEXT:   .short 0

; NumFunctions
; CHECK-NEXT:   .long 1
; NumConstants
; CHECK-NEXT:   .long 0
; NumRecords
; CHECK-NEXT:   .long 1

; StackSizeRecord[NumFunctions]
;   StackSizeRecord[0]
;     CHECK-NEXT:   .quad _main
;     CHECK-NEXT:   .quad 24
;     CHECK-NEXT:   .quad 1

; Constants[NumConstants] (empty)

; StkMapRecord[NumRecords]
;   StkMapRecord[0]
;     CHECK-NEXT:	.quad 0
;     CHECK-NEXT:   .long {{.*}}
;     CHECK-NEXT:   .short {{.*}}
;     NumLocations
;     CHECK-NEXT:   .short 4
;     Location[NumLocations]
;       Location[0]
;         CHECK-NEXT: .byte   1
;         CHECK-NEXT: .byte   0
;         CHECK-NEXT: .short  1
;         CHECK-NEXT: .short  {{.*}}
;         CHECK-NEXT: .short  0
;         CHECK-NEXT: .long   0
;       Location[1]
;         CHECK-NEXT: .byte   4
;         CHECK-NEXT: .byte   0
;         CHECK-NEXT: .short  8
;         CHECK-NEXT: .short  0
;         CHECK-NEXT: .short  0
;         CHECK-NEXT: .long   22
;       Location[2]
;         CHECK-NEXT: .byte   4
;         CHECK-NEXT: .byte   0
;         CHECK-NEXT: .short  8
;         CHECK-NEXT: .short  {{.*}}
;         CHECK-NEXT: .short  0
;         CHECK-NEXT: .long   15360
;       Location[3]
;         CHECK-NEXT: .byte   1
;         CHECK-NEXT: .byte   0
;         CHECK-NEXT: .short  2
;         CHECK-NEXT: .short  {{.*}}
;         CHECK-NEXT: .short  0
;         CHECK-NEXT: .long   0


declare void @llvm.experimental.stackmap(i64, i32, ...)

define dso_local i32 @main(i32 %argc, i8** %argv) {
entry:
  %intreg = icmp eq i32 %argc, 5
  %halfreg = sitofp i32 %argc to half
  call void (i64, i32, ...) @llvm.experimental.stackmap(
    i64 0,
    i32 0,
    i1 %intreg,
    i7 22,
    half 1.0,
    half %halfreg)
  ret i32 0
}
