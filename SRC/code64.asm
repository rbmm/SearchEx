_TEXT segment 'CODE'

strnstr proc
	jrcxz @@3
	cmp rcx,r8
	jb @@3
	push rdi
	push rsi
	mov rdi,rdx
	mov al,[r9]
	inc r9
	dec r8
	sub rcx,r8
@@1:
	repne scasb
	jne @@2
	mov rsi,r9
	mov rdx,rdi
	mov r10,rcx
	mov rcx,r8
	test ecx,ecx
	repe cmpsb
	je @@2
	mov rcx,r10
	mov rdi,rdx
	jmp @@1
@@2:
	sete al
	movzx rax,al
	neg rax
	and rax,rdi
	pop rsi
	pop rdi
	ret
@@3:
	xor rax,rax
	ret
strnstr endp

wtrnstr proc
	jrcxz @@3
	cmp rcx,r8
	jb @@3
	push rdi
	push rsi
	mov rdi,rdx
	mov ax,[r9]
	inc r9
	inc r9
	dec r8
	sub rcx,r8
@@1:
	repne scasw
	jne @@2
	mov rsi,r9
	mov rdx,rdi
	mov r10,rcx
	mov rcx,r8
	test ecx,ecx
	repe cmpsw
	je @@2
	mov rcx,r10
	mov rdi,rdx
	jmp @@1
@@2:
	sete al
	movzx rax,al
	neg rax
	and rax,rdi
	pop rsi
	pop rdi
	ret
@@3:
	xor rax,rax
	ret
wtrnstr endp

_TEXT ENDS
END