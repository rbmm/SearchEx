.686

_TEXT segment


@strnstr@16 proc
	jecxz @@3
	cmp ecx,[esp + 4]
	jb @@3
	push edi
	push esi
	push ebx
	push ebp
	mov ebx,[esp + 20]
	mov ebp,[esp + 24]
	mov edi,edx
	mov al,[ebp]
	inc ebp
	dec ebx
	sub ecx,ebx
@@1:
	repne scasb
	jne @@2
	mov esi,ebp
	mov edx,edi
	push ecx
	mov ecx,ebx
	test ecx,ecx
	repe cmpsb
	pop ecx
	je @@2
	mov edi,edx
	jmp @@1
@@2:
	sete al
	movzx eax,al
	neg eax
	and eax,edi
	pop ebp
	pop ebx
	pop esi
	pop edi
	ret 8
@@3:
	xor eax,eax
	ret 8
@strnstr@16 endp

@wtrnstr@16 proc
	jecxz @@3
	cmp ecx,[esp + 4]
	jb @@3
	push edi
	push esi
	push ebx
	push ebp
	mov ebx,[esp + 20]
	mov ebp,[esp + 24]
	mov edi,edx
	mov ax,[ebp]
	inc ebp
	inc ebp
	dec ebx
	sub ecx,ebx
@@1:
	repne scasw
	jne @@2
	mov esi,ebp
	mov edx,edi
	push ecx
	mov ecx,ebx
	test ecx,ecx
	repe cmpsw
	pop ecx
	je @@2
	mov edi,edx
	jmp @@1
@@2:
	sete al
	movzx eax,al
	neg eax
	and eax,edi
	pop ebp
	pop ebx
	pop esi
	pop edi
	ret 8
@@3:
	xor eax,eax
	ret 8
@wtrnstr@16 endp

_TEXT ends
end