# Kuno — kernel estilo Unix (x86_64), fase 1

Base real y compilable. Estado honesto de cada pieza:

| Componente | Estado |
|---|---|
| Arranque **BIOS + UEFI** | Multiboot2 vía GRUB2 (`grub-mkrescue` hace una ISO híbrida). Código propio: transición 32→64 bits, paginación 4 GiB |
| **FAT32** | Lectura completa (LFN, subdirectorios, particiones MBR). **Probado en host.** Falta escritura |
| **Multi-GPU** | Capa `struct gpu` + escaneo PCI + driver BGA (Bochs/QEMU) + framebuffer del firmware. Intel/AMD/NVIDIA: detectadas, sin driver nativo |
| **GUI** | Compositor con ventanas, fuente 8x8, render por cada GPU. Sin ratón/eventos aún |
| **Compilador** `kcc` | Subconjunto de C → x86-64 (ABI SysV). **Probado en host.** Corre en el host, no dentro del kernel |
| **Unix** | Tabla de syscalls (numeración Linux) sobre fds + FAT32. Aún sin IDT, procesos ni modo usuario |

## Uso
    make            # kernel (build/kuno.elf) + kcc
    make test       # prueba FAT32 y kcc en el host
    make run-bios   # requiere grub-mkrescue, xorriso, qemu
    make run-uefi   # requiere OVMF
    make run-2gpu   # dos tarjetas -> verifica el multi-GPU

**Aviso:** el entorno donde se generó no tenía QEMU, así que el kernel compila y
su cabecera Multiboot2 y su código de arranque están verificados estáticamente,
pero **no se ha arrancado**. Espere pequeños ajustes en el primer arranque real.

## Hoja de ruta (orden recomendado)
1. IDT + excepciones + PIC/APIC + temporizador (PIT/HPET)
2. Gestor de memoria física (bitmap) y virtual (paginación de 4 KiB, higher-half)
3. Planificador, procesos, modo usuario, instrucción `SYSCALL`, cargador ELF
4. VFS real (montajes, inodos), devfs, pipes; FAT32 con escritura
5. Teclado/ratón PS/2 → USB (xHCI); eventos para la GUI
6. Almacenamiento: AHCI/NVMe; libc propia (+ `kcc` autoalojado)
7. GPU nativas (Intel i915 / virtio-gpu primero)
8. Cargador propio UEFI (PE32+) y BIOS (MBR) para prescindir de GRUB
