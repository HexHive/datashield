; NOTE: Assertions have been autogenerated by utils/update_llc_test_checks.py
; RUN: llc < %s -mtriple=i686-unknown -mattr=+sse2 | FileCheck %s --check-prefix=CHECK --check-prefix=X32
; RUN: llc < %s -mtriple=x86_64-unknown -mattr=+sse2 | FileCheck %s --check-prefix=CHECK --check-prefix=X64

; test vector shifts converted to proper SSE2 vector shifts when the shift
; amounts are the same.

define void @shift1a(<2 x i64> %val, <2 x i64>* %dst) nounwind {
; X32-LABEL: shift1a:
; X32:       # BB#0: # %entry
; X32-NEXT:    movl {{[0-9]+}}(%esp), %eax
; X32-NEXT:    psrlq $32, %xmm0
; X32-NEXT:    movdqa %xmm0, (%eax)
; X32-NEXT:    retl
;
; X64-LABEL: shift1a:
; X64:       # BB#0: # %entry
; X64-NEXT:    psrlq $32, %xmm0
; X64-NEXT:    movdqa %xmm0, (%rdi)
; X64-NEXT:    retq
entry:
  %lshr = lshr <2 x i64> %val, < i64 32, i64 32 >
  store <2 x i64> %lshr, <2 x i64>* %dst
  ret void
}

define void @shift1b(<2 x i64> %val, <2 x i64>* %dst, i64 %amt) nounwind {
; X32-LABEL: shift1b:
; X32:       # BB#0: # %entry
; X32-NEXT:    movl {{[0-9]+}}(%esp), %eax
; X32-NEXT:    movd {{.*#+}} xmm1 = mem[0],zero,zero,zero
; X32-NEXT:    pshufd {{.*#+}} xmm1 = xmm1[0,0,1,1]
; X32-NEXT:    movd {{.*#+}} xmm2 = mem[0],zero,zero,zero
; X32-NEXT:    pshufd {{.*#+}} xmm2 = xmm2[0,0,1,1]
; X32-NEXT:    punpckldq {{.*#+}} xmm2 = xmm2[0],xmm1[0],xmm2[1],xmm1[1]
; X32-NEXT:    psrlq %xmm2, %xmm0
; X32-NEXT:    movdqa %xmm0, (%eax)
; X32-NEXT:    retl
;
; X64-LABEL: shift1b:
; X64:       # BB#0: # %entry
; X64-NEXT:    movd %rsi, %xmm1
; X64-NEXT:    psrlq %xmm1, %xmm0
; X64-NEXT:    movdqa %xmm0, (%rdi)
; X64-NEXT:    retq
entry:
  %0 = insertelement <2 x i64> undef, i64 %amt, i32 0
  %1 = insertelement <2 x i64> %0, i64 %amt, i32 1
  %lshr = lshr <2 x i64> %val, %1
  store <2 x i64> %lshr, <2 x i64>* %dst
  ret void
}

define void @shift2a(<4 x i32> %val, <4 x i32>* %dst) nounwind {
; X32-LABEL: shift2a:
; X32:       # BB#0: # %entry
; X32-NEXT:    movl {{[0-9]+}}(%esp), %eax
; X32-NEXT:    psrld $17, %xmm0
; X32-NEXT:    movdqa %xmm0, (%eax)
; X32-NEXT:    retl
;
; X64-LABEL: shift2a:
; X64:       # BB#0: # %entry
; X64-NEXT:    psrld $17, %xmm0
; X64-NEXT:    movdqa %xmm0, (%rdi)
; X64-NEXT:    retq
entry:
  %lshr = lshr <4 x i32> %val, < i32 17, i32 17, i32 17, i32 17 >
  store <4 x i32> %lshr, <4 x i32>* %dst
  ret void
}

define void @shift2b(<4 x i32> %val, <4 x i32>* %dst, i32 %amt) nounwind {
; X32-LABEL: shift2b:
; X32:       # BB#0: # %entry
; X32-NEXT:    movl {{[0-9]+}}(%esp), %eax
; X32-NEXT:    movd {{.*#+}} xmm1 = mem[0],zero,zero,zero
; X32-NEXT:    psrld %xmm1, %xmm0
; X32-NEXT:    movdqa %xmm0, (%eax)
; X32-NEXT:    retl
;
; X64-LABEL: shift2b:
; X64:       # BB#0: # %entry
; X64-NEXT:    movd %esi, %xmm1
; X64-NEXT:    psrld %xmm1, %xmm0
; X64-NEXT:    movdqa %xmm0, (%rdi)
; X64-NEXT:    retq
entry:
  %0 = insertelement <4 x i32> undef, i32 %amt, i32 0
  %1 = insertelement <4 x i32> %0, i32 %amt, i32 1
  %2 = insertelement <4 x i32> %1, i32 %amt, i32 2
  %3 = insertelement <4 x i32> %2, i32 %amt, i32 3
  %lshr = lshr <4 x i32> %val, %3
  store <4 x i32> %lshr, <4 x i32>* %dst
  ret void
}


define void @shift3a(<8 x i16> %val, <8 x i16>* %dst) nounwind {
; X32-LABEL: shift3a:
; X32:       # BB#0: # %entry
; X32-NEXT:    movl {{[0-9]+}}(%esp), %eax
; X32-NEXT:    psrlw $5, %xmm0
; X32-NEXT:    movdqa %xmm0, (%eax)
; X32-NEXT:    retl
;
; X64-LABEL: shift3a:
; X64:       # BB#0: # %entry
; X64-NEXT:    psrlw $5, %xmm0
; X64-NEXT:    movdqa %xmm0, (%rdi)
; X64-NEXT:    retq
entry:
  %lshr = lshr <8 x i16> %val, < i16 5, i16 5, i16 5, i16 5, i16 5, i16 5, i16 5, i16 5 >
  store <8 x i16> %lshr, <8 x i16>* %dst
  ret void
}

; properly zero extend the shift amount
define void @shift3b(<8 x i16> %val, <8 x i16>* %dst, i16 %amt) nounwind {
; X32-LABEL: shift3b:
; X32:       # BB#0: # %entry
; X32-NEXT:    movl {{[0-9]+}}(%esp), %eax
; X32-NEXT:    movzwl {{[0-9]+}}(%esp), %ecx
; X32-NEXT:    movd %ecx, %xmm1
; X32-NEXT:    psrlw %xmm1, %xmm0
; X32-NEXT:    movdqa %xmm0, (%eax)
; X32-NEXT:    retl
;
; X64-LABEL: shift3b:
; X64:       # BB#0: # %entry
; X64-NEXT:    movzwl %si, %eax
; X64-NEXT:    movd %eax, %xmm1
; X64-NEXT:    psrlw %xmm1, %xmm0
; X64-NEXT:    movdqa %xmm0, (%rdi)
; X64-NEXT:    retq
entry:
  %0 = insertelement <8 x i16> undef, i16 %amt, i32 0
  %1 = insertelement <8 x i16> %0, i16 %amt, i32 1
  %2 = insertelement <8 x i16> %1, i16 %amt, i32 2
  %3 = insertelement <8 x i16> %2, i16 %amt, i32 3
  %4 = insertelement <8 x i16> %3, i16 %amt, i32 4
  %5 = insertelement <8 x i16> %4, i16 %amt, i32 5
  %6 = insertelement <8 x i16> %5, i16 %amt, i32 6
  %7 = insertelement <8 x i16> %6, i16 %amt, i32 7
  %lshr = lshr <8 x i16> %val, %7
  store <8 x i16> %lshr, <8 x i16>* %dst
  ret void
}
