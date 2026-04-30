# xv6 GUI + Game + Network + Mouse Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Polish xv6's desktop with a Windows XP Luna theme, restyle the game as Minecraft, fix mouse speed, and make the remote shell reachable from another machine.

**Architecture:** All GUI changes live in `guilib.h` (colors/constants) and `desktop.c` (drawing logic). Mouse fix is one line in `mouse.c`. Networking fix is the Makefile QEMU `hostfwd` binding + rshd password auth. No new files needed.

**Tech Stack:** xv6 C kernel, QEMU user-mode networking, PS/2 mouse, VGA framebuffer

---

## File Map

| File | What changes |
|------|-------------|
| `xv6-public/mouse.c:149-150` | Mouse multiplier `* 3` → `* 1` |
| `xv6-public/guilib.h:9-36` | All color constants replaced with XP Luna palette |
| `xv6-public/guilib.h:44-50` | Titlebar/border constants for XP proportions |
| `xv6-public/desktop.c:791-814` | `draw_desktop_bg` — Bliss-style sky+hills gradient |
| `xv6-public/desktop.c:847-884` | `draw_window` — gradient title bar, rectangular XP buttons |
| `xv6-public/desktop.c:1178-1232` | `draw_taskbar` — blue XP gradient, green Start button |
| `xv6-public/desktop.c:240-262` | Icon setup — rename "doom" → "Craft" |
| `xv6-public/desktop.c:2523-2940` | `doom_draw` — Minecraft visuals (walls/ceiling/floor/HUD/weapons) |
| `xv6-public/desktop.c:2692-2698` | Enemy sprite colors — zombie/skeleton/creeper |
| `xv6-public/Makefile:166` | QEMU `hostfwd` — bind `0.0.0.0` so LAN-reachable |
| `xv6-public/rshd.c` | Password auth + CRLF fix + zombie reap |

---

## Task 1: Mouse Speed Fix

**Files:**
- Modify: `xv6-public/mouse.c:149-150`

- [ ] **Step 1: Change the multiplier from 3 to 1**

In `mouse.c`, lines 149–150 currently read:
```c
      mouse.x += dx * 3;
      mouse.y -= dy * 3; // Y is inverted, 3x sensitivity
```
Replace with:
```c
      mouse.x += dx;
      mouse.y -= dy; // Y is inverted
```

- [ ] **Step 2: Build and verify it compiles**

```bash
cd xv6-public && make
```
Expected: build succeeds with no errors.

- [ ] **Step 3: Commit**

```bash
git add xv6-public/mouse.c
git commit -m "fix: reduce mouse sensitivity from 3x to 1x"
```

---

## Task 2: Windows XP Luna Theme — Colors

**Files:**
- Modify: `xv6-public/guilib.h:9-36`

- [ ] **Step 1: Replace color constants with XP Luna palette**

In `guilib.h`, replace lines 9–36 (all `#define COL_*` and the GNOME comment) with:
```c
// Windows XP Luna theme colors
#define COL_DESKTOP_BG1  RGB(5, 90, 174)     // XP sky blue (top)
#define COL_DESKTOP_BG2  RGB(70, 170, 60)    // XP bliss green (bottom hills)
#define COL_TASKBAR      RGB(26, 76, 179)    // XP taskbar blue (left/right)
#define COL_TASKBAR_HI   RGB(55, 120, 215)   // XP taskbar highlight
#define COL_WIN_TITLE    RGB(10, 36, 106)    // XP title bar (inactive)
#define COL_WIN_TITLE_AC RGB(10, 60, 200)    // XP title bar (active, top)
#define COL_WIN_TITLE_AC2 RGB(55, 130, 230)  // XP title bar (active, bottom of gradient)
#define COL_WIN_BG       RGB(236, 233, 216)  // XP window content area (luna beige)
#define COL_WIN_BORDER   RGB(10, 36, 106)    // XP window border blue
#define COL_BTN_CLOSE    RGB(211, 53, 44)    // XP close button red
#define COL_BTN_MIN      RGB(42, 100, 201)   // XP min button blue
#define COL_BTN_MAX      RGB(42, 100, 201)   // XP max button blue
#define COL_TEXT_WHITE   RGB(255, 255, 255)
#define COL_TEXT_LIGHT   RGB(200, 200, 200)
#define COL_TEXT_BLACK   RGB(0, 0, 0)
#define COL_TEXT_GRAY    RGB(128, 128, 128)
#define COL_ACCENT       RGB(10, 70, 190)
#define COL_HIGHLIGHT    RGB(49, 106, 197)
#define COL_SHADOW       RGB(0, 0, 0)
#define COL_MENU_BG      RGB(236, 233, 216)  // XP menu bg (beige)
#define COL_MENU_HI      RGB(49, 106, 197)
#define COL_TERM_BG      RGB(0, 0, 0)
#define COL_TERM_FG      RGB(192, 192, 192)
#define COL_TERM_CURSOR  RGB(255, 255, 255)
#define COL_ICON_BG      RGB(49, 106, 197)
#define COL_FM_BG        RGB(236, 233, 216)
#define COL_FM_SIDEBAR   RGB(213, 208, 198)
#define COL_SCROLLBAR    RGB(120, 120, 150)
#define COL_START_TOP    RGB(55, 170, 55)    // XP Start button top
#define COL_START_BOT    RGB(20, 120, 20)    // XP Start button bottom
```

