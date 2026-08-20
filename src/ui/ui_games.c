/**
 * @file ui_games.c
 * @brief Mini-games page: Snake and Tetris for the 400x300 mono panel.
 *
 * Menu: UP/DOWN pick the game, MID starts it, LEFT/RIGHT stay page navigation.
 * Playing: D-pad controls the game, TRI drops back to the menu.
 */

#include "ui_games.h"
#include "ui_page_mgr.h"
#include "ai_ui_icon_font.h"
#include "tal_api.h"
#include "lvgl.h"
#include "lv_vendor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BTN_UP    0
#define BTN_DOWN  1
#define BTN_LEFT  2
#define BTN_RIGHT 3
#define BTN_MID   4
#define BTN_SW1   5
#define BTN_SW2   6
#define BTN_TRI   7
#define BTN_GREEN 8

typedef enum {
    GAME_MODE_MENU = 0,
    GAME_MODE_SNAKE,
    GAME_MODE_TETRIS,
} GAME_MODE_E;

/* ---- Snake ---- */
#define SNAKE_COLS  28
#define SNAKE_ROWS  16
#define SNAKE_CELL  12
#define SNAKE_OX    56
#define SNAKE_OY    42
#define SNAKE_MAX   (SNAKE_COLS * SNAKE_ROWS)

typedef struct {
    int x;
    int y;
} SNAKE_PT_T;

/* ---- Tetris ---- */
#define TET_COLS  10
#define TET_ROWS  20
#define TET_CELL  11
#define TET_OX    24
#define TET_OY    36

static const int8_t TET_SHAPES[7][4][4][2] = {
    {{{0,0},{1,0},{2,0},{3,0}}, {{0,0},{0,1},{0,2},{0,3}}, {{0,0},{1,0},{2,0},{3,0}}, {{0,0},{0,1},{0,2},{0,3}}},
    {{{0,0},{0,1},{1,0},{1,1}}, {{0,0},{0,1},{1,0},{1,1}}, {{0,0},{0,1},{1,0},{1,1}}, {{0,0},{0,1},{1,0},{1,1}}},
    {{{1,0},{2,0},{0,1},{1,1}}, {{0,0},{0,1},{1,1},{1,2}}, {{1,0},{2,0},{0,1},{1,1}}, {{0,0},{0,1},{1,1},{1,2}}},
    {{{1,0},{0,1},{1,1},{2,1}}, {{0,1},{1,0},{1,1},{1,2}}, {{0,1},{1,0},{2,0},{1,1}}, {{0,0},{0,1},{0,2},{1,1}}},
    {{{0,1},{1,1},{2,1},{2,2}}, {{0,0},{0,1},{1,1},{1,2}}, {{0,0},{1,0},{2,0},{2,1}}, {{0,0},{1,0},{1,1},{2,1}}},
    {{{0,1},{0,2},{1,0},{1,1}}, {{0,0},{1,0},{1,1},{2,1}}, {{1,0},{2,0},{0,1},{1,1}}, {{0,0},{0,1},{1,1},{2,1}}},
    {{{0,1},{1,1},{2,1},{1,2}}, {{0,1},{1,0},{1,1},{2,1}}, {{1,0},{2,0},{0,1},{1,1}}, {{0,0},{1,0},{1,1},{1,2}}},
};

static lv_obj_t *sg_root = NULL;
static lv_obj_t *sg_title = NULL;
static lv_obj_t *sg_hint = NULL;
static lv_obj_t *sg_score = NULL;
static lv_obj_t *sg_menu_snake = NULL;
static lv_obj_t *sg_menu_tetris = NULL;
static lv_obj_t *sg_board = NULL;
static lv_obj_t *sg_cells[SNAKE_COLS * SNAKE_ROWS];
static lv_timer_t *sg_timer = NULL;

static GAME_MODE_E sg_mode = GAME_MODE_MENU;
static int sg_menu_pick = 0; /* 0 snake, 1 tetris */
static bool sg_over = false;
static int sg_points = 0;

/* snake */
static SNAKE_PT_T sg_snake[SNAKE_MAX];
static int sg_snake_len;
static int sg_dir; /* 0 up 1 right 2 down 3 left */
static int sg_food_x;
static int sg_food_y;

/* tetris */
static uint8_t sg_tet_board[TET_ROWS][TET_COLS];
static int sg_tet_type;
static int sg_tet_rot;
static int sg_tet_x;
static int sg_tet_y;

