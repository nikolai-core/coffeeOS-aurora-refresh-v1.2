#include <stdint.h>

#include "app.h"
#include "ascii_util.h"
#include "mouse.h"

#define CALC_DISPLAY_MAX_CHARS 12
#define CALC_DISPLAY_BUFFER_LEN 16
#define CALC_PADDING 8
#define CALC_GAP 4
#define CALC_DISPLAY_H 48
#define CALC_BUTTON_W 54
#define CALC_BUTTON_H 44

#define CALC_COLOR_BG 0x1C1C1Eu
#define CALC_COLOR_DISPLAY_BG 0x2C2C2Eu
#define CALC_COLOR_DISPLAY_TEXT 0xFFFFFFu
#define CALC_COLOR_NUMBER 0x3A3A3Cu
#define CALC_COLOR_NUMBER_HOVER 0x4A4A4Cu
#define CALC_COLOR_OPERATOR 0xFF9500u
#define CALC_COLOR_OPERATOR_HOVER 0xFFAA33u
#define CALC_COLOR_OPERATOR_ACTIVE_BG 0xFFFFFFu
#define CALC_COLOR_OPERATOR_ACTIVE_TEXT 0xFF9500u
#define CALC_COLOR_FUNCTION 0xA0A0A0u
#define CALC_COLOR_FUNCTION_HOVER 0xBBBBBBu
#define CALC_COLOR_EQUALS 0xFF9500u
#define CALC_COLOR_EQUALS_HOVER 0xFFAA33u
#define CALC_COLOR_TEXT_LIGHT 0xFFFFFFu
#define CALC_COLOR_TEXT_DARK 0x000000u

#if CALC_DISPLAY_BUFFER_LEN < 2
#error "CALC_DISPLAY_BUFFER_LEN must be at least 2 (one char plus terminator)"
#endif

#if CALC_DISPLAY_BUFFER_LEN <= CALC_DISPLAY_MAX_CHARS
#error "CALC_DISPLAY_BUFFER_LEN must be greater than CALC_DISPLAY_MAX_CHARS"
#endif

enum CalcButtonKind {
    CALC_BUTTON_NUMBER = 0,
    CALC_BUTTON_OPERATOR = 1,
    CALC_BUTTON_FUNCTION = 2,
    CALC_BUTTON_EQUALS = 3
};

enum CalcOperatorActive {
    CALC_OPERATOR_NONE = 0,
    CALC_OPERATOR_DIV = 1,
    CALC_OPERATOR_MUL = 2,
    CALC_OPERATOR_SUB = 3,
    CALC_OPERATOR_ADD = 4
};

struct CalcButton {
    int x;
    int y;
    int w;
    int h;
    const char *label;
    char action;
    int kind;
    int op_active;
};

#define CALC_GRID_TOP (CALC_PADDING + CALC_DISPLAY_H + CALC_PADDING)
#define CALC_COL_STEP (CALC_BUTTON_W + CALC_GAP)
#define CALC_ROW_STEP (CALC_BUTTON_H + CALC_GAP)

static double operand_a;
static double operand_b;
static char pending_op;
static char last_op;
static double last_operand;
static char display[CALC_DISPLAY_BUFFER_LEN];
static int input_mode;
static int error;
static int operator_active;

static int calc_client_x;
static int calc_client_y;
static int calc_client_w;
static int calc_client_h;
static int calc_hover_index;

