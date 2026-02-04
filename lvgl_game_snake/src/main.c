#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <lvgl.h>

// Use the board's pwm-leds backlight node directly.
#define BACKLIGHT_NODE DT_NODELABEL(pwm_lcd0)

static const struct pwm_dt_spec backlight =
	PWM_DT_SPEC_GET(BACKLIGHT_NODE);

LOG_MODULE_REGISTER(lvgl_game_snake, LOG_LEVEL_INF);

#define GRID_SIZE 20
#define MAX_SNAKE_LEN 100
#define TICK_MS 200
#define GRID_LINE_COLOR_HEX 0x000000//0x2c3e50

struct cell_pos {
	int x;
	int y;
};

static struct cell_pos snake[MAX_SNAKE_LEN];
static int snake_len;
static int dir_x;
static int dir_y;
static int pending_dir_x;
static int pending_dir_y;
static bool pending_dir;
static struct cell_pos food;
static int cell_size;
static int origin_x;
static int origin_y;
static int score;
static bool food_magic;
static int food_bonus;

static lv_obj_t *segments[MAX_SNAKE_LEN];
static lv_obj_t *food_obj;
static lv_obj_t *score_label;
static lv_obj_t *grid_border;
static lv_obj_t *touch_layer;
static lv_obj_t *legend_box;
static lv_obj_t *legend_red;
static lv_obj_t *legend_yellow;
static lv_obj_t *legend_red_label;
static lv_obj_t *legend_yellow_label;
static lv_obj_t *grid_lines[(GRID_SIZE + 1) * 2];
static lv_point_t grid_line_points[(GRID_SIZE + 1) * 2][2];

static uint32_t rand_state;

static uint32_t prng_next(void)
{
	rand_state ^= rand_state << 13;
	rand_state ^= rand_state >> 17;
	rand_state ^= rand_state << 5;
	return rand_state;
}

static bool snake_contains(int x, int y)
{
	for (int i = 0; i < snake_len; i++) {
		if (snake[i].x == x && snake[i].y == y) {
			return true;
		}
	}

	return false;
}

static void place_food(void)
{
	if (snake_len >= (GRID_SIZE * GRID_SIZE)) {
		return;
	}

	/* 1 in 6 chance for magic food. */
	food_magic = ((prng_next() % 6U) == 0U);
	food_bonus = food_magic ? (int)(1 + (prng_next() % 3U)) : 1;

	for (;;) {
		/* Keep food away from the outermost grid border (1..GRID_SIZE-2). */
		int x = 1 + (int)(prng_next() % (GRID_SIZE - 2));
		int y = 1 + (int)(prng_next() % (GRID_SIZE - 2));

		if (!snake_contains(x, y)) {
			food.x = x;
			food.y = y;
			break;
		}
	}
}

static void update_objects(void)
{
	int draw = cell_size - 1;

	if (draw < 1) {
		draw = 1;
	}

	for (int i = 0; i < MAX_SNAKE_LEN; i++) {
		if (i < snake_len) {
			lv_obj_clear_flag(segments[i], LV_OBJ_FLAG_HIDDEN);
			lv_obj_set_size(segments[i], draw, draw);
			lv_obj_set_pos(segments[i],
					   origin_x + snake[i].x * cell_size,
					   origin_y + snake[i].y * cell_size);
		} else {
			lv_obj_add_flag(segments[i], LV_OBJ_FLAG_HIDDEN);
		}
	}

	lv_obj_set_size(food_obj, draw, draw);
	lv_obj_set_pos(food_obj,
			 origin_x + food.x * cell_size,
			 origin_y + food.y * cell_size);
	if (food_magic) {
		lv_obj_set_style_bg_color(food_obj, lv_color_hex(0xf1c40f), 0);
	} else {
		lv_obj_set_style_bg_color(food_obj, lv_color_hex(0xe74c3c), 0);
	}

	lv_label_set_text_fmt(score_label, "Score: %d", score);
}

static void reset_game(void)
{
	snake_len = 3;
	dir_x = 1;
	dir_y = 0;
	score = 0;

	int start_x = GRID_SIZE / 2;
	int start_y = GRID_SIZE / 2;

	for (int i = 0; i < snake_len; i++) {
		snake[i].x = start_x - i;
		snake[i].y = start_y;
	}

	place_food();
	update_objects();
}

static void update_direction_from_point(const lv_point_t *point)
{
	int head_px = origin_x + snake[0].x * cell_size + cell_size / 2;
	int head_py = origin_y + snake[0].y * cell_size + cell_size / 2;
	int dx = point->x - head_px;
	int dy = point->y - head_py;

	if (dx == 0 && dy == 0) {
		return;
	}

	int ndx = 0;
	int ndy = 0;

	int abs_dx = (dx < 0) ? -dx : dx;
	int abs_dy = (dy < 0) ? -dy : dy;

	if (abs_dx > abs_dy) {
		ndx = (dx > 0) ? 1 : -1;
	} else {
		ndy = (dy > 0) ? 1 : -1;
	}

	if (ndx == -dir_x && ndy == -dir_y) {
		return;
	}

	pending_dir_x = ndx;
	pending_dir_y = ndy;
	pending_dir = true;
}

