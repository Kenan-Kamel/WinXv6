# xv6 Extended OS — Architecture & Features

---

## Slide 1: Project Overview

**xv6** is a teaching OS (MIT, based on Unix v6) for x86-64.

We extended it with:
- A **graphical desktop environment** (windowed GUI, games, apps)
- A **PS/2 mouse driver**
- A **VGA/framebuffer driver**
- A **full TCP/IP network stack**
- An **Intel e1000 NIC driver**
- A **remote shell daemon (rshd)**

All running on bare-metal (or QEMU) — no Linux, no libraries.

---

## Slide 2: Boot Sequence

**`main.c`** — kernel entry point. After standard xv6 init, we added:

```
mouseinit()     → PS/2 mouse driver
gui_keys_init() → GUI keyboard ring buffer
netinit()       → e1000 NIC + TCP/IP stack
```

**`init.c`** — PID 1 (first user process). Forks three children:

1. `rshd`    — network shell daemon, always running in background
2. `desktop` — full GUI session (exits cleanly if no VGA hardware)
3. `sh`      — serial-console fallback shell (always available)

---

## Slide 3: New Kernel Drivers Overview

| File | What it does |
|------|-------------|
| `vga.c` | Programs the Bochs VGA chip for 1024×768×32bpp framebuffer |
| `mouse.c` | PS/2 mouse — IRQ12 interrupt handler, tracks X/Y/buttons |
| `e1000.c` | Intel e1000 NIC — DMA TX/RX rings, interrupt handler |
| `pci.c` | PCI config space scanner — finds devices by vendor/device ID |
| `net.c` | Full TCP/IP stack — ARP, IP, ICMP, TCP state machine |
| `sysnet.c` | Network syscalls — socket, bind, listen, accept |
| `sysgui.c` | GUI syscalls — screen_init, flush_screen, getmouse, getkey_async |

---

## Slide 4: VGA Driver (`vga.c`)

**Problem:** xv6 had no framebuffer — only text-mode console.

**Solution:** Bochs Graphics Adapter (BGA) — built into QEMU's VGA card.

**How it works:**
- Programs two I/O ports (`0x01CE` / `0x01CF`) to set resolution and color depth
- Finds the framebuffer's **physical address** by reading PCI BAR0 of the VGA device
- Maps it into kernel virtual address space using xv6's existing identity mapping of the 4th GB (`P2V(fb_phys_addr)`)
- Mode: **1024×768, 32 bits per pixel** (XRGB format)

**Key functions:**
- `vga_init()` — detects BGA, sets video mode, maps framebuffer
- `vga_flush(src, size)` — copies user back-buffer to hardware framebuffer
- `vga_get_fb()` — returns kernel pointer to framebuffer

**User app access via syscall:** `flush_screen(backbuf)` — safe copy from user space.

---

## Slide 5: PS/2 Mouse Driver (`mouse.c`)

**Hardware:** PS/2 controller at I/O ports `0x60` (data) and `0x64` (cmd/status).

**Initialization sequence:**
1. Enable auxiliary (mouse) device — send `0xA8` to command port
2. Read current command byte, set IRQ12 enable bit, clear mouse-clock-disable bit
3. Write command byte back
4. Send `0xF6` (set defaults), `0xF3 200` (200 samples/sec), `0xF4` (enable reporting)
5. Enable IRQ12 in the IOAPIC — `ioapicenable(IRQ_MOUSE, 0)`

**Interrupt handler (`mouseintr`):**
- PS/2 sends **3-byte packets** per movement event
- Byte 0: button state + overflow flags + sign bits
- Byte 1: delta-X (signed)
- Byte 2: delta-Y (signed, inverted for screen coordinates)
- Applies **2× sensitivity**, clamps cursor to screen bounds
- Protected by a spinlock

**Hook in `trap.c`:**
```c
case T_IRQ0 + IRQ_MOUSE:
    mouseintr();
    lapiceoi();
    break;
```