Also add a new constant at the end of the guilib.h constants section (after `#define BTN_SIZE 14`):
```c
#define BTN_W 22
#define BTN_H 14
```

- [ ] **Step 2: Build to verify no compile errors**

```bash
cd xv6-public && make
```
Expected: builds cleanly.

---

## Task 3: Windows XP Luna Theme — Desktop Background (Bliss)

**Files:**
- Modify: `xv6-public/desktop.c` — `draw_desktop_bg` function (lines 791–814)

- [ ] **Step 1: Replace draw_desktop_bg with Bliss sky+hills**

Replace the entire `draw_desktop_bg` function body:
```c
static void
draw_desktop_bg(void)
{
  int w = screen.width;
  int h = screen.height - TASKBAR_HEIGHT;
  int split = h * 65 / 100; // 65% sky, 35% hills

  // Sky: deep blue at top fading to bright blue horizon
  fb_gradient_v(&screen, 0, 0, w, split, RGB(5,90,174), RGB(130,200,255));

  // Hills: bright green at horizon fading to darker green
  fb_gradient_v(&screen, 0, split, w, h - split, RGB(100,190,70), RGB(50,130,30));

  // Simple hill curve: draw a rough arc of green over the horizon
  for(int x = 0; x < w; x++){
    // Two overlapping hills using a simple parabola approximation
    int hill1 = split - (x - w*3/8) * (x - w*3/8) / (w*2);
    int hill2 = split - (x - w*5/8) * (x - w*5/8) / (w*3);
    int hill = hill1 > hill2 ? hill1 : hill2;
    if(hill < split - 30) hill = split - 30;
    if(hill > split + 20) hill = split + 20;
    // Fill from hill top to split with sky blue (erasing gradient into hill shape)
    for(int y = hill; y < split; y++){
      int t = (y - hill) * 256 / (split - hill + 1);
      int r = 100 + (130-100)*t/256;
      int g = 190 + (200-190)*t/256;
      int bv = 70 + (255-70)*t/256;
      fb_pixel(&screen, x, y, RGB(r,g,bv));
    }
  }
}
```

- [ ] **Step 2: Build**

```bash
cd xv6-public && make
```
Expected: builds cleanly.

---

## Task 4: Windows XP Luna Theme — Window Chrome

**Files:**
- Modify: `xv6-public/desktop.c` — `draw_window` function (lines 847–884)

- [ ] **Step 1: Replace title bar drawing with XP gradient + rectangular buttons**

Find this block in `draw_window` (currently draws flat title + circles):
```c
  // Title bar
  uint tc = is_active ? COL_WIN_TITLE_AC : COL_WIN_TITLE;
  fb_fill_rect(&screen, wx, wy, ww, TITLEBAR_HEIGHT, tc);
  fb_text_nobg(&screen, wx+10, wy+(TITLEBAR_HEIGHT-FONT_H)/2, w->title, COL_TEXT_WHITE);

  // Buttons
  int btn_y = wy + (TITLEBAR_HEIGHT - BTN_SIZE) / 2;
  int close_x = wx + ww - BTN_SIZE - 8;
  int min_x = close_x - BTN_SIZE - 6;
  int max_x = min_x - BTN_SIZE - 6;
  fb_fill_circle(&screen, close_x+BTN_SIZE/2, btn_y+BTN_SIZE/2, BTN_SIZE/2, COL_BTN_CLOSE);
  fb_fill_circle(&screen, min_x+BTN_SIZE/2, btn_y+BTN_SIZE/2, BTN_SIZE/2, COL_BTN_MIN);
  fb_fill_circle(&screen, max_x+BTN_SIZE/2, btn_y+BTN_SIZE/2, BTN_SIZE/2, COL_BTN_MAX);
```

