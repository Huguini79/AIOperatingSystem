# Kuno 0.2 — kernel estilo Unix (x86_64)

Base real y compilable. Estado honesto de cada pieza:

| Componente | Estado |
|---|---|
| Arranque **BIOS + UEFI** | Multiboot2 vía GRUB2 (`grub-mkrescue` hace una ISO híbrida). Código propio: transición 32→64 bits, paginación 4 GiB |
| **FAT32** | Lectura completa (LFN, subdirectorios, particiones MBR). **Probado en host.** Falta escritura |
| **Multi-GPU** | Capa `struct gpu` + escaneo PCI + driver BGA (Bochs/QEMU) + framebuffer del firmware. Intel/AMD/NVIDIA: detectadas, sin driver nativo |
| **Teclado y ratón** | PS/2 por sondeo (i8042), scancodes set 1, Shift/Ctrl/Alt/Super/Bloq Mayús, flechas, F1-F12, ratón de 3 botones. **Decodificadores probados en host.** Distribución US. Sin USB aún |
| **GUI** | Ventanas con foco, Alt+Tab, arrastre con ratón, maximizar, barra de tareas, puntero, doble buffer por GPU. **Renderizada y revisada en host** |
| **Shell** | `help ls cd pwd cat hexdump echo clear uname mem gpus dmesg reboot halt exit`, historial (↑/↓), rutas con `..`, comillas. **Probada en host** |
| **Explorador** | Ventana de archivos navegable con flechas/Enter/Retroceso o ratón; visor de texto |
| **Consola de texto** | Si no hay framebuffer, la misma shell sobre VGA 0xB8000 |
| **Compilador** `kcc` | Subconjunto de C → x86-64 (ABI SysV). **Probado en host.** Corre en el host, no dentro del kernel |
| **Unix** | Tabla de syscalls (numeración Linux) sobre fds + FAT32. Aún sin IDT, procesos ni modo usuario |

## Uso
    make            # kernel (build/kuno.elf) + kcc
    make test       # FAT32, teclado/ratón, shell y kcc en el host
    make snap       # captura PNG de la GUI real (build/snap*.png)
    make run-bios   # requiere grub-mkrescue, xorriso, qemu
    make run-uefi   # requiere OVMF
    make run-2gpu   # dos tarjetas -> verifica el multi-GPU

**Aviso:** el entorno donde se generó no tenía QEMU, así que el kernel compila y
su cabecera Multiboot2 y su código de arranque están verificados estáticamente,
pero **no se ha arrancado**. Espere pequeños ajustes en el primer arranque real.

## Controles
| Tecla | Acción |
|---|---|
| Alt+Tab | siguiente ventana |
| Alt+T / Alt+F / Alt+I | terminal / explorador / panel de sistema |
| Alt+M · Alt+F4 | maximizar · cerrar |
| Alt+flechas | mover ventana |
| Ctrl+L · Ctrl+C · ↑/↓ · PgUp/PgDn | limpiar · cancelar · historial · desplazar |
| Explorador: ↑↓ Enter Retroceso | seleccionar · abrir · subir |
| Ratón | clic en ventana para enfocar, arrastrar título, [X] cierra, clic en la barra de tareas |

(La tecla Super también funciona en lugar de Alt.)

## Hoja de ruta (orden recomendado)
0. Teclado/ratón por interrupciones (IRQ1/IRQ12) en lugar de sondeo; USB HID (xHCI) para portátiles sin PS/2
1. IDT + excepciones + PIC/APIC + temporizador (PIT/HPET)
2. Gestor de memoria física (bitmap) y virtual (paginación de 4 KiB, higher-half)
3. Planificador, procesos, modo usuario, instrucción `SYSCALL`, cargador ELF
4. VFS real (montajes, inodos), devfs, pipes; FAT32 con escritura
5. Teclado/ratón PS/2 → USB (xHCI); eventos para la GUI
6. Almacenamiento: AHCI/NVMe; libc propia (+ `kcc` autoalojado)
7. GPU nativas (Intel i915 / virtio-gpu primero)
8. Cargador propio UEFI (PE32+) y BIOS (MBR) para prescindir de GRUB