---

## Slide 6: Keyboard Routing for GUI

**Problem:** xv6's keyboard driver sends keystrokes to the console. The GUI needs them too.

**Solution:** A ring buffer in `sysgui.c` (`GUI_KEYBUF_SIZE = 256` entries).

**Modified `kbd.c`:**
```c
if(gui_active){
    gui_key_put(c);   // redirect to GUI ring buffer
} else {
    // original console path
}
```

`gui_active` is set to `1` when `vga_init()` succeeds.

**User app reads keys** via `getkey_async()` syscall — returns next key or `-1` (non-blocking).

---

## Slide 7: PCI Driver (`pci.c`)

**Why needed:** Both the VGA card and the e1000 NIC are PCI devices. Their MMIO addresses and IRQ lines must be discovered at runtime.

**PCI Config Space:** accessed via I/O ports `0xCF8` (address) and `0xCFC` (data).

**`pci_find(vendor, device, out)`:**
- Scans all bus (0–7) / slot (0–31) / function (0–7) combinations
- Reads 32-bit vendor+device ID
- On match: reads BAR0 (base address register — the MMIO address) and IRQ line
- Returns 0 on success, -1 if not found

**`pci_enable_master(dev)`:**
- Sets the Bus Master bit in the PCI command register
- Required so the NIC can do DMA (write packets to RAM without CPU involvement)

---

## Slide 8: Intel e1000 NIC Driver (`e1000.c`)

**Hardware:** QEMU emulates an **Intel 82540EM** when you pass `-device e1000`.

**Initialization:**
1. `pci_find(0x8086, 0x100E)` — locate the NIC
2. Enable PCI bus mastering (DMA)
3. Map MMIO registers: `regs = p2v(dev.bar0)`
4. Reset chip, set link-up + full-duplex
5. Read MAC address from hardware registers
6. Set up **TX ring**: 8 descriptors, 2KB buffers each; mark all as done initially
7. Set up **RX ring**: 8 descriptors, 2KB buffers; program base address + length registers
8. Enable receive interrupt (`E1000_IMS = RXT0 | RXO`)
9. Enable the NIC's IRQ in the IOAPIC

**TX (`e1000tx`):**
- Check `DD` (Descriptor Done) bit — ring full if not set
- Copy packet into buffer, set length + command flags (EOP, IFCS, RS)
- Advance tail register → hardware sends

**RX (`e1000_drain_rx`):**
- Walk ring looking for `DD` bit
- Call `netin(buf, len)` for each complete packet
- Give descriptor back to hardware, advance tail

**Interrupt hook in `trap.c`** (IRQ discovered at runtime):
```c
if(e1000_irq != 0 && tf->trapno == (uint)(T_IRQ0 + e1000_irq)){
    e1000intr(); lapiceoi();
}
```

**Fallback polling:** `e1000poll()` called every timer tick (10ms) — packets never get stuck even if PCI IRQ routing is misconfigured.

---

## Slide 9: TCP/IP Stack (`net.c`)

A full network stack written from scratch in ~560 lines.

**Protocol layers:**

```
Application (rshd)
      ↓
   Sockets (net.c)
      ↓
    TCP (net.c)
      ↓
     IP (net.c)
      ↓
    ARP (net.c)
      ↓
  Ethernet (net.c)
      ↓
  e1000 (e1000.c)
```

**IP config (QEMU user-mode networking):**
- Guest IP: `10.0.2.15 /24`
- Gateway: `10.0.2.2`
- Host-side NAT — the host machine's IP is the one to use for connections

---

## Slide 10: ARP, IP, ICMP

**ARP (Address Resolution Protocol):**
- Cache of 8 entries (IP → MAC)
- On incoming ARP request for our IP: send ARP reply
- On outgoing packet: look up destination MAC; if off-subnet, look up gateway MAC
- **Key fix:** pre-seed gateway MAC `52:55:0a:00:02:02` at boot — without this, the first SYN-ACK is dropped while waiting for ARP