Replace with:
```c
  // Title bar — XP gradient
  if(is_active){
    fb_gradient_v(&screen, wx, wy, ww, TITLEBAR_HEIGHT, COL_WIN_TITLE_AC, COL_WIN_TITLE_AC2);
  } else {
    fb_gradient_v(&screen, wx, wy, ww, TITLEBAR_HEIGHT, RGB(10,36,106), RGB(62,88,162));
  }
  // Title bar highlight line at top
  fb_fill_rect(&screen, wx, wy, ww, 1, RGB(80,130,220));
  fb_text_nobg(&screen, wx+10, wy+(TITLEBAR_HEIGHT-FONT_H)/2, w->title, COL_TEXT_WHITE);

  // XP rectangular buttons (close, max, min) — right side
  int btn_y = wy + (TITLEBAR_HEIGHT - BTN_H) / 2;
  int close_x = wx + ww - BTN_W - 4;
  int max_x   = close_x - BTN_W - 2;
  int min_x   = max_x - BTN_W - 2;

  // Close button (red)
  fb_gradient_v(&screen, close_x, btn_y, BTN_W, BTN_H, RGB(230,80,70), RGB(170,30,20));
  fb_rect(&screen, close_x, btn_y, BTN_W, BTN_H, RGB(200,50,40));
  fb_text_nobg(&screen, close_x+7, btn_y+1, "x", COL_TEXT_WHITE);

  // Max button (blue)
  fb_gradient_v(&screen, max_x, btn_y, BTN_W, BTN_H, COL_WIN_TITLE_AC2, COL_WIN_TITLE_AC);
  fb_rect(&screen, max_x, btn_y, BTN_W, BTN_H, RGB(80,130,220));
  fb_text_nobg(&screen, max_x+7, btn_y+1, "o", COL_TEXT_WHITE);

  // Min button (blue)
  fb_gradient_v(&screen, min_x, btn_y, BTN_W, BTN_H, COL_WIN_TITLE_AC2, COL_WIN_TITLE_AC);
  fb_rect(&screen, min_x, btn_y, BTN_W, BTN_H, RGB(80,130,220));
  fb_text_nobg(&screen, min_x+7, btn_y+1, "_", COL_TEXT_WHITE);
```

- [ ] **Step 2: Fix button hit-testing to use new button coordinates**

Search in `handle_input` for the section that detects button clicks. Find these lines (around line 400–450):
```c
  int close_x = wx + w->w - BTN_SIZE - 8;
  int min_x = close_x - BTN_SIZE - 6;
  int max_x = min_x - BTN_SIZE - 6;
```
Replace with (matching the new layout):
```c
  int close_x = wx + w->w - BTN_W - 4;
  int max_x   = close_x - BTN_W - 2;
  int min_x   = max_x - BTN_W - 2;
```
And update any `point_in_rect` calls that use `BTN_SIZE` for close/min/max to use `BTN_W, BTN_H` instead.

- [ ] **Step 3: Build**

```bash
cd xv6-public && make
```
Expected: builds cleanly.

---

## Task 5: Windows XP Luna Theme — Taskbar & Start Button

**Files:**
- Modify: `xv6-public/desktop.c` — `draw_taskbar` function (lines 1178–1232)

- [ ] **Step 1: Replace draw_taskbar with XP-style blue gradient + green Start button**

