# Kuno 0.3 — kernel estilo Windows XP (x86_64)

Base real y compilable. Mejoras respecto a 0.2: escritorio con iconos, tema
visual aproximado a Windows XP (Luna), calculadora, panel de control, Snake,
Tetris y un juego de coches simple (vista de carretera).

| Componente | Estado |
|---|---|
| Arranque **BIOS + UEFI** | Multiboot2 vía GRUB2. Transición 32→64 bits, paginación 4 GiB |
| **FAT32** | Lectura completa (LFN, subdirectorios, MBR). Falta escritura |
| **Multi-GPU** | Capa `struct gpu` + PCI + BGA + framebuffer firmware |
| **Teclado y ratón** | PS/2 por sondeo, scancodes set 1, ratón 3 botones |
| **GUI estilo XP** | Escritorio con iconos, menú Inicio, barra de tareas verde, ventanas azul Luna, arrastre, maximizar, Alt+Tab |
| **Programas** | Terminal (shell), Explorador de archivos, Visor de texto, Calculadora (enteros), Panel de control, Acerca de |
| **Juegos** | Snake, Tetris, Kuno Rally (coches en carretera) |
| **Shell** | `help ls cd pwd cat hexdump echo clear uname mem gpus dmesg ps uptime reboot halt exit` |
| **Compilador** `kcc` | Subconjunto de C → x86-64 en el **host** (aún no autoalojado) |
| **Multitarea preemptiva** | IDT + PIC 8259A + PIT 100 Hz + round-robin de tareas de kernel. `ps` / `uptime` |
| **Unix** | Syscalls básicas sobre FAT32. Sin procesos ring-3 / modo usuario aún |

## Uso
    make            # kernel (build/kuno.elf) + kcc
    make test       # pruebas en el host
    make snap       # captura PNG de la GUI
    make run-bios   # QEMU + GRUB BIOS
    make run-uefi   # QEMU + OVMF

## Controles

| Tecla / acción | Efecto |
|---|---|
| Clic en iconos del escritorio | Abre el programa |
| Botón **Inicio** / menú | Lista de aplicaciones |
| Alt+Tab | Siguiente ventana |
| Alt+T / Alt+F / Alt+C | Terminal / Archivos / Calculadora |
| Alt+S / Alt+R / Alt+G | Snake / Tetris / Coches |
| Alt+I / Alt+P | Sistema / Panel de control |
| Alt+M · Alt+F4 | Maximizar · Cerrar |
| Ratón | Arrastrar título, [X] cierra, clic barra de tareas |
| **Snake / Tetris / Race** | Flechas; **R** o Enter reinicia tras game over |
| Tetris | Arriba o X = rotar, Espacio = hard drop |

## Juegos
- **Snake**: crece al comer, bordes y auto-colisión = game over.
- **Tetris**: 7 piezas, niveles, limpieza de líneas.
- **Kuno Rally**: carretera con perspectiva simple, esquiva otros coches, vidas.

## Limitaciones honestas
- El “3D” del juego de coches es una proyección 2D/pseudo-3D de software (no hay GPU 3D ni motor de polígonos).
- Calculadora solo enteros (el kernel se compila con `-mgeneral-regs-only`, sin FPU/SSE).
- `kcc` sigue ejecutándose en el host; el autoalojamiento requiere libc + IDT + procesos.
- FAT32 sigue siendo de solo lectura.
- Sin USB, sin escritura a disco, sin multitarea real.

## Hoja de ruta (orden recomendado)
0. Teclado/ratón por interrupciones; USB HID
1. IDT + excepciones + PIC/APIC + temporizador
2. Gestor de memoria física y virtual
3. Planificador, procesos, modo usuario, SYSCALL, cargador ELF
4. VFS + FAT32 con escritura
5. libc mínima + kcc autoalojado
6. GPU nativas / virtio-gpu
7. Cargador propio UEFI/BIOS