static void __style_cell(lv_obj_t *cell, bool filled)
{
    lv_obj_set_style_bg_color(cell, filled ? lv_color_black() : lv_color_white(), 0);
    lv_obj_set_style_border_width(cell, 1, 0);
    lv_obj_set_style_border_color(cell, lv_color_black(), 0);
    lv_obj_set_style_radius(cell, 0, 0);
    lv_obj_set_style_pad_all(cell, 0, 0);
}

static void __set_score(const char *txt)
{
    if (sg_score) {
        lv_label_set_text(sg_score, txt);
    }
}

static void __set_score_fmt(const char *fmt, int val)
{
    char buf[32];
    snprintf(buf, sizeof(buf), fmt, val);
    __set_score(buf);
}

static void __menu_refresh(void)
{
    if (!sg_menu_snake || !sg_menu_tetris) {
        return;
    }
    lv_label_set_text_fmt(sg_menu_snake, "%s Snake", sg_menu_pick == 0 ? ">" : " ");
    lv_label_set_text_fmt(sg_menu_tetris, "%s Tetris", sg_menu_pick == 1 ? ">" : " ");
}

static void __show_menu_ui(bool show)
{
    if (sg_menu_snake) {
        show ? lv_obj_clear_flag(sg_menu_snake, LV_OBJ_FLAG_HIDDEN)
             : lv_obj_add_flag(sg_menu_snake, LV_OBJ_FLAG_HIDDEN);
    }
    if (sg_menu_tetris) {
        show ? lv_obj_clear_flag(sg_menu_tetris, LV_OBJ_FLAG_HIDDEN)
             : lv_obj_add_flag(sg_menu_tetris, LV_OBJ_FLAG_HIDDEN);
    }
    if (sg_hint) {
        lv_label_set_text(sg_hint, show ? "Up/Down pick   Mid start   Left/Right page"
                                        : "Triangle to quit");
    }
}

