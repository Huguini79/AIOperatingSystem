/* Kuno - arranque Multiboot2 (x86_64).
 * GRUB2 carga este kernel tanto en BIOS (ISO El Torito) como en UEFI
 * (grub-mkrescue genera una ISO hibrida) y entrega un framebuffer lineal. */
.set MB2_MAGIC, 0xE85250D6

.section .multiboot2, "a"
.align 8
mb2_start:
    .long MB2_MAGIC
    .long 0                                   /* i386 */
    .long mb2_end - mb2_start
    .long 0x100000000 - (MB2_MAGIC + 0 + (mb2_end - mb2_start))
    .align 8
    .short 5, 0                               /* tag: framebuffer */
    .long 20
    .long 1024, 768, 32
    .align 8
    .short 0, 0                               /* tag: fin */
    .long 8
mb2_end:

.section .text
.code32
.global _start
_start:
    cli
    cld
    mov $stack_top, %esp
    mov %eax, %edi                            /* magic Multiboot2 */
    mov %ebx, %esi                            /* puntero a la info */

    /* Paginacion de identidad: 4 GiB con paginas de 2 MiB */
    mov $pdpt, %eax
    or  $3, %eax
    mov %eax, pml4
    xor %ecx, %ecx
1:  mov %ecx, %eax
    shl $12, %eax
    add $pd, %eax
    or  $3, %eax
    mov %eax, pdpt(,%ecx,8)
    inc %ecx
    cmp $4, %ecx
    jne 1b
    xor %ecx, %ecx
2:  mov %ecx, %eax
    shl $21, %eax
    or  $0x83, %eax                           /* P | RW | PS */
    mov %eax, pd(,%ecx,8)
    inc %ecx
    cmp $2048, %ecx
    jne 2b

    mov $pml4, %eax
    mov %eax, %cr3
    mov %cr4, %eax
    or  $0x20, %eax                           /* PAE */
    mov %eax, %cr4
    mov $0xC0000080, %ecx                     /* EFER */
    rdmsr
    or  $0x100, %eax                          /* LME */
    wrmsr
    mov %cr0, %eax
    or  $0x80000001, %eax                     /* PG | PE */
    mov %eax, %cr0
    lgdt gdt_ptr
    ljmp $0x08, $long_mode

.code64
long_mode:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs
    mov $stack_top, %esp
    xor %ebp, %ebp
    mov %esi, %esi
    call kmain
hang:
    cli
    cld
    hlt
    jmp hang

.section .rodata
.align 8
gdt:
    .quad 0
    .quad 0x00AF9A000000FFFF                  /* codigo 64 */
    .quad 0x00CF92000000FFFF                  /* datos */
gdt_end:
gdt_ptr:
    .word gdt_end - gdt - 1
    .long gdt

.section .bss
.align 4096
pml4: .skip 4096
pdpt: .skip 4096
pd:   .skip 16384
.align 16
stack_bottom: .skip 65536
stack_top:
.section .note.GNU-stack,"",@progbits