**IP:**
- Builds IP headers (version, TTL, protocol, checksum)
- Checksum = one's complement sum of all 16-bit header words, folded and negated
- Routes via gateway for off-subnet destinations

**ICMP (ping):**
- Detects echo-request (type 8)
- Flips to echo-reply (type 0), recalculates checksum, sends back
- xv6 is pingable: `ping 10.0.2.15` from inside QEMU works

---

## Slide 11: TCP State Machine

Full TCP handshake and teardown:

```
CLOSED
  │  bind()+listen()
  ▼
LISTEN  ← incoming SYN → send SYN-ACK → SYN_RCVD
                                              │ ACK received
                                              ▼
                                        ESTABLISHED
                                         │        │
                                  recv FIN     send FIN
                                         ▼        ▼
                                    CLOSE_WAIT  FIN_WAIT1
                                         │        │
                                    send FIN  recv FIN/ACK
                                         ▼        ▼
                                      LAST_ACK  FIN_WAIT2
                                         │
                                    recv ACK
                                         ▼
                                       CLOSED
```

**Per-socket state (`struct sock`):**
- 4096-byte RX ring buffer
- `snd_nxt`, `snd_una`, `rcv_nxt` sequence number tracking
- Accept queue (4 slots) for listening sockets
- Spinlock for thread safety

**Socket table:** 16 sockets (`NSOCK = 16`), statically allocated in kernel.

---

## Slide 12: Network System Calls (`sysnet.c`)

**`file.c` modified** — added `FD_SOCKET` as a fourth file type:

| File operation | Socket behavior |
|---------------|----------------|
| `fileread` | calls `sockread` — blocks until data available or EOF |
| `filewrite` | calls `sockwrite` — sends TCP segments |
| `fileclose` | calls `sockclose` — sends FIN, initiates teardown |

**Four new syscalls:**

| Syscall | # | Description |
|---------|---|-------------|
| `socket()` | 27 | Allocate TCP socket, return fd |
| `bind(fd, port)` | 28 | Bind to port, set state = LISTEN |
| `listen(fd, backlog)` | 29 | No-op (already listening after bind) |
| `accept(fd, 0, 0)` | 30 | Block until connection ready, return new fd |

User-space API in `user.h`:
```c
int socket(int, int, int);
int bind(int, int);
int listen(int, int);
int accept(int, void*, void*);
```

---

## Slide 13: Remote Shell Daemon (`rshd.c`)

**What it is:** A TCP server that gives remote users a real xv6 shell.

**How to connect:**
```bash
nc localhost 2323     # from any terminal on the host machine
# Password: xv6
```

QEMU port forwarding: `-hostfwd=tcp:0.0.0.0:2323-:23`

**Flow:**
1. `socket()` → `bind(lfd, 23)` → `listen(lfd, 4)` → loop
2. `accept()` blocks until client connects
3. `fork()` — child process handles the connection
4. Child: sends banner + password prompt
5. Reads password line, validates with strcmp
6. On success: `close(0); dup(cfd)` — wire socket fd to stdin/stdout/stderr
7. `exec("sh", argv)` — the remote user gets a full xv6 shell
8. Parent: closes the per-connection fd, loops back to `accept()`

---

## Slide 14: GUI System Calls (`sysgui.c`)

Five new syscalls added for the GUI:

| Syscall | # | Description |
|---------|---|-------------|
| `screen_init(info*)` | 22 | Call `vga_init()`, fill `screen_info` struct (width/height/bpp) |
| `flush_screen(buf*)` | 23 | Validate user pointer, call `vga_flush()` to blit to hardware |
| `getmouse(info*)` | 24 | Fill `mouse_info` struct with current X/Y/buttons |
| `getkey_async()` | 25 | Return next key from ring buffer, or -1 if empty |
| `halt()` | 26 | Write `0x2000` to QEMU shutdown port (`0x604`) |