static void __board_show(bool show)
{
    int i;

    if (!sg_board) {
        return;
    }
    show ? lv_obj_clear_flag(sg_board, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(sg_board, LV_OBJ_FLAG_HIDDEN);
    for (i = 0; i < (int)(sizeof(sg_cells) / sizeof(sg_cells[0])); i++) {
        if (sg_cells[i]) {
            show ? lv_obj_clear_flag(sg_cells[i], LV_OBJ_FLAG_HIDDEN)
                 : lv_obj_add_flag(sg_cells[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void __layout_cells(int cols, int rows, int cell, int ox, int oy)
{
    int i;

    for (i = 0; i < (int)(sizeof(sg_cells) / sizeof(sg_cells[0])); i++) {
        if (!sg_cells[i]) {
            continue;
        }
        if ((i % cols) < cols && (i / cols) < rows) {
            lv_obj_set_size(sg_cells[i], cell, cell);
            lv_obj_set_pos(sg_cells[i], ox + (i % cols) * cell, oy + (i / cols) * cell);
            __style_cell(sg_cells[i], false);
            lv_obj_clear_flag(sg_cells[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(sg_cells[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void __paint_cell_grid(int cols, int rows, const uint8_t *map)
{
    int i;

    for (i = 0; i < cols * rows; i++) {
        if (sg_cells[i]) {
            __style_cell(sg_cells[i], map && map[i] != 0);
        }
    }
}

static bool __snake_cell_free(int x, int y)
{
    int i;

    for (i = 0; i < sg_snake_len; i++) {
        if (sg_snake[i].x == x && sg_snake[i].y == y) {
            return false;
        }
    }
    return true;
}

static void __snake_place_food(void)
{
    int tries = 0;

    do {
        sg_food_x = rand() % SNAKE_COLS;
        sg_food_y = rand() % SNAKE_ROWS;
    } while (!__snake_cell_free(sg_food_x, sg_food_y) && ++tries < 200);
}

static void __snake_render(void)
{
    uint8_t map[SNAKE_COLS * SNAKE_ROWS];
    int i;

    memset(map, 0, sizeof(map));
    for (i = 0; i < sg_snake_len; i++) {
        if (sg_snake[i].x >= 0 && sg_snake[i].x < SNAKE_COLS && sg_snake[i].y >= 0 &&
            sg_snake[i].y < SNAKE_ROWS) {
            map[sg_snake[i].y * SNAKE_COLS + sg_snake[i].x] = 1;
        }
    }
    if (sg_food_x >= 0 && sg_food_y >= 0) {
        map[sg_food_y * SNAKE_COLS + sg_food_x] = 1;
    }
    __paint_cell_grid(SNAKE_COLS, SNAKE_ROWS, map);
}

static void __snake_start(void)
{
    sg_mode = GAME_MODE_SNAKE;
    sg_over = false;
    sg_points = 0;
    sg_snake_len = 3;
    sg_dir = 1;
    sg_snake[0].x = 5;
    sg_snake[0].y = SNAKE_ROWS / 2;
    sg_snake[1].x = 4;
    sg_snake[1].y = sg_snake[0].y;
    sg_snake[2].x = 3;
    sg_snake[2].y = sg_snake[0].y;
    __snake_place_food();
    __show_menu_ui(false);
    __board_show(true);
    __layout_cells(SNAKE_COLS, SNAKE_ROWS, SNAKE_CELL, SNAKE_OX, SNAKE_OY);
    __set_score("Snake 0");
    __snake_render();
}

static void __snake_set_dir(int nd)
{
    if ((sg_dir + 2) % 4 == nd) {
        return;
    }
    sg_dir = nd;
}

static void __snake_step(void)
{
    SNAKE_PT_T head;
    int i;

    if (sg_mode != GAME_MODE_SNAKE || sg_over) {
        return;
    }

    head = sg_snake[0];
    if (sg_dir == 0) {
        head.y--;
    } else if (sg_dir == 1) {
        head.x++;
    } else if (sg_dir == 2) {
        head.y++;
    } else {
        head.x--;
    }

    if (head.x < 0 || head.x >= SNAKE_COLS || head.y < 0 || head.y >= SNAKE_ROWS) {
        sg_over = true;
        __set_score("Snake over - Green retry");
        return;
    }
    for (i = 0; i < sg_snake_len; i++) {
        if (sg_snake[i].x == head.x && sg_snake[i].y == head.y) {
            sg_over = true;
            __set_score("Snake over - Green retry");
            return;
        }
    }

    for (i = sg_snake_len; i > 0; i--) {
        sg_snake[i] = sg_snake[i - 1];
    }
    sg_snake[0] = head;

    if (head.x == sg_food_x && head.y == sg_food_y) {
        sg_snake_len++;
        if (sg_snake_len > SNAKE_MAX) {
            sg_snake_len = SNAKE_MAX;
        }
        sg_points += 10;
        __set_score_fmt("Snake %d", sg_points);
        __snake_place_food();
    }
    __snake_render();
}

static bool __tet_collide_piece(int px, int py, int rot)
{
    int b;

    for (b = 0; b < 4; b++) {
        int x = px + TET_SHAPES[sg_tet_type][rot][b][0];
        int y = py + TET_SHAPES[sg_tet_type][rot][b][1];
        if (x < 0 || x >= TET_COLS || y < 0 || y >= TET_ROWS) {
            return true;
        }
        if (y >= 0 && sg_tet_board[y][x]) {
            return true;
        }
    }
    return false;
}

static void __tet_merge_piece(void)
{
    int b;

    for (b = 0; b < 4; b++) {
        int x = sg_tet_x + TET_SHAPES[sg_tet_type][sg_tet_rot][b][0];
        int y = sg_tet_y + TET_SHAPES[sg_tet_type][sg_tet_rot][b][1];
        if (y >= 0 && y < TET_ROWS && x >= 0 && x < TET_COLS) {
            sg_tet_board[y][x] = 1;
        }
    }
}

static void __tet_clear_lines(void)
{
    int y;

    for (y = TET_ROWS - 1; y >= 0; y--) {
        int x;
        bool full = true;

        for (x = 0; x < TET_COLS; x++) {
            if (!sg_tet_board[y][x]) {
                full = false;
                break;
            }
        }
        if (!full) {
            continue;
        }
        sg_points += 100;
        memmove(&sg_tet_board[1][0], &sg_tet_board[0][0], sizeof(sg_tet_board[0]) * y);
        memset(sg_tet_board[0], 0, sizeof(sg_tet_board[0]));
        y++;
    }
}

static void __tet_spawn(void)
{
    sg_tet_type = rand() % 7;
    sg_tet_rot = 0;
    sg_tet_x = TET_COLS / 2 - 2;
    sg_tet_y = 0;
    if (__tet_collide_piece(sg_tet_x, sg_tet_y, sg_tet_rot)) {
        sg_over = true;
        __set_score("Tetris over - Green retry");
    }
}

static void __tet_render(void)
{
    uint8_t map[TET_COLS * TET_ROWS];
    int y, x, b;

    memset(map, 0, sizeof(map));
    for (y = 0; y < TET_ROWS; y++) {
        for (x = 0; x < TET_COLS; x++) {
            if (sg_tet_board[y][x]) {
                map[y * TET_COLS + x] = 1;
            }
        }
    }
    for (b = 0; b < 4; b++) {
        int x = sg_tet_x + TET_SHAPES[sg_tet_type][sg_tet_rot][b][0];
        int y = sg_tet_y + TET_SHAPES[sg_tet_type][sg_tet_rot][b][1];
        if (y >= 0 && y < TET_ROWS && x >= 0 && x < TET_COLS) {
            map[y * TET_COLS + x] = 1;
        }
    }
    __paint_cell_grid(TET_COLS, TET_ROWS, map);
}

static void __tet_start(void)
{
    sg_mode = GAME_MODE_TETRIS;
    sg_over = false;
    sg_points = 0;
    memset(sg_tet_board, 0, sizeof(sg_tet_board));
    __show_menu_ui(false);
    __board_show(true);
    __layout_cells(TET_COLS, TET_ROWS, TET_CELL, TET_OX, TET_OY);
    __set_score("Tetris 0");
    __tet_spawn();
    __tet_render();
}

static void __tet_step(void)
{
    if (sg_mode != GAME_MODE_TETRIS || sg_over) {
        return;
    }
    if (!__tet_collide_piece(sg_tet_x, sg_tet_y + 1, sg_tet_rot)) {
        sg_tet_y++;
    } else {
        __tet_merge_piece();
        __tet_clear_lines();
        __set_score_fmt("Tetris %d", sg_points);
        __tet_spawn();
    }
    __tet_render();
}

static void __return_menu(void)
{
    sg_mode = GAME_MODE_MENU;
    sg_over = false;
    sg_points = 0;
    __board_show(false);
    __set_score("");
    __menu_refresh();
    __show_menu_ui(true);
}

static void __start_selected(void)
{
    uint32_t seed = (uint32_t)tal_system_get_millisecond();
    srand(seed ^ (uint32_t)sg_menu_pick);

    if (sg_menu_pick == 0) {
        __snake_start();
    } else {
        __tet_start();
    }
}

static void __timer_cb(lv_timer_t *timer)
{
    static uint8_t tet_div;

    (void)timer;
    if (sg_mode == GAME_MODE_SNAKE) {
        __snake_step();
    } else if (sg_mode == GAME_MODE_TETRIS) {
        if (++tet_div >= 3) {
            tet_div = 0;
            __tet_step();
        }
    } else {
        tet_div = 0;
    }
}

void games_page_create(lv_obj_t *parent)
{
    lv_font_t *font = ai_ui_get_text_font();
    int i;

    sg_mode = GAME_MODE_MENU;
    sg_menu_pick = 0;
    sg_over = false;

    sg_root = lv_obj_create(parent);
    lv_obj_set_size(sg_root, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(sg_root, lv_color_white(), 0);
    lv_obj_set_style_border_width(sg_root, 0, 0);
    lv_obj_set_style_pad_all(sg_root, 0, 0);
    lv_obj_clear_flag(sg_root, LV_OBJ_FLAG_SCROLLABLE);

    sg_title = lv_label_create(sg_root);
    lv_obj_set_style_text_font(sg_title, font, 0);
    lv_obj_set_style_text_color(sg_title, lv_color_black(), 0);
    lv_label_set_text(sg_title, "Games");
    lv_obj_align(sg_title, LV_ALIGN_TOP_MID, 0, 6);

    sg_score = lv_label_create(sg_root);
    lv_obj_set_style_text_font(sg_score, font, 0);
    lv_obj_set_style_text_color(sg_score, lv_color_black(), 0);
    lv_label_set_text(sg_score, "");
    lv_obj_align(sg_score, LV_ALIGN_TOP_MID, 0, 24);

    sg_menu_snake = lv_label_create(sg_root);
    lv_obj_set_style_text_font(sg_menu_snake, font, 0);
    lv_obj_set_style_text_color(sg_menu_snake, lv_color_black(), 0);
    lv_obj_align(sg_menu_snake, LV_ALIGN_CENTER, 0, -24);

    sg_menu_tetris = lv_label_create(sg_root);
    lv_obj_set_style_text_font(sg_menu_tetris, font, 0);
    lv_obj_set_style_text_color(sg_menu_tetris, lv_color_black(), 0);
    lv_obj_align(sg_menu_tetris, LV_ALIGN_CENTER, 0, 8);

    sg_hint = lv_label_create(sg_root);
    lv_obj_set_style_text_font(sg_hint, font, 0);
    lv_obj_set_style_text_color(sg_hint, lv_color_black(), 0);
    lv_obj_align(sg_hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    sg_board = lv_obj_create(sg_root);
    lv_obj_set_size(sg_board, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_opa(sg_board, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sg_board, 0, 0);
    lv_obj_clear_flag(sg_board, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(sg_board, LV_OBJ_FLAG_HIDDEN);

    for (i = 0; i < (int)(sizeof(sg_cells) / sizeof(sg_cells[0])); i++) {
        sg_cells[i] = lv_obj_create(sg_board);
        lv_obj_add_flag(sg_cells[i], LV_OBJ_FLAG_HIDDEN);
        __style_cell(sg_cells[i], false);
    }

    __menu_refresh();
    __show_menu_ui(true);
    __board_show(false);

    if (!sg_timer) {
        sg_timer = lv_timer_create(__timer_cb, 180, NULL);
    }
}

void games_page_destroy(void)
{
    if (sg_timer) {
        lv_timer_del(sg_timer);
        sg_timer = NULL;
    }
    if (sg_root) {
        lv_obj_del(sg_root);
        sg_root = NULL;
    }
    sg_title = sg_hint = sg_score = sg_menu_snake = sg_menu_tetris = sg_board = NULL;
    memset(sg_cells, 0, sizeof(sg_cells));
    sg_mode = GAME_MODE_MENU;
}

void games_page_on_press(void)
{
    if (sg_mode == GAME_MODE_MENU) {
        __start_selected();
        return;
    }
    if (sg_over) {
        if (sg_mode == GAME_MODE_SNAKE) {
            __snake_start();
        } else if (sg_mode == GAME_MODE_TETRIS) {
            __tet_start();
        }
    }
}

bool games_is_playing(void)
{
    return sg_mode == GAME_MODE_SNAKE || sg_mode == GAME_MODE_TETRIS;
}

static void __snake_input(int btn)
{
    if (sg_over) {
        return;
    }
    if (btn == BTN_UP) {
        __snake_set_dir(0);
    } else if (btn == BTN_RIGHT) {
        __snake_set_dir(1);
    } else if (btn == BTN_DOWN) {
        __snake_set_dir(2);
    } else if (btn == BTN_LEFT) {
        __snake_set_dir(3);
    }
}

static void __tet_input(int btn)
{
    if (sg_over) {
        return;
    }
    if (btn == BTN_LEFT && !__tet_collide_piece(sg_tet_x - 1, sg_tet_y, sg_tet_rot)) {
        sg_tet_x--;
    } else if (btn == BTN_RIGHT && !__tet_collide_piece(sg_tet_x + 1, sg_tet_y, sg_tet_rot)) {
        sg_tet_x++;
    } else if (btn == BTN_UP) {
        int nr = (sg_tet_rot + 1) % 4;
        if (!__tet_collide_piece(sg_tet_x, sg_tet_y, nr)) {
            sg_tet_rot = nr;
        }
    } else if (btn == BTN_DOWN) {
        if (!__tet_collide_piece(sg_tet_x, sg_tet_y + 1, sg_tet_rot)) {
            sg_tet_y++;
        } else {
            __tet_merge_piece();
            __tet_clear_lines();
            __set_score_fmt("Tetris %d", sg_points);
            __tet_spawn();
        }
    }
    __tet_render();
}

/** Runs with the display lock held. @return true if the games page owns the key. */
static bool __handle_key(int btn_idx)
{
    /* Triangle is the back key: one level out of a running game, and on the
     * picker it falls through to its usual "jump to the face" action. The
     * centre key used to quit on a long press, but it sits under the thumb
     * during play and kept ending games by accident. */
    if (btn_idx == BTN_TRI && games_is_playing()) {
        __return_menu();
        return true;
    }

    if (btn_idx == BTN_MID || btn_idx == BTN_GREEN) {
        games_page_on_press();
        return true;
    }

    if (sg_mode == GAME_MODE_MENU) {
        /* LEFT/RIGHT keep paging, TRI keeps jumping home, SW1/SW2 keep their jobs. */
        if (btn_idx == BTN_UP || btn_idx == BTN_DOWN) {
            sg_menu_pick = (btn_idx == BTN_UP) ? 0 : 1;
            __menu_refresh();
            return true;
        }
        return false;
    }

    if (btn_idx == BTN_UP || btn_idx == BTN_DOWN || btn_idx == BTN_LEFT || btn_idx == BTN_RIGHT) {
        if (sg_mode == GAME_MODE_SNAKE) {
            __snake_input(btn_idx);
        } else {
            __tet_input(btn_idx);
        }
        return true;
    }
    return false;
}

bool games_btn_event(int btn_idx, bool pressed)
{
    bool consumed;

    if (page_mgr_get_current() != PAGE_IDX_GAMES) {
        return false;
    }

    if (!pressed) {
        return false;
    }

    lv_vendor_disp_lock();
    consumed = __handle_key(btn_idx);
    lv_vendor_disp_unlock();
    return consumed;
}
