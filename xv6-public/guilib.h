// GUI Library for xv6 Desktop Environment
#pragma once
#include "types.h"
#include "vga.h"

// Color helpers (32-bit XRGB)
#define RGB(r,g,b) ((uint)(0xFF000000 | ((r)<<16) | ((g)<<8) | (b)))

// Desktop theme colors (GNOME-inspired dark theme)
#define COL_DESKTOP_BG1  RGB(26, 26, 46)
#define COL_DESKTOP_BG2  RGB(22, 33, 62)
#define COL_TASKBAR      RGB(36, 36, 36)
#define COL_TASKBAR_HI   RGB(60, 60, 60)
#define COL_WIN_TITLE    RGB(48, 48, 48)
#define COL_WIN_TITLE_AC RGB(53, 132, 228)
#define COL_WIN_BG       RGB(255, 255, 255)
#define COL_WIN_BORDER   RGB(30, 30, 30)
#define COL_BTN_CLOSE    RGB(224, 27, 36)
#define COL_BTN_MIN      RGB(245, 194, 17)
#define COL_BTN_MAX      RGB(46, 194, 126)
#define COL_TEXT_WHITE   RGB(255, 255, 255)
#define COL_TEXT_LIGHT   RGB(200, 200, 200)
#define COL_TEXT_BLACK   RGB(0, 0, 0)
#define COL_TEXT_GRAY    RGB(128, 128, 128)
#define COL_ACCENT       RGB(53, 132, 228)
#define COL_HIGHLIGHT    RGB(53, 132, 228)
#define COL_SHADOW       RGB(0, 0, 0)
#define COL_MENU_BG      RGB(48, 48, 48)
#define COL_MENU_HI      RGB(53, 132, 228)
#define COL_TERM_BG      RGB(30, 30, 30)
#define COL_TERM_FG      RGB(204, 204, 204)
#define COL_TERM_CURSOR  RGB(255, 255, 255)
#define COL_ICON_BG      RGB(53, 132, 228)
#define COL_FM_BG        RGB(43, 43, 43)
#define COL_FM_SIDEBAR   RGB(36, 36, 36)
#define COL_SCROLLBAR    RGB(80, 80, 80)

// Font dimensions
#define FONT_W 8
#define FONT_H 16
#define CHAR_W FONT_W
#define CHAR_H FONT_H

// Taskbar
#define TASKBAR_HEIGHT 40

// Window title bar
#define TITLEBAR_HEIGHT 32
#define WIN_BORDER 2
#define BTN_SIZE 14

// Maximum windows
#define MAX_WINDOWS 16

// Icon sizes
#define ICON_SIZE 48
#define ICON_PADDING 20
#define ICON_TEXT_GAP 8

// Desktop icon types
#define ICON_TERMINAL   0
#define ICON_FOLDER     1
#define ICON_DOCUMENT   2
#define ICON_SETTINGS   3
#define ICON_ABOUT      4
#define ICON_GAME       5

// Window types
#define WIN_NONE        0
#define WIN_TERMINAL    1
#define WIN_FILEMANAGER 2
#define WIN_ABOUT       3
#define WIN_TEXTEDITOR  4
#define WIN_SNAKE       5
#define WIN_MINESWEEPER 6
#define WIN_PAINT       7
#define WIN_DOOM        8
#define WIN_CALCULATOR  9
#define WIN_SETTINGS    10
#define WIN_SHUTDOWN    11

// Snake constants
#define SNAKE_MAX_LEN 256
#define SNAKE_CELL 16

// Minesweeper constants
#define MINE_ROWS 10
#define MINE_COLS 10
#define MINE_COUNT 12
#define MINE_CELL 24

// Paint constants
#define PAINT_W 200
#define PAINT_H 150

// Doom constants
#define DOOM_MAP_W 24
#define DOOM_MAP_H 24
#define DOOM_RENDER_W 320
#define DOOM_RENDER_H 200
#define FP_SHIFT 8
#define FP_ONE (1 << FP_SHIFT)
#define DOOM_MAX_ENEMIES 16
#define DOOM_MAX_PICKUPS 16
#define DOOM_NUM_LEVELS 3

// Framebuffer structure
struct framebuf {
  uint *pixels;
  int width;
  int height;
};

// Game state structures
struct snake_state {
  int sx[SNAKE_MAX_LEN], sy[SNAKE_MAX_LEN];
  int len;
  int dir; // 0=up 1=right 2=down 3=left
  int food_x, food_y;
  int score;
  int game_over;
  int last_tick;
  int grid_w, grid_h;
};