Replace the entire `draw_taskbar` function:
```c
static void
draw_taskbar(void)
{
  int ty = screen.height - TASKBAR_HEIGHT;
  int tw = screen.width;

  // XP taskbar: blue gradient (left edge darker, center lighter)
  fb_gradient_v(&screen, 0, ty, tw, TASKBAR_HEIGHT, RGB(42,95,198), RGB(26,65,155));
  // Highlight line at top of taskbar
  fb_fill_rect(&screen, 0, ty, tw, 1, RGB(90,150,230));

  // Start button: green gradient, rounded-looking rect
  int sb_w = 72, sb_h = TASKBAR_HEIGHT - 6;
  int sb_x = 4, sb_y = ty + 3;
  int start_hover = point_in_rect(mouse.x, mouse.y, sb_x, sb_y, sb_w, sb_h);
  fb_gradient_v(&screen, sb_x, sb_y, sb_w, sb_h,
                start_hover ? RGB(80,200,80) : COL_START_TOP,
                start_hover ? RGB(30,150,30) : COL_START_BOT);
  fb_rect(&screen, sb_x, sb_y, sb_w, sb_h, RGB(30,120,30));
  // Start button highlight
  fb_fill_rect(&screen, sb_x+1, sb_y+1, sb_w-2, 2, RGB(100,220,100));
  // Windows flag icon (4 colored squares)
  int fx = sb_x + 6, fy = sb_y + sb_h/2 - 7;
  fb_fill_rect(&screen, fx,   fy,   6, 6, RGB(255,50,50));   // red
  fb_fill_rect(&screen, fx+7, fy,   6, 6, RGB(50,150,255));  // blue
  fb_fill_rect(&screen, fx,   fy+7, 6, 6, RGB(50,200,50));   // green
  fb_fill_rect(&screen, fx+7, fy+7, 6, 6, RGB(255,200,50));  // yellow
  fb_text_nobg(&screen, sb_x+22, sb_y+(sb_h-FONT_H)/2, "start", COL_TEXT_WHITE);

  // Separator after start button
  fb_fill_rect(&screen, sb_x+sb_w+4, ty+4, 1, TASKBAR_HEIGHT-8, RGB(30,60,140));
  fb_fill_rect(&screen, sb_x+sb_w+5, ty+4, 1, TASKBAR_HEIGHT-8, RGB(80,130,220));

  // Window buttons in taskbar
  int bx = sb_x + sb_w + 10;
  for(int i = 0; i < MAX_WINDOWS; i++){
    if(!windows[i].active) continue;
    int bw = fb_text_width(windows[i].title) + 20;
    if(bw < 90) bw = 90;
    int hover = point_in_rect(mouse.x, mouse.y, bx, ty+4, bw, TASKBAR_HEIGHT-8);
    int active = (i == active_window && !windows[i].minimized);
    if(active){
      fb_gradient_v(&screen, bx, ty+4, bw, TASKBAR_HEIGHT-8, RGB(80,140,230), RGB(40,90,190));
      fb_rect(&screen, bx, ty+4, bw, TASKBAR_HEIGHT-8, RGB(90,150,240));
    } else if(hover){
      fb_gradient_v(&screen, bx, ty+4, bw, TASKBAR_HEIGHT-8, RGB(65,120,215), RGB(35,80,175));
      fb_rect(&screen, bx, ty+4, bw, TASKBAR_HEIGHT-8, RGB(80,130,220));
    } else {
      fb_gradient_v(&screen, bx, ty+4, bw, TASKBAR_HEIGHT-8, RGB(55,110,205), RGB(30,70,165));
      fb_rect(&screen, bx, ty+4, bw, TASKBAR_HEIGHT-8, RGB(60,100,195));
    }
    fb_text_nobg(&screen, bx+8, ty+(TASKBAR_HEIGHT-FONT_H)/2, windows[i].title, COL_TEXT_WHITE);
    bx += bw + 3;
  }

  // Right side tray: clock + power
  int power_x = screen.width - 30;
  int power_hover = point_in_rect(mouse.x, mouse.y, power_x-2, ty, 30, TASKBAR_HEIGHT);
  // Power button (simple)
  fb_fill_rect(&screen, power_x, ty+8, 16, 16, power_hover ? COL_BTN_CLOSE : RGB(30,70,170));
  fb_rect(&screen, power_x, ty+8, 16, 16, RGB(80,130,220));
  fb_fill_rect(&screen, power_x+7, ty+6, 2, 8, COL_TEXT_WHITE);

  // Clock
  int ticks_val = uptime();
  int secs = ticks_val / 100;
  int mins = (secs / 60) % 60;
  int hrs = (secs / 3600) % 24;
  int ss = secs % 60;
  char clock[16];
  clock[0] = '0'+hrs/10; clock[1] = '0'+hrs%10;
  clock[2] = ':';
  clock[3] = '0'+mins/10; clock[4] = '0'+mins%10;
  clock[5] = ':';
  clock[6] = '0'+ss/10; clock[7] = '0'+ss%10;
  clock[8] = 0;
  // Tray background
  int cw = fb_text_width(clock) + 8;
  int cx2 = power_x - cw - 4;
  fb_gradient_v(&screen, cx2-2, ty+2, cw+12, TASKBAR_HEIGHT-4, RGB(30,70,170), RGB(20,50,140));
  fb_rect(&screen, cx2-2, ty+2, cw+12, TASKBAR_HEIGHT-4, RGB(50,100,200));
  fb_text_nobg(&screen, cx2, ty+(TASKBAR_HEIGHT-FONT_H)/2, clock, COL_TEXT_WHITE);
}
```

- [ ] **Step 2: Fix Start button click handling**

In `handle_input`, find where "Activities" button is handled (it opens context menu). Replace the `ah` / "Activities" check with the new Start button bounds:
```c
    // Start button click (was "Activities")
    int sb_w = 72, sb_h = TASKBAR_HEIGHT - 6;
    if(point_in_rect(mouse.x, mouse.y, 4, ty+3, sb_w, sb_h)){
```

- [ ] **Step 3: Build**

```bash
cd xv6-public && make
```
Expected: builds cleanly.

---

## Task 6: Minecraft Game — Visuals (Walls, Ceiling, Floor, Sprites)

**Files:**
- Modify: `xv6-public/desktop.c` — `doom_draw` function
- Modify: `xv6-public/desktop.c` — icon/window setup (rename to "Craft")

- [ ] **Step 1: Rename the game from "doom" to "Craft" in setup_desktop**

Find in `setup_desktop` (around line 258):
```c
  gui_strcpy(icons[7].target, "doom");
```
Replace with:
```c
  gui_strcpy(icons[7].target, "craft");
```
And find the icon name:
```c
  gui_strcpy(icons[7].name, "Doom");
```
Replace with:
```c
  gui_strcpy(icons[7].name, "Craft");
```

Also find the icon open handler (around line 296):
```c
  } else if(gui_strcmp(target, "doom") == 0){
```
Replace with:
```c
  } else if(gui_strcmp(target, "craft") == 0){
```

And the window title in `doom_init`:
```c
  // Change the window title — find the open_window call for doom, or set it in doom_init
```
In `doom_init` (line 2111), the window is opened elsewhere. Find:
```c
    if(idx >= 0) doom_init(&windows[idx]);
```
This is preceded by an `open_window` call. Find the open_window call above it and change title from `"Doom"` to `"Craft"`.

