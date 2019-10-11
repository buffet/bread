/* Copyright (c), Niclas Meyer <niclas@countingsort.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "bread.h"

#include <memory.h>
#include <stdbool.h>
#include <stdlib.h>

#include <termios.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) - 0x60)

enum key {
    KEY_LOWEST = 256,
    KEY_NOKEY,
    KEY_UP,
    KEY_DOWN,
    KEY_RIGHT,
    KEY_LEFT,
};

struct buffer {
    char *start;
    char *gap;
    char *post;
    char *end;
    int relpos; // assumed to be vaild at all times
};

static bool
buffer_init(struct buffer *b, size_t init_size)
{
    b->start = malloc(init_size);
    if (!b->start) {
        return false;
    }

    b->gap    = b->start;
    b->end    = b->start + init_size;
    b->post   = b->end;
    b->relpos = 0;

    return true;
}

static void
buffer_forwards(struct buffer *b)
{
    if (b->post + b->relpos < b->end) {
        ++b->relpos;
    }
}

static void
buffer_backwards(struct buffer *b)
{
    if (b->gap + b->relpos > b->start) {
        --b->relpos;
    }
}

static void
buffer_move_gap(struct buffer *b)
{
    if (b->relpos == 0) {
        return;
    }

    if (b->relpos < 0) {
        b->gap += b->relpos;
        b->post += b->relpos;
        memmove(b->post, b->gap, -b->relpos);
    } else {
        memmove(b->gap, b->post, b->relpos);
        b->gap += b->relpos;
        b->post += b->relpos;
    }

    b->relpos = 0;
}

static bool
buffer_insertch(struct buffer *b, char ch)
{
    buffer_move_gap(b);

    if (b->gap == b->post) {
        size_t newsize = (b->end - b->start) * 2;
        char *newbuf   = realloc(b->start, newsize);

        if (!newbuf) {
            return false;
        }

        b->gap   = newbuf + (b->gap - b->start);
        b->post  = newbuf + newsize + (b->end - b->post);
        b->start = newbuf;
        b->end   = newbuf + newsize;
    }

    *b->gap++ = ch;

    return true;
}

static void
buffer_delete_forwards(struct buffer *b)
{
    buffer_move_gap(b);

    if (b->post < b->end) {
        ++b->post;
    }
}

static void
buffer_delete_backwards(struct buffer *b)
{
    buffer_move_gap(b);

    if (b->gap > b->start) {
        --b->gap;
    }
}

static bool
setup_terminal(struct termios *old_termios)
{
    if (tcgetattr(STDIN_FILENO, old_termios) < 0) {
        return false;
    }

    struct termios raw = *old_termios;
    raw.c_oflag &= OPOST;
    raw.c_lflag &= ~(ECHO | ICANON);

    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) < 0) {
        return false;
    }

    return true;
}

static bool
restore_terminal(struct termios *old_termios)
{
    if (tcsetattr(STDIN_FILENO, TCSANOW, old_termios) < 0) {
        return false;
    }

    return true;
}

static int
getkey(void)
{
    char c[3];
    int n = read(STDIN_FILENO, c, sizeof(c));

    if (n < 0) {
        return KEY_NOKEY;
    }

    if (n < sizeof(c)) {
        return c[0];
    }

    if (!(c[0] == '\033' && c[1] == '[')) {
        return c[0];
    }

    switch (c[2]) {
    case 'A':
        return KEY_UP;
    case 'B':
        return KEY_DOWN;
    case 'C':
        return KEY_RIGHT;
    case 'D':
        return KEY_LEFT;
    default:
        return KEY_NOKEY;
    }
}

char *
bread_line(const char *prompt)
{
    printf("%s", prompt);
    fflush(stdout);

    size_t prompt_len = 0;
    bool counting     = true;

    for (const char *c = prompt; *c; ++c) {
        if (*c == '\001') {
            counting = false;
        } else if (*c == '\002') {
            counting = true;
        } else {
            if (counting) {
                ++prompt_len;
            }
        }
    }

    struct termios old_termios;
    if (!setup_terminal(&old_termios)) {
        return NULL;
    }

    struct buffer buffer;
    if (!buffer_init(&buffer, BREAD_INITIAL_SIZE)) {
        return NULL;
    }

    for (;;) {
        int k = getkey();

        if (k == '\n') {
            printf("\r\n");
            break;
        }

        switch (k) {
        case KEY_NOKEY:
            // EMPTY
            break;
        case CTRL_KEY('b'):
        case KEY_LEFT:
            buffer_backwards(&buffer);
            break;
        case CTRL_KEY('f'):
        case KEY_RIGHT:
            buffer_forwards(&buffer);
            break;
        case KEY_UP:
        case CTRL_KEY('a'):
            buffer.relpos = buffer.start - buffer.gap - buffer.relpos;
            break;
        case KEY_DOWN:
        case CTRL_KEY('e'):
            buffer.relpos = buffer.end - buffer.post;
            break;
        case CTRL_KEY('d'):
            buffer_delete_forwards(&buffer);
            break;
        case CTRL_KEY('u'):
            buffer.gap  = buffer.start;
            buffer.post = buffer.end;
            break;
        case CTRL_KEY('h'):
        case '\x7f':
            buffer_delete_backwards(&buffer);
            break;
        default:
            if (!buffer_insertch(&buffer, k)) {
                free(buffer.start);
                return NULL;
            }
        }

        size_t postsize = buffer.end - buffer.post;

        printf(
            "\r\033[%luC\033[K%.*s%.*s",
            prompt_len,
            (int)(buffer.gap - buffer.start),
            buffer.start,
            (int)postsize,
            buffer.post);

        if (postsize - buffer.relpos > 0) {
            printf("\033[%luD", postsize - buffer.relpos);
        }
        fflush(stdout);
    }

    size_t presize  = buffer.gap - buffer.start;
    size_t postsize = buffer.end - buffer.post;
    size_t size     = presize + postsize;

    char *line = malloc(size + 1);
    if (!line) {
        free(buffer.start);
        return NULL;
    }

    memcpy(line, buffer.start, presize);
    memcpy(line + presize, buffer.post, postsize);

    line[size] = '\0';

    restore_terminal(&old_termios);

    return line;
}