struct mine_state {
  uchar grid[MINE_ROWS][MINE_COLS];   // 0=hidden 1=revealed 2=flagged
  uchar mines[MINE_ROWS][MINE_COLS];  // 1=mine
  uchar counts[MINE_ROWS][MINE_COLS]; // neighbor count
  int game_over, won;
  int revealed;
};

struct paint_state {
  uint canvas[PAINT_W * PAINT_H];
  uint cur_color;
  int brush_size;
  int last_px, last_py;
  int painting;
};

struct doom_enemy {
  int alive;
  int x, y;       // fixed-point
  int hp;
  int type;       // 0=imp 1=demon 2=boss
  int last_atk;   // tick of last attack
  int hurt_timer; // flash when hit
};

struct doom_pickup {
  int active;
  int x, y;       // fixed-point
  int type;       // 0=health 1=ammo 2=armor 3=key
};

struct doom_state {
  int pos_x, pos_y; // fixed-point (<<8)
  int angle;        // 0-359 degrees
  uchar map[DOOM_MAP_H][DOOM_MAP_W];

  // Player stats
  int health;
  int armor;
  int ammo;
  int max_ammo;
  int score;
  int has_key;
  int weapon;     // 0=fist 1=pistol 2=shotgun
  int shoot_timer;
  int hurt_flash;
  int dead;

  // Enemies
  struct doom_enemy enemies[DOOM_MAX_ENEMIES];
  int num_enemies;

  // Pickups
  struct doom_pickup pickups[DOOM_MAX_PICKUPS];
  int num_pickups;

  // Level
  int level;
  int level_complete;
  int exit_x, exit_y; // map coords of exit

  // Game stats
  int kills;
  int total_enemies;
  int game_tick;
};

// Window structure
struct window {
  int active;
  int x, y, w, h;
  char title[64];
  int visible;
  int minimized;
  int maximized;
  int orig_x, orig_y, orig_w, orig_h;
  int type;

  // Terminal state
  char text[8192];
  int text_len;
  int scroll_y;
  char input_line[256];
  int input_pos;

  // File manager state
  char cwd[128];
  char files[32][32];
  int file_count;
  int selected_file;
  int fm_action; // 0=none 1=creating 2=confirm_delete
  char fm_input[64];
  int fm_input_pos;

  // Text editor state
  char edit_buf[4096];
  int edit_len;
  char edit_filename[64];
  char edit_filepath[128];
  int edit_saved;

  // Game state (union to save memory)
  union {
    struct snake_state snake;
    struct mine_state mine;
    struct paint_state paint;
    struct doom_state doom;
  } game;
};

// Desktop icon
struct desktop_icon {
  int x, y;
  char name[32];
  char target[32];
  int icon_type;
  int selected;
};

// Context menu
struct context_menu {
  int visible;
  int x, y;
  int width, height;
  char items[8][32];
  int item_count;
};

// Notification toast
struct notification {
  int active;
  char text[64];
  int timer;
};

// Drawing functions
void fb_clear(struct framebuf *fb, uint color);
void fb_pixel(struct framebuf *fb, int x, int y, uint color);
void fb_rect(struct framebuf *fb, int x, int y, int w, int h, uint color);
void fb_fill_rect(struct framebuf *fb, int x, int y, int w, int h, uint color);
void fb_line(struct framebuf *fb, int x0, int y0, int x1, int y1, uint color);
void fb_fill_circle(struct framebuf *fb, int cx, int cy, int r, uint color);
void fb_gradient_v(struct framebuf *fb, int x, int y, int w, int h, uint c1, uint c2);
void fb_char(struct framebuf *fb, int x, int y, char c, uint fg, uint bg);
void fb_text(struct framebuf *fb, int x, int y, char *s, uint fg, uint bg);
void fb_text_nobg(struct framebuf *fb, int x, int y, char *s, uint fg);
int  fb_text_width(char *s);

// Icons
void draw_icon_terminal(struct framebuf *fb, int x, int y, int size);
void draw_icon_folder(struct framebuf *fb, int x, int y, int size);
void draw_icon_document(struct framebuf *fb, int x, int y, int size);
void draw_icon_settings(struct framebuf *fb, int x, int y, int size);
void draw_icon_about(struct framebuf *fb, int x, int y, int size);
void draw_icon_game(struct framebuf *fb, int x, int y, int size, uint color);

// Cursor
void draw_cursor(struct framebuf *fb, int x, int y);

// Utility
int  point_in_rect(int px, int py, int rx, int ry, int rw, int rh);
void gui_strcpy(char *dst, char *src);
int  gui_strlen(char *s);
void gui_strcat(char *dst, char *src);
void gui_itoa(int n, char *buf);
