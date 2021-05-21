; RUN: opt -safe-stack -S -mtriple=i386-pc-linux-gnu < %s -o - | FileCheck %s
; RUN: opt -safe-stack -S -mtriple=x86_64-pc-linux-gnu < %s -o - | FileCheck %s
; RUN: opt -safe-stack -S -mtriple=i386-pc-linux-musl < %s -o - | FileCheck --check-prefix TCB32 %s
; RUN: opt -safe-stack -S -mtriple=x86_64-pc-linux-musl < %s -o - | FileCheck --check-prefix TCB64 %s

@.str = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1

; Address-of local taken (j = &a)
; Requires protector.

define void @foo() nounwind uwtable safestack {
entry:
  ; CHECK: __safestack_unsafe_stack_ptr
  ; TCB32: %unsafe_stack_ptr = load i8*, i8* addrspace(256)* inttoptr (i32 36 to i8* addrspace(256)*)
  ; TCB32-NEXT: %unsafe_stack_static_top = getelementptr i8, i8* %unsafe_stack_ptr, i32 -16
  ; TCB32-NEXT: store i8* %unsafe_stack_static_top, i8* addrspace(256)* inttoptr (i32 36 to i8* addrspace(256)*)
  ; TCB64: %unsafe_stack_ptr = load i8*, i8* addrspace(257)* inttoptr (i32 72 to i8* addrspace(257)*)
  ; TCB64-NEXT: %unsafe_stack_static_top = getelementptr i8, i8* %unsafe_stack_ptr, i32 -16
  ; TCB64-NEXT: store i8* %unsafe_stack_static_top, i8* addrspace(257)* inttoptr (i32 72 to i8* addrspace(257)*)
  %retval = alloca i32, align 4
  %a = alloca i32, align 4
  %j = alloca i32*, align 8
  store i32 0, i32* %retval
  %0 = load i32, i32* %a, align 4
  %add = add nsw i32 %0, 1
  store i32 %add, i32* %a, align 4
  store i32* %a, i32** %j, align 8
  ; TCB32: store i8* %unsafe_stack_ptr, i8* addrspace(256)* inttoptr (i32 36 to i8* addrspace(256)*)
  ; TCB64: store i8* %unsafe_stack_ptr, i8* addrspace(257)* inttoptr (i32 72 to i8* addrspace(257)*)
  ret void
}

