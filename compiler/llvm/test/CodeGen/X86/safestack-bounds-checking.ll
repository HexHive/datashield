; RUN: llc -mtriple=x86_64-linux-gnu -mattr=+mpx,+separate-stack-seg -stop-after x86-safestack-bounds-checking -o - %s | FileCheck %s

@__safestack_unsafe_stack_ptr = external thread_local(initialexec) global i8*

; Function Attrs: nounwind safestack uwtable
define void @bad_store() #0 {
; CHECK-LABEL: @bad_store()
entry:
  %unsafe_stack_ptr = load i8*, i8** @__safestack_unsafe_stack_ptr
  %unsafe_stack_static_top = getelementptr i8, i8* %unsafe_stack_ptr, i32 -16
  store i8* %unsafe_stack_static_top, i8** @__safestack_unsafe_stack_ptr
  %0 = getelementptr i8, i8* %unsafe_stack_ptr, i32 -4
  %a.unsafe = bitcast i8* %0 to i32*
  %1 = ptrtoint i32* %a.unsafe to i64
  %2 = inttoptr i64 %1 to i64*
; CHECK: %3 = bitcast i64* %2 to i8*
; CHECK: %4 = getelementptr i8, i8* %3, i64 8
; CHECK: call void asm "bndcu $0, %bnd0", "*m"(i8* %4)
  store i64 0, i64* %2
  store i8* %unsafe_stack_ptr, i8** @__safestack_unsafe_stack_ptr
  ret void
}

; Function Attrs: nounwind safestack uwtable
define void @good_store() #0 {
; CHECK-LABEL: @good_store()
entry:
  %a = alloca i32, align 4
  %0 = bitcast i32* %a to i8*
  store i8 0, i8* %0
  ret void
}

; Function Attrs: nounwind safestack uwtable
define void @overflow_gep_store() #0 {
; CHECK-LABEL: @overflow_gep_store()
entry:
  %unsafe_stack_ptr = load i8*, i8** @__safestack_unsafe_stack_ptr
  %unsafe_stack_static_top = getelementptr i8, i8* %unsafe_stack_ptr, i32 -16
  store i8* %unsafe_stack_static_top, i8** @__safestack_unsafe_stack_ptr
  %0 = getelementptr i8, i8* %unsafe_stack_ptr, i32 -8
  %a.unsafe = bitcast i8* %0 to i32*
  %1 = bitcast i32* %a.unsafe to i8*
  %2 = getelementptr i8, i8* %1, i32 4
; CHECK: %3 = getelementptr i8, i8* %2, i64 1
; CHECK: call void asm "bndcu $0, %bnd0", "*m"(i8* %3)
  store i8 0, i8* %2
  %3 = getelementptr i8, i8* %unsafe_stack_ptr, i32 -4
  %b.unsafe = bitcast i8* %3 to i32*
  %4 = bitcast i32* %b.unsafe to i8*
  %5 = getelementptr i8, i8* %4, i32 4
; CHECK: %7 = getelementptr i8, i8* %6, i64 1
; CHECK: call void asm "bndcu $0, %bnd0", "*m"(i8* %7)
  store i8 0, i8* %5
  store i8* %unsafe_stack_ptr, i8** @__safestack_unsafe_stack_ptr
  ret void
}

; Function Attrs: nounwind safestack uwtable
define void @underflow_gep_store() #0 {
; CHECK-LABEL: @underflow_gep_store()
entry:
  %unsafe_stack_ptr = load i8*, i8** @__safestack_unsafe_stack_ptr
  %unsafe_stack_static_top = getelementptr i8, i8* %unsafe_stack_ptr, i32 -16
  store i8* %unsafe_stack_static_top, i8** @__safestack_unsafe_stack_ptr
  %0 = getelementptr i8, i8* %unsafe_stack_ptr, i32 -4
  %a.unsafe = bitcast i8* %0 to i32*
  %1 = bitcast i32* %a.unsafe to i8*
  %2 = getelementptr i8, i8* %1, i32 -1
; CHECK: %3 = getelementptr i8, i8* %2, i64 1
; CHECK: call void asm "bndcu $0, %bnd0", "*m"(i8* %3)
  store i8 0, i8* %2
  store i8* %unsafe_stack_ptr, i8** @__safestack_unsafe_stack_ptr
  ret void
}

; Function Attrs: nounwind safestack uwtable
define void @good_gep_store() #0 {
; CHECK-LABEL: @good_gep_store()
entry:
  %a = alloca i32, align 4
  %0 = bitcast i32* %a to i8*
  %1 = getelementptr i8, i8* %0, i32 3
  store i8 0, i8* %1
  ret void
}

; Function Attrs: safestack
define void @call_memset(i64 %len) #1 {
; CHECK-LABEL: @call_memset(i64 %len)
entry:
  %unsafe_stack_ptr = load i8*, i8** @__safestack_unsafe_stack_ptr
  %unsafe_stack_static_top = getelementptr i8, i8* %unsafe_stack_ptr, i32 -16
  store i8* %unsafe_stack_static_top, i8** @__safestack_unsafe_stack_ptr
  %0 = getelementptr i8, i8* %unsafe_stack_ptr, i32 -10
  %q.unsafe = bitcast i8* %0 to [10 x i8]*
  %arraydecay = getelementptr inbounds [10 x i8], [10 x i8]* %q.unsafe, i32 0, i32 0
; CHECK: %1 = getelementptr i8, i8* %0, i64 %len
; CHECK: call void asm "bndcu $0, %bnd0", "*m"(i8* %1)
  call void @llvm.memset.p0i8.i64(i8* %arraydecay, i8 1, i64 %len, i32 1, i1 false)
  store i8* %unsafe_stack_ptr, i8** @__safestack_unsafe_stack_ptr
  ret void
}

; Function Attrs: argmemonly nounwind
declare void @llvm.memset.p0i8.i64(i8* nocapture writeonly, i8, i64, i32, i1) #3

attributes #0 = { nounwind safestack uwtable }
attributes #1 = { safestack }
attributes #3 = { argmemonly nounwind "target-features"="+mpx,+separate-stack-seg" }