static const struct CalcButton calc_buttons[] = {
    {CALC_PADDING, CALC_GRID_TOP, CALC_BUTTON_W, CALC_BUTTON_H, "AC", 27, CALC_BUTTON_FUNCTION, CALC_OPERATOR_NONE},
    {CALC_PADDING + CALC_COL_STEP, CALC_GRID_TOP, CALC_BUTTON_W, CALC_BUTTON_H, "+/-", 's', CALC_BUTTON_FUNCTION, CALC_OPERATOR_NONE},
    {CALC_PADDING + (2 * CALC_COL_STEP), CALC_GRID_TOP, CALC_BUTTON_W, CALC_BUTTON_H, "%", '%', CALC_BUTTON_FUNCTION, CALC_OPERATOR_NONE},
    {CALC_PADDING + (3 * CALC_COL_STEP), CALC_GRID_TOP, CALC_BUTTON_W, CALC_BUTTON_H, "/", '/', CALC_BUTTON_OPERATOR, CALC_OPERATOR_DIV},

    {CALC_PADDING, CALC_GRID_TOP + CALC_ROW_STEP, CALC_BUTTON_W, CALC_BUTTON_H, "7", '7', CALC_BUTTON_NUMBER, CALC_OPERATOR_NONE},
    {CALC_PADDING + CALC_COL_STEP, CALC_GRID_TOP + CALC_ROW_STEP, CALC_BUTTON_W, CALC_BUTTON_H, "8", '8', CALC_BUTTON_NUMBER, CALC_OPERATOR_NONE},
    {CALC_PADDING + (2 * CALC_COL_STEP), CALC_GRID_TOP + CALC_ROW_STEP, CALC_BUTTON_W, CALC_BUTTON_H, "9", '9', CALC_BUTTON_NUMBER, CALC_OPERATOR_NONE},
    {CALC_PADDING + (3 * CALC_COL_STEP), CALC_GRID_TOP + CALC_ROW_STEP, CALC_BUTTON_W, CALC_BUTTON_H, "*", '*', CALC_BUTTON_OPERATOR, CALC_OPERATOR_MUL},

    {CALC_PADDING, CALC_GRID_TOP + (2 * CALC_ROW_STEP), CALC_BUTTON_W, CALC_BUTTON_H, "4", '4', CALC_BUTTON_NUMBER, CALC_OPERATOR_NONE},
    {CALC_PADDING + CALC_COL_STEP, CALC_GRID_TOP + (2 * CALC_ROW_STEP), CALC_BUTTON_W, CALC_BUTTON_H, "5", '5', CALC_BUTTON_NUMBER, CALC_OPERATOR_NONE},
    {CALC_PADDING + (2 * CALC_COL_STEP), CALC_GRID_TOP + (2 * CALC_ROW_STEP), CALC_BUTTON_W, CALC_BUTTON_H, "6", '6', CALC_BUTTON_NUMBER, CALC_OPERATOR_NONE},
    {CALC_PADDING + (3 * CALC_COL_STEP), CALC_GRID_TOP + (2 * CALC_ROW_STEP), CALC_BUTTON_W, CALC_BUTTON_H, "-", '-', CALC_BUTTON_OPERATOR, CALC_OPERATOR_SUB},

    {CALC_PADDING, CALC_GRID_TOP + (3 * CALC_ROW_STEP), CALC_BUTTON_W, CALC_BUTTON_H, "1", '1', CALC_BUTTON_NUMBER, CALC_OPERATOR_NONE},
    {CALC_PADDING + CALC_COL_STEP, CALC_GRID_TOP + (3 * CALC_ROW_STEP), CALC_BUTTON_W, CALC_BUTTON_H, "2", '2', CALC_BUTTON_NUMBER, CALC_OPERATOR_NONE},
    {CALC_PADDING + (2 * CALC_COL_STEP), CALC_GRID_TOP + (3 * CALC_ROW_STEP), CALC_BUTTON_W, CALC_BUTTON_H, "3", '3', CALC_BUTTON_NUMBER, CALC_OPERATOR_NONE},
    {CALC_PADDING + (3 * CALC_COL_STEP), CALC_GRID_TOP + (3 * CALC_ROW_STEP), CALC_BUTTON_W, CALC_BUTTON_H, "+", '+', CALC_BUTTON_OPERATOR, CALC_OPERATOR_ADD},

    {CALC_PADDING, CALC_GRID_TOP + (4 * CALC_ROW_STEP), (CALC_BUTTON_W * 2) + CALC_GAP, CALC_BUTTON_H, "0", '0', CALC_BUTTON_NUMBER, CALC_OPERATOR_NONE},
    {CALC_PADDING + (2 * CALC_COL_STEP), CALC_GRID_TOP + (4 * CALC_ROW_STEP), CALC_BUTTON_W, CALC_BUTTON_H, ".", '.', CALC_BUTTON_NUMBER, CALC_OPERATOR_NONE},
    {CALC_PADDING + (3 * CALC_COL_STEP), CALC_GRID_TOP + (4 * CALC_ROW_STEP), CALC_BUTTON_W, CALC_BUTTON_H, "=", '=', CALC_BUTTON_EQUALS, CALC_OPERATOR_NONE}
};

