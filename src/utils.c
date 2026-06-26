#include "types.h"
#include "utils.h"
#include <ctype.h>

bool g_no_color = false;
bool g_ascii_tree = false;
Stats g_stats = {0};

const char *PIPE_STR = "│   ";
const char *ELBOW_STR = "└── ";
const char *TEE_STR = "├── ";
const char *INDENT_STR = "    ";
const char *BULLET_POINT = "• ";

void display_help() {
    printf("Usage: mdtree [OPTIONS] [markdown_file | directory]\n");
    printf("\n");
    printf("Visualize the structure of Markdown files, showing headings and content\n");
    printf("similar to how 'tree' shows directories and files.\n");
    printf("\n");
    printf("Options:\n");
    printf("  -d, --depth <level>    Limit the display to a certain heading level.\n");
    printf("                         1-6: Show headings up to the specified level (e.g., -d 2 shows H1 and H2).\n");
    printf("                         7:   Show all headings and all text content (default).\n");
    printf("  -f, --find <string>    Search the file for the given string (case-sensitive) and show only matched lines and their parent headings.\n");
    printf("  -i, --case-insensitive Make the search case-insensitive when used with -f or -r.\n");
    printf("  -r, --regex <regex>    Search the file using a regular expression.\n");
    printf("  -I, --ignore <regex>   Ignore headings (and their children) matching the regex.\n");
    printf("  -H, --headings-only    Strictly show only headings (like a Table of Contents).\n");
    printf("  -c, --no-color         Disable colored output.\n");
    printf("  -a, --ascii            Use ASCII characters for tree branches instead of box-drawing characters.\n");
    printf("  -s, --stats            Print statistics about parsed files at the end.\n");
    printf("  -n, --line-numbers     Show original line numbers next to each tree item.\n");
    printf("  -w, --no-warnings      Suppress linter warnings at the end of the output.\n");
    printf("  -v, --version          Display version information.\n");
    printf("  -h, --help             Display this help message and exit.\n");
    printf("\n");
    printf("Examples:\n");
    printf("  mdtree my_document.md\n");
    printf("  mdtree -d 3 another_doc.md\n");
    printf("  mdtree -n -d 7 .\n");
}


// Function to determine the indentation level of a line (leading spaces/tabs)
// Returns the number of leading spaces. Tabs are counted as 4 spaces.
int get_raw_indentation_level(const char *line) {
    int indent = 0;
    for (int i = 0; line[i] != '\0'; i++) {
        if (line[i] == ' ') {
            indent++;
        } else if (line[i] == '\t') {
            indent += 4; // Assume tab is 4 spaces for indentation
        } else {
            break;
        }
    }
    return indent;
}

