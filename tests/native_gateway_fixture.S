.text
.globl gateway_fixture_next
.type gateway_fixture_next,@function
gateway_fixture_next:
.byte 0x48,0x63,0xf6,0x48,0x63,0xc2,0x48,0x63,0x8f,0x08,0x03,0,0,0x48,0x8d,0x14,0x76
push %rbp
add %rax,%rdx
mov %rsp,%rbp
lea gateway_fixture_end(%rip),%rax
shl $4,%rdx
add %rdi,%rdx
cmp 8(%rdx),%ecx
jge 1f
lea 1(%rcx),%eax
mov %eax,0x308(%rdi)
mov (%rdx),%rax
lea (%rax,%rcx,8),%rax
1: pop %rbp
ret
.size gateway_fixture_next,.-gateway_fixture_next
.globl gateway_fixture_part
.type gateway_fixture_part,@function
gateway_fixture_part:
.byte 0x55,0x48,0x89,0xe5,0x53,0x48,0x89,0xfb,0x48,0x83,0xec,0x18
call gateway_fixture_predicate
test %al,%al
jz 2f
incq 8(%rbx)
2: add $0x18,%rsp
pop %rbx
pop %rbp
ret
.size gateway_fixture_part,.-gateway_fixture_part
.data
.align 8
gateway_fixture_end: .quad 0
.section .note.GNU-stack,"",@progbits