static int calc_is_digit(char c) {
    return c >= '0' && c <= '9';
}

static void calc_copy_string(char *dst, int dst_len, const char *src) {
    int i = 0;

    if (dst_len <= 0) {
        return;
    }

    while (src[i] != '\0' && i + 1 < dst_len) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static double calc_parse_display_value(void) {
    int i = 0;
    int negative = 0;
    double value = 0.0;
    double frac_scale = 0.1;
    int seen_dot = 0;

    if (display[0] == '-') {
        negative = 1;
        i = 1;
    }

    while (display[i] != '\0') {
        char c = display[i];
        if (c == '.') {
            seen_dot = 1;
            i++;
            continue;
        }
        if (!calc_is_digit(c)) {
            break;
        }
        if (!seen_dot) {
            value = (value * 10.0) + (double)(c - '0');
        } else {
            value += ((double)(c - '0') * frac_scale);
            frac_scale *= 0.1;
        }
        i++;
    }

    return negative ? -value : value;
}

static void calc_set_error_text(const char *text) {
    calc_copy_string(display, CALC_DISPLAY_BUFFER_LEN, text);
    error = 1;
    input_mode = 0;
    pending_op = 0;
    operator_active = CALC_OPERATOR_NONE;
}

static void calc_trim_fraction(char *buf) {
    int len = (int)ascii_strlen(buf);

    while (len > 0 && buf[len - 1] == '0') {
        buf[len - 1] = '\0';
        len--;
    }
    if (len > 0 && buf[len - 1] == '.') {
        buf[len - 1] = '\0';
    }
}

static void calc_append_u32(char *buf, int buf_len, uint32_t value) {
    char digits[10];
    int count = 0;
    int i;
    int len = (int)ascii_strlen(buf);

    if (len >= buf_len - 1) {
        return;
    }

    if (value == 0u) {
        buf[len] = '0';
        buf[len + 1] = '\0';
        return;
    }

    while (value > 0u && count < (int)(sizeof(digits) / sizeof(digits[0]))) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    for (i = count - 1; i >= 0; i--) {
        if (len >= buf_len - 1) {
            break;
        }
        buf[len++] = digits[i];
    }
    buf[len] = '\0';
}

/* 6dp is fine for now */
static int calc_double_to_str(double value, char out[CALC_DISPLAY_BUFFER_LEN]) {
    uint32_t int_part;
    double frac;
    uint32_t frac_scaled;
    int negative = 0;
    int round_up = 0;
    int int_len;

    out[0] = '\0';
    if (value < 0.0) {
        negative = 1;
        value = -value;
    }

    if (value > 4294967295.0) {
        return 0;
    }

    int_part = (uint32_t)value;
    frac = value - (double)int_part;

    if (negative) {
        calc_copy_string(out, CALC_DISPLAY_BUFFER_LEN, "-");
    }
    calc_append_u32(out, CALC_DISPLAY_BUFFER_LEN, int_part);

    frac_scaled = (uint32_t)(frac * 1000000.0 + 0.5);
    if (frac_scaled >= 1000000u) {
        frac_scaled = 0u;
        round_up = 1;
    }
    if (round_up) {
        int len = (int)ascii_strlen(out);
        int start = negative ? 1 : 0;
        int carry = 1;

        while (len > start && carry) {
            len--;
            if (out[len] == '9') {
                out[len] = '0';
            } else if (out[len] >= '0' && out[len] <= '8') {
                out[len] = (char)(out[len] + 1);
                carry = 0;
            } else {
                carry = 0;
            }
        }
        if (carry) {
            int insert_at = start;
            int i;
            int total_len = (int)ascii_strlen(out);
            if (total_len + 1 >= CALC_DISPLAY_BUFFER_LEN) {
                return 0;
            }
            for (i = total_len; i >= insert_at; i--) {
                out[i + 1] = out[i];
            }
            out[insert_at] = '1';
        }
    }

    if (frac_scaled > 0u) {
        char frac_buf[8];
        frac_buf[0] = '.';
        frac_buf[1] = (char)('0' + ((frac_scaled / 100000u) % 10u));
        frac_buf[2] = (char)('0' + ((frac_scaled / 10000u) % 10u));
        frac_buf[3] = (char)('0' + ((frac_scaled / 1000u) % 10u));
        frac_buf[4] = (char)('0' + ((frac_scaled / 100u) % 10u));
        frac_buf[5] = (char)('0' + ((frac_scaled / 10u) % 10u));
        frac_buf[6] = (char)('0' + (frac_scaled % 10u));
        frac_buf[7] = '\0';

        calc_copy_string(out + ascii_strlen(out),
                         CALC_DISPLAY_BUFFER_LEN - (int)ascii_strlen(out),
                         frac_buf);
        calc_trim_fraction(out);
    }

    int_len = (int)ascii_strlen(out);
    return int_len <= CALC_DISPLAY_MAX_CHARS;
}

static void calc_set_display_from_double(double value) {
    char next[CALC_DISPLAY_BUFFER_LEN];

    if (!calc_double_to_str(value, next)) {
        calc_set_error_text("Overflow");
        return;
    }
    calc_copy_string(display, CALC_DISPLAY_BUFFER_LEN, next);
    error = 0;
}

static int calc_operator_to_active(char op) {
    if (op == '/') return CALC_OPERATOR_DIV;
    if (op == '*') return CALC_OPERATOR_MUL;
    if (op == '-') return CALC_OPERATOR_SUB;
    if (op == '+') return CALC_OPERATOR_ADD;
    return CALC_OPERATOR_NONE;
}

static int calc_apply_operation(double a, double b, char op, double *out) {
    if (op == '+') {
        *out = a + b;
        return 1;
    }
    if (op == '-') {
        *out = a - b;
        return 1;
    }
    if (op == '*') {
        *out = a * b;
        return 1;
    }
    if (op == '/') {
        if (b == 0.0) {
            return 0;
        }
        *out = a / b;
        return 1;
    }
    return 0;
}

/* full reset, clears repeat-equals state too */
static void calc_all_clear(void) {
    operand_a = 0.0;
    operand_b = 0.0;
    pending_op = 0;
    last_op = 0;
    last_operand = 0.0;
    calc_copy_string(display, CALC_DISPLAY_BUFFER_LEN, "0");
    input_mode = 1;
    error = 0;
    operator_active = CALC_OPERATOR_NONE;
}

static void calc_clear_entry(void) {
    calc_copy_string(display, CALC_DISPLAY_BUFFER_LEN, "0");
    input_mode = 1;
    error = 0;
}

static void calc_prepare_for_numeric_input(void) {
    if (error) {
        calc_all_clear();
    }
    if (!input_mode) {
        if (pending_op == 0) {
            last_op = 0;
            last_operand = 0.0;
        }
        calc_copy_string(display, CALC_DISPLAY_BUFFER_LEN, "0");
        input_mode = 1;
    }
}

static void calc_input_digit(char digit) {
    int len;

    calc_prepare_for_numeric_input();
    len = (int)ascii_strlen(display);

    if (ascii_streq(display, "0")) {
        display[0] = digit;
        display[1] = '\0';
        return;
    }
    if (ascii_streq(display, "-0")) {
        display[1] = digit;
        display[2] = '\0';
        return;
    }
    if (len >= CALC_DISPLAY_MAX_CHARS) {
        return;
    }
    display[len] = digit;
    display[len + 1] = '\0';
}

static void calc_input_decimal_point(void) {
    int i = 0;
    int len;

    calc_prepare_for_numeric_input();
    while (display[i] != '\0') {
        if (display[i] == '.') {
            return;
        }
        i++;
    }

    len = (int)ascii_strlen(display);
    if (len >= CALC_DISPLAY_MAX_CHARS) {
        return;
    }
    display[len] = '.';
    display[len + 1] = '\0';
}

static void calc_toggle_sign(void) {
    int len;
    int i;

    if (error) {
        calc_all_clear();
    }

    if (display[0] == '-') {
        for (i = 0; display[i] != '\0'; i++) {
            display[i] = display[i + 1];
        }
        input_mode = 1;
        operand_a = calc_parse_display_value();
        return;
    }

    len = (int)ascii_strlen(display);
    if (len + 1 >= CALC_DISPLAY_BUFFER_LEN || len + 1 > CALC_DISPLAY_MAX_CHARS) {
        return;
    }
    for (i = len; i >= 0; i--) {
        display[i + 1] = display[i];
    }
    display[0] = '-';
    input_mode = 1;
    operand_a = calc_parse_display_value();
}

static void calc_backspace(void) {
    int len;

    if (error) {
        calc_all_clear();
        return;
    }

    if (!input_mode) {
        input_mode = 1;
    }

    len = (int)ascii_strlen(display);
    if (len <= 1) {
        calc_copy_string(display, CALC_DISPLAY_BUFFER_LEN, "0");
        return;
    }

    display[len - 1] = '\0';
    if (ascii_streq(display, "-")) {
        calc_copy_string(display, CALC_DISPLAY_BUFFER_LEN, "0");
    }
}

static void calc_percentage(void) {
    double current;

    if (error) {
        calc_all_clear();
    }

    current = calc_parse_display_value();
    if (pending_op != 0) {
        current = (operand_a * current) / 100.0;
    } else {
        current = current / 100.0;
    }

    calc_set_display_from_double(current);
    if (!error) {
        input_mode = 0;
    }
}

static void calc_evaluate(void) {
    double current = calc_parse_display_value();
    double result = 0.0;

    if (error) {
        return;
    }

    if (pending_op != 0) {
        operand_b = current;
        if (!calc_apply_operation(operand_a, operand_b, pending_op, &result)) {
            calc_set_error_text("Error");
            return;
        }

        operand_a = result;
        last_op = pending_op;
        last_operand = operand_b;
        pending_op = 0;
        operator_active = CALC_OPERATOR_NONE;
        calc_set_display_from_double(result);
        if (!error) {
            input_mode = 0;
        }
        return;
    }

    if (last_op != 0) {
        if (!calc_apply_operation(current, last_operand, last_op, &result)) {
            calc_set_error_text("Error");
            return;
        }
        operand_a = result;
        calc_set_display_from_double(result);
        if (!error) {
            input_mode = 0;
        }
    }
}

static void calc_set_operator(char op) {
    double current = calc_parse_display_value();
    double result = 0.0;

    if (error) {
        return;
    }

    if (pending_op != 0 && input_mode) {
        operand_b = current;
        if (!calc_apply_operation(operand_a, operand_b, pending_op, &result)) {
            calc_set_error_text("Error");
            return;
        }
        operand_a = result;
        calc_set_display_from_double(result);
        if (error) {
            return;
        }
    } else if (pending_op == 0) {
        operand_a = current;
    }

    pending_op = op;
    operator_active = calc_operator_to_active(op);
    input_mode = 0;
}

static int calc_hit_test_button(int x, int y) {
    int count = (int)(sizeof(calc_buttons) / sizeof(calc_buttons[0]));
    int i;

    for (i = 0; i < count; i++) {
        if (x >= calc_buttons[i].x && y >= calc_buttons[i].y
            && x < calc_buttons[i].x + calc_buttons[i].w && y < calc_buttons[i].y + calc_buttons[i].h) {
            return i;
        }
    }
    return -1;
}

static void calc_handle_action(char action) {
    if (action >= '0' && action <= '9') {
        calc_input_digit(action);
        return;
    }

    if (action == '.') {
        calc_input_decimal_point();
        return;
    }
    if (action == '+' || action == '-' || action == '*' || action == '/') {
        calc_set_operator(action);
        return;
    }
    if (action == '=' || action == '\n') {
        calc_evaluate();
        return;
    }
    if (action == '\b') {
        calc_backspace();
        return;
    }
    if (action == 27) {
        calc_all_clear();
        return;
    }
    if (action == '%') {
        calc_percentage();
        return;
    }
    if (action == 's') {
        calc_toggle_sign();
        return;
    }
    if (action == 'c' || action == 'C') {
        calc_clear_entry();
    }
}

static void calc_draw_button(const struct CalcButton *button, int hovered) {
    uint32_t bg = CALC_COLOR_NUMBER;
    uint32_t fg = CALC_COLOR_TEXT_LIGHT;
    int text_w = (int)ascii_strlen(button->label) * 8;
    int text_x;
    int text_y;

    if (button->kind == CALC_BUTTON_FUNCTION) {
        bg = hovered ? CALC_COLOR_FUNCTION_HOVER : CALC_COLOR_FUNCTION;
        fg = CALC_COLOR_TEXT_DARK;
    } else if (button->kind == CALC_BUTTON_OPERATOR) {
        bg = hovered ? CALC_COLOR_OPERATOR_HOVER : CALC_COLOR_OPERATOR;
        fg = CALC_COLOR_TEXT_LIGHT;
        if (operator_active == button->op_active) {
            bg = CALC_COLOR_OPERATOR_ACTIVE_BG;
            fg = CALC_COLOR_OPERATOR_ACTIVE_TEXT;
        }
    } else if (button->kind == CALC_BUTTON_EQUALS) {
        bg = hovered ? CALC_COLOR_EQUALS_HOVER : CALC_COLOR_EQUALS;
        fg = CALC_COLOR_TEXT_LIGHT;
    } else {
        bg = hovered ? CALC_COLOR_NUMBER_HOVER : CALC_COLOR_NUMBER;
        fg = CALC_COLOR_TEXT_LIGHT;
    }

    app_draw_rect(button->x, button->y, button->w, button->h, bg);
    app_draw_hline(button->x, button->y, button->w, 0x000000u);
    app_draw_hline(button->x, button->y + button->h - 1, button->w, 0x000000u);
    app_draw_vline(button->x, button->y, button->h, 0x000000u);
    app_draw_vline(button->x + button->w - 1, button->y, button->h, 0x000000u);

    text_x = button->x + (button->w - text_w) / 2;
    text_y = button->y + (button->h - 16) / 2;
    app_draw_string(text_x, text_y, button->label, fg, bg);
}

static void calc_draw_display(void) {
    int display_w = calc_client_w - (CALC_PADDING * 2);
    int text_len = (int)ascii_strlen(display);
    int text_x;
    int text_y = CALC_PADDING + ((CALC_DISPLAY_H - 16) / 2);

    if (display_w < 1) {
        return;
    }

    text_x = CALC_PADDING + display_w - CALC_PADDING - (text_len * 8);
    if (text_x < CALC_PADDING + 4) {
        text_x = CALC_PADDING + 4;
    }

    app_draw_rect(CALC_PADDING, CALC_PADDING, display_w, CALC_DISPLAY_H, CALC_COLOR_DISPLAY_BG);
    app_draw_string(text_x, text_y, display, CALC_COLOR_DISPLAY_TEXT, CALC_COLOR_DISPLAY_BG);
}

/* todo: cache button layout if this grows */
static void calc_on_draw(int win_x, int win_y, int win_w, int win_h) {
    int count = (int)(sizeof(calc_buttons) / sizeof(calc_buttons[0]));
    int i;
    int mouse_x;
    int mouse_y;

    calc_client_x = win_x;
    calc_client_y = win_y;
    calc_client_w = win_w;
    calc_client_h = win_h;

    mouse_get_pos(&mouse_x, &mouse_y);
    calc_hover_index = calc_hit_test_button(mouse_x - calc_client_x, mouse_y - calc_client_y);

    app_clear(CALC_COLOR_BG);
    calc_draw_display();

    for (i = 0; i < count; i++) {
        calc_draw_button(&calc_buttons[i], i == calc_hover_index);
    }
}

static void calc_on_key(char c) {
    calc_handle_action(c);
}

static void calc_on_click(int x, int y, int btn) {
    int index;
    int count = (int)(sizeof(calc_buttons) / sizeof(calc_buttons[0]));

    if ((btn & 0x01) == 0) {
        return;
    }

    index = calc_hit_test_button(x, y);
    if (index < 0 || index >= count) {
        return;
    }

    calc_handle_action(calc_buttons[index].action);
}

static void calc_on_init(void) {
    calc_client_x = 0;
    calc_client_y = 0;
    calc_client_w = 0;
    calc_client_h = 0;
    calc_hover_index = -1;
    calc_all_clear();
}

App calc_app = {
    .title = "Calculator",
    .x = 300,
    .y = 80,
    .w = 246,
    .h = 329,
    .bg_color = CALC_COLOR_BG,
    .on_init = calc_on_init,
    .on_draw = calc_on_draw,
    .on_key = calc_on_key,
    .on_click = calc_on_click,
    .on_close = 0
};