- [ ] **Step 2: Replace ceiling/floor drawing in doom_draw**

Find in `doom_draw` (around line 2537):
```c
  // Draw ceiling and floor
  fb_gradient_v(&screen, cx, cy, rw, view_h/2, RGB(20,20,40), RGB(50,50,70));
  fb_gradient_v(&screen, cx, cy+view_h/2, rw, view_h/2, RGB(70,50,35), RGB(35,25,18));
```
Replace with:
```c
  // Minecraft ceiling = sky blue
  fb_fill_rect(&screen, cx, cy, rw, view_h/2, RGB(102,153,216));
  // Clouds (simple white rectangles near top)
  fb_fill_rect(&screen, cx+rw/6,   cy+10, 40, 12, RGB(240,240,255));
  fb_fill_rect(&screen, cx+rw/2,   cy+18, 50, 10, RGB(240,240,255));
  fb_fill_rect(&screen, cx+rw*3/4, cy+8,  35, 14, RGB(240,240,255));
  // Minecraft floor = grass top / dirt below
  fb_fill_rect(&screen, cx, cy+view_h/2, rw, 4, RGB(94,139,75));     // grass strip
  fb_fill_rect(&screen, cx, cy+view_h/2+4, rw, view_h/2-4, RGB(101,67,33)); // dirt
```

- [ ] **Step 3: Replace wall colors with Minecraft block palette**

Find in `doom_draw` the wall color switch statement:
```c
    // Wall colors by type
    uint wc;
    switch(wall_type){
    case 1: wc = RGB(160,40,40); break;    // red brick
    case 2: wc = RGB(40,40,160); break;    // blue stone
    case 3: wc = RGB(40,130,40); break;    // green moss
    case 4: wc = RGB(150,150,40); break;   // yellow pillar
    case 5: wc = RGB(0,200,100); break;    // exit door (green)
    case 6: wc = RGB(200,0,0); break;      // locked door (red)
    default: wc = RGB(128,128,128); break;
    }
```
Replace with:
```c
    // Minecraft block colors
    uint wc;
    switch(wall_type){
    case 1: wc = RGB(125,125,125); break;  // stone
    case 2: wc = RGB(157,128,77);  break;  // oak wood planks
    case 3: wc = RGB(94,139,75);   break;  // grass side
    case 4: wc = RGB(101,67,33);   break;  // dirt
    case 5: wc = RGB(100,50,200);  break;  // nether portal (exit)
    case 6: wc = RGB(139,90,43);   break;  // oak door (locked)
    default: wc = RGB(125,125,125); break;
    }
```

- [ ] **Step 4: Replace enemy sprite colors with Minecraft mobs**

Find the enemy sprite color switch:
```c
          switch(d->enemies[i].type){
          case 0: sprite_color = RGB(180,80,40); break;  // imp = brown
          case 1: sprite_color = RGB(180,40,80); break;   // demon = pink
          case 2: sprite_color = RGB(200,0,0); break;     // boss = dark red
          default: sprite_color = RGB(180,180,180); break;
          }
```
Replace with:
```c
          switch(d->enemies[i].type){
          case 0: sprite_color = RGB(80,120,50);  break;  // zombie = green-gray
          case 1: sprite_color = RGB(200,200,200); break; // skeleton = bone white
          case 2: sprite_color = RGB(50,160,50);  break;  // creeper = bright green
          default: sprite_color = RGB(150,150,150); break;
          }
```

- [ ] **Step 5: Replace pickup colors with Minecraft items**

Find the pickup sprite color switch:
```c
        switch(d->pickups[i].type){
        case 0: sprite_color = RGB(255,0,0); break;    // health = red cross
        case 1: sprite_color = RGB(200,200,0); break;  // ammo = yellow
        case 2: sprite_color = RGB(0,100,200); break;  // armor = blue
        case 3: sprite_color = RGB(255,215,0); break;  // key = gold
        default: sprite_color = RGB(255,255,255); break;
        }
```
Replace with:
```c
        switch(d->pickups[i].type){
        case 0: sprite_color = RGB(255,50,50);  break;  // heart (health)
        case 1: sprite_color = RGB(180,120,50); break;  // arrow (ammo)
        case 2: sprite_color = RGB(160,160,200); break; // iron chestplate (armor)
        case 3: sprite_color = RGB(68,213,234);  break; // diamond (key)
        default: sprite_color = RGB(255,255,255); break;
        }
```

- [ ] **Step 6: Build**

```bash
cd xv6-public && make
```
Expected: builds cleanly.

---

## Task 7: Minecraft Game — HUD (Hearts + Hotbar)

**Files:**
- Modify: `xv6-public/desktop.c` — HUD section of `doom_draw` (lines 2829–2886)

- [ ] **Step 1: Replace the entire HUD bar section with hearts + hotbar**