static void touch_event_cb(lv_event_t *event)
{
	lv_event_code_t code = lv_event_get_code(event);

	if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING) {
		return;
	}

	lv_point_t point;
	lv_indev_t *indev = lv_event_get_indev(event);

	if (indev == NULL) {
		return;
	}

	lv_indev_get_point(indev, &point);
	update_direction_from_point(&point);
}

static void game_tick_cb(lv_timer_t *timer)
{
	ARG_UNUSED(timer);

	if (pending_dir) {
		dir_x = pending_dir_x;
		dir_y = pending_dir_y;
		pending_dir = false;
	}

	int next_x = snake[0].x + dir_x;
	int next_y = snake[0].y + dir_y;

	if (next_x < 0 || next_y < 0 || next_x >= GRID_SIZE || next_y >= GRID_SIZE) {
		reset_game();
		return;
	}

	if (snake_contains(next_x, next_y)) {
		reset_game();
		return;
	}

	struct cell_pos tail_prev = snake[snake_len - 1];

	for (int i = snake_len - 1; i > 0; i--) {
		snake[i] = snake[i - 1];
	}

	snake[0].x = next_x;
	snake[0].y = next_y;

	if (next_x == food.x && next_y == food.y) {
		int grow = food_magic ? food_bonus : 1;
		for (int i = 0; i < grow; i++) {
			if (snake_len < MAX_SNAKE_LEN) {
				snake_len++;
				snake[snake_len - 1] = tail_prev;
			}
		}
		score += grow;
		place_food();
	}

	update_objects();
}

