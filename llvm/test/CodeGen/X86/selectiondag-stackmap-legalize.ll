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
;     CHECK-NEXT:   .short 7
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
;         CHECK-NEXT: .byte   1
;         CHECK-NEXT: .byte   0
;         CHECK-NEXT: .short  1
;         CHECK-NEXT: .short  {{.*}}
;         CHECK-NEXT: .short  0
;         CHECK-NEXT: .long   0
;       Location[3]
;         CHECK-NEXT: .byte   1
;         CHECK-NEXT: .byte   0
;         CHECK-NEXT: .short  16
;         CHECK-NEXT: .short  {{.*}}
;         CHECK-NEXT: .short  0
;         CHECK-NEXT: .long   0
;       Location[4]
;         CHECK-NEXT: .byte   1
;         CHECK-NEXT: .byte   0
;         CHECK-NEXT: .short  16
;         CHECK-NEXT: .short  {{.*}}
;         CHECK-NEXT: .short  0
;         CHECK-NEXT: .long   0
;       Location[5]
;         CHECK-NEXT: .byte   4
;         CHECK-NEXT: .byte   0
;         CHECK-NEXT: .short  8
;         CHECK-NEXT: .short  {{.*}}
;         CHECK-NEXT: .short  0
;         CHECK-NEXT: .long   66
;       Location[4]
;         CHECK-NEXT: .byte   1
;         CHECK-NEXT: .byte   0
;         CHECK-NEXT: .short  4
;         CHECK-NEXT: .short  {{.*}}
;         CHECK-NEXT: .short  0
;         CHECK-NEXT: .long   0

@p32 = external global i8 addrspace(270)*

declare void @llvm.experimental.stackmap(i64, i32, ...)

define dso_local i32 @main(i32 %argc, i8** %argv) {
entry:
  %i1reg = icmp eq i32 %argc, 5
  %i7reg = zext i1 %i1reg to i7
  %i128reg = zext i1 %i1reg to i128
  %halfreg = sitofp i32 %argc to half
  %ptr32 = load i8 addrspace(270)*, i8 addrspace(270)** @p32
  call void (i64, i32, ...) @llvm.experimental.stackmap(
    i64 0,
    i32 0,
    i1 %i1reg,
    i7 22,
    i7 %i7reg,
    half 1.0,
    half %halfreg,
    i128 66,
    ; FIXME: test non-constant i128 once these are fixed:
    ;  - https://github.com/llvm/llvm-project/issues/26431
    ;  - https://github.com/llvm/llvm-project/issues/55957
    ;i128 %i128reg
    i8 addrspace(270)* %ptr32)
  ret i32 0
}