Find the HUD section (starts at `// === HUD BAR (bottom 40px) ===` around line 2829). Replace from that comment all the way to the key indicator block (ending around line 2886) with:

```c
  // === MINECRAFT HUD ===
  int hud_y = cy + view_h;
  fb_fill_rect(&screen, cx, hud_y, rw, 40, RGB(60,50,40));
  fb_fill_rect(&screen, cx, hud_y, rw, 1, RGB(100,90,80));

  // Hearts (10 hearts = 100 HP, each heart = 10 HP)
  // Draw from left
  for(int h2 = 0; h2 < 10; h2++){
    int hx = cx + 4 + h2 * 18;
    int hy = hud_y + 4;
    int filled = (d->health - h2*10);
    // Heart outline (dark)
    fb_fill_rect(&screen, hx+2, hy,   8, 2, RGB(30,10,10));
    fb_fill_rect(&screen, hx,   hy+2, 12, 8, RGB(30,10,10));
    fb_fill_rect(&screen, hx+2, hy+10, 8, 2, RGB(30,10,10));
    fb_fill_rect(&screen, hx+3, hy+11, 6, 1, RGB(30,10,10));
    // Heart fill
    uint hcol = filled >= 10 ? RGB(220,50,50) : (filled > 0 ? RGB(140,30,30) : RGB(50,20,20));
    fb_fill_rect(&screen, hx+2, hy+1,   8, 2, hcol);
    fb_fill_rect(&screen, hx+1, hy+2,  10, 7, hcol);
    fb_fill_rect(&screen, hx+2, hy+9,   8, 2, hcol);
    fb_fill_rect(&screen, hx+3, hy+10,  6, 1, hcol);
    fb_fill_rect(&screen, hx+4, hy+11,  4, 1, hcol);
  }

  // Hotbar (9 slots, centered, selected slot highlighted)
  // Weapons map: 0=sword(slot0), 1=pickaxe(slot1), 2=bow(slot2)
  int slot_w = 26, slot_h = 26;
  int hb_total = 9 * slot_w + 8 * 2; // 9 slots + 2px gap
  int hb_x = cx + rw/2 - hb_total/2;
  int hb_y = hud_y + 8;
  for(int sl = 0; sl < 9; sl++){
    int sx2 = hb_x + sl*(slot_w+2);
    int selected = (sl == d->weapon);
    // Slot background
    uint slot_bg = selected ? RGB(220,180,100) : RGB(80,70,60);
    uint slot_bd = selected ? RGB(240,210,140) : RGB(40,35,30);
    fb_fill_rect(&screen, sx2, hb_y, slot_w, slot_h, slot_bg);
    fb_rect(&screen, sx2, hb_y, slot_w, slot_h, slot_bd);
    // Draw item in slot
    if(sl == 0){
      // Sword (diagonal gray rectangle)
      fb_fill_rect(&screen, sx2+10, hb_y+3, 4, 14, RGB(180,180,200));
      fb_fill_rect(&screen, sx2+6,  hb_y+15, 12, 3, RGB(120,80,40));
    } else if(sl == 1){
      // Pickaxe
      fb_fill_rect(&screen, sx2+11, hb_y+6, 3, 14, RGB(120,80,40));
      fb_fill_rect(&screen, sx2+4,  hb_y+3, 16, 5, RGB(180,180,200));
    } else if(sl == 2){
      // Bow (arc shape)
      fb_fill_rect(&screen, sx2+12, hb_y+3, 2, 18, RGB(120,80,40));
      fb_fill_rect(&screen, sx2+7,  hb_y+4, 5, 2, RGB(120,80,40));
      fb_fill_rect(&screen, sx2+7,  hb_y+18, 5, 2, RGB(120,80,40));
      fb_fill_rect(&screen, sx2+9,  hb_y+6, 2, 12, RGB(200,200,200)); // string
    }
  }

  // Ammo count (right of hotbar)
  if(d->weapon == 2){ // bow shows arrow count
    char ammo_str[8]; gui_itoa(d->ammo, ammo_str);
    fb_text_nobg(&screen, cx+rw-60, hud_y+12, ammo_str, RGB(200,200,100));
  }

  // Diamond key indicator
  if(d->has_key){
    fb_fill_rect(&screen, cx+rw-24, hud_y+6, 14, 14, RGB(68,213,234));
    fb_text_nobg(&screen, cx+rw-28, hud_y+22, "KEY", RGB(68,213,234));
  }
```

- [ ] **Step 2: Replace weapon display at bottom with Minecraft sword/pickaxe/bow**