static void setup_ui(void)
{
	lv_obj_t *screen = lv_scr_act();
	lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
	lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
	lv_coord_t width = lv_disp_get_hor_res(NULL);
	lv_coord_t height = lv_disp_get_ver_res(NULL);
	lv_coord_t size = width < height ? width : height;
	/* Fit the square grid inside the circular display area. */
	lv_coord_t usable = (size * 7) / 10;
	const int border_w = 2;
	lv_coord_t grid_px;

	cell_size = (usable - (2 * border_w)) / GRID_SIZE;
	if (cell_size < 1) {
		cell_size = 1;
	}

	grid_px = cell_size * GRID_SIZE + (2 * border_w);
	origin_x = (width - (cell_size * GRID_SIZE)) / 2;
	origin_y = (height - (cell_size * GRID_SIZE)) / 2;

	score_label = lv_label_create(screen);
	lv_label_set_text(score_label, "Score: 0");
	lv_obj_align(score_label, LV_ALIGN_TOP_MID, 0, 10);
	lv_obj_set_style_text_color(score_label, lv_color_hex(0xffffff), 0);

	grid_border = lv_obj_create(screen);
	lv_obj_clear_flag(grid_border, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_pos(grid_border, origin_x - border_w, origin_y - border_w);
	lv_obj_set_size(grid_border, grid_px, grid_px);
	lv_obj_set_style_radius(grid_border, 0, 0);
	lv_obj_set_style_pad_all(grid_border, 0, 0);
	lv_obj_set_style_bg_color(grid_border, lv_color_hex(0x111111), 0);
	lv_obj_set_style_bg_opa(grid_border, LV_OPA_20, 0);
	lv_obj_set_style_border_width(grid_border, 0, 0);

	int line_idx = 0;
	for (int i = 0; i <= GRID_SIZE; i++) {
		int pos = border_w + i * cell_size;

		grid_line_points[line_idx][0] = (lv_point_t){ pos, 0 };
		grid_line_points[line_idx][1] =
			(lv_point_t){ pos, grid_px };
		grid_lines[line_idx] = lv_line_create(grid_border);
		lv_line_set_points(grid_lines[line_idx], grid_line_points[line_idx], 2);
		lv_obj_set_style_line_width(grid_lines[line_idx], 1, 0);
		lv_obj_set_style_line_color(grid_lines[line_idx], lv_color_hex(GRID_LINE_COLOR_HEX), 0);
		lv_obj_set_style_line_opa(grid_lines[line_idx], LV_OPA_40, 0);
		line_idx++;

		grid_line_points[line_idx][0] = (lv_point_t){ 0, pos };
		grid_line_points[line_idx][1] =
			(lv_point_t){ grid_px, pos };
		grid_lines[line_idx] = lv_line_create(grid_border);
		lv_line_set_points(grid_lines[line_idx], grid_line_points[line_idx], 2);
		lv_obj_set_style_line_width(grid_lines[line_idx], 1, 0);
		lv_obj_set_style_line_color(grid_lines[line_idx], lv_color_hex(GRID_LINE_COLOR_HEX), 0);
		lv_obj_set_style_line_opa(grid_lines[line_idx], LV_OPA_40, 0);
		line_idx++;
	}

	for (int i = 0; i < MAX_SNAKE_LEN; i++) {
		segments[i] = lv_obj_create(screen);
		lv_obj_clear_flag(segments[i], LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_style_radius(segments[i], 0, 0);
		lv_obj_set_style_border_width(segments[i], 0, 0);
		lv_obj_set_style_bg_color(segments[i], lv_color_hex(0x2ecc71), 0);
		lv_obj_add_flag(segments[i], LV_OBJ_FLAG_HIDDEN);
	}

	food_obj = lv_obj_create(screen);
	lv_obj_clear_flag(food_obj, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_radius(food_obj, 0, 0);
	lv_obj_set_style_border_width(food_obj, 0, 0);
	lv_obj_set_style_bg_color(food_obj, lv_color_hex(0xe74c3c), 0);

	rand_state = (uint32_t)k_uptime_get_32();
	if (rand_state == 0) {
		rand_state = 1;
	}

	/* Legend in the left margin between display edge and grid area. */
	if (origin_x > 12) {
		int legend_w = origin_x - 6;
		int legend_x = 3;
		int legend_cell = cell_size;
		int legend_h = (legend_cell * 2) + 16;
		int legend_y = (height - legend_h) / 2;

		legend_box = lv_obj_create(screen);
		lv_obj_clear_flag(legend_box, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_pos(legend_box, legend_x, legend_y);
		lv_obj_set_size(legend_box, legend_w, legend_h);
		lv_obj_set_style_bg_opa(legend_box, LV_OPA_TRANSP, 0);
		lv_obj_set_style_border_width(legend_box, 0, 0);
		lv_obj_set_style_pad_all(legend_box, 0, 0);

		legend_red = lv_obj_create(legend_box);
		lv_obj_clear_flag(legend_red, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_size(legend_red, legend_cell, legend_cell);
		lv_obj_set_pos(legend_red, 0, 0);
		lv_obj_set_style_radius(legend_red, 0, 0);
		lv_obj_set_style_border_width(legend_red, 0, 0);
		lv_obj_set_style_bg_color(legend_red, lv_color_hex(0xe74c3c), 0);

		legend_red_label = lv_label_create(legend_box);
		lv_label_set_text(legend_red_label, "+1");
		lv_obj_set_style_text_font(legend_red_label, &lv_font_montserrat_10, 0);
		lv_obj_set_style_text_color(legend_red_label, lv_color_hex(0xffffff), 0);
		lv_obj_align_to(legend_red_label, legend_red, LV_ALIGN_OUT_RIGHT_MID, 6, -1);

		legend_yellow = lv_obj_create(legend_box);
		lv_obj_clear_flag(legend_yellow, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_size(legend_yellow, legend_cell, legend_cell);
		lv_obj_set_pos(legend_yellow, 0, legend_cell + 8);
		lv_obj_set_style_radius(legend_yellow, 0, 0);
		lv_obj_set_style_border_width(legend_yellow, 0, 0);
		lv_obj_set_style_bg_color(legend_yellow, lv_color_hex(0xf1c40f), 0);

		legend_yellow_label = lv_label_create(legend_box);
		lv_label_set_text(legend_yellow_label, "+1-3");
		lv_obj_set_style_text_font(legend_yellow_label, &lv_font_montserrat_10, 0);
		lv_obj_set_style_text_color(legend_yellow_label, lv_color_hex(0xffffff), 0);
		lv_obj_align_to(legend_yellow_label, legend_yellow, LV_ALIGN_OUT_RIGHT_MID, 6, -1);
	}

	touch_layer = lv_obj_create(screen);
	lv_obj_clear_flag(touch_layer, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(touch_layer, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(touch_layer, width, height);
	lv_obj_set_style_bg_opa(touch_layer, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(touch_layer, 0, 0);
	lv_obj_add_event_cb(touch_layer, touch_event_cb, LV_EVENT_ALL, NULL);
	lv_obj_move_foreground(touch_layer);

	reset_game();
	lv_timer_create(game_tick_cb, TICK_MS, NULL);
}

int main(void)
{
	const struct device *display;
	uint32_t backlight_period;

	/* Turn on backlight */
	if (!device_is_ready(backlight.dev)) {
		return;
	}

	backlight_period = backlight.period;
	if (backlight_period == 0U) {
		backlight_period = PWM_USEC(4000);
	}

	pwm_set_dt(&backlight, backlight_period, backlight_period / 2U);

 	display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	if (!device_is_ready(display)) {
		LOG_ERR("Display device not ready");
		return;
	}

	/* Wait for display init */
	k_sleep(K_MSEC(200));	

	display_blanking_off(display);

	setup_ui();

	while (1) {
		lv_timer_handler();
		k_msleep(10);
	}
}
