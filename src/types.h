#ifndef TYPES_H
#define TYPES_H

#include <iostream>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cstdlib>

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
extern bool g_no_color;
extern bool g_ascii_tree;

#define COLOR_RESET       (g_no_color ? "" : "\033[0m")
#define COLOR_BOLD        (g_no_color ? "" : "\033[1m")
#define COLOR_DIM         (g_no_color ? "" : "\033[2m")
#define COLOR_UNDERLINE   (g_no_color ? "" : "\033[4m")

// Foreground colors
#define COLOR_BLACK       (g_no_color ? "" : "\033[30m")
#define COLOR_RED         (g_no_color ? "" : "\033[31m")
#define COLOR_GREEN       (g_no_color ? "" : "\033[32m")
#define COLOR_YELLOW      (g_no_color ? "" : "\033[33m")
#define COLOR_BLUE        (g_no_color ? "" : "\033[34m")
#define COLOR_MAGENTA     (g_no_color ? "" : "\033[35m")
#define COLOR_CYAN        (g_no_color ? "" : "\033[36m")
#define COLOR_WHITE       (g_no_color ? "" : "\033[37m")

// Bright foreground colors
#define COLOR_BRIGHT_BLACK  (g_no_color ? "" : "\033[90m") // Dark grey
#define COLOR_BRIGHT_RED    (g_no_color ? "" : "\033[91m")
#define COLOR_BRIGHT_GREEN  (g_no_color ? "" : "\033[92m")
#define COLOR_BRIGHT_YELLOW (g_no_color ? "" : "\033[93m")
#define COLOR_BRIGHT_BLUE   (g_no_color ? "" : "\033[94m")
#define COLOR_BRIGHT_MAGENTA (g_no_color ? "" : "\033[95m")
#define COLOR_BRIGHT_CYAN   (g_no_color ? "" : "\033[96m")
#define COLOR_BRIGHT_WHITE  (g_no_color ? "" : "\033[97m") // Lighter white/grey

#define COLOR_BOLD_BRIGHT_YELLOW (g_no_color ? "" : "\033[1m\033[93m")
#define COLOR_BOLD_BRIGHT_CYAN   (g_no_color ? "" : "\033[1m\033[96m")
#define VLINE_STR (g_ascii_tree ? "|" : "│")
#define HLINE_STR (g_ascii_tree ? "------------------------------" : "──────────────────────────────")


// --- Data Structures ---

struct Stats {
    int files_parsed = 0;
    int headings = 0;
    int lists = 0;
    int words = 0;
    int characters = 0;
    int code_blocks = 0;
    int completed_tasks = 0;
    int incomplete_tasks = 0;
    int links = 0;
    int images = 0;
    int empty_lines = 0;
    int non_empty_lines = 0;
    int tables = 0;
    int blockquotes = 0;
};

extern Stats g_stats;

enum LineType {
    TYPE_HEADING,
    TYPE_CONTENT,
    TYPE_UNORDERED_LIST_ITEM,
    TYPE_ORDERED_LIST_ITEM,
    TYPE_EMPTY, // For actual empty lines
    TYPE_CODE_BLOCK_CONTENT,
    TYPE_BLOCKQUOTE,
    TYPE_HORIZONTAL_RULE,
    TYPE_TASK_LIST_ITEM_UNCHECKED,
    TYPE_TASK_LIST_ITEM_CHECKED,
    TYPE_TABLE_CONTENT
};

struct ParsedLine {
    LineType type;
    int level; // 1-6 for headings, 0 for content/empty, raw indentation level for lists
    std::string text;
    int list_number; // Only used for TYPE_ORDERED_LIST_ITEM
    int original_line_num;
};

struct LintWarning {
    int line_number;
    std::string message;
};

struct Config {
    int max_level_filter = 0;
    bool show_line_numbers = false;
    bool suppress_warnings = false;
    std::string search_query = "";
    bool case_insensitive_search = false;
    bool use_regex = false;
    bool no_color = false;
    bool ascii_tree = false;
    bool show_stats = false;
    bool headings_only = false;
    std::string ignore_query = "";
};

#endif