Find the weapon display section (around line 2799):
```c
  // Weapon display at bottom center
  int wpn_cx = cx + rw/2;
  int wpn_y = cy + view_h - 30;
  if(d->shoot_timer > 0){
    ...
  }
  if(d->weapon == 0){ ... fist ... }
  else if(d->weapon == 1){ ... pistol ... }
  else if(d->weapon == 2){ ... shotgun ... }
```
Replace the entire weapon display block with:
```c
  // Minecraft weapon display
  int wpn_cx = cx + rw/2;
  int wpn_y = cy + view_h - 50;
  if(d->shoot_timer > 0){
    // Swing effect: offset weapon
    wpn_y += 15;
  }
  if(d->weapon == 0){
    // Sword: gray blade + brown handle
    fb_fill_rect(&screen, wpn_cx-4,  wpn_y, 8, 30, RGB(180,180,200));
    fb_fill_rect(&screen, wpn_cx-8,  wpn_y+28, 16, 4, RGB(120,80,40));
    fb_fill_rect(&screen, wpn_cx-2,  wpn_y+32, 4, 10, RGB(120,80,40));
  } else if(d->weapon == 1){
    // Pickaxe: brown handle + gray head
    fb_fill_rect(&screen, wpn_cx-3,  wpn_y+10, 6, 30, RGB(120,80,40));
    fb_fill_rect(&screen, wpn_cx-14, wpn_y, 28, 10, RGB(180,180,200));
    fb_fill_rect(&screen, wpn_cx-14, wpn_y, 10, 18, RGB(180,180,200));
  } else if(d->weapon == 2){
    // Bow: brown arc + string
    fb_fill_rect(&screen, wpn_cx,    wpn_y, 4, 40, RGB(120,80,40));
    fb_fill_rect(&screen, wpn_cx-10, wpn_y+4, 10, 4, RGB(120,80,40));
    fb_fill_rect(&screen, wpn_cx-10, wpn_y+34, 10, 4, RGB(120,80,40));
    fb_fill_rect(&screen, wpn_cx-8,  wpn_y+8, 2, 26, RGB(220,220,220));
  }
```

- [ ] **Step 3: Replace crosshair with white Minecraft crosshair**

Find:
```c
  // Crosshair
  int chx = cx + rw/2, chy = cy + view_h/2;
  fb_fill_rect(&screen, chx-5, chy, 4, 1, RGB(0,255,0));
  fb_fill_rect(&screen, chx+2, chy, 4, 1, RGB(0,255,0));
  fb_fill_rect(&screen, chx, chy-5, 1, 4, RGB(0,255,0));
  fb_fill_rect(&screen, chx, chy+2, 1, 4, RGB(0,255,0));
```
Replace with:
```c
  // Minecraft crosshair (white, slightly larger)
  int chx = cx + rw/2, chy = cy + view_h/2;
  fb_fill_rect(&screen, chx-7, chy-1, 6, 3, RGB(255,255,255));
  fb_fill_rect(&screen, chx+2, chy-1, 6, 3, RGB(255,255,255));
  fb_fill_rect(&screen, chx-1, chy-7, 3, 6, RGB(255,255,255));
  fb_fill_rect(&screen, chx-1, chy+2, 3, 6, RGB(255,255,255));
```

- [ ] **Step 4: Update death/level-complete overlays to Minecraft style**

Find:
```c
    fb_text_nobg(&screen, cx+rw/2-52, cy+view_h/2-20, "YOU DIED", RGB(255,50,50));
```
Replace with:
```c
    fb_text_nobg(&screen, cx+rw/2-52, cy+view_h/2-20, "YOU DIED", RGB(220,50,50));
```
No other changes needed here — the death screen colors already look fine.

- [ ] **Step 5: Build**

```bash
cd xv6-public && make
```
Expected: builds cleanly.

---

## Task 8: Networking — Reachable from Another Machine

**Files:**
- Modify: `xv6-public/Makefile:166`
- Modify: `xv6-public/rshd.c`

**Context:** QEMU currently uses `hostfwd=tcp::2323-:23` which binds to `127.0.0.1` only — unreachable from another machine. We need `0.0.0.0:2323` so the host's LAN IP is exposed. We also add a simple password prompt to rshd so it isn't completely open, and fix zombie processes.

- [ ] **Step 1: Fix QEMU hostfwd to bind on all interfaces**

In `Makefile` line 166, find:
```
	-netdev user,id=net0,hostfwd=tcp::2323-:23 -device e1000,netdev=net0 \
```
Replace with:
```
	-netdev user,id=net0,hostfwd=tcp:0.0.0.0:2323-:23 -device e1000,netdev=net0 \
```

- [ ] **Step 2: Add password auth + zombie reap + CRLF fix to rshd.c**