**`flush_screen` safety check:**
```c
if(addr < PGSIZE || addr + FB_SIZE > proc->sz)
    return -1;
```
Prevents user from pointing at kernel memory.

---

## Slide 15: GUI Library (`guilib.c`)

All rendering goes to a **user-space back-buffer** (allocated with `malloc`), then `flush_screen()` blits it to hardware each frame.

**Drawing primitives:**
- `fb_pixel` — single pixel
- `fb_rect` / `fb_fill_rect` — rectangle outline / filled
- `fb_line` — Bresenham line
- `fb_fill_circle` — midpoint circle
- `fb_gradient_v` — vertical color gradient (used for title bars, desktop bg)

**Font:** 8×16 bitmap, ASCII 32–126, hard-coded as 95×16 byte array.
- Each character: 16 bytes, one per row, MSB = leftmost pixel
- `fb_char(fb, x, y, c, fg, bg)` — renders one glyph
- `fb_text(fb, x, y, str, fg, bg)` — renders a string

**No image files.** All icons and graphics are drawn in code using primitives.

---

## Slide 16: Desktop Icons (Drawn in Code)

Six icon types, each drawn as a 48×48px graphic using drawing primitives:

| Icon | `draw_icon_*` | Visual |
|------|--------------|--------|
| Terminal | `draw_icon_terminal` | Black rectangle + `>_` text |
| Folder | `draw_icon_folder` | Yellow trapezoid tab + rectangle body |
| Document | `draw_icon_document` | White page with folded corner + horizontal lines |
| Settings | `draw_icon_settings` | Gear shape from circles and rectangles |
| About | `draw_icon_about` | Blue filled circle with "i" |
| Game | `draw_icon_game` | Colored square + controller shape |

Double-clicking an icon opens the corresponding window.

---

## Slide 17: Windows XP Luna Theme

Colors defined in `guilib.h` matching the XP "Luna Blue" theme:

| Element | Color | Hex |
|---------|-------|-----|
| Desktop background top | XP sky blue | `#055AAE` |
| Desktop background bottom | XP bliss green | `#46AA3C` |
| Title bar active (top) | Deep blue | `#0A3CC8` |
| Title bar active (bottom) | Bright blue | `#3782E6` |
| Title bar inactive | Dark blue | `#0A246A` |
| Window content area | Luna beige | `#ECE9D8` |
| Taskbar | XP blue | `#1A4CB3` |
| Close button | Red | `#D3352C` |
| Start button | Green gradient | `#37AA37` → `#147814` |

**Window anatomy:**
- 2px colored border
- 32px title bar with gradient + centered title text
- Three rectangular buttons top-right (close/maximize/minimize)
- Content area in luna beige

---

## Slide 18: Desktop Environment (`desktop.c`)

**Main loop (runs forever as a user process):**
```
handle_input()     ← polls getmouse + getkey_async each frame
update games()     ← snake tick, doom AI update
render()           ← draw bg + icons + windows + taskbar + cursor
flush_screen()     ← blit back-buffer to hardware
sleep(1)           ← yield CPU, let mouse interrupts accumulate
```

**Window management:**
- Up to 16 windows (`MAX_WINDOWS`)
- Active window gets focus (brighter title bar)
- Windows are draggable by their title bar
- Minimize hides the window; taskbar button toggles it back
- Maximize expands to fill the content area

**Desktop icons:** arranged in 3 columns of up to 4 icons each.

**Context menu:** right-click desktop shows New/Refresh options.

**Notification toast:** small pop-up at bottom right (e.g., "File saved"), auto-dismisses after 2.5 seconds.

---

## Slide 19: Built-in Apps

