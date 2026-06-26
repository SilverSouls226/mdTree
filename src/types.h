#ifndef TYPES_H
#define TYPES_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

extern const char *PIPE_STR;
extern const char *ELBOW_STR;
extern const char *TEE_STR;
extern const char *INDENT_STR;
extern const char *BULLET_POINT;

// --- Constants and Symbols ---
#define MAX_LINE_LENGTH 2048
#define MAX_HEADING_LEVEL 6
#define MAX_AWK_LEVEL 7 // Our custom -L 7 to show all text

// --- ANSI Color Codes ---
#define COLOR_RESET       "\033[0m"
#define COLOR_BOLD        "\033[1m"
#define COLOR_DIM         "\033[2m"
#define COLOR_UNDERLINE   "\033[4m"

// Foreground colors
#define COLOR_BLACK       "\033[30m"
#define COLOR_RED         "\033[31m"
#define COLOR_GREEN       "\033[32m"
#define COLOR_YELLOW      "\033[33m"
#define COLOR_BLUE        "\033[34m"
#define COLOR_MAGENTA     "\033[35m"
#define COLOR_CYAN        "\033[36m"
#define COLOR_WHITE       "\033[37m"

// Bright foreground colors
#define COLOR_BRIGHT_BLACK  "\033[90m" // Dark grey
#define COLOR_BRIGHT_RED    "\033[91m"
#define COLOR_BRIGHT_GREEN  "\033[92m"
#define COLOR_BRIGHT_YELLOW "\033[93m"
#define COLOR_BRIGHT_BLUE   "\033[94m"
#define COLOR_BRIGHT_MAGENTA "\033[95m"
#define COLOR_BRIGHT_CYAN   "\033[96m"
#define COLOR_BRIGHT_WHITE  "\033[97m" // Lighter white/grey


// --- Data Structures ---
typedef enum {
    TYPE_HEADING,
    TYPE_CONTENT,
    TYPE_UNORDERED_LIST_ITEM,
    TYPE_ORDERED_LIST_ITEM,
    TYPE_EMPTY, // For actual empty lines
    TYPE_CODE_BLOCK_CONTENT,
    TYPE_BLOCKQUOTE,
    TYPE_HORIZONTAL_RULE,
    TYPE_TASK_LIST_ITEM_UNCHECKED,
    TYPE_TASK_LIST_ITEM_CHECKED
} LineType;

typedef struct {
    LineType type;
    int level; // 1-6 for headings, 0 for content/empty, raw indentation level for lists
    char *text; // Dynamically allocated string for the line content
    int list_number; // Only used for TYPE_ORDERED_LIST_ITEM
    int original_line_num;
} ParsedLine;

typedef struct {
    int line_number;
    char message[256];
} LintWarning;

typedef struct {
    int max_level_filter;
    bool show_line_numbers;
    bool suppress_warnings;
    char *search_query;
    bool case_insensitive_search;
} Config;

#endif
