
BITS 32

SECTION .text

GLOBAL sound_outb
GLOBAL sound_inb
GLOBAL sound_wait

sound_outb:
    mov dx, [esp + 4]
    mov al, [esp + 8]
    out dx, al
    ret

sound_inb:
    mov dx, [esp + 4]
    xor eax, eax
    in al, dx
    ret

sound_wait:
    mov ecx, [esp + 4]
    test ecx, ecx
    jz .done

.loop:
    pause
    loop .loop

.done:
    ret
SECTION .note.GNU-stack noalloc noexec nowrite progbits