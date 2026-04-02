// xv6 Desktop Environment - GNOME-inspired GUI with Games
#include "types.h"
#include "stat.h"
#include "vga.h"
#include "mouse.h"
#include "user.h"
#include "fcntl.h"
#include "guilib.h"

// ---- Global state ----
static struct framebuf screen;
static struct mouse_info mouse;
static struct mouse_info prev_mouse;
static int mouse_clicked, mouse_right_clicked, mouse_released;

static struct window windows[MAX_WINDOWS];
static int active_window = -1;
static int dragging_window = -1;
static int drag_off_x, drag_off_y;
static int resizing_window = -1;

static struct desktop_icon icons[16];
static int num_icons = 0;

static struct context_menu ctx_menu;
static struct notification notif;

static int tick_counter = 0;
static uint *bg_cache = 0;
static int wallpaper_style = 0;
static int last_icon_click = -1;
static int last_icon_tick = 0;

// Calculator state
static int calc_acc;
static int calc_cur;
static int calc_op;  // 0=none 1=+ 2=- 3=* 4=/
static int calc_fresh;

// Pseudo-random number generator
static uint rand_seed = 12345;
static uint
gui_rand(void)
{
  rand_seed ^= rand_seed << 13;
  rand_seed ^= rand_seed >> 17;
  rand_seed ^= rand_seed << 5;
  return rand_seed;
}

// ---- String helpers ----
static int gui_strcmp(char *a, char *b) {
  while(*a && *b && *a == *b){ a++; b++; }
  return *a - *b;
}

static void gui_memset(void *dst, int c, int n) {
  char *d = (char*)dst;
  for(int i = 0; i < n; i++) d[i] = c;
}

static void gui_memcpy(void *dst, void *src, int n) {
  char *d = (char*)dst, *s = (char*)src;
  for(int i = 0; i < n; i++) d[i] = s[i];
}

static void show_notif(char *msg) {
  gui_strcpy(notif.text, msg);
  notif.active = 1;
  notif.timer = 150;
}

// ---- Forward declarations ----
static void setup_desktop(void);
static void handle_input(void);
static void render(void);
static int  open_window(char *title, int type, int x, int y, int w, int h);
static void close_window(int idx);
static void draw_desktop_bg(void);
static void draw_desktop_icons(void);
static void draw_window(struct window *w, int idx);
static void draw_taskbar(void);
static void draw_context_menu(void);
static void draw_notification(void);

// Terminal
static void terminal_handle_key(struct window *w, int key);
static void terminal_exec_cmd(struct window *w, char *cmd);
static void terminal_append(struct window *w, char *text);

// File manager
static void filemanager_refresh(struct window *w);
static void filemanager_handle_click(struct window *w, int mx, int my);
static void filemanager_handle_key(struct window *w, int key);

// Games
static void snake_init(struct window *w);
static void snake_update(struct window *w);
static void snake_handle_key(struct window *w, int key);
static void snake_draw(struct window *w);

static void mine_init(struct window *w);
static void mine_handle_click(struct window *w, int mx, int my, int right);
static void mine_draw(struct window *w);

static void paint_init(struct window *w);
static void paint_handle_mouse(struct window *w, int mx, int my, int pressed);
static void paint_handle_click(struct window *w, int mx, int my);
static void paint_draw(struct window *w);

static void doom_init(struct window *w);
static void doom_handle_key(struct window *w, int key);
static void doom_draw(struct window *w);

// Sin/cos table for doom (scaled by 256 = FP_ONE)
// Precomputed sin values for 0-90 degrees * 256, every 1 degree
static int sin_tab[360];
static int cos_tab[360];

// Hardcoded sin(0..90)*256 lookup - exact values
static const short sin_q1[91] = {
  0,   4,   9,  13,  18,  22,  27,  31,  36,  40,
 44,  49,  53,  57,  62,  66,  70,  75,  79,  83,
 87,  91,  95,  99, 104, 107, 111, 115, 119, 123,
126, 130, 133, 137, 140, 143, 147, 150, 153, 156,
159, 162, 165, 167, 170, 173, 175, 178, 180, 182,
185, 187, 189, 191, 193, 195, 197, 199, 200, 202,
204, 205, 207, 208, 209, 211, 212, 213, 214, 215,
216, 217, 218, 219, 220, 220, 221, 222, 222, 223,
223, 224, 224, 224, 225, 225, 225, 225, 226, 226,
256
};

static void init_trig(void) {
  // Fill sin table using quadrant symmetry from hardcoded Q1 values
  for(int i = 0; i <= 90; i++)
    sin_tab[i] = sin_q1[i];
  for(int i = 91; i < 180; i++)
    sin_tab[i] = sin_q1[180 - i];
  for(int i = 180; i < 360; i++)
    sin_tab[i] = -sin_tab[i - 180];
  // cos is sin shifted by 90
  for(int i = 0; i < 360; i++)
    cos_tab[i] = sin_tab[(i + 90) % 360];
}

// Fixed-point abs
static int fp_abs(int x) { return x < 0 ? -x : x; }

// Bring window to front (z-order) - unused for now, managed by active_window index

// ---- Main ----
int
main(int argc, char *argv[])
{
  struct screen_info si;

  printf(1, "desktop: initializing graphics...\n");
  if(screen_init(&si) < 0){
    printf(1, "desktop: failed to init screen, falling back to shell\n");
    char *sh_argv[] = { "sh", 0 };
    exec("sh", sh_argv);
    exit();
  }

  int fb_size = si.width * si.height * 4;
  uint *backbuf = (uint*)malloc(fb_size);
  if(!backbuf){
    printf(1, "desktop: failed to allocate back buffer\n");
    exit();
  }

  screen.pixels = backbuf;
  screen.width = si.width;
  screen.height = si.height;

  gui_memset(&mouse, 0, sizeof(mouse));
  gui_memset(&prev_mouse, 0, sizeof(prev_mouse));
  gui_memset(windows, 0, sizeof(windows));
  gui_memset(&ctx_menu, 0, sizeof(ctx_menu));
  gui_memset(&notif, 0, sizeof(notif));

  rand_seed = uptime() * 31337 + 42;
  init_trig();
  setup_desktop();

  for(;;){
    handle_input();

    // Update games
    for(int i = 0; i < MAX_WINDOWS; i++){
      if(!windows[i].active || windows[i].minimized) continue;
      if(windows[i].type == WIN_SNAKE) snake_update(&windows[i]);
    }

    render();
    flush_screen(backbuf);
    tick_counter++;

    // Notification timer
    if(notif.active && --notif.timer <= 0)
      notif.active = 0;

    // Small sleep to avoid busy-spinning and let mouse interrupts accumulate
    sleep(1);
  }
}

// ---- Desktop setup ----
static void
setup_desktop(void)
{
  int col1_x = 40, col2_x = 140, col3_x = 240;
  int y = 40, spacing = ICON_SIZE + ICON_TEXT_GAP + FONT_H + 20;

  // Column 1: System apps
  gui_strcpy(icons[0].name, "Terminal");
  gui_strcpy(icons[0].target, "terminal");
  icons[0].icon_type = ICON_TERMINAL;
  icons[0].x = col1_x; icons[0].y = y;
  num_icons++;

  gui_strcpy(icons[1].name, "Files");
  gui_strcpy(icons[1].target, "files");
  icons[1].icon_type = ICON_FOLDER;
  icons[1].x = col1_x; icons[1].y = y + spacing;
  num_icons++;

  gui_strcpy(icons[2].name, "Editor");
  gui_strcpy(icons[2].target, "editor");
  icons[2].icon_type = ICON_DOCUMENT;
  icons[2].x = col1_x; icons[2].y = y + spacing*2;
  num_icons++;

  gui_strcpy(icons[3].name, "About");
  gui_strcpy(icons[3].target, "about");
  icons[3].icon_type = ICON_ABOUT;
  icons[3].x = col1_x; icons[3].y = y + spacing*3;
  num_icons++;

  // Column 2: Games
  gui_strcpy(icons[4].name, "Snake");
  gui_strcpy(icons[4].target, "snake");
  icons[4].icon_type = ICON_GAME;
  icons[4].x = col2_x; icons[4].y = y;
  num_icons++;

  gui_strcpy(icons[5].name, "Mines");
  gui_strcpy(icons[5].target, "mines");
  icons[5].icon_type = ICON_GAME;
  icons[5].x = col2_x; icons[5].y = y + spacing;
  num_icons++;

  gui_strcpy(icons[6].name, "Paint");
  gui_strcpy(icons[6].target, "paint");
  icons[6].icon_type = ICON_GAME;
  icons[6].x = col2_x; icons[6].y = y + spacing*2;
  num_icons++;

  gui_strcpy(icons[7].name, "Doom");
  gui_strcpy(icons[7].target, "doom");
  icons[7].icon_type = ICON_GAME;
  icons[7].x = col2_x; icons[7].y = y + spacing*3;
  num_icons++;

  // Column 3: Utilities
  gui_strcpy(icons[8].name, "Calc");
  gui_strcpy(icons[8].target, "calc");
  icons[8].icon_type = ICON_SETTINGS;
  icons[8].x = col3_x; icons[8].y = y;
  num_icons++;

  gui_strcpy(icons[9].name, "Settings");
  gui_strcpy(icons[9].target, "settings");
  icons[9].icon_type = ICON_SETTINGS;
  icons[9].x = col3_x; icons[9].y = y + spacing;
  num_icons++;
}

// Open an app by target name
static void open_app(char *target) {
  if(gui_strcmp(target, "terminal") == 0)
    open_window("Terminal", WIN_TERMINAL, 200, 80, 640, 420);
  else if(gui_strcmp(target, "files") == 0)
    open_window("Files", WIN_FILEMANAGER, 250, 60, 600, 450);
  else if(gui_strcmp(target, "editor") == 0)
    open_window("Text Editor", WIN_TEXTEDITOR, 280, 90, 550, 400);
  else if(gui_strcmp(target, "about") == 0)
    open_window("About xv6", WIN_ABOUT, 300, 150, 420, 320);
  else if(gui_strcmp(target, "snake") == 0){
    int idx = open_window("Snake", WIN_SNAKE, 200, 80, 340, 380);
    if(idx >= 0) snake_init(&windows[idx]);
  } else if(gui_strcmp(target, "mines") == 0){
    int idx = open_window("Minesweeper", WIN_MINESWEEPER, 250, 100, 280, 310);
    if(idx >= 0) mine_init(&windows[idx]);
  } else if(gui_strcmp(target, "paint") == 0){
    int idx = open_window("Paint", WIN_PAINT, 200, 60, 360, 280);
    if(idx >= 0) paint_init(&windows[idx]);
  } else if(gui_strcmp(target, "doom") == 0){
    int idx = open_window("Doom 3D", WIN_DOOM, 150, 50, 424, 280);
    if(idx >= 0) doom_init(&windows[idx]);
  } else if(gui_strcmp(target, "calc") == 0){
    int idx = open_window("Calculator", WIN_CALCULATOR, 350, 120, 240, 320);
    if(idx >= 0){
      calc_acc = 0; calc_cur = 0; calc_op = 0; calc_fresh = 1;
    }
  } else if(gui_strcmp(target, "settings") == 0){
    open_window("Settings", WIN_SETTINGS, 250, 80, 480, 380);
  }
}

// ---- Input handling ----
static void
handle_input(void)
{
  prev_mouse = mouse;
  getmouse(&mouse);

  mouse_clicked = (mouse.buttons & MOUSE_LEFT) && !(prev_mouse.buttons & MOUSE_LEFT);
  mouse_right_clicked = (mouse.buttons & MOUSE_RIGHT) && !(prev_mouse.buttons & MOUSE_RIGHT);
  mouse_released = !(mouse.buttons & MOUSE_LEFT) && (prev_mouse.buttons & MOUSE_LEFT);

  // Paint: continuous mouse handling
  if(active_window >= 0 && windows[active_window].active &&
     !windows[active_window].minimized &&
     windows[active_window].type == WIN_PAINT){
    struct window *pw = &windows[active_window];
    int wx = pw->x + WIN_BORDER;
    int wy = pw->y + TITLEBAR_HEIGHT;
    int pressed = (mouse.buttons & MOUSE_LEFT);
    if(point_in_rect(mouse.x, mouse.y, wx, wy + 24, pw->w - 60, pw->h - 24)){
      paint_handle_mouse(pw, mouse.x - wx, mouse.y - wy - 24, pressed);
    } else if(!pressed){
      pw->game.paint.painting = 0;
      pw->game.paint.last_px = -1;
    }
  }

  // Window dragging
  if(mouse_released){
    dragging_window = -1;
    resizing_window = -1;
  }

  if(dragging_window >= 0 && (mouse.buttons & MOUSE_LEFT)){
    windows[dragging_window].x = mouse.x - drag_off_x;
    windows[dragging_window].y = mouse.y - drag_off_y;
    if(windows[dragging_window].y < 0) windows[dragging_window].y = 0;
    if(windows[dragging_window].y > screen.height - TASKBAR_HEIGHT - TITLEBAR_HEIGHT)
      windows[dragging_window].y = screen.height - TASKBAR_HEIGHT - TITLEBAR_HEIGHT;
    return;
  }

  // Window resizing
  if(resizing_window >= 0 && (mouse.buttons & MOUSE_LEFT)){
    struct window *rw = &windows[resizing_window];
    int new_w = mouse.x - rw->x;
    int new_h = mouse.y - rw->y - TITLEBAR_HEIGHT;
    if(new_w < 200) new_w = 200;
    if(new_h < 100) new_h = 100;
    if(new_w > screen.width) new_w = screen.width;
    if(new_h > screen.height - TASKBAR_HEIGHT) new_h = screen.height - TASKBAR_HEIGHT;
    rw->w = new_w;
    rw->h = new_h;
    return;
  }

  // Context menu click
  if(ctx_menu.visible && mouse_clicked){
    if(point_in_rect(mouse.x, mouse.y, ctx_menu.x, ctx_menu.y,
                     ctx_menu.width, ctx_menu.height)){
      int item = (mouse.y - ctx_menu.y - 2) / (FONT_H + 8);
      if(item >= 0 && item < ctx_menu.item_count){
        char *it = ctx_menu.items[item];
        if(gui_strcmp(it, "New File") == 0){
          int fd = open("newfile.txt", O_CREATE | O_RDWR);
          if(fd >= 0){ close(fd); show_notif("File created"); }
        } else if(gui_strcmp(it, "New Folder") == 0){
          mkdir("newfolder");
          show_notif("Folder created");
        } else if(gui_strcmp(it, "Wallpaper") == 0){
          wallpaper_style = (wallpaper_style + 1) % 5;
          if(bg_cache){ free(bg_cache); bg_cache = 0; }
        } else {
          // Try to match as app target
          char target[32];
          // Convert menu item to lowercase target
          int len = gui_strlen(it);
          for(int i = 0; i < len && i < 31; i++){
            target[i] = (it[i] >= 'A' && it[i] <= 'Z') ? it[i] + 32 : it[i];
          }
          target[len] = 0;
          open_app(target);
        }
      }
    }
    ctx_menu.visible = 0;
    return;
  }

  // Right click -> context menu
  if(mouse_right_clicked){
    int on_window = 0;
    for(int i = MAX_WINDOWS - 1; i >= 0; i--){
      if(!windows[i].active || windows[i].minimized) continue;
      if(point_in_rect(mouse.x, mouse.y, windows[i].x, windows[i].y,
                       windows[i].w, windows[i].h + TITLEBAR_HEIGHT)){
        on_window = 1;
        if(windows[i].type == WIN_MINESWEEPER){
          mine_handle_click(&windows[i],
            mouse.x - windows[i].x - WIN_BORDER,
            mouse.y - windows[i].y - TITLEBAR_HEIGHT, 1);
        }
        break;
      }
    }
    if(!on_window && mouse.y < screen.height - TASKBAR_HEIGHT){
      ctx_menu.visible = 1;
      ctx_menu.x = mouse.x;
      ctx_menu.y = mouse.y;
      gui_strcpy(ctx_menu.items[0], "Terminal");
      gui_strcpy(ctx_menu.items[1], "Files");
      gui_strcpy(ctx_menu.items[2], "Editor");
      gui_strcpy(ctx_menu.items[3], "Calc");
      gui_strcpy(ctx_menu.items[4], "New File");
      gui_strcpy(ctx_menu.items[5], "New Folder");
      gui_strcpy(ctx_menu.items[6], "Wallpaper");
      gui_strcpy(ctx_menu.items[7], "About");
      ctx_menu.item_count = 8;
      ctx_menu.width = 150;
      ctx_menu.height = ctx_menu.item_count * (FONT_H + 8) + 4;
      if(ctx_menu.x + ctx_menu.width > screen.width)
        ctx_menu.x = screen.width - ctx_menu.width;
      if(ctx_menu.y + ctx_menu.height > screen.height - TASKBAR_HEIGHT)
        ctx_menu.y = screen.height - TASKBAR_HEIGHT - ctx_menu.height;
    }
    return;
  }

  // Left click
  if(mouse_clicked){
    ctx_menu.visible = 0;

    // Taskbar
    if(mouse.y >= screen.height - TASKBAR_HEIGHT){
      // Power button (right side, before clock)
      int power_x = screen.width - 100;
      if(mouse.x >= power_x && mouse.x < power_x + 30){
        // Show shutdown dialog
        open_window("Power", WIN_SHUTDOWN, screen.width/2 - 160, screen.height/2 - 80, 320, 160);
        return;
      }

      if(mouse.x < 100){
        // Activities menu
        ctx_menu.visible = 1;
        gui_strcpy(ctx_menu.items[0], "Terminal");
        gui_strcpy(ctx_menu.items[1], "Files");
        gui_strcpy(ctx_menu.items[2], "Editor");
        gui_strcpy(ctx_menu.items[3], "Calc");
        gui_strcpy(ctx_menu.items[4], "Snake");
        gui_strcpy(ctx_menu.items[5], "Doom");
        gui_strcpy(ctx_menu.items[6], "Settings");
        gui_strcpy(ctx_menu.items[7], "About");
        ctx_menu.item_count = 8;
        ctx_menu.width = 150;
        ctx_menu.height = ctx_menu.item_count * (FONT_H + 8) + 4;
        ctx_menu.x = 0;
        ctx_menu.y = screen.height - TASKBAR_HEIGHT - ctx_menu.height;
      } else {
        // Window buttons in taskbar
        int bx = 110;
        for(int i = 0; i < MAX_WINDOWS; i++){
          if(!windows[i].active) continue;
          int bw = fb_text_width(windows[i].title) + 20;
          if(bw < 80) bw = 80;
          if(point_in_rect(mouse.x, mouse.y, bx,
                           screen.height - TASKBAR_HEIGHT + 4, bw, TASKBAR_HEIGHT - 8)){
            if(windows[i].minimized){
              windows[i].minimized = 0;
              active_window = i;
            } else if(active_window == i){
              windows[i].minimized = 1;
              active_window = -1;
            } else {
              active_window = i;
            }
            break;
          }
          bx += bw + 4;
        }
      }
      return;
    }

    // Check windows (top to bottom = reverse order)
    int clicked_win = -1;
    for(int i = MAX_WINDOWS - 1; i >= 0; i--){
      if(!windows[i].active || windows[i].minimized) continue;
      int wx = windows[i].x, wy = windows[i].y;
      int ww = windows[i].w, wh = windows[i].h + TITLEBAR_HEIGHT;

      if(point_in_rect(mouse.x, mouse.y, wx, wy, ww, wh)){
        clicked_win = i;
        active_window = i;

        // Check resize handle (bottom-right corner, 12x12)
        if(point_in_rect(mouse.x, mouse.y, wx+ww-12, wy+wh-12, 12, 12) &&
           !windows[i].maximized){
          resizing_window = i;
          break;
        }

        if(mouse.y < wy + TITLEBAR_HEIGHT){
          // Title bar buttons
          int btn_y = wy + (TITLEBAR_HEIGHT - BTN_SIZE) / 2;
          int close_x = wx + ww - BTN_SIZE - 8;
          int min_x = close_x - BTN_SIZE - 6;
          int max_x = min_x - BTN_SIZE - 6;

          if(point_in_rect(mouse.x, mouse.y, close_x, btn_y, BTN_SIZE, BTN_SIZE)){
            close_window(i); return;
          }
          if(point_in_rect(mouse.x, mouse.y, min_x, btn_y, BTN_SIZE, BTN_SIZE)){
            windows[i].minimized = 1;
            if(active_window == i) active_window = -1;
            return;
          }
          if(point_in_rect(mouse.x, mouse.y, max_x, btn_y, BTN_SIZE, BTN_SIZE)){
            if(!windows[i].maximized){
              windows[i].orig_x = windows[i].x;
              windows[i].orig_y = windows[i].y;
              windows[i].orig_w = windows[i].w;
              windows[i].orig_h = windows[i].h;
              windows[i].x = 0; windows[i].y = 0;
              windows[i].w = screen.width;
              windows[i].h = screen.height - TASKBAR_HEIGHT - TITLEBAR_HEIGHT;
              windows[i].maximized = 1;
            } else {
              windows[i].x = windows[i].orig_x;
              windows[i].y = windows[i].orig_y;
              windows[i].w = windows[i].orig_w;
              windows[i].h = windows[i].orig_h;
              windows[i].maximized = 0;
            }
            return;
          }
          // Start drag
          dragging_window = i;
          drag_off_x = mouse.x - wx;
          drag_off_y = mouse.y - wy;
        } else {
          // Content area clicks
          int cmx = mouse.x - wx - WIN_BORDER;
          int cmy = mouse.y - wy - TITLEBAR_HEIGHT;
          if(windows[i].type == WIN_FILEMANAGER)
            filemanager_handle_click(&windows[i], cmx, cmy);
          else if(windows[i].type == WIN_MINESWEEPER)
            mine_handle_click(&windows[i], cmx, cmy, 0);
          else if(windows[i].type == WIN_PAINT)
            paint_handle_click(&windows[i], cmx, cmy);
          else if(windows[i].type == WIN_SHUTDOWN){
            // Shutdown dialog buttons
            int bw = 100, bh = 36;
            int by = cmy;
            int bx_shut = (windows[i].w / 2 - bw - 10);
            int bx_reboot = (windows[i].w / 2 + 10);
            int bx_cancel = (windows[i].w / 2 - bw/2);
            if(by >= 60 && by < 60+bh){
              if(cmx >= bx_shut && cmx < bx_shut + bw){
                halt(0); // shutdown
              } else if(cmx >= bx_reboot && cmx < bx_reboot + bw){
                halt(1); // reboot
              }
            }
            if(by >= 105 && by < 105+30){
              if(cmx >= bx_cancel && cmx < bx_cancel + bw){
                close_window(i);
              }
            }
          } else if(windows[i].type == WIN_CALCULATOR){
            // Calculator button grid
            int gx = 10, gy = 60;
            int btn_w = 50, btn_h = 36;
            int col = (cmx - gx) / (btn_w + 4);
            int row = (cmy - gy) / (btn_h + 4);
            if(col >= 0 && col < 4 && row >= 0 && row < 5){
              char *btns[5][4] = {
                {"7","8","9","/"},
                {"4","5","6","*"},
                {"1","2","3","-"},
                {"0",".","=","+"},
                {"C","","",""}
              };
              char *b = btns[row][col];
              if(b[0] >= '0' && b[0] <= '9'){
                if(calc_fresh){ calc_cur = 0; calc_fresh = 0; }
                calc_cur = calc_cur * 10 + (b[0] - '0');
              } else if(b[0] == '+' || b[0] == '-' || b[0] == '*' || b[0] == '/'){
                // Execute pending op
                if(calc_op == 1) calc_acc += calc_cur;
                else if(calc_op == 2) calc_acc -= calc_cur;
                else if(calc_op == 3) calc_acc *= calc_cur;
                else if(calc_op == 4 && calc_cur != 0) calc_acc /= calc_cur;
                else calc_acc = calc_cur;
                calc_cur = 0; calc_fresh = 1;
                if(b[0] == '+') calc_op = 1;
                else if(b[0] == '-') calc_op = 2;
                else if(b[0] == '*') calc_op = 3;
                else if(b[0] == '/') calc_op = 4;
              } else if(b[0] == '='){
                if(calc_op == 1) calc_acc += calc_cur;
                else if(calc_op == 2) calc_acc -= calc_cur;
                else if(calc_op == 3) calc_acc *= calc_cur;
                else if(calc_op == 4 && calc_cur != 0) calc_acc /= calc_cur;
                else calc_acc = calc_cur;
                calc_cur = calc_acc;
                calc_op = 0; calc_fresh = 1;
              } else if(b[0] == 'C'){
                calc_acc = 0; calc_cur = 0; calc_op = 0; calc_fresh = 1;
              }
            }
          } else if(windows[i].type == WIN_SETTINGS){
            // Wallpaper buttons in settings
            if(cmy >= 180 && cmy < 210){
              int bx = 20;
              for(int s = 0; s < 5; s++){
                if(cmx >= bx && cmx < bx + 60){
                  wallpaper_style = s;
                  if(bg_cache){ free(bg_cache); bg_cache = 0; }
                  show_notif("Wallpaper changed");
                }
                bx += 70;
              }
            }
          }
        }
        break;
      }
    }

    // Desktop icon click
    if(clicked_win < 0){
      for(int i = 0; i < num_icons; i++){
        icons[i].selected = 0;
        if(point_in_rect(mouse.x, mouse.y, icons[i].x, icons[i].y,
                         ICON_SIZE, ICON_SIZE + ICON_TEXT_GAP + FONT_H)){
          icons[i].selected = 1;
          // Double-click protection: only open if different icon or enough time passed
          if(i != last_icon_click || tick_counter - last_icon_tick > 30){
            open_app(icons[i].target);
            last_icon_click = i;
            last_icon_tick = tick_counter;
          }
        }
      }
    }
  }

  // Keyboard input
  int key;
  while((key = getkey_async()) > 0){
    // Global shortcuts
    if(key == 17){ // Ctrl+Q - close active window
      if(active_window >= 0 && windows[active_window].active){
        close_window(active_window);
        continue;
      }
    }

    if(active_window >= 0 && windows[active_window].active &&
       !windows[active_window].minimized){
      struct window *w = &windows[active_window];
      switch(w->type){
      case WIN_TERMINAL: terminal_handle_key(w, key); break;
      case WIN_FILEMANAGER: filemanager_handle_key(w, key); break;
      case WIN_TEXTEDITOR:
        if(key == 19){ // Ctrl+S = save
          int fd = open(w->edit_filepath[0] ? w->edit_filepath : w->edit_filename,
                       O_CREATE | O_RDWR);
          if(fd >= 0){ write(fd, w->edit_buf, w->edit_len); close(fd); }
          w->edit_saved = 1;
          show_notif("File saved");
        } else if(key == '\b'){
          if(w->edit_len > 0) w->edit_len--;
        } else if(key == '\n' && w->edit_len < 4090){
          w->edit_buf[w->edit_len++] = '\n';
        } else if(key >= 32 && key < 127 && w->edit_len < 4090){
          w->edit_buf[w->edit_len++] = key;
        }
        w->edit_buf[w->edit_len] = 0;
        break;
      case WIN_SNAKE: snake_handle_key(w, key); break;
      case WIN_DOOM: doom_handle_key(w, key); break;
      case WIN_CALCULATOR:
        if(key >= '0' && key <= '9'){
          if(calc_fresh){ calc_cur = 0; calc_fresh = 0; }
          calc_cur = calc_cur * 10 + (key - '0');
        } else if(key == '\n' || key == '='){
          if(calc_op == 1) calc_acc += calc_cur;
          else if(calc_op == 2) calc_acc -= calc_cur;
          else if(calc_op == 3) calc_acc *= calc_cur;
          else if(calc_op == 4 && calc_cur != 0) calc_acc /= calc_cur;
          else calc_acc = calc_cur;
          calc_cur = calc_acc;
          calc_op = 0; calc_fresh = 1;
        } else if(key == '+'){
          if(calc_op) { /* apply prev op first */ }
          calc_acc = calc_op ? calc_acc : calc_cur;
          calc_op = 1; calc_cur = 0; calc_fresh = 1;
        } else if(key == '-'){
          calc_acc = calc_op ? calc_acc : calc_cur;
          calc_op = 2; calc_cur = 0; calc_fresh = 1;
        } else if(key == 'c' || key == 'C'){
          calc_acc = 0; calc_cur = 0; calc_op = 0; calc_fresh = 1;
        }
        break;
      case WIN_SHUTDOWN:
        if(key == 0x1B) close_window(active_window); // ESC to cancel
        break;
      }
    }
  }
}

// ---- Window management ----
static int
open_window(char *title, int type, int x, int y, int w, int h)
{
  int idx = -1;
  for(int i = 0; i < MAX_WINDOWS; i++){
    if(!windows[i].active){ idx = i; break; }
  }
  if(idx < 0) return -1;

  gui_memset(&windows[idx], 0, sizeof(struct window));
  windows[idx].active = 1;
  windows[idx].visible = 1;
  windows[idx].x = x;
  windows[idx].y = y;
  windows[idx].w = w;
  windows[idx].h = h;
  windows[idx].type = type;
  gui_strcpy(windows[idx].title, title);
  windows[idx].selected_file = -1;

  if(type == WIN_TERMINAL){
    terminal_append(&windows[idx], "xv6 Terminal\n");
    terminal_append(&windows[idx], "Type 'help' for commands.\n\n$ ");
  } else if(type == WIN_FILEMANAGER){
    gui_strcpy(windows[idx].cwd, "/");
    filemanager_refresh(&windows[idx]);
  } else if(type == WIN_TEXTEDITOR){
    gui_strcpy(windows[idx].edit_filename, "untitled.txt");
  }

  active_window = idx;
  return idx;
}

static void close_window(int idx) {
  windows[idx].active = 0;
  if(active_window == idx) active_window = -1;
}

// ---- Rendering ----
static void
render(void)
{
  // Cached background
  int bg_pixels = screen.width * (screen.height - TASKBAR_HEIGHT);
  if(!bg_cache){
    draw_desktop_bg();
    bg_cache = (uint*)malloc(bg_pixels * 4);
    if(bg_cache)
      gui_memcpy(bg_cache, screen.pixels, bg_pixels * 4);
  } else {
    gui_memcpy(screen.pixels, bg_cache, bg_pixels * 4);
  }

  draw_desktop_icons();

  for(int i = 0; i < MAX_WINDOWS; i++){
    if(windows[i].active && !windows[i].minimized)
      draw_window(&windows[i], i);
  }

  draw_taskbar();
  if(ctx_menu.visible) draw_context_menu();
  if(notif.active) draw_notification();
  draw_cursor(&screen, mouse.x, mouse.y);
}

static void
draw_desktop_bg(void)
{
  uint c1, c2;
  switch(wallpaper_style){
  case 0: c1 = RGB(26,26,46); c2 = RGB(22,33,62); break;
  case 1: c1 = RGB(20,20,20); c2 = RGB(40,20,60); break;
  case 2: c1 = RGB(10,30,20); c2 = RGB(20,50,40); break;
  case 3: c1 = RGB(40,20,10); c2 = RGB(60,30,20); break;
  case 4: c1 = RGB(15,15,30); c2 = RGB(45,25,55); break;
  default: c1 = RGB(26,26,46); c2 = RGB(22,33,62); break;
  }
  fb_gradient_v(&screen, 0, 0, screen.width, screen.height - TASKBAR_HEIGHT, c1, c2);

  // Add subtle pattern for some wallpapers
  if(wallpaper_style == 4){
    // Dot pattern
    for(int y = 20; y < screen.height - TASKBAR_HEIGHT; y += 40){
      for(int x = 20; x < screen.width; x += 40){
        fb_fill_circle(&screen, x, y, 1, RGB(60,40,70));
      }
    }
  }
}

static void
draw_desktop_icons(void)
{
  for(int i = 0; i < num_icons; i++){
    int ix = icons[i].x, iy = icons[i].y;
    if(icons[i].selected)
      fb_fill_rect(&screen, ix-4, iy-4, ICON_SIZE+8,
                   ICON_SIZE+ICON_TEXT_GAP+FONT_H+8, RGB(53,132,228));

    switch(icons[i].icon_type){
    case ICON_TERMINAL: draw_icon_terminal(&screen, ix, iy, ICON_SIZE); break;
    case ICON_FOLDER:   draw_icon_folder(&screen, ix, iy, ICON_SIZE); break;
    case ICON_DOCUMENT: draw_icon_document(&screen, ix, iy, ICON_SIZE); break;
    case ICON_ABOUT:    draw_icon_about(&screen, ix, iy, ICON_SIZE); break;
    case ICON_SETTINGS: draw_icon_settings(&screen, ix, iy, ICON_SIZE); break;
    case ICON_GAME: {
      uint colors[] = {RGB(46,194,126), RGB(224,27,36), RGB(53,132,228), RGB(245,194,17)};
      int ci = i - 4;
      if(ci < 0) ci = 0;
      if(ci > 3) ci = 3;
      draw_icon_game(&screen, ix, iy, ICON_SIZE, colors[ci]);
      break;
    }
    }

    int tw = fb_text_width(icons[i].name);
    int tx = ix + (ICON_SIZE - tw) / 2;
    fb_text_nobg(&screen, tx, iy + ICON_SIZE + ICON_TEXT_GAP, icons[i].name, COL_TEXT_WHITE);
  }
}

static void
draw_window(struct window *w, int idx)
{
  int wx = w->x, wy = w->y;
  int ww = w->w, wh = w->h;
  int is_active = (idx == active_window);

  // Shadow
  fb_fill_rect(&screen, wx+4, wy+4, ww, wh+TITLEBAR_HEIGHT, RGB(0,0,0));

  // Border
  uint bord = is_active ? COL_WIN_TITLE_AC : COL_WIN_BORDER;
  fb_fill_rect(&screen, wx-WIN_BORDER, wy-WIN_BORDER,
               ww+2*WIN_BORDER, wh+TITLEBAR_HEIGHT+2*WIN_BORDER, bord);

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

  int cx = wx, cy = wy + TITLEBAR_HEIGHT;
  int cw = ww, ch = wh;

  // Resize handle (bottom-right corner)
  if(!w->maximized && w->type != WIN_SHUTDOWN){
    fb_line(&screen, wx+ww-3, wy+wh+TITLEBAR_HEIGHT-10, wx+ww-3, wy+wh+TITLEBAR_HEIGHT-3, RGB(120,120,120));
    fb_line(&screen, wx+ww-7, wy+wh+TITLEBAR_HEIGHT-6, wx+ww-3, wy+wh+TITLEBAR_HEIGHT-6, RGB(120,120,120));
    fb_line(&screen, wx+ww-11, wy+wh+TITLEBAR_HEIGHT-3, wx+ww-3, wy+wh+TITLEBAR_HEIGHT-3, RGB(100,100,100));
  }

  switch(w->type){
  case WIN_TERMINAL: {
    fb_fill_rect(&screen, cx, cy, cw, ch, COL_TERM_BG);
    int tx = cx+6, ty = cy+4;
    int max_cols = (cw-12)/FONT_W;
    int max_lines = (ch-8)/FONT_H;
    int col = 0;
    int total_lines = 1;
    for(int i = 0; i < w->text_len; i++){
      if(w->text[i] == '\n') { total_lines++; col = 0; }
      else { col++; if(col >= max_cols){ total_lines++; col = 0; } }
    }
    col = 0;
    int skip = total_lines > max_lines ? total_lines - max_lines : 0;
    int cur_line = 0;
    for(int i = 0; i < w->text_len; i++){
      if(w->text[i] == '\n'){ cur_line++; col = 0; continue; }
      col++;
      if(col >= max_cols){ cur_line++; col = 1; }
      if(cur_line >= skip){
        int draw_line = cur_line - skip;
        if(draw_line < max_lines){
          fb_char(&screen, tx+(col-1)*FONT_W, ty+draw_line*FONT_H,
                  w->text[i], COL_TERM_FG, COL_TERM_BG);
        }
      }
    }
    // Cursor blink
    if(is_active && (tick_counter/20)%2==0){
      int cpos_x = tx + w->input_pos * FONT_W;
      int cpos_y = ty + (total_lines > max_lines ? max_lines-1 : total_lines-1) * FONT_H;
      if(cpos_y >= cy && cpos_y + FONT_H <= cy + ch)
        fb_fill_rect(&screen, cpos_x, cpos_y, FONT_W, FONT_H, COL_TERM_CURSOR);
    }
    break;
  }

  case WIN_FILEMANAGER: {
    int sidebar_w = 120;
    fb_fill_rect(&screen, cx, cy, sidebar_w, ch, COL_FM_SIDEBAR);
    fb_text_nobg(&screen, cx+10, cy+10, "Home", COL_TEXT_WHITE);
    fb_text_nobg(&screen, cx+10, cy+30, "Root /", COL_TEXT_LIGHT);
    fb_fill_rect(&screen, cx+sidebar_w, cy, 1, ch, RGB(60,60,60));

    int content_x = cx + sidebar_w + 1;
    int content_w = cw - sidebar_w - 1;
    fb_fill_rect(&screen, content_x, cy, content_w, ch, COL_FM_BG);

    // Toolbar
    fb_fill_rect(&screen, content_x, cy, content_w, 28, RGB(50,50,50));
    fb_text_nobg(&screen, content_x+10, cy+6, w->cwd, COL_TEXT_WHITE);

    // Action buttons
    int btn_x = content_x + content_w - 200;
    fb_fill_rect(&screen, btn_x, cy+2, 60, 22, RGB(46,194,126));
    fb_text_nobg(&screen, btn_x+8, cy+5, "New", COL_TEXT_WHITE);
    fb_fill_rect(&screen, btn_x+66, cy+2, 60, 22, RGB(224,27,36));
    fb_text_nobg(&screen, btn_x+74, cy+5, "Del", COL_TEXT_WHITE);
    fb_fill_rect(&screen, btn_x+132, cy+2, 60, 22, COL_ACCENT);
    fb_text_nobg(&screen, btn_x+136, cy+5, "Open", COL_TEXT_WHITE);

    // Creating file input
    if(w->fm_action == 1){
      fb_fill_rect(&screen, content_x, cy+28, content_w, 24, RGB(60,60,60));
      fb_text_nobg(&screen, content_x+4, cy+32, "Name:", COL_TEXT_WHITE);
      fb_fill_rect(&screen, content_x+50, cy+30, content_w-60, 20, RGB(40,40,40));
      fb_text_nobg(&screen, content_x+54, cy+32, w->fm_input, COL_TEXT_WHITE);
    }

    // File list
    int fy = cy + 28 + (w->fm_action == 1 ? 24 : 0);
    for(int i = 0; i < w->file_count && fy+24 < cy+ch; i++){
      if(i == w->selected_file)
        fb_fill_rect(&screen, content_x, fy, content_w, 24, COL_HIGHLIGHT);

      int namelen = gui_strlen(w->files[i]);
      if(namelen > 0 && w->files[i][namelen-1] == '/'){
        fb_fill_rect(&screen, content_x+8, fy+4, 14, 12, RGB(245,194,17));
        fb_fill_rect(&screen, content_x+8, fy+2, 8, 4, RGB(245,194,17));
      } else {
        fb_fill_rect(&screen, content_x+8, fy+2, 12, 16, RGB(200,200,200));
      }
      fb_text_nobg(&screen, content_x+30, fy+4, w->files[i], COL_TEXT_WHITE);
      fy += 24;
    }
    break;
  }

  case WIN_ABOUT: {
    fb_fill_rect(&screen, cx, cy, cw, ch, RGB(36,36,36));
    fb_fill_circle(&screen, cx+cw/2, cy+50, 30, COL_ACCENT);
    fb_text_nobg(&screen, cx+cw/2-16, cy+42, "xv6", COL_TEXT_WHITE);
    char *t1 = "xv6 Desktop Environment";
    fb_text_nobg(&screen, cx+(cw-fb_text_width(t1))/2, cy+90, t1, COL_TEXT_WHITE);
    char *t2 = "Version 3.0 - Full Desktop";
    fb_text_nobg(&screen, cx+(cw-fb_text_width(t2))/2, cy+112, t2, COL_TEXT_LIGHT);
    char *t3 = "GNOME-inspired desktop for xv6";
    fb_text_nobg(&screen, cx+(cw-fb_text_width(t3))/2, cy+145, t3, COL_TEXT_LIGHT);
    char *t4 = "Snake | Minesweeper | Paint | Doom";
    fb_text_nobg(&screen, cx+(cw-fb_text_width(t4))/2, cy+170, t4, COL_TEXT_LIGHT);
    char *t5 = "Calculator | File Manager | Editor";
    fb_text_nobg(&screen, cx+(cw-fb_text_width(t5))/2, cy+190, t5, COL_TEXT_LIGHT);
    char *t6 = "CS461 Spring 2026";
    fb_text_nobg(&screen, cx+(cw-fb_text_width(t6))/2, cy+230, t6, COL_TEXT_GRAY);
    char *t7 = "Shutdown & Reboot | Wallpapers";
    fb_text_nobg(&screen, cx+(cw-fb_text_width(t7))/2, cy+260, t7, COL_TEXT_GRAY);
    break;
  }

  case WIN_TEXTEDITOR: {
    fb_fill_rect(&screen, cx, cy, cw, ch, RGB(40,44,52));
    // Toolbar
    fb_fill_rect(&screen, cx, cy, cw, 28, RGB(50,54,60));
    fb_text_nobg(&screen, cx+8, cy+6, w->edit_filename, COL_TEXT_LIGHT);
    // Save button
    fb_fill_rect(&screen, cx+cw-70, cy+3, 60, 22, COL_ACCENT);
    fb_text_nobg(&screen, cx+cw-62, cy+6, "Save", COL_TEXT_WHITE);
    if(w->edit_saved){
      fb_text_nobg(&screen, cx+cw-140, cy+6, "Saved!", RGB(46,194,126));
    }
    // Line numbers and text
    int tx = cx+44, ty = cy+32;
    int col = 0;
    int max_cols = (cw-52)/FONT_W;
    int line_num = 1;
    // Draw line number for first line
    char lnbuf[8];
    gui_itoa(line_num, lnbuf);
    fb_text_nobg(&screen, cx+4, ty, lnbuf, RGB(100,100,100));
    for(int i = 0; i < w->edit_len && ty+FONT_H < cy+ch; i++){
      if(w->edit_buf[i] == '\n'){
        ty += FONT_H; col = 0;
        line_num++;
        gui_itoa(line_num, lnbuf);
        fb_text_nobg(&screen, cx+4, ty, lnbuf, RGB(100,100,100));
      } else {
        fb_char(&screen, tx+col*FONT_W, ty, w->edit_buf[i],
                RGB(171,178,191), RGB(40,44,52));
        col++;
        if(col >= max_cols){ ty += FONT_H; col = 0; line_num++; }
      }
    }
    if(is_active && (tick_counter/20)%2==0)
      fb_fill_rect(&screen, tx+col*FONT_W, ty, 2, FONT_H, COL_TERM_CURSOR);

    // Handle save button click
    if(mouse_clicked && point_in_rect(mouse.x, mouse.y, cx+cw-70, cy+3, 60, 22)){
      int fd = open(w->edit_filepath[0] ? w->edit_filepath : w->edit_filename,
                   O_CREATE | O_RDWR);
      if(fd >= 0){ write(fd, w->edit_buf, w->edit_len); close(fd); }
      w->edit_saved = 1;
      show_notif("File saved");
    }
    break;
  }

  case WIN_SNAKE:       snake_draw(w); break;
  case WIN_MINESWEEPER: mine_draw(w); break;
  case WIN_PAINT:       paint_draw(w); break;
  case WIN_DOOM:        doom_draw(w); break;

  case WIN_CALCULATOR: {
    fb_fill_rect(&screen, cx, cy, cw, ch, RGB(40,40,40));

    // Display
    fb_fill_rect(&screen, cx+10, cy+10, cw-20, 40, RGB(30,30,30));
    fb_rect(&screen, cx+10, cy+10, cw-20, 40, RGB(80,80,80));
    char disp[32];
    gui_itoa(calc_fresh ? calc_cur : calc_cur, disp);
    int dw = fb_text_width(disp);
    fb_text_nobg(&screen, cx+cw-20-dw, cy+22, disp, COL_TEXT_WHITE);

    // Buttons
    char *btns[5][4] = {
      {"7","8","9","/"},
      {"4","5","6","*"},
      {"1","2","3","-"},
      {"0",".","=","+"},
      {"C","","",""}
    };
    int gx = cx+10, gy = cy+60;
    int btn_w = 50, btn_h = 36;
    for(int r = 0; r < 5; r++){
      for(int c = 0; c < 4; c++){
        if(btns[r][c][0] == 0) continue;
        int bx = gx + c * (btn_w + 4);
        int by = gy + r * (btn_h + 4);
        uint bc = RGB(60,60,60);
        if(btns[r][c][0] >= '0' && btns[r][c][0] <= '9') bc = RGB(70,70,70);
        else if(btns[r][c][0] == '=') bc = COL_ACCENT;
        else if(btns[r][c][0] == 'C') bc = COL_BTN_CLOSE;
        fb_fill_rect(&screen, bx, by, btn_w, btn_h, bc);
        fb_rect(&screen, bx, by, btn_w, btn_h, RGB(90,90,90));
        fb_text_nobg(&screen, bx + (btn_w - fb_text_width(btns[r][c]))/2,
                     by + (btn_h - FONT_H)/2, btns[r][c], COL_TEXT_WHITE);
      }
    }
    break;
  }

  case WIN_SETTINGS: {
    fb_fill_rect(&screen, cx, cy, cw, ch, RGB(36,36,36));

    // Title
    fb_text_nobg(&screen, cx+20, cy+15, "System Settings", COL_TEXT_WHITE);
    fb_fill_rect(&screen, cx+20, cy+35, cw-40, 1, RGB(60,60,60));

    // System info section
    fb_text_nobg(&screen, cx+20, cy+50, "System Information", COL_ACCENT);

    fb_text_nobg(&screen, cx+20, cy+75, "OS:", COL_TEXT_GRAY);
    fb_text_nobg(&screen, cx+120, cy+75, "xv6 x86_64", COL_TEXT_WHITE);

    fb_text_nobg(&screen, cx+20, cy+95, "Desktop:", COL_TEXT_GRAY);
    fb_text_nobg(&screen, cx+120, cy+95, "xv6-DE v3.0", COL_TEXT_WHITE);

    fb_text_nobg(&screen, cx+20, cy+115, "Display:", COL_TEXT_GRAY);
    fb_text_nobg(&screen, cx+120, cy+115, "1024x768 32bpp", COL_TEXT_WHITE);

    char uptxt[32] = "Uptime:";
    int ut = uptime() / 100;
    char ubuf[16];
    gui_itoa(ut, ubuf);
    fb_text_nobg(&screen, cx+20, cy+135, uptxt, COL_TEXT_GRAY);
    fb_text_nobg(&screen, cx+120, cy+135, ubuf, COL_TEXT_WHITE);
    fb_text_nobg(&screen, cx+120+fb_text_width(ubuf)+4, cy+135, "seconds", COL_TEXT_GRAY);

    // Wallpaper section
    fb_fill_rect(&screen, cx+20, cy+160, cw-40, 1, RGB(60,60,60));
    fb_text_nobg(&screen, cx+20, cy+165, "Wallpaper", COL_ACCENT);

    int bx = cx+20;
    char *wnames[] = {"Dark", "Purple", "Green", "Warm", "Dotted"};
    uint wcolors[] = {RGB(26,26,46), RGB(40,20,60), RGB(10,30,20), RGB(60,30,20), RGB(45,25,55)};
    for(int s = 0; s < 5; s++){
      fb_fill_rect(&screen, bx, cy+185, 56, 20, wcolors[s]);
      if(wallpaper_style == s)
        fb_rect(&screen, bx-1, cy+184, 58, 22, COL_ACCENT);
      fb_text_nobg(&screen, bx+4, cy+210, wnames[s], COL_TEXT_LIGHT);
      bx += 70;
    }

    // Power section
    fb_fill_rect(&screen, cx+20, cy+240, cw-40, 1, RGB(60,60,60));
    fb_text_nobg(&screen, cx+20, cy+250, "Power", COL_ACCENT);

    fb_fill_rect(&screen, cx+20, cy+275, 120, 30, COL_BTN_CLOSE);
    fb_text_nobg(&screen, cx+30, cy+282, "Shutdown", COL_TEXT_WHITE);
    fb_fill_rect(&screen, cx+160, cy+275, 120, 30, RGB(53,132,228));
    fb_text_nobg(&screen, cx+175, cy+282, "Reboot", COL_TEXT_WHITE);

    // Check clicks on power buttons
    if(mouse_clicked){
      if(point_in_rect(mouse.x, mouse.y, cx+20, cy+275, 120, 30))
        halt(0);
      if(point_in_rect(mouse.x, mouse.y, cx+160, cy+275, 120, 30))
        halt(1);
    }
    break;
  }

  case WIN_SHUTDOWN: {
    fb_fill_rect(&screen, cx, cy, cw, ch, RGB(45,45,45));

    // Title
    char *msg = "Power Off This System?";
    fb_text_nobg(&screen, cx+(cw-fb_text_width(msg))/2, cy+15, msg, COL_TEXT_WHITE);

    fb_fill_rect(&screen, cx+20, cy+40, cw-40, 1, RGB(70,70,70));

    // Buttons
    int bw = 100, bh = 36;
    int bx_shut = cw/2 - bw - 10;
    int bx_reboot = cw/2 + 10;

    fb_fill_rect(&screen, cx+bx_shut, cy+60, bw, bh, COL_BTN_CLOSE);
    fb_text_nobg(&screen, cx+bx_shut+12, cy+70, "Shutdown", COL_TEXT_WHITE);

    fb_fill_rect(&screen, cx+bx_reboot, cy+60, bw, bh, RGB(53,132,228));
    fb_text_nobg(&screen, cx+bx_reboot+16, cy+70, "Reboot", COL_TEXT_WHITE);

    int bx_cancel = cw/2 - bw/2;
    fb_fill_rect(&screen, cx+bx_cancel, cy+105, bw, 30, RGB(70,70,70));
    fb_text_nobg(&screen, cx+bx_cancel+20, cy+112, "Cancel", COL_TEXT_WHITE);

    fb_text_nobg(&screen, cx+20, cy+ch-20, "Press ESC to cancel", COL_TEXT_GRAY);
    break;
  }
  }
}

static void
draw_taskbar(void)
{
  int ty = screen.height - TASKBAR_HEIGHT;
  fb_fill_rect(&screen, 0, ty, screen.width, TASKBAR_HEIGHT, COL_TASKBAR);
  fb_fill_rect(&screen, 0, ty, screen.width, 1, RGB(60,60,60));

  // Activities button
  int ah = point_in_rect(mouse.x, mouse.y, 0, ty, 100, TASKBAR_HEIGHT);
  fb_fill_rect(&screen, 2, ty+4, 96, TASKBAR_HEIGHT-8, ah ? COL_TASKBAR_HI : COL_TASKBAR);
  fb_text_nobg(&screen, 12, ty+(TASKBAR_HEIGHT-FONT_H)/2, "Activities", COL_TEXT_WHITE);
  fb_fill_rect(&screen, 102, ty+6, 1, TASKBAR_HEIGHT-12, RGB(80,80,80));

  // Window buttons
  int bx = 110;
  for(int i = 0; i < MAX_WINDOWS; i++){
    if(!windows[i].active) continue;
    int bw = fb_text_width(windows[i].title) + 20;
    if(bw < 80) bw = 80;
    uint bc = COL_TASKBAR;
    if(i == active_window && !windows[i].minimized) bc = RGB(60,60,60);
    else if(point_in_rect(mouse.x, mouse.y, bx, ty+4, bw, TASKBAR_HEIGHT-8))
      bc = COL_TASKBAR_HI;
    fb_fill_rect(&screen, bx, ty+4, bw, TASKBAR_HEIGHT-8, bc);
    if(i == active_window && !windows[i].minimized)
      fb_fill_rect(&screen, bx, ty+TASKBAR_HEIGHT-3, bw, 3, COL_ACCENT);
    fb_text_nobg(&screen, bx+10, ty+(TASKBAR_HEIGHT-FONT_H)/2, windows[i].title, COL_TEXT_WHITE);
    bx += bw + 4;
  }

  // Right side: Power button + Clock
  int power_x = screen.width - 100;
  int power_hover = point_in_rect(mouse.x, mouse.y, power_x, ty, 30, TASKBAR_HEIGHT);
  fb_fill_rect(&screen, power_x, ty+6, 26, TASKBAR_HEIGHT-12,
               power_hover ? COL_BTN_CLOSE : RGB(60,60,60));
  // Power icon (simple circle with line)
  fb_fill_circle(&screen, power_x+13, ty+TASKBAR_HEIGHT/2, 7, power_hover ? COL_BTN_CLOSE : RGB(60,60,60));
  fb_rect(&screen, power_x+6, ty+13, 14, 14, COL_TEXT_WHITE);
  fb_fill_rect(&screen, power_x+12, ty+10, 2, 8, COL_TEXT_WHITE);

  // Clock with seconds
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
  fb_text_nobg(&screen, screen.width-fb_text_width(clock)-8,
               ty+(TASKBAR_HEIGHT-FONT_H)/2, clock, COL_TEXT_WHITE);
}

static void
draw_context_menu(void)
{
  fb_fill_rect(&screen, ctx_menu.x+3, ctx_menu.y+3, ctx_menu.width, ctx_menu.height, RGB(0,0,0));
  fb_fill_rect(&screen, ctx_menu.x, ctx_menu.y, ctx_menu.width, ctx_menu.height, COL_MENU_BG);
  fb_rect(&screen, ctx_menu.x, ctx_menu.y, ctx_menu.width, ctx_menu.height, RGB(80,80,80));
  for(int i = 0; i < ctx_menu.item_count; i++){
    int iy = ctx_menu.y + 2 + i*(FONT_H+8);
    if(point_in_rect(mouse.x, mouse.y, ctx_menu.x, iy, ctx_menu.width, FONT_H+8))
      fb_fill_rect(&screen, ctx_menu.x+2, iy, ctx_menu.width-4, FONT_H+8, COL_MENU_HI);
    fb_text_nobg(&screen, ctx_menu.x+12, iy+4, ctx_menu.items[i], COL_TEXT_WHITE);
  }
}

static void
draw_notification(void)
{
  int nw = fb_text_width(notif.text) + 30;
  int nx = screen.width - nw - 10;
  int ny = 10;
  fb_fill_rect(&screen, nx+2, ny+2, nw, 32, RGB(0,0,0));
  fb_fill_rect(&screen, nx, ny, nw, 32, RGB(50,50,50));
  fb_rect(&screen, nx, ny, nw, 32, COL_ACCENT);
  fb_text_nobg(&screen, nx+15, ny+8, notif.text, COL_TEXT_WHITE);
}

// ===== TERMINAL =====
static void terminal_append(struct window *w, char *text) {
  while(*text && w->text_len < (int)sizeof(w->text)-1)
    w->text[w->text_len++] = *text++;
  w->text[w->text_len] = 0;
}

static void
terminal_handle_key(struct window *w, int key)
{
  if(key == '\n'){
    w->input_line[w->input_pos] = 0;
    terminal_append(w, w->input_line);
    terminal_append(w, "\n");
    if(w->input_pos > 0)
      terminal_exec_cmd(w, w->input_line);
    terminal_append(w, "$ ");
    w->input_pos = 0;
    gui_memset(w->input_line, 0, sizeof(w->input_line));
  } else if(key == '\b'){
    if(w->input_pos > 0){
      w->input_pos--;
      if(w->text_len > 0) w->text_len--;
      w->text[w->text_len] = 0;
    }
  } else if(key >= 32 && key < 127 && w->input_pos < 250){
    w->input_line[w->input_pos++] = key;
    char ch[2] = {key, 0};
    terminal_append(w, ch);
  }
}

static void
terminal_exec_cmd(struct window *w, char *cmd)
{
  if(gui_strcmp(cmd, "clear") == 0){
    w->text_len = 0; w->text[0] = 0; return;
  }
  if(gui_strcmp(cmd, "help") == 0){
    terminal_append(w, "Commands: ls cat mkdir rm echo clear help\n");
    terminal_append(w, "          touch FILE  - create empty file\n");
    terminal_append(w, "          write FILE TEXT - write to file\n");
    terminal_append(w, "          shutdown / reboot - power control\n");
    terminal_append(w, "          uptime - show system uptime\n");
    terminal_append(w, "          date - show time since boot\n");
    return;
  }

  // Parse
  char *argv[10];
  int argc = 0;
  char cmd_copy[256];
  gui_strcpy(cmd_copy, cmd);
  char *p = cmd_copy;
  while(*p && argc < 9){
    while(*p == ' ') p++;
    if(!*p) break;
    argv[argc++] = p;
    while(*p && *p != ' ') p++;
    if(*p) *p++ = 0;
  }
  argv[argc] = 0;
  if(argc == 0) return;

  // Built-in: echo
  if(gui_strcmp(argv[0], "echo") == 0){
    for(int i = 1; i < argc; i++){
      if(i > 1) terminal_append(w, " ");
      terminal_append(w, argv[i]);
    }
    terminal_append(w, "\n");
    return;
  }
  // Built-in: touch
  if(gui_strcmp(argv[0], "touch") == 0 && argc > 1){
    int fd = open(argv[1], O_CREATE | O_RDWR);
    if(fd >= 0){ close(fd); terminal_append(w, "created\n"); }
    else terminal_append(w, "error: cannot create\n");
    return;
  }
  // Built-in: write FILE text...
  if(gui_strcmp(argv[0], "write") == 0 && argc > 2){
    int fd = open(argv[1], O_CREATE | O_RDWR);
    if(fd >= 0){
      for(int i = 2; i < argc; i++){
        if(i > 2) write(fd, " ", 1);
        write(fd, argv[i], gui_strlen(argv[i]));
      }
      write(fd, "\n", 1);
      close(fd);
      terminal_append(w, "written\n");
    } else terminal_append(w, "error: cannot open\n");
    return;
  }
  // Built-in: rm
  if(gui_strcmp(argv[0], "rm") == 0 && argc > 1){
    if(unlink(argv[1]) < 0)
      terminal_append(w, "error: cannot remove\n");
    else
      terminal_append(w, "removed\n");
    return;
  }
  // Built-in: shutdown
  if(gui_strcmp(argv[0], "shutdown") == 0){
    terminal_append(w, "Shutting down...\n");
    halt(0);
    return;
  }
  // Built-in: reboot
  if(gui_strcmp(argv[0], "reboot") == 0){
    terminal_append(w, "Rebooting...\n");
    halt(1);
    return;
  }
  // Built-in: uptime / date
  if(gui_strcmp(argv[0], "uptime") == 0 || gui_strcmp(argv[0], "date") == 0){
    int ut = uptime() / 100;
    int hrs = ut / 3600;
    int mins = (ut / 60) % 60;
    int secs = ut % 60;
    char buf[32];
    gui_itoa(hrs, buf);
    terminal_append(w, buf);
    terminal_append(w, "h ");
    gui_itoa(mins, buf);
    terminal_append(w, buf);
    terminal_append(w, "m ");
    gui_itoa(secs, buf);
    terminal_append(w, buf);
    terminal_append(w, "s\n");
    return;
  }

  // External command via fork/exec
  int pp[2];
  if(pipe(pp) < 0){ terminal_append(w, "error: pipe\n"); return; }
  int pid = fork();
  if(pid < 0){ terminal_append(w, "error: fork\n"); close(pp[0]); close(pp[1]); return; }
  if(pid == 0){
    close(pp[0]);
    close(1); dup(pp[1]);
    close(2); dup(pp[1]);
    close(pp[1]);
    exec(argv[0], argv);
    printf(1, "exec failed: %s\n", argv[0]);
    exit();
  }
  close(pp[1]);
  char buf[512];
  int n;
  while((n = read(pp[0], buf, sizeof(buf)-1)) > 0){
    buf[n] = 0;
    terminal_append(w, buf);
  }
  close(pp[0]);
  wait();
}

// ===== FILE MANAGER =====
static void
filemanager_refresh(struct window *w)
{
  w->file_count = 0;
  w->selected_file = -1;
  w->fm_action = 0;
  int fd = open(w->cwd, O_RDONLY);
  if(fd < 0) return;
  struct { ushort inum; char name[14]; } de;
  while(read(fd, &de, sizeof(de)) == sizeof(de)){
    if(de.inum == 0) continue;
    if(w->file_count >= 32) break;
    gui_strcpy(w->files[w->file_count], de.name);
    char path[128];
    gui_strcpy(path, w->cwd);
    if(path[gui_strlen(path)-1] != '/') gui_strcat(path, "/");
    gui_strcat(path, de.name);
    struct stat st;
    if(stat(path, &st) >= 0 && st.type == 1)
      gui_strcat(w->files[w->file_count], "/");
    w->file_count++;
  }
  close(fd);
}

static void
filemanager_handle_key(struct window *w, int key)
{
  if(w->fm_action == 1){
    if(key == '\n'){
      char path[128];
      gui_strcpy(path, w->cwd);
      if(path[gui_strlen(path)-1] != '/') gui_strcat(path, "/");
      gui_strcat(path, w->fm_input);
      int fd = open(path, O_CREATE | O_RDWR);
      if(fd >= 0){ close(fd); show_notif("File created"); }
      filemanager_refresh(w);
    } else if(key == '\b'){
      if(w->fm_input_pos > 0) w->fm_input[--w->fm_input_pos] = 0;
    } else if(key == 0x1B){
      w->fm_action = 0;
    } else if(key >= 32 && key < 127 && w->fm_input_pos < 60){
      w->fm_input[w->fm_input_pos++] = key;
      w->fm_input[w->fm_input_pos] = 0;
    }
  }
}

static void
filemanager_handle_click(struct window *w, int mx, int my)
{
  int sidebar_w = 120;
  int content_w = w->w - sidebar_w - 1;

  if(mx < sidebar_w){
    gui_strcpy(w->cwd, "/");
    filemanager_refresh(w);
    return;
  }

  // Toolbar buttons
  int btn_x = content_w - 200 + sidebar_w;
  if(my < 28){
    if(mx >= btn_x && mx < btn_x + 60){
      w->fm_action = 1;
      gui_memset(w->fm_input, 0, sizeof(w->fm_input));
      w->fm_input_pos = 0;
      return;
    }
    if(mx >= btn_x+66 && mx < btn_x+126){
      if(w->selected_file >= 0){
        char path[128];
        gui_strcpy(path, w->cwd);
        if(path[gui_strlen(path)-1] != '/') gui_strcat(path, "/");
        char clean[32];
        gui_strcpy(clean, w->files[w->selected_file]);
        int cl = gui_strlen(clean);
        if(cl > 0 && clean[cl-1] == '/') clean[cl-1] = 0;
        gui_strcat(path, clean);
        unlink(path);
        show_notif("File deleted");
        filemanager_refresh(w);
      }
      return;
    }
    if(mx >= btn_x+132 && mx < btn_x+192){
      if(w->selected_file >= 0){
        char *name = w->files[w->selected_file];
        int nl = gui_strlen(name);
        if(nl > 0 && name[nl-1] != '/'){
          char path[128];
          gui_strcpy(path, w->cwd);
          if(path[gui_strlen(path)-1] != '/') gui_strcat(path, "/");
          gui_strcat(path, name);
          int widx = open_window("Text Editor", WIN_TEXTEDITOR, 280, 90, 550, 400);
          if(widx >= 0){
            gui_strcpy(windows[widx].edit_filename, name);
            gui_strcpy(windows[widx].edit_filepath, path);
            int fd = open(path, O_RDONLY);
            if(fd >= 0){
              int n = read(fd, windows[widx].edit_buf, sizeof(windows[widx].edit_buf)-1);
              if(n > 0) windows[widx].edit_len = n;
              windows[widx].edit_buf[windows[widx].edit_len] = 0;
              close(fd);
            }
          }
        }
      }
      return;
    }
    return;
  }

  // File list
  int fy_offset = 28 + (w->fm_action == 1 ? 24 : 0);
  int idx = (my - fy_offset) / 24;
  if(idx >= 0 && idx < w->file_count){
    w->selected_file = idx;
    char *name = w->files[idx];
    int len = gui_strlen(name);
    if(len > 0 && name[len-1] == '/'){
      char newpath[128];
      if(gui_strcmp(name, "../") == 0){
        gui_strcpy(newpath, w->cwd);
        int pl = gui_strlen(newpath);
        if(pl > 1 && newpath[pl-1] == '/') newpath[pl-1] = 0;
        int last = 0;
        for(int i = 0; newpath[i]; i++)
          if(newpath[i] == '/') last = i;
        newpath[last+1] = 0;
        if(newpath[0] == 0) gui_strcpy(newpath, "/");
      } else if(gui_strcmp(name, "./") == 0){
        return;
      } else {
        gui_strcpy(newpath, w->cwd);
        if(newpath[gui_strlen(newpath)-1] != '/') gui_strcat(newpath, "/");
        char clean[32];
        gui_strcpy(clean, name);
        int nl = gui_strlen(clean);
        if(nl > 0 && clean[nl-1] == '/') clean[nl-1] = 0;
        gui_strcat(newpath, clean);
      }
      gui_strcpy(w->cwd, newpath);
      filemanager_refresh(w);
    }
  }
}

// ===== SNAKE GAME =====
static void
snake_init(struct window *w)
{
  struct snake_state *s = &w->game.snake;
  s->grid_w = (w->w - 4) / SNAKE_CELL;
  s->grid_h = (w->h - 28) / SNAKE_CELL;
  s->len = 4;
  s->dir = 1;
  s->score = 0;
  s->game_over = 0;
  s->last_tick = tick_counter;
  for(int i = 0; i < s->len; i++){
    s->sx[i] = s->grid_w/2 - i;
    s->sy[i] = s->grid_h/2;
  }
  s->food_x = gui_rand() % s->grid_w;
  s->food_y = gui_rand() % s->grid_h;
}

static void
snake_update(struct window *w)
{
  struct snake_state *s = &w->game.snake;
  if(s->game_over) return;
  if(tick_counter - s->last_tick < 8) return;
  s->last_tick = tick_counter;

  int nx = s->sx[0], ny = s->sy[0];
  switch(s->dir){
  case 0: ny--; break;
  case 1: nx++; break;
  case 2: ny++; break;
  case 3: nx--; break;
  }

  if(nx < 0 || nx >= s->grid_w || ny < 0 || ny >= s->grid_h){
    s->game_over = 1; return;
  }
  for(int i = 0; i < s->len; i++){
    if(s->sx[i] == nx && s->sy[i] == ny){
      s->game_over = 1; return;
    }
  }

  int ate = (nx == s->food_x && ny == s->food_y);
  if(ate){
    s->score += 10;
    if(s->len < SNAKE_MAX_LEN) s->len++;
    s->food_x = gui_rand() % s->grid_w;
    s->food_y = gui_rand() % s->grid_h;
  }

  for(int i = s->len-1; i > 0; i--){
    s->sx[i] = s->sx[i-1];
    s->sy[i] = s->sy[i-1];
  }
  s->sx[0] = nx;
  s->sy[0] = ny;
}

static void
snake_handle_key(struct window *w, int key)
{
  struct snake_state *s = &w->game.snake;
  if(s->game_over){
    if(key == 'r') snake_init(w);
    return;
  }
  if((key == 'w' || key == 0xE2) && s->dir != 2) s->dir = 0;
  if((key == 'd' || key == 0xE5) && s->dir != 3) s->dir = 1;
  if((key == 's' || key == 0xE3) && s->dir != 0) s->dir = 2;
  if((key == 'a' || key == 0xE4) && s->dir != 1) s->dir = 3;
}

static void
snake_draw(struct window *w)
{
  struct snake_state *s = &w->game.snake;
  int cx = w->x, cy = w->y + TITLEBAR_HEIGHT;

  fb_fill_rect(&screen, cx, cy, w->w, w->h, RGB(20,20,30));

  char score_str[32] = "Score: ";
  char num[16]; gui_itoa(s->score, num);
  gui_strcat(score_str, num);
  fb_text_nobg(&screen, cx+8, cy+4, score_str, COL_TEXT_WHITE);

  int ox = cx + 2, oy = cy + 24;

  fb_rect(&screen, ox-1, oy-1, s->grid_w*SNAKE_CELL+2, s->grid_h*SNAKE_CELL+2, RGB(60,60,60));

  fb_fill_rect(&screen, ox+s->food_x*SNAKE_CELL+2, oy+s->food_y*SNAKE_CELL+2,
               SNAKE_CELL-4, SNAKE_CELL-4, RGB(224,27,36));

  for(int i = 0; i < s->len; i++){
    uint color = i == 0 ? RGB(46,194,126) : RGB(36,160,100);
    fb_fill_rect(&screen, ox+s->sx[i]*SNAKE_CELL+1, oy+s->sy[i]*SNAKE_CELL+1,
                 SNAKE_CELL-2, SNAKE_CELL-2, color);
  }

  if(s->game_over){
    fb_fill_rect(&screen, cx+w->w/2-80, cy+w->h/2-20, 160, 40, RGB(0,0,0));
    fb_text_nobg(&screen, cx+w->w/2-72, cy+w->h/2-8, "GAME OVER (R)", COL_BTN_CLOSE);
  }
}

// ===== MINESWEEPER =====
static void
mine_init(struct window *w)
{
  struct mine_state *m = &w->game.mine;
  gui_memset(m, 0, sizeof(*m));
  m->revealed = 0;
  int placed = 0;
  while(placed < MINE_COUNT){
    int r = gui_rand() % MINE_ROWS;
    int c = gui_rand() % MINE_COLS;
    if(!m->mines[r][c]){
      m->mines[r][c] = 1;
      placed++;
    }
  }
  for(int r = 0; r < MINE_ROWS; r++){
    for(int c = 0; c < MINE_COLS; c++){
      if(m->mines[r][c]) continue;
      int cnt = 0;
      for(int dr = -1; dr <= 1; dr++)
        for(int dc = -1; dc <= 1; dc++){
          int nr = r+dr, nc = c+dc;
          if(nr >= 0 && nr < MINE_ROWS && nc >= 0 && nc < MINE_COLS)
            cnt += m->mines[nr][nc];
        }
      m->counts[r][c] = cnt;
    }
  }
}

static void mine_reveal(struct mine_state *m, int r, int c);
static void
mine_reveal(struct mine_state *m, int r, int c)
{
  if(r < 0 || r >= MINE_ROWS || c < 0 || c >= MINE_COLS) return;
  if(m->grid[r][c] != 0) return;
  m->grid[r][c] = 1;
  m->revealed++;
  if(m->counts[r][c] == 0){
    for(int dr = -1; dr <= 1; dr++)
      for(int dc = -1; dc <= 1; dc++)
        mine_reveal(m, r+dr, c+dc);
  }
}

static void
mine_handle_click(struct window *w, int mx, int my, int right)
{
  struct mine_state *m = &w->game.mine;
  if(m->game_over) {
    if(!right){ mine_init(w); } return;
  }
  int c = mx / MINE_CELL;
  int r = (my - 24) / MINE_CELL;
  if(r < 0 || r >= MINE_ROWS || c < 0 || c >= MINE_COLS) return;

  if(right){
    if(m->grid[r][c] == 0) m->grid[r][c] = 2;
    else if(m->grid[r][c] == 2) m->grid[r][c] = 0;
  } else {
    if(m->grid[r][c] == 2) return;
    if(m->mines[r][c]){
      m->game_over = 1;
      for(int i = 0; i < MINE_ROWS; i++)
        for(int j = 0; j < MINE_COLS; j++)
          m->grid[i][j] = 1;
    } else {
      mine_reveal(m, r, c);
      if(m->revealed >= MINE_ROWS * MINE_COLS - MINE_COUNT){
        m->won = 1;
        m->game_over = 1;
      }
    }
  }
}

static void
mine_draw(struct window *w)
{
  struct mine_state *m = &w->game.mine;
  int cx = w->x, cy = w->y + TITLEBAR_HEIGHT;
  fb_fill_rect(&screen, cx, cy, w->w, w->h, RGB(60,60,60));

  char *status = m->game_over ? (m->won ? "YOU WIN! Click to restart" : "BOOM! Click to restart") : "Left=reveal Right=flag";
  fb_text_nobg(&screen, cx+4, cy+4, status, m->game_over ? COL_BTN_CLOSE : COL_TEXT_WHITE);

  int ox = cx, oy = cy + 24;
  uint num_colors[] = {0, RGB(0,0,255), RGB(0,128,0), RGB(255,0,0),
                       RGB(0,0,128), RGB(128,0,0), RGB(0,128,128),
                       RGB(0,0,0), RGB(128,128,128)};

  for(int r = 0; r < MINE_ROWS; r++){
    for(int c = 0; c < MINE_COLS; c++){
      int x = ox + c * MINE_CELL;
      int y = oy + r * MINE_CELL;

      if(m->grid[r][c] == 0){
        fb_fill_rect(&screen, x, y, MINE_CELL, MINE_CELL, RGB(180,180,180));
        fb_fill_rect(&screen, x, y, MINE_CELL, 2, RGB(220,220,220));
        fb_fill_rect(&screen, x, y, 2, MINE_CELL, RGB(220,220,220));
        fb_fill_rect(&screen, x, y+MINE_CELL-2, MINE_CELL, 2, RGB(100,100,100));
        fb_fill_rect(&screen, x+MINE_CELL-2, y, 2, MINE_CELL, RGB(100,100,100));
      } else if(m->grid[r][c] == 2){
        fb_fill_rect(&screen, x, y, MINE_CELL, MINE_CELL, RGB(180,180,180));
        fb_fill_rect(&screen, x+8, y+4, 4, 12, RGB(60,60,60));
        fb_fill_rect(&screen, x+8, y+4, 10, 6, RGB(224,27,36));
      } else {
        fb_fill_rect(&screen, x, y, MINE_CELL, MINE_CELL, RGB(200,200,200));
        fb_rect(&screen, x, y, MINE_CELL, MINE_CELL, RGB(160,160,160));
        if(m->mines[r][c]){
          fb_fill_circle(&screen, x+MINE_CELL/2, y+MINE_CELL/2, 6, RGB(0,0,0));
        } else if(m->counts[r][c] > 0){
          char ch = '0' + m->counts[r][c];
          fb_char(&screen, x+8, y+4, ch, num_colors[m->counts[r][c]], RGB(200,200,200));
        }
      }
    }
  }
}

// ===== PAINT =====
static void
paint_init(struct window *w)
{
  struct paint_state *p = &w->game.paint;
  p->cur_color = RGB(0,0,0);
  p->brush_size = 3;
  p->last_px = -1;
  p->last_py = -1;
  p->painting = 0;
  for(int i = 0; i < PAINT_W * PAINT_H; i++)
    p->canvas[i] = RGB(255,255,255);
}

static void
paint_canvas_pixel(struct paint_state *p, int x, int y, int size)
{
  for(int dy = -size; dy <= size; dy++)
    for(int dx = -size; dx <= size; dx++)
      if(dx*dx + dy*dy <= size*size){
        int px = x+dx, py = y+dy;
        if(px >= 0 && px < PAINT_W && py >= 0 && py < PAINT_H)
          p->canvas[py * PAINT_W + px] = p->cur_color;
      }
}

static void
paint_handle_mouse(struct window *w, int mx, int my, int pressed)
{
  struct paint_state *p = &w->game.paint;
  int scale_x = (w->w - 60);
  int scale_y = (w->h - 24);
  int cx_coord = mx * PAINT_W / scale_x;
  int cy_coord = my * PAINT_H / scale_y;

  if(pressed){
    if(cx_coord >= 0 && cx_coord < PAINT_W && cy_coord >= 0 && cy_coord < PAINT_H){
      paint_canvas_pixel(p, cx_coord, cy_coord, p->brush_size);
      if(p->last_px >= 0){
        int dx = cx_coord - p->last_px;
        int dy = cy_coord - p->last_py;
        int steps = fp_abs(dx) > fp_abs(dy) ? fp_abs(dx) : fp_abs(dy);
        if(steps > 0){
          for(int i = 1; i < steps; i++){
            int lx = p->last_px + dx * i / steps;
            int ly = p->last_py + dy * i / steps;
            paint_canvas_pixel(p, lx, ly, p->brush_size);
          }
        }
      }
      p->last_px = cx_coord;
      p->last_py = cy_coord;
    }
    p->painting = 1;
  } else {
    p->painting = 0;
    p->last_px = -1;
    p->last_py = -1;
  }
}

static void
paint_handle_click(struct window *w, int mx, int my)
{
  struct paint_state *p = &w->game.paint;
  int palette_x = w->w - 56;
  if(mx >= palette_x){
    uint colors[] = {
      RGB(0,0,0), RGB(255,255,255), RGB(224,27,36), RGB(46,194,126),
      RGB(53,132,228), RGB(245,194,17), RGB(128,0,128), RGB(255,128,0),
      RGB(128,128,128), RGB(0,128,128)
    };
    int idx = (my - 24) / 22;
    if(idx >= 0 && idx < 10)
      p->cur_color = colors[idx];
    if(idx == 10) p->brush_size = 1;
    if(idx == 11) p->brush_size = 3;
    if(idx == 12) p->brush_size = 6;
    if(idx == 13){
      for(int i = 0; i < PAINT_W * PAINT_H; i++)
        p->canvas[i] = RGB(255,255,255);
    }
  }
}

static void
paint_draw(struct window *w)
{
  struct paint_state *p = &w->game.paint;
  int cx = w->x, cy = w->y + TITLEBAR_HEIGHT;
  fb_fill_rect(&screen, cx, cy, w->w, w->h, RGB(80,80,80));

  int canvas_w = w->w - 60;
  int canvas_h = w->h - 24;

  // Toolbar
  fb_fill_rect(&screen, cx, cy, canvas_w, 20, RGB(60,60,60));
  fb_text_nobg(&screen, cx+4, cy+2, "Paint", COL_TEXT_WHITE);

  // Draw canvas (scaled)
  for(int y = 0; y < PAINT_H; y++){
    int screen_y = cy + 20 + y * canvas_h / PAINT_H;
    if(screen_y >= cy + w->h) break;
    for(int x = 0; x < PAINT_W; x++){
      int screen_x = cx + x * canvas_w / PAINT_W;
      uint color = p->canvas[y * PAINT_W + x];
      int pw = canvas_w / PAINT_W;
      int ph = canvas_h / PAINT_H;
      if(pw < 1) pw = 1;
      if(ph < 1) ph = 1;
      fb_fill_rect(&screen, screen_x, screen_y, pw, ph, color);
    }
  }

  // Color palette (right side)
  int px = cx + canvas_w + 4;
  uint colors[] = {
    RGB(0,0,0), RGB(255,255,255), RGB(224,27,36), RGB(46,194,126),
    RGB(53,132,228), RGB(245,194,17), RGB(128,0,128), RGB(255,128,0),
    RGB(128,128,128), RGB(0,128,128)
  };
  for(int i = 0; i < 10; i++){
    int py = cy + 24 + i * 22;
    fb_fill_rect(&screen, px, py, 48, 18, colors[i]);
    if(colors[i] == p->cur_color)
      fb_rect(&screen, px-1, py-1, 50, 20, COL_TEXT_WHITE);
  }

  int by = cy + 24 + 10 * 22 + 4;
  char *sizes[] = {"S", "M", "L"};
  int bvals[] = {1, 3, 6};
  for(int i = 0; i < 3; i++){
    fb_fill_rect(&screen, px, by + i*22, 48, 18,
                 p->brush_size == bvals[i] ? COL_ACCENT : RGB(60,60,60));
    fb_text_nobg(&screen, px+20, by + i*22 + 1, sizes[i], COL_TEXT_WHITE);
  }

  fb_fill_rect(&screen, px, by + 3*22, 48, 18, COL_BTN_CLOSE);
  fb_text_nobg(&screen, px+4, by + 3*22 + 1, "Clear", COL_TEXT_WHITE);
}

// ===== DOOM 3D RAYCASTER - FULL GAME =====

// Distance between two fixed-point positions
static int doom_dist(int x1, int y1, int x2, int y2) {
  int dx = (x1 - x2) / 16; // scale down to avoid overflow
  int dy = (y1 - y2) / 16;
  int d2 = dx*dx + dy*dy;
  // Integer sqrt approximation
  if(d2 <= 0) return 0;
  int r = d2;
  int x = d2;
  for(int i = 0; i < 8; i++){
    if(x <= 0) break;
    x = (x + d2 / x) / 2;
  }
  return x * 16; // scale back
}

// Try to move player, returns 1 if moved
static int doom_try_move(struct doom_state *d, int nx, int ny) {
  int mx = nx >> FP_SHIFT;
  int my = ny >> FP_SHIFT;
  // Keep small margin from walls
  int margin = FP_ONE / 5;
  int mx2 = (nx + margin) >> FP_SHIFT;
  int mx3 = (nx - margin) >> FP_SHIFT;
  int my2 = (ny + margin) >> FP_SHIFT;
  int my3 = (ny - margin) >> FP_SHIFT;

  if(mx < 0 || mx >= DOOM_MAP_W || my < 0 || my >= DOOM_MAP_H) return 0;

  // Check all 4 corners of bounding box
  int blocked = 0;
  if(d->map[my2][mx2] >= 1 && d->map[my2][mx2] != 5) blocked = 1;
  if(d->map[my2][mx3] >= 1 && d->map[my2][mx3] != 5) blocked = 1;
  if(d->map[my3][mx2] >= 1 && d->map[my3][mx2] != 5) blocked = 1;
  if(d->map[my3][mx3] >= 1 && d->map[my3][mx3] != 5) blocked = 1;

  if(!blocked){
    d->pos_x = nx;
    d->pos_y = ny;
    return 1;
  }

  // Wall sliding: try X only, then Y only
  int ox = d->pos_x, oy = d->pos_y;
  int slide = 0;

  // Try X only
  mx2 = (nx + margin) >> FP_SHIFT;
  mx3 = (nx - margin) >> FP_SHIFT;
  my2 = (oy + margin) >> FP_SHIFT;
  my3 = (oy - margin) >> FP_SHIFT;
  int xblocked = 0;
  if(mx2 >= 0 && mx2 < DOOM_MAP_W && mx3 >= 0 && mx3 < DOOM_MAP_W){
    if((d->map[my2][mx2] >= 1 && d->map[my2][mx2] != 5) ||
       (d->map[my2][mx3] >= 1 && d->map[my2][mx3] != 5) ||
       (d->map[my3][mx2] >= 1 && d->map[my3][mx2] != 5) ||
       (d->map[my3][mx3] >= 1 && d->map[my3][mx3] != 5))
      xblocked = 1;
  } else xblocked = 1;
  if(!xblocked){ d->pos_x = nx; slide = 1; }

  // Try Y only
  mx2 = (ox + margin) >> FP_SHIFT;
  mx3 = (ox - margin) >> FP_SHIFT;
  my2 = (ny + margin) >> FP_SHIFT;
  my3 = (ny - margin) >> FP_SHIFT;
  int yblocked = 0;
  if(my2 >= 0 && my2 < DOOM_MAP_H && my3 >= 0 && my3 < DOOM_MAP_H){
    if((d->map[my2][mx2] >= 1 && d->map[my2][mx2] != 5) ||
       (d->map[my2][mx3] >= 1 && d->map[my2][mx3] != 5) ||
       (d->map[my3][mx2] >= 1 && d->map[my3][mx2] != 5) ||
       (d->map[my3][mx3] >= 1 && d->map[my3][mx3] != 5))
      yblocked = 1;
  } else yblocked = 1;
  if(!yblocked){ d->pos_y = ny; slide = 1; }

  return slide;
}

// Place enemies and pickups for a level
static void doom_place_entities(struct doom_state *d) {
  d->num_enemies = 0;
  d->num_pickups = 0;

  // Scan map for entity markers (we place them programmatically per level)
  if(d->level == 0){
    // Level 1: 5 imps
    int ex[] = {10,6,18,14,20};
    int ey[] = {3,8,8,14,18};
    for(int i = 0; i < 5 && d->num_enemies < DOOM_MAX_ENEMIES; i++){
      struct doom_enemy *e = &d->enemies[d->num_enemies++];
      e->alive = 1;
      e->x = ex[i] * FP_ONE + FP_ONE/2;
      e->y = ey[i] * FP_ONE + FP_ONE/2;
      e->hp = 2;
      e->type = 0;
      e->last_atk = 0;
      e->hurt_timer = 0;
    }
    // Pickups: health, ammo
    int px[] = {5,12,18,8};
    int py[] = {5,10,3,15};
    int pt[] = {1,0,1,0}; // 0=health 1=ammo
    for(int i = 0; i < 4 && d->num_pickups < DOOM_MAX_PICKUPS; i++){
      struct doom_pickup *p = &d->pickups[d->num_pickups++];
      p->active = 1;
      p->x = px[i] * FP_ONE + FP_ONE/2;
      p->y = py[i] * FP_ONE + FP_ONE/2;
      p->type = pt[i];
    }
  } else if(d->level == 1){
    // Level 2: 7 enemies (mix of imps and demons)
    int ex[] = {4,10,16,20,6,14,11};
    int ey[] = {4,6,4,10,16,16,20};
    int et[] = {0,0,1,0,1,0,1};
    for(int i = 0; i < 7 && d->num_enemies < DOOM_MAX_ENEMIES; i++){
      struct doom_enemy *e = &d->enemies[d->num_enemies++];
      e->alive = 1;
      e->x = ex[i] * FP_ONE + FP_ONE/2;
      e->y = ey[i] * FP_ONE + FP_ONE/2;
      e->hp = et[i] == 1 ? 4 : 2;
      e->type = et[i];
      e->last_atk = 0;
      e->hurt_timer = 0;
    }
    int px[] = {3,8,18,12,6};
    int py[] = {12,3,18,12,20};
    int pt[] = {0,1,0,1,2}; // 2=armor
    for(int i = 0; i < 5 && d->num_pickups < DOOM_MAX_PICKUPS; i++){
      struct doom_pickup *p = &d->pickups[d->num_pickups++];
      p->active = 1;
      p->x = px[i] * FP_ONE + FP_ONE/2;
      p->y = py[i] * FP_ONE + FP_ONE/2;
      p->type = pt[i];
    }
    // Key pickup (needed for exit)
    struct doom_pickup *kp = &d->pickups[d->num_pickups++];
    kp->active = 1;
    kp->x = 20 * FP_ONE + FP_ONE/2;
    kp->y = 20 * FP_ONE + FP_ONE/2;
    kp->type = 3; // key
  } else if(d->level == 2){
    // Level 3: boss + 4 imps
    int ex[] = {12,4,20,4,20};
    int ey[] = {12,4,4,18,18};
    int et[] = {2,0,0,0,0}; // type 2 = boss
    for(int i = 0; i < 5 && d->num_enemies < DOOM_MAX_ENEMIES; i++){
      struct doom_enemy *e = &d->enemies[d->num_enemies++];
      e->alive = 1;
      e->x = ex[i] * FP_ONE + FP_ONE/2;
      e->y = ey[i] * FP_ONE + FP_ONE/2;
      e->hp = et[i] == 2 ? 15 : 2;
      e->type = et[i];
      e->last_atk = 0;
      e->hurt_timer = 0;
    }
    int px[] = {2,22,2,22,12,12};
    int py[] = {2,2,22,22,6,18};
    int pt[] = {0,0,1,1,0,1};
    for(int i = 0; i < 6 && d->num_pickups < DOOM_MAX_PICKUPS; i++){
      struct doom_pickup *p = &d->pickups[d->num_pickups++];
      p->active = 1;
      p->x = px[i] * FP_ONE + FP_ONE/2;
      p->y = py[i] * FP_ONE + FP_ONE/2;
      p->type = pt[i];
    }
  }
  d->total_enemies = d->num_enemies;
}

static void doom_load_level(struct doom_state *d, int level);

static void
doom_init(struct window *w)
{
  struct doom_state *d = &w->game.doom;
  gui_memset(d, 0, sizeof(*d));
  d->health = 100;
  d->armor = 0;
  d->ammo = 24;
  d->max_ammo = 99;
  d->weapon = 1; // pistol
  d->level = 0;
  d->score = 0;
  d->kills = 0;
  doom_load_level(d, 0);
}

static void
doom_load_level(struct doom_state *d, int level)
{
  d->level = level;
  d->level_complete = 0;
  d->has_key = 0;
  d->shoot_timer = 0;
  d->game_tick = 0;

  gui_memset(d->map, 0, sizeof(d->map));

  // Build walls around border
  for(int i = 0; i < DOOM_MAP_W; i++){
    d->map[0][i] = 1;
    d->map[DOOM_MAP_H-1][i] = 1;
  }
  for(int i = 0; i < DOOM_MAP_H; i++){
    d->map[i][0] = 1;
    d->map[i][DOOM_MAP_W-1] = 1;
  }

  if(level == 0){
    // Level 1: Training facility
    d->pos_x = 2 * FP_ONE + FP_ONE/2;
    d->pos_y = 2 * FP_ONE + FP_ONE/2;
    d->angle = 0;
    d->exit_x = 22; d->exit_y = 22;

    // Interior walls - corridors
    for(int i = 4; i < 10; i++) d->map[4][i] = 2;
    for(int i = 4; i < 8; i++) d->map[i][9] = 2;
    d->map[4][7] = 0; // doorway

    for(int i = 12; i < 20; i++) d->map[10][i] = 3;
    d->map[10][15] = 0; // doorway

    for(int i = 10; i < 16; i++) d->map[i][12] = 2;
    d->map[13][12] = 0; // doorway

    for(int i = 2; i < 8; i++) d->map[16][i] = 4;
    d->map[16][5] = 0; // doorway

    for(int i = 16; i < 22; i++) d->map[i][8] = 3;
    d->map[19][8] = 0; // doorway

    // Some pillars
    d->map[7][15] = 4;
    d->map[7][19] = 4;
    d->map[14][19] = 2;

    // Exit marker (type 5 = exit door, passable)
    d->map[d->exit_y][d->exit_x] = 5;

  } else if(level == 1){
    // Level 2: Dungeon - needs key
    d->pos_x = 2 * FP_ONE + FP_ONE/2;
    d->pos_y = 2 * FP_ONE + FP_ONE/2;
    d->angle = 45;
    d->exit_x = 22; d->exit_y = 12;

    // Cross-shaped corridors
    for(int i = 1; i < DOOM_MAP_W-1; i++){
      d->map[8][i] = 3;
      d->map[16][i] = 3;
    }
    for(int i = 1; i < DOOM_MAP_H-1; i++){
      d->map[i][8] = 2;
      d->map[i][16] = 2;
    }
    // Doorways through corridors
    d->map[8][4] = 0; d->map[8][12] = 0; d->map[8][20] = 0;
    d->map[16][4] = 0; d->map[16][12] = 0; d->map[16][20] = 0;
    d->map[4][8] = 0; d->map[12][8] = 0; d->map[20][8] = 0;
    d->map[4][16] = 0; d->map[12][16] = 0; d->map[20][16] = 0;

    // Pillars in rooms
    d->map[3][3] = 4; d->map[5][5] = 4;
    d->map[3][13] = 4; d->map[5][19] = 4;
    d->map[19][3] = 4; d->map[20][13] = 4;
    d->map[19][19] = 4;

    // Exit (locked - need key)
    d->map[d->exit_y][d->exit_x] = 5;

  } else if(level == 2){
    // Level 3: Boss arena
    d->pos_x = 2 * FP_ONE + FP_ONE/2;
    d->pos_y = 12 * FP_ONE + FP_ONE/2;
    d->angle = 0;
    d->exit_x = 12; d->exit_y = 12; // center, unlocks when boss dies

    // Arena walls forming an octagonal chamber
    for(int i = 6; i < 18; i++){
      d->map[3][i] = 2;
      d->map[21][i] = 2;
      d->map[i][3] = 3;
      d->map[i][21] = 3;
    }
    // Corner pillars
    d->map[5][5] = 4; d->map[5][19] = 4;
    d->map[19][5] = 4; d->map[19][19] = 4;
    // Inner pillars (cover)
    d->map[8][8] = 4; d->map[8][16] = 4;
    d->map[16][8] = 4; d->map[16][16] = 4;
    d->map[12][6] = 4; d->map[12][18] = 4;

    // Doorways
    d->map[3][12] = 0;
    d->map[21][12] = 0;
    d->map[12][3] = 0;
    d->map[12][21] = 0;

    // Exit blocked until boss dies (type 6 = locked exit)
    d->map[d->exit_y][d->exit_x] = 6;
  }

  doom_place_entities(d);
}

static void
doom_handle_key(struct window *w, int key)
{
  struct doom_state *d = &w->game.doom;

  if(d->dead){
    if(key == 'r'){
      // Restart game
      doom_init(w);
    }
    return;
  }

  if(d->level_complete){
    if(key == '\n' || key == ' '){
      if(d->level + 1 < DOOM_NUM_LEVELS){
        doom_load_level(d, d->level + 1);
      } else {
        // Game won - restart
        doom_init(w);
      }
    }
    return;
  }

  int move_speed = FP_ONE / 3;
  int strafe_speed = FP_ONE / 4;
  int rot_speed = 8;

  int fwd_x = cos_tab[d->angle];
  int fwd_y = sin_tab[d->angle];
  int right_x = cos_tab[(d->angle + 90) % 360];
  int right_y = sin_tab[(d->angle + 90) % 360];

  int nx, ny;

  // W = forward
  if(key == 'w' || key == 0xE2){
    nx = d->pos_x + fwd_x * move_speed / FP_ONE;
    ny = d->pos_y + fwd_y * move_speed / FP_ONE;
    doom_try_move(d, nx, ny);
  }
  // S = backward
  if(key == 's' || key == 0xE3){
    nx = d->pos_x - fwd_x * move_speed / FP_ONE;
    ny = d->pos_y - fwd_y * move_speed / FP_ONE;
    doom_try_move(d, nx, ny);
  }
  // A = turn left (or strafe with shift? just turn for simplicity)
  if(key == 'a'){
    d->angle = (d->angle - rot_speed + 360) % 360;
  }
  // D = turn right
  if(key == 'd'){
    d->angle = (d->angle + rot_speed) % 360;
  }
  // Arrow keys: left/right = turn, up/down = move
  if(key == 0xE4){ // left arrow
    d->angle = (d->angle - rot_speed + 360) % 360;
  }
  if(key == 0xE5){ // right arrow
    d->angle = (d->angle + rot_speed) % 360;
  }
  if(key == 0xE2){ // up arrow (already handled with W above)
  }
  if(key == 0xE3){ // down arrow (already handled with S above)
  }

  // Q = strafe left
  if(key == 'q'){
    nx = d->pos_x - right_x * strafe_speed / FP_ONE;
    ny = d->pos_y - right_y * strafe_speed / FP_ONE;
    doom_try_move(d, nx, ny);
  }
  // E = strafe right
  if(key == 'e'){
    nx = d->pos_x + right_x * strafe_speed / FP_ONE;
    ny = d->pos_y + right_y * strafe_speed / FP_ONE;
    doom_try_move(d, nx, ny);
  }

  // Space = shoot
  if(key == ' ' && d->shoot_timer <= 0){
    if(d->weapon == 0){
      // Fist - melee range
      d->shoot_timer = 8;
      for(int i = 0; i < d->num_enemies; i++){
        if(!d->enemies[i].alive) continue;
        int edist = doom_dist(d->pos_x, d->pos_y, d->enemies[i].x, d->enemies[i].y);
        if(edist < FP_ONE * 2){
          d->enemies[i].hp -= 2;
          d->enemies[i].hurt_timer = 6;
          if(d->enemies[i].hp <= 0){
            d->enemies[i].alive = 0;
            d->kills++;
            d->score += d->enemies[i].type == 2 ? 500 : (d->enemies[i].type == 1 ? 200 : 100);
          }
        }
      }
    } else if(d->ammo > 0){
      // Hitscan weapon
      d->ammo--;
      int dmg = d->weapon == 2 ? 4 : 2;
      d->shoot_timer = d->weapon == 2 ? 12 : 6;

      // Find closest enemy in crosshair (within ~15 degree cone)
      int best = -1;
      int best_dist = 999999;
      for(int i = 0; i < d->num_enemies; i++){
        if(!d->enemies[i].alive) continue;
        int edx = d->enemies[i].x - d->pos_x;
        int edy = d->enemies[i].y - d->pos_y;
        // Angle to enemy
        // Use atan2 approximation: check if enemy is in front and within cone
        int edist = doom_dist(d->pos_x, d->pos_y, d->enemies[i].x, d->enemies[i].y);
        if(edist < FP_ONE) continue; // too close to compute angle

        // Dot product with view direction to check if in front
        int dot = (edx * cos_tab[d->angle] + edy * sin_tab[d->angle]) / FP_ONE;
        if(dot <= 0) continue; // behind us

        // Cross product magnitude for angle deviation
        int cross = fp_abs(edx * sin_tab[d->angle] - edy * cos_tab[d->angle]) / FP_ONE;
        // Check if within ~15 degree cone (cross/dot < tan(15) ~ 0.27 ~ 69/256)
        if(cross * 256 < dot * 80){
          // Raycast to check line of sight
          int clear = 1;
          int steps = edist / (FP_ONE/4);
          if(steps > 64) steps = 64;
          for(int s = 1; s < steps; s++){
            int cx = d->pos_x + (edx * s / steps);
            int cy = d->pos_y + (edy * s / steps);
            int cmx = cx >> FP_SHIFT;
            int cmy = cy >> FP_SHIFT;
            if(cmx >= 0 && cmx < DOOM_MAP_W && cmy >= 0 && cmy < DOOM_MAP_H){
              if(d->map[cmy][cmx] >= 1 && d->map[cmy][cmx] <= 4){
                clear = 0; break;
              }
            }
          }
          if(clear && edist < best_dist){
            best = i;
            best_dist = edist;
          }
        }
      }
      if(best >= 0){
        // Shotgun spread: damage falls with distance
        if(d->weapon == 2 && best_dist > FP_ONE * 8)
          dmg = 2; // reduced at range
        d->enemies[best].hp -= dmg;
        d->enemies[best].hurt_timer = 6;
        if(d->enemies[best].hp <= 0){
          d->enemies[best].alive = 0;
          d->kills++;
          d->score += d->enemies[best].type == 2 ? 500 : (d->enemies[best].type == 1 ? 200 : 100);

          // Boss kill on level 3 opens exit
          if(d->enemies[best].type == 2 && d->level == 2){
            d->map[d->exit_y][d->exit_x] = 5; // unlock exit
          }
        }
      }
    }
  }

  // Number keys switch weapons
  if(key == '1') d->weapon = 0; // fist
  if(key == '2') d->weapon = 1; // pistol
  if(key == '3') d->weapon = 2; // shotgun

  // Check pickups
  for(int i = 0; i < d->num_pickups; i++){
    if(!d->pickups[i].active) continue;
    int pdist = doom_dist(d->pos_x, d->pos_y, d->pickups[i].x, d->pickups[i].y);
    if(pdist < FP_ONE){
      switch(d->pickups[i].type){
      case 0: // health
        if(d->health < 100){ d->health += 25; if(d->health > 100) d->health = 100; d->pickups[i].active = 0; }
        break;
      case 1: // ammo
        if(d->ammo < d->max_ammo){ d->ammo += 12; if(d->ammo > d->max_ammo) d->ammo = d->max_ammo; d->pickups[i].active = 0; }
        break;
      case 2: // armor
        if(d->armor < 100){ d->armor += 50; if(d->armor > 100) d->armor = 100; d->pickups[i].active = 0; }
        break;
      case 3: // key
        d->has_key = 1;
        d->pickups[i].active = 0;
        break;
      }
    }
  }

  // Check exit
  int pmx = d->pos_x >> FP_SHIFT;
  int pmy = d->pos_y >> FP_SHIFT;
  if(pmx == d->exit_x && pmy == d->exit_y){
    if(d->map[d->exit_y][d->exit_x] == 5){
      if(d->level == 1 && !d->has_key){
        // Need key for level 2 exit
      } else {
        d->level_complete = 1;
        d->score += 1000;
      }
    }
  }
}

// Enemy AI update (called from main loop via doom_update placeholder)
static void
doom_update_enemies(struct doom_state *d)
{
  d->game_tick++;
  if(d->shoot_timer > 0) d->shoot_timer--;
  if(d->hurt_flash > 0) d->hurt_flash--;

  for(int i = 0; i < d->num_enemies; i++){
    struct doom_enemy *e = &d->enemies[i];
    if(!e->alive) continue;
    if(e->hurt_timer > 0) e->hurt_timer--;

    int edist = doom_dist(d->pos_x, d->pos_y, e->x, e->y);

    // Only update every few ticks (enemy speed)
    int speed_div = e->type == 2 ? 4 : (e->type == 1 ? 3 : 5);
    if(d->game_tick % speed_div != 0) continue;

    // Move toward player if in range
    if(edist < FP_ONE * 12 && edist > FP_ONE){
      int dx = d->pos_x - e->x;
      int dy = d->pos_y - e->y;
      // Normalize: move by fixed amount toward player
      int move = FP_ONE / 6;
      if(e->type == 1) move = FP_ONE / 4; // demons are faster
      if(e->type == 2) move = FP_ONE / 5; // boss medium speed

      // Simple: move along larger axis
      int nx = e->x, ny = e->y;
      if(fp_abs(dx) > fp_abs(dy)){
        nx += dx > 0 ? move : -move;
      } else {
        ny += dy > 0 ? move : -move;
      }

      // Check collision with walls
      int cmx = nx >> FP_SHIFT;
      int cmy = ny >> FP_SHIFT;
      if(cmx >= 0 && cmx < DOOM_MAP_W && cmy >= 0 && cmy < DOOM_MAP_H){
        if(d->map[cmy][cmx] == 0 || d->map[cmy][cmx] >= 5){
          e->x = nx; e->y = ny;
        }
      }
    }

    // Attack player if close
    int atk_range = e->type == 2 ? FP_ONE * 4 : FP_ONE * 2;
    int atk_cooldown = e->type == 2 ? 20 : 40;
    if(edist < atk_range && d->game_tick - e->last_atk > atk_cooldown){
      e->last_atk = d->game_tick;
      int dmg = e->type == 2 ? 15 : (e->type == 1 ? 12 : 8);
      // Armor absorbs some damage
      if(d->armor > 0){
        int absorbed = dmg / 2;
        if(absorbed > d->armor) absorbed = d->armor;
        d->armor -= absorbed;
        dmg -= absorbed;
      }
      d->health -= dmg;
      d->hurt_flash = 8;
      if(d->health <= 0){
        d->health = 0;
        d->dead = 1;
      }
    }
  }
}

static void
doom_draw(struct window *w)
{
  struct doom_state *d = &w->game.doom;
  int cx = w->x, cy = w->y + TITLEBAR_HEIGHT;
  int rw = w->w;
  int rh = w->h;
  int view_h = rh - 40; // Reserve bottom 40px for HUD bar

  // Update enemies
  if(!d->dead && !d->level_complete)
    doom_update_enemies(d);

  // Draw ceiling and floor
  fb_gradient_v(&screen, cx, cy, rw, view_h/2, RGB(20,20,40), RGB(50,50,70));
  fb_gradient_v(&screen, cx, cy+view_h/2, rw, view_h/2, RGB(70,50,35), RGB(35,25,18));

  // Z-buffer for sprite rendering
  int zbuf[424]; // max window width
  for(int i = 0; i < rw && i < 424; i++) zbuf[i] = 999999;

  // Raycasting
  int fov = 60;
  for(int col = 0; col < rw; col++){
    int ray_angle = (d->angle - fov/2 + col * fov / rw + 360) % 360;
    int ray_cos = cos_tab[ray_angle];
    int ray_sin = sin_tab[ray_angle];

    int map_x = d->pos_x >> FP_SHIFT;
    int map_y = d->pos_y >> FP_SHIFT;

    int step_x = ray_cos >= 0 ? 1 : -1;
    int step_y = ray_sin >= 0 ? 1 : -1;

    int delta_x = ray_cos != 0 ? fp_abs(FP_ONE * FP_ONE / ray_cos) : 999999;
    int delta_y = ray_sin != 0 ? fp_abs(FP_ONE * FP_ONE / ray_sin) : 999999;

    int side_x, side_y;
    if(ray_cos < 0)
      side_x = (d->pos_x - (map_x << FP_SHIFT)) * delta_x / FP_ONE;
    else
      side_x = (((map_x + 1) << FP_SHIFT) - d->pos_x) * delta_x / FP_ONE;
    if(ray_sin < 0)
      side_y = (d->pos_y - (map_y << FP_SHIFT)) * delta_y / FP_ONE;
    else
      side_y = (((map_y + 1) << FP_SHIFT) - d->pos_y) * delta_y / FP_ONE;

    int hit = 0, side = 0;
    int dist = 0;
    int wall_type = 1;
    for(int step = 0; step < 64; step++){
      if(side_x < side_y){
        dist = side_x;
        side_x += delta_x;
        map_x += step_x;
        side = 0;
      } else {
        dist = side_y;
        side_y += delta_y;
        map_y += step_y;
        side = 1;
      }
      if(map_x >= 0 && map_x < DOOM_MAP_W && map_y >= 0 && map_y < DOOM_MAP_H){
        if(d->map[map_y][map_x] >= 1 && d->map[map_y][map_x] <= 4){
          hit = 1;
          wall_type = d->map[map_y][map_x];
          break;
        } else if(d->map[map_y][map_x] == 5){
          // Exit door - render as special
          hit = 1;
          wall_type = 5;
          break;
        } else if(d->map[map_y][map_x] == 6){
          // Locked exit
          hit = 1;
          wall_type = 6;
          break;
        }
      } else {
        hit = 1;
        break;
      }
    }

    if(!hit || dist <= 0) dist = FP_ONE;

    // Fix fisheye
    int angle_diff = ray_angle - d->angle;
    if(angle_diff < -180) angle_diff += 360;
    if(angle_diff > 180) angle_diff -= 360;
    int cos_idx = (angle_diff + 360) % 360;
    int cos_diff = cos_tab[cos_idx];
    if(cos_diff <= 0) cos_diff = 1;
    dist = dist * cos_diff / FP_ONE;
    if(dist <= 0) dist = 1;

    zbuf[col] = dist;

    // Wall height
    int wall_h = view_h * FP_ONE / dist;
    if(wall_h > view_h * 3) wall_h = view_h * 3;
    int wall_top = (view_h - wall_h) / 2;
    int wall_bot = wall_top + wall_h;
    if(wall_top < 0) wall_top = 0;
    if(wall_bot > view_h) wall_bot = view_h;

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

    if(side){
      int rv = (wc >> 16) & 0xFF;
      int gv = (wc >> 8) & 0xFF;
      int bv = wc & 0xFF;
      wc = RGB(rv*2/3, gv*2/3, bv*2/3);
    }

    // Distance shading
    int shade = FP_ONE * 10 / dist;
    if(shade > 256) shade = 256;
    if(shade < 20) shade = 20;
    {
      int rv = ((wc >> 16) & 0xFF) * shade / 256;
      int gv = ((wc >> 8) & 0xFF) * shade / 256;
      int bv = (wc & 0xFF) * shade / 256;
      wc = RGB(rv, gv, bv);
    }

    for(int y = wall_top; y < wall_bot; y++)
      fb_pixel(&screen, cx + col, cy + y, wc);
  }

  // --- Draw sprites (enemies and pickups) ---
  // Simple billboard sprites sorted by distance
  for(int pass = 0; pass < 2; pass++){
    // pass 0 = pickups, pass 1 = enemies
    int count = pass == 0 ? d->num_pickups : d->num_enemies;
    for(int i = 0; i < count; i++){
      int sx, sy, alive_or_active;
      uint sprite_color;
      int sprite_h_scale;

      if(pass == 0){
        if(!d->pickups[i].active) continue;
        sx = d->pickups[i].x; sy = d->pickups[i].y;
        alive_or_active = 1;
        switch(d->pickups[i].type){
        case 0: sprite_color = RGB(255,0,0); break;    // health = red cross
        case 1: sprite_color = RGB(200,200,0); break;  // ammo = yellow
        case 2: sprite_color = RGB(0,100,200); break;  // armor = blue
        case 3: sprite_color = RGB(255,215,0); break;  // key = gold
        default: sprite_color = RGB(255,255,255); break;
        }
        sprite_h_scale = 3; // smaller
      } else {
        if(!d->enemies[i].alive) continue;
        sx = d->enemies[i].x; sy = d->enemies[i].y;
        alive_or_active = 1;
        if(d->enemies[i].hurt_timer > 0)
          sprite_color = RGB(255,255,255); // flash white when hurt
        else {
          switch(d->enemies[i].type){
          case 0: sprite_color = RGB(180,80,40); break;  // imp = brown
          case 1: sprite_color = RGB(180,40,80); break;   // demon = pink
          case 2: sprite_color = RGB(200,0,0); break;     // boss = dark red
          default: sprite_color = RGB(180,180,180); break;
          }
        }
        sprite_h_scale = d->enemies[i].type == 2 ? 1 : 2; // boss is bigger
      }

      // Transform sprite position relative to player
      int dx = sx - d->pos_x;
      int dy = sy - d->pos_y;

      // Rotate into view space
      int inv_det = FP_ONE * FP_ONE / ((-sin_tab[d->angle]) * cos_tab[(d->angle+90)%360] / FP_ONE -
                    cos_tab[d->angle] * (-sin_tab[(d->angle+90)%360]) / FP_ONE);
      // This is getting complex with fixed point, use simpler approach

      // Transform to camera space using dot products
      int cam_x = (dx * cos_tab[d->angle] + dy * sin_tab[d->angle]) / FP_ONE; // depth
      int cam_y = (-dx * sin_tab[d->angle] + dy * cos_tab[d->angle]) / FP_ONE; // lateral

      if(cam_x <= FP_ONE/4) continue; // behind camera or too close

      // Screen X position
      int screen_x = rw/2 + cam_y * rw / (cam_x * 2 / FP_ONE * fov / 60);
      if(cam_x == 0) continue;

      // Sprite screen size
      int spr_size = view_h * FP_ONE / cam_x / sprite_h_scale;
      if(spr_size < 4) spr_size = 4;
      if(spr_size > view_h) spr_size = view_h;

      int spr_top = view_h/2 - spr_size/2;
      int spr_bot = spr_top + spr_size;
      int spr_left = screen_x - spr_size/2;
      int spr_right = screen_x + spr_size/2;

      // Clip
      if(spr_right < 0 || spr_left >= rw) continue;
      if(spr_left < 0) spr_left = 0;
      if(spr_right > rw) spr_right = rw;
      if(spr_top < 0) spr_top = 0;
      if(spr_bot > view_h) spr_bot = view_h;

      // Draw sprite columns (only where not behind wall)
      for(int scol = spr_left; scol < spr_right; scol++){
        if(scol < 0 || scol >= rw) continue;
        if(cam_x >= zbuf[scol]) continue; // behind wall

        // Draw sprite column with simple shape
        if(pass == 0){
          // Pickup: small diamond/cross shape
          int rel = scol - (screen_x - spr_size/2);
          int center = spr_size / 2;
          int dist_from_center = fp_abs(rel - center);
          if(dist_from_center < spr_size/3){
            for(int sy2 = spr_top + spr_size/4; sy2 < spr_bot - spr_size/4; sy2++){
              fb_pixel(&screen, cx + scol, cy + sy2, sprite_color);
            }
          }
        } else {
          // Enemy: filled body shape
          int rel = scol - (screen_x - spr_size/2);
          int center = spr_size / 2;
          int dist_from_center = fp_abs(rel - center);

          // Body (narrower at top and bottom)
          int body_width = spr_size / 2 - dist_from_center;
          if(body_width > 0 || dist_from_center < spr_size / 3){
            // Head region (top 30%)
            int head_bot = spr_top + spr_size * 3 / 10;
            if(dist_from_center < spr_size / 5){
              for(int sy2 = spr_top; sy2 < head_bot && sy2 < spr_bot; sy2++){
                fb_pixel(&screen, cx + scol, cy + sy2, sprite_color);
              }
            }
            // Body region
            if(dist_from_center < spr_size / 3){
              for(int sy2 = head_bot; sy2 < spr_bot; sy2++){
                fb_pixel(&screen, cx + scol, cy + sy2, sprite_color);
              }
            }
            // Eyes (two dark pixels in head)
            if(pass == 1 && d->enemies[i].hurt_timer <= 0){
              int eye_y = spr_top + spr_size / 6;
              if(rel == center - spr_size/8 || rel == center + spr_size/8){
                if(eye_y >= 0 && eye_y < view_h)
                  fb_pixel(&screen, cx + scol, cy + eye_y, RGB(255,0,0));
              }
            }
          }
        }
      }
    }
  }

  // Hurt flash overlay
  if(d->hurt_flash > 0){
    for(int y = 0; y < view_h; y += 2){
      for(int x = 0; x < rw; x += 2){
        fb_pixel(&screen, cx+x, cy+y, RGB(200,0,0));
      }
    }
  }

  // Weapon display at bottom center
  int wpn_cx = cx + rw/2;
  int wpn_y = cy + view_h - 30;
  if(d->shoot_timer > 0){
    // Muzzle flash
    fb_fill_rect(&screen, wpn_cx-4, wpn_y-20, 8, 15, RGB(255,255,100));
    fb_fill_rect(&screen, wpn_cx-2, wpn_y-25, 4, 10, RGB(255,200,50));
  }
  if(d->weapon == 0){
    // Fist
    fb_fill_rect(&screen, wpn_cx-10, wpn_y, 20, 25, RGB(200,160,120));
    fb_fill_rect(&screen, wpn_cx-8, wpn_y+2, 16, 8, RGB(180,140,100));
  } else if(d->weapon == 1){
    // Pistol
    fb_fill_rect(&screen, wpn_cx-3, wpn_y-15, 6, 25, RGB(100,100,100));
    fb_fill_rect(&screen, wpn_cx-8, wpn_y+5, 16, 12, RGB(140,100,60));
  } else if(d->weapon == 2){
    // Shotgun
    fb_fill_rect(&screen, wpn_cx-4, wpn_y-20, 8, 30, RGB(80,80,80));
    fb_fill_rect(&screen, wpn_cx-2, wpn_y-20, 4, 30, RGB(100,100,100));
    fb_fill_rect(&screen, wpn_cx-10, wpn_y+5, 20, 14, RGB(120,80,40));
  }

  // Crosshair
  int chx = cx + rw/2, chy = cy + view_h/2;
  fb_fill_rect(&screen, chx-5, chy, 4, 1, RGB(0,255,0));
  fb_fill_rect(&screen, chx+2, chy, 4, 1, RGB(0,255,0));
  fb_fill_rect(&screen, chx, chy-5, 1, 4, RGB(0,255,0));
  fb_fill_rect(&screen, chx, chy+2, 1, 4, RGB(0,255,0));

  // === HUD BAR (bottom 40px) ===
  int hud_y = cy + view_h;
  fb_fill_rect(&screen, cx, hud_y, rw, 40, RGB(30,30,30));
  fb_fill_rect(&screen, cx, hud_y, rw, 1, RGB(80,80,80));

  // Health bar
  fb_text_nobg(&screen, cx+4, hud_y+4, "HP", RGB(200,0,0));
  fb_fill_rect(&screen, cx+24, hud_y+4, 102, 14, RGB(60,60,60));
  int hp_w = d->health;
  if(hp_w > 100) hp_w = 100;
  uint hp_color = hp_w > 50 ? RGB(0,180,0) : (hp_w > 25 ? RGB(200,200,0) : RGB(200,0,0));
  fb_fill_rect(&screen, cx+25, hud_y+5, hp_w, 12, hp_color);
  char hpbuf[8]; gui_itoa(d->health, hpbuf);
  fb_text_nobg(&screen, cx+50, hud_y+4, hpbuf, COL_TEXT_WHITE);

  // Armor bar
  fb_text_nobg(&screen, cx+4, hud_y+22, "AR", RGB(0,100,200));
  fb_fill_rect(&screen, cx+24, hud_y+22, 102, 14, RGB(60,60,60));
  int ar_w = d->armor;
  if(ar_w > 100) ar_w = 100;
  fb_fill_rect(&screen, cx+25, hud_y+23, ar_w, 12, RGB(0,100,200));

  // Ammo
  char ammo_str[16] = "AMMO:";
  char ammo_num[8]; gui_itoa(d->ammo, ammo_num);
  gui_strcat(ammo_str, ammo_num);
  fb_text_nobg(&screen, cx+140, hud_y+4, ammo_str, RGB(200,200,0));

  // Weapon name
  char *wpn_names[] = {"FIST", "PISTOL", "SHOTGUN"};
  fb_text_nobg(&screen, cx+140, hud_y+22, wpn_names[d->weapon], COL_TEXT_WHITE);

  // Score
  char score_str[24] = "SCORE:";
  char score_num[12]; gui_itoa(d->score, score_num);
  gui_strcat(score_str, score_num);
  fb_text_nobg(&screen, cx+260, hud_y+4, score_str, COL_TEXT_WHITE);

  // Level
  char lvl_str[16] = "LVL:";
  char lvl_num[4]; gui_itoa(d->level + 1, lvl_num);
  gui_strcat(lvl_str, lvl_num);
  fb_text_nobg(&screen, cx+260, hud_y+22, lvl_str, COL_TEXT_LIGHT);

  // Kills
  char kill_str[16] = "KILLS:";
  char kill_num[8]; gui_itoa(d->kills, kill_num);
  gui_strcat(kill_str, kill_num);
  gui_strcat(kill_str, "/");
  gui_itoa(d->total_enemies, kill_num);
  gui_strcat(kill_str, kill_num);
  fb_text_nobg(&screen, cx+350, hud_y+4, kill_str, COL_TEXT_LIGHT);

  // Key indicator
  if(d->has_key){
    fb_fill_rect(&screen, cx+350, hud_y+22, 12, 12, RGB(255,215,0));
    fb_text_nobg(&screen, cx+366, hud_y+22, "KEY", RGB(255,215,0));
  }

  // Minimap (top-right corner, over the 3D view)
  int mm_size = 2;
  int mm_x = cx + rw - DOOM_MAP_W * mm_size - 4;
  int mm_y = cy + 4;
  fb_fill_rect(&screen, mm_x-1, mm_y-1, DOOM_MAP_W*mm_size+2, DOOM_MAP_H*mm_size+2, RGB(0,0,0));
  for(int r = 0; r < DOOM_MAP_H; r++)
    for(int c = 0; c < DOOM_MAP_W; c++){
      uint mc;
      if(d->map[r][c] == 0) mc = RGB(20,20,20);
      else if(d->map[r][c] == 5) mc = RGB(0,200,100);
      else if(d->map[r][c] == 6) mc = RGB(200,0,0);
      else mc = RGB(80,80,80);
      fb_fill_rect(&screen, mm_x+c*mm_size, mm_y+r*mm_size, mm_size, mm_size, mc);
    }
  // Player dot
  int pmx = mm_x + (d->pos_x >> FP_SHIFT) * mm_size;
  int pmy = mm_y + (d->pos_y >> FP_SHIFT) * mm_size;
  fb_fill_rect(&screen, pmx, pmy, mm_size, mm_size, RGB(0,255,0));
  // Enemy dots
  for(int i = 0; i < d->num_enemies; i++){
    if(!d->enemies[i].alive) continue;
    int emx = mm_x + (d->enemies[i].x >> FP_SHIFT) * mm_size;
    int emy = mm_y + (d->enemies[i].y >> FP_SHIFT) * mm_size;
    fb_fill_rect(&screen, emx, emy, mm_size, mm_size, RGB(255,0,0));
  }

  // Death screen
  if(d->dead){
    fb_fill_rect(&screen, cx+rw/2-100, cy+view_h/2-30, 200, 60, RGB(100,0,0));
    fb_rect(&screen, cx+rw/2-100, cy+view_h/2-30, 200, 60, RGB(200,0,0));
    fb_text_nobg(&screen, cx+rw/2-52, cy+view_h/2-20, "YOU DIED", RGB(255,50,50));
    char dscore[24] = "Score: ";
    gui_itoa(d->score, score_num);
    gui_strcat(dscore, score_num);
    fb_text_nobg(&screen, cx+rw/2-40, cy+view_h/2, dscore, COL_TEXT_WHITE);
    fb_text_nobg(&screen, cx+rw/2-60, cy+view_h/2+16, "Press R to restart", COL_TEXT_LIGHT);
  }

  // Level complete screen
  if(d->level_complete){
    fb_fill_rect(&screen, cx+rw/2-110, cy+view_h/2-40, 220, 80, RGB(0,60,0));
    fb_rect(&screen, cx+rw/2-110, cy+view_h/2-40, 220, 80, RGB(0,200,0));
    fb_text_nobg(&screen, cx+rw/2-60, cy+view_h/2-28, "LEVEL COMPLETE!", RGB(0,255,0));
    char lscore[24] = "Score: ";
    gui_itoa(d->score, score_num);
    gui_strcat(lscore, score_num);
    fb_text_nobg(&screen, cx+rw/2-40, cy+view_h/2-8, lscore, COL_TEXT_WHITE);
    if(d->level + 1 < DOOM_NUM_LEVELS)
      fb_text_nobg(&screen, cx+rw/2-76, cy+view_h/2+12, "ENTER for next level", COL_TEXT_WHITE);
    else
      fb_text_nobg(&screen, cx+rw/2-64, cy+view_h/2+12, "YOU WIN! ENTER=new", COL_TEXT_WHITE);
  }
}