| App | Window Type | Key Features |
|-----|------------|-------------|
| Terminal | `WIN_TERMINAL` | Built-in `ls`, `echo`, `clear`, `help`, `date`, `calc`; passes unknown commands to xv6 `exec` |
| File Manager | `WIN_FILEMANAGER` | Reads real xv6 filesystem; create/delete files |
| Text Editor | `WIN_TEXTEDITOR` | Open files, edit, Ctrl+S to save |
| About | `WIN_ABOUT` | OS name, version, authors |
| Calculator | `WIN_CALCULATOR` | +/−/×/÷ with accumulator |
| Settings | `WIN_SETTINGS` | Wallpaper style toggle |
| Shutdown | `WIN_SHUTDOWN` | Calls `halt` syscall → QEMU power-off |

---

## Slide 20: Games — Snake & Minesweeper & Paint

**Snake (`WIN_SNAKE`):**
- 16×16px cells, grid fills window content area
- Arrow keys change direction; snake grows when it eats food
- `snake_update()` called every frame; speed is fixed tick-based
- Collision with self or wall → game over screen
- Score displayed live

**Minesweeper (`WIN_MINESWEEPER`):**
- 10×10 grid, 12 mines, 24×24px cells
- Left-click reveals cell; right-click places flag
- Recursive flood-fill reveal for empty cells
- Win condition: all non-mines revealed
- Mine locations randomized using xv6's `uptime()`-seeded xorshift PRNG

**Paint (`WIN_PAINT`):**
- 200×150 pixel canvas
- Mouse-down draws; color palette at top
- Adjustable brush size

---

## Slide 21: Minecraft Raycaster (`doom_draw` / `doom_*`)

A **DDA raycaster** (Wolfenstein-3D algorithm) running in a window:

**Rendering:**
- For each vertical screen column, cast a ray from the player position
- DDA (Digital Differential Analyzer) steps the ray through map grid cells
- When a wall is hit: compute distance, calculate wall-strip height (perspective)
- 4 block types drawn as colored pixel patterns: grass, stone, wood planks, dirt
- Sky: blue gradient ceiling; Floor: brown gradient
- White crosshair at screen center

**Math:**
- Fixed-point arithmetic: 8-bit fractional part (`FP_SHIFT = 8`)
- Sin/cos tables: precomputed at startup from a hard-coded quarter-sine lookup using quadrant symmetry (90 values → full 360-degree tables)
- No floating point used anywhere

**HUD:**
- 10 heart icons for health (bottom left)
- 9-slot hotbar at bottom center with selected slot highlighted
- Weapon label text (Sword / Pickaxe / Axe / Bow / TNT)

**Enemies (3 types, up to 16 per level):**
- Zombie — melee, moves toward player
- Skeleton — ranged, fires projectiles
- Creeper — rushes player, explodes on contact

**Controls:** WASD to move, mouse to look (reads delta each frame), 1–5 to select weapon, R to restart, ESC to close.

**3 levels** with different map layouts loaded from `doom_load_level()`.

---

## Slide 22: How It All Connects

```
Hardware (QEMU)
├── VGA/BGA chip ──── vga.c ─────────────────→ sys_flush_screen → desktop.c
├── PS/2 Mouse ────── mouse.c → IRQ12 trap ─→ sys_getmouse     → desktop.c
├── PS/2 Keyboard ── kbd.c (modified) ──────→ sys_getkey_async → desktop.c
├── Intel e1000 NIC ─ e1000.c → IRQ trap ──→ net.c (TCP/IP)
│                                                    │
│                                             sys_socket/bind/
│                                             listen/accept
│                                                    │
│                                             FD_SOCKET in file.c
│                                                    │
│                                              rshd.c (user)
│                                                    │
│                                         host port 2323 → nc
└── PCI bus ─────── pci.c (shared by vga + e1000)
```

**New syscalls summary:**

| Range | Syscalls |
|-------|---------|
| 22–26 | GUI: screen_init, flush_screen, getmouse, getkey_async, halt |
| 27–30 | Network: socket, bind, listen, accept |

Everything else (file I/O, process management, memory) is standard xv6.