Replace the entire `rshd.c` with an improved version:
```c
// rshd - remote shell daemon with password auth
// Listens on TCP port 23. Connects protected by a simple password.
//
// Usage (inside xv6): rshd
// From host:    nc   <host-ip> 2323
// From LAN:     nc   <host-ip> 2323   (after Makefile hostfwd fix)

#include "types.h"
#include "user.h"
#include "fcntl.h"

#define PORT 23
#define PASSWORD "xv6"

static void
dbg(char *msg)
{
  int fd = open("console", O_RDWR);
  if (fd >= 0) {
    write(fd, msg, strlen(msg));
    close(fd);
  }
}

static void
net_write(int fd, char *s)
{
  write(fd, s, strlen(s));
}

// Read one line from fd into buf (max len-1 chars), strip CR/LF
static int
readline(int fd, char *buf, int len)
{
  int i = 0;
  char c;
  while(i < len-1){
    if(read(fd, &c, 1) <= 0) return -1;
    if(c == '\r') continue;
    if(c == '\n') break;
    buf[i++] = c;
  }
  buf[i] = 0;
  return i;
}

int
main(void)
{
  dbg("rshd: start\n");

  int lfd = socket(0, 0, 0);
  if (lfd < 0) { dbg("rshd: socket failed\n"); exit(); }

  if (bind(lfd, PORT) < 0) { dbg("rshd: bind failed\n"); exit(); }
  if (listen(lfd, 4) < 0) { dbg("rshd: listen failed\n"); exit(); }

  dbg("rshd: listening on port 23 (host port 2323)\n");

  for (;;) {
    // Reap zombies (non-blocking wait for any child)
    while(wait() >= 0);

    int cfd = accept(lfd, 0, 0);
    if (cfd < 0) continue;

    int pid = fork();
    if (pid < 0) { close(cfd); continue; }

    if (pid == 0) {
      close(lfd);

      // Password challenge
      net_write(cfd, "xv6 login: password: ");
      char pass[64];
      if(readline(cfd, pass, sizeof(pass)) < 0){
        net_write(cfd, "\r\nRead error.\r\n");
        close(cfd); exit();
      }

      // Compare password
      int ok = 1;
      char *pw = PASSWORD;
      int i;
      for(i = 0; pw[i] && pass[i]; i++)
        if(pw[i] != pass[i]){ ok = 0; break; }
      if(pw[i] != 0 || pass[i] != 0) ok = 0;

      if(!ok){
        net_write(cfd, "\r\nAccess denied.\r\n");
        close(cfd); exit();
      }

      net_write(cfd, "\r\nWelcome to xv6!\r\n");

      // Wire socket to stdin/stdout/stderr
      close(0); dup(cfd);
      close(1); dup(cfd);
      close(2); dup(cfd);
      close(cfd);

      char *argv[] = { "sh", 0 };
      exec("sh", argv);
      write(1, "exec sh failed\r\n", 16);
      exit();
    }

    close(cfd);
  }
}
```

- [ ] **Step 3: Build**

```bash
cd xv6-public && make
```
Expected: builds cleanly.

- [ ] **Step 4: Commit all networking changes**

```bash
git add xv6-public/Makefile xv6-public/rshd.c
git commit -m "fix: expose rshd to LAN via 0.0.0.0 hostfwd, add password auth"
```

---

## Task 9: Final Build + Commit

- [ ] **Step 1: Full clean build**

```bash
cd xv6-public && make clean && make
```
Expected: builds with no errors or warnings about our changed files.

- [ ] **Step 2: Commit all GUI changes**

```bash
git add xv6-public/mouse.c xv6-public/guilib.h xv6-public/desktop.c
git commit -m "feat: Windows XP Luna theme, Minecraft game, mouse fix"
```

---

## Networking How-To (for the user)

After running `make qemu-gui`:
1. xv6 runs at IP `10.0.2.15` (QEMU internal)
2. QEMU forwards **host port 2323** → **xv6 port 23**
3. From the **same machine**: `nc localhost 2323`, password: `xv6`
4. From **another machine on the LAN**: `nc <your-host-ip> 2323`, password: `xv6`
   - Find your host IP with `ip addr` or `ifconfig` on the host

> **Note:** True SSH (encrypted) requires implementing RSA/DH + AES in xv6 — not feasible. `rshd` is a password-authenticated raw TCP shell, which is the realistic maximum for this kernel.

---

## Self-Review

- Mouse fix: ✅ one-line change, no dependencies
- XP Colors: ✅ covers all COL_ constants; added COL_WIN_TITLE_AC2, BTN_W, BTN_H for new code
- Desktop bg: ✅ Bliss-style sky+hills
- draw_window: ✅ gradient title bar, rectangular buttons, BTN_W/BTN_H used consistently
- draw_taskbar: ✅ Start button + blue gradient + window buttons updated
- Button click-testing: ✅ Task 4 Step 2 covers this
- Minecraft walls/ceiling/floor: ✅ Tasks 6+7 cover all visual elements
- Enemy/pickup sprites: ✅ zombie/skeleton/creeper + heart/arrow/iron/diamond
- HUD: ✅ hearts + hotbar replaces HP/AR bars
- Weapons: ✅ sword/pickaxe/bow replaces fist/pistol/shotgun
- Crosshair: ✅ white instead of green
- Networking: ✅ hostfwd bound to 0.0.0.0, rshd has password + zombie reap
- Icon rename doom→craft: ✅ Task 6 Step 1