void print_formatted_text(const char *text, const char *initial_color_code, const char *reset_color_code) {
    char *current = (char *)text;
    printf("%s", initial_color_code);

    while (*current != '\0') {
        if (*current == '\\' && *(current + 1) != '\0') {
            printf("%c", *(current + 1));
            current += 2;
        } else if (*current == '`') {
            char *end = strchr(current + 1, '`');
            if (end) {
                *end = '\0';
                printf("%s%s%s", COLOR_BRIGHT_CYAN, current + 1, initial_color_code);
                *end = '`';
                current = end + 1;
            } else {
                printf("`");
                current++;
            }
        } else if (*current == '!' && *(current + 1) == '[') {
            // Image link ![alt](url)
            char *close_bracket = strchr(current + 2, ']');
            if (close_bracket && *(close_bracket + 1) == '(') {
                char *close_paren = strchr(close_bracket + 2, ')');
                if (close_paren) {
                    *close_bracket = '\0';
                    *close_paren = '\0';
                    char *alt_text = current + 2;
                    char *url = close_bracket + 2;
                    
                    printf("\033]8;;%s\033\\", url);
                    char new_color[128];
                    snprintf(new_color, sizeof(new_color), "%s%s", initial_color_code, COLOR_DIM);
                    printf("%s", new_color);
                    printf("🖼️ ");
                    print_formatted_text(alt_text, new_color, "");
                    printf("\033[22m%s", initial_color_code); // reset dim
                    printf("\033]8;;\033\\");
                    
                    *close_bracket = ']';
                    *close_paren = ')';
                    current = close_paren + 1;
                    continue;
                }
            }
            printf("!");
            current++;
        } else if (*current == '[') {
            // Standard link [text](url)
            char *close_bracket = strchr(current + 1, ']');
            if (close_bracket && *(close_bracket + 1) == '(') {
                char *close_paren = strchr(close_bracket + 2, ')');
                if (close_paren) {
                    *close_bracket = '\0';
                    *close_paren = '\0';
                    char *link_text = current + 1;
                    char *url = close_bracket + 2;
                    
                    printf("\033]8;;%s\033\\", url);
                    char new_color[128];
                    snprintf(new_color, sizeof(new_color), "%s\033[4m%s", initial_color_code, COLOR_BLUE);
                    print_formatted_text(link_text, new_color, "");
                    printf("\033[24m%s", initial_color_code); // 24m resets underline
                    printf("\033]8;;\033\\");
                    
                    *close_bracket = ']';
                    *close_paren = ')';
                    current = close_paren + 1;
                    continue;
                }
            }
            printf("[");
            current++;
        } else if (strncmp(current, "**", 2) == 0) {
            char *end = strstr(current + 2, "**");
            if (end) {
                *end = '\0';
                char new_color[128];
                snprintf(new_color, sizeof(new_color), "%s%s", initial_color_code, COLOR_BOLD);
                print_formatted_text(current + 2, new_color, "");
                *end = '*';
                printf("\033[22m%s", initial_color_code); // 22m resets bold/dim
                current = end + 2;
            } else {
                printf("**");
                current += 2;
            }
        } else if (*current == '~') {
            int num_tildes = (*(current + 1) == '~') ? 2 : 1;
            char *search_str = (num_tildes == 2) ? "~~" : "~";
            char *end = strstr(current + num_tildes, search_str);
            if (end) {
                *end = '\0';
                char new_color[128];
                snprintf(new_color, sizeof(new_color), "%s\033[9m", initial_color_code);
                print_formatted_text(current + num_tildes, new_color, "");
                *end = '~';
                printf("\033[29m%s", initial_color_code); // 29m resets strikethrough
                current = end + num_tildes;
            } else {
                printf("%c", *current);
                if (num_tildes == 2) {
                    printf("%c", *(current + 1));
                }
                current += num_tildes;
            }
        } else if (*current == '*' || *current == '_') {
            char marker = *current;
            char *end = current + 1;
            bool found = false;
            while (*end != '\0') {
                if (*end == marker) {
                    // skip double markers
                    if (marker == '*' && *(end + 1) == '*') {
                        end += 2;
                        continue;
                    }
                    found = true;
                    break;
                }
                if (*end == '\\' && *(end + 1) != '\0') end++; // skip escaped
                end++;
            }
            if (found) {
                *end = '\0';
                char new_color[128];
                snprintf(new_color, sizeof(new_color), "%s\033[3m", initial_color_code);
                print_formatted_text(current + 1, new_color, "");
                *end = marker;
                printf("\033[23m%s", initial_color_code); // 23m resets italic
                current = end + 1;
            } else {
                printf("%c", marker);
                current++;
            }
        } else {
            printf("%c", *current);
            current++;
        }
    }
    printf("%s", reset_color_code);
}

bool find_substring_case_insensitive(const char *haystack, const char *needle) {
    if (!*needle) return true;
    for (; *haystack; ++haystack) {
        if (tolower((unsigned char)*haystack) == tolower((unsigned char)*needle)) {
            const char *h, *n;
            for (h = haystack, n = needle; *h && *n; ++h, ++n) {
                if (tolower((unsigned char)*h) != tolower((unsigned char)*n)) break;
            }
            if (!*n) return true; // Matched entire needle
        }
    }
    return false;
}

void apply_config(Config *config) {
    g_no_color = config->no_color;
    g_ascii_tree = config->ascii_tree;
    if (g_ascii_tree) {
        PIPE_STR = "|   ";
        ELBOW_STR = "`-- ";
        TEE_STR = "|-- ";
        BULLET_POINT = "* ";
    }
}

int count_words(const char *text) {
    int count = 0;
    bool in_word = false;
    while (*text) {
        if (isspace((unsigned char)*text)) {
            in_word = false;
        } else if (!in_word) {
            in_word = true;
            count++;
        }
        text++;
    }
    return count;
}
