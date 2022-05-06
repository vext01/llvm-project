; RUN: llc -debug-only=legalize-types -print-after=finalize-isel %s -O1 -mtriple=x86_64-unknown-unknown -o /dev/null 2>&1 | FileCheck %s


declare void @llvm.experimental.stackmap(i64, i32, ...)

define dso_local i32 @main(i32 %argc, i8** %argv) {
entry:
  %x = icmp eq i32 %argc, 5
  call void (i64, i32, ...) @llvm.experimental.stackmap(i64 0, i32 0, i1 %x, i7 2)
  ; CHECK: Legalizing node: t14: ch,glue = stackmap t10, t10:1, Constant:i64<0>, Constant:i32<0>, t7, Constant:i7<2>
  ; CHECK-NEXT: Analyzing result type: ch
  ; CHECK-NEXT: Legal result type
  ; CHECK-NEXT: Analyzing result type: glue
  ; CHECK-NEXT: Legal result type
  ; CHECK-NEXT: Analyzing operand: t10: ch,glue = callseq_start t0, TargetConstant:i64<0>, TargetConstant:i64<0>
  ; CHECK-NEXT: Legal operand
  ; CHECK-NEXT: Analyzing operand: t10: ch,glue = callseq_start t0, TargetConstant:i64<0>, TargetConstant:i64<0>
  ; CHECK-NEXT: Legal operand
  ; CHECK-NEXT: Analyzing operand: t11: i64 = Constant<0>
  ; CHECK-NEXT: Legal operand
  ; CHECK-NEXT: Analyzing operand: t12: i32 = Constant<0>
  ; CHECK-NEXT: Legal operand
  ; CHECK-NEXT: Analyzing operand: t7: i1 = setcc t2, Constant:i32<5>, seteq:ch
  ; CHECK-NEXT: Promote integer operand: t14: ch,glue = stackmap t10, t10:1, Constant:i64<0>, Constant:i32<0>, t7, Constant:i7<2>

  ; CHECK: Legalizing node: t14: ch,glue = stackmap t10, t10:1, Constant:i64<0>, Constant:i32<0>, t23, Constant:i7<2>
  ; CHECK-NEXT: Analyzing result type: ch
  ; CHECK-NEXT: Legal result type
  ; CHECK-NEXT: Analyzing result type: glue
  ; CHECK-NEXT: Legal result type
  ; CHECK-NEXT: Analyzing operand: t10: ch,glue = callseq_start t0, TargetConstant:i64<0>, TargetConstant:i64<0>
  ; CHECK-NEXT: Legal operand
  ; CHECK-NEXT: Analyzing operand: t10: ch,glue = callseq_start t0, TargetConstant:i64<0>, TargetConstant:i64<0>
  ; CHECK-NEXT: Legal operand
  ; CHECK-NEXT: Analyzing operand: t11: i64 = Constant<0>
  ; CHECK-NEXT: Legal operand
  ; CHECK-NEXT: Analyzing operand: t12: i32 = Constant<0>
  ; CHECK-NEXT: Legal operand
  ; CHECK-NEXT: Analyzing operand: t23: i8 = and t21, Constant:i8<1>
  ; CHECK-NEXT: Legal operand
  ; CHECK-NEXT: Analyzing operand: t13: i7 = Constant<2>
  ; CHECK-NEXT: Promote integer operand: t14: ch,glue = stackmap t10, t10:1, Constant:i64<0>, Constant:i32<0>, t23, Constant:i7<2>

  ; CHECK: # Machine code for function main
  ; CHECK: bb.0.entry:
  ; CHECK: STACKMAP 0, 0, killed %3:gr8, 2, 2, implicit-def dead early-clobber $r11
  ; CHECK: # End machine code for function main
  ret i32 0
}
