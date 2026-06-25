#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <getopt.h>
#include <ctype.h> // For isdigit()

// --- Constants and Symbols ---
#define MAX_LINE_LENGTH 2048
#define MAX_HEADING_LEVEL 6
#define MAX_AWK_LEVEL 7 // Our custom -L 7 to show all text

const char *PIPE_STR = "│   ";
const char *ELBOW_STR = "└── ";
const char *TEE_STR = "├── ";
const char *INDENT_STR = "    ";

// Unicode bullet point character
const char *BULLET_POINT = "• "; // Note the space after for formatting

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
    TYPE_EMPTY // For actual empty lines
} LineType;

typedef struct {
    LineType type;
    int level; // 1-6 for headings, 0 for content/empty, raw indentation level for lists
    char *text; // Dynamically allocated string for the line content
    int list_number; // Only used for TYPE_ORDERED_LIST_ITEM
} ParsedLine;

// Dynamic array for ParsedLine structs
ParsedLine *lines_data = NULL;
int num_lines = 0;
int capacity_lines = 0;

// --- Helper Functions ---

void display_help() {
    printf("Usage: mdtree [OPTIONS] <markdown_file>\n");
    printf("\n");
    printf("Visualize the structure of a Markdown file, showing headings and content\n");
    printf("similar to how 'tree' shows directories and files.\n");
    printf("\n");
    printf("Options:\n");
    printf("  -L <level>    Limit the display to a certain heading level.\n");
    printf("                1-6: Show headings up to the specified level (e.g., -L 2 shows H1 and H2).\n");
    printf("                7:   Show all headings and all text content (default).\n");
    printf("  -h, --help    Display this help message and exit.\n");
    printf("\n");
    printf("Examples:\n");
    printf("  mdtree my_document.md\n");
    printf("  mdtree -L 3 another_doc.md\n");
    printf("  mdtree -L 7 all_content.md\n");
}

// Function to add a parsed line to our dynamic array
void add_parsed_line(LineType type, int level, const char *text, int list_number) {
    if (num_lines >= capacity_lines) {
        capacity_lines = (capacity_lines == 0) ? 100 : capacity_lines * 2;
        ParsedLine *new_lines_data = realloc(lines_data, capacity_lines * sizeof(ParsedLine));
        if (new_lines_data == NULL) {
            perror("Failed to reallocate memory for lines_data");
            exit(EXIT_FAILURE);
        }
        lines_data = new_lines_data;
    }
    lines_data[num_lines].type = type;
    lines_data[num_lines].level = level;
    lines_data[num_lines].text = strdup(text); // Duplicate the string to own it
    if (lines_data[num_lines].text == NULL) {
        perror("Failed to duplicate string for parsed line");
        exit(EXIT_FAILURE);
    }
    lines_data[num_lines].list_number = list_number;
    num_lines++;
}

// Function to free allocated memory
void cleanup() {
    for (int i = 0; i < num_lines; i++) {
        free(lines_data[i].text);
    }
    free(lines_data);
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

// Function to print text, applying bold formatting for **text**
void print_formatted_text(const char *text, const char *initial_color_code, const char *reset_color_code) {
    char *current = (char *)text;
    char *bold_start;
    char *bold_end;

    printf("%s", initial_color_code); // Apply initial color for the whole line

    while (*current != '\0') {
        bold_start = strstr(current, "**");
        if (bold_start == NULL) {
            // No more bold markers, print the rest of the string
            printf("%s", current);
            break;
        }

        // Print text before the bold marker
        *bold_start = '\0'; // Temporarily null-terminate to print segment
        printf("%s", current);
        *bold_start = '*'; // Restore for next search if needed (though not strictly necessary here)

        bold_end = strstr(bold_start + 2, "**");
        if (bold_end == NULL) {
            // Unmatched bold marker, print the rest as regular text
            printf("%s", bold_start);
            break;
        }

        // Print bold text
        *bold_end = '\0'; // Temporarily null-terminate for bold segment
        printf("%s%s%s%s", COLOR_BOLD, bold_start + 2, initial_color_code, bold_end); // Bold, then restore initial color
        *bold_end = '*'; // Restore

        current = bold_end + 2;
    }
    printf("%s", reset_color_code); // Ensure reset at the end
}


// --- Main Logic ---

int main(int argc, char *argv[]) {
    int max_level_filter = MAX_AWK_LEVEL;
    const char *md_file_path = NULL;
    int opt;

    while ((opt = getopt(argc, argv, "L:h")) != -1) {
        switch (opt) {
            case 'L':
                max_level_filter = atoi(optarg);
                if (max_level_filter < 1 || max_level_filter > MAX_AWK_LEVEL) {
                    fprintf(stderr, "Error: Invalid level for -L. Must be between 1 and %d.\n", MAX_AWK_LEVEL);
                    display_help();
                    return EXIT_FAILURE;
                }
                break;
            case 'h':
                display_help();
                return EXIT_SUCCESS;
            case '?':
                fprintf(stderr, "Error: Unknown option '-%c'.\n", optopt);
                display_help();
                return EXIT_FAILURE;
            case ':':
                fprintf(stderr, "Error: Option '-%c' requires an argument.\n", optopt);
                display_help();
                return EXIT_FAILURE;
        }
    }

    if (optind < argc) {
        md_file_path = argv[optind];
    } else {
        fprintf(stderr, "Error: No Markdown file specified.\n");
        display_help();
        return EXIT_FAILURE;
    }

    FILE *fp = fopen(md_file_path, "r");
    if (fp == NULL) {
        perror("Error opening Markdown file");
        return EXIT_FAILURE;
    }

    atexit(cleanup);

    char line_buffer[MAX_LINE_LENGTH];
    LineType prev_line_type = TYPE_EMPTY;
    int prev_line_idx = -1;

    // --- First Pass: Parse file and store data ---
    while (fgets(line_buffer, sizeof(line_buffer), fp) != NULL) {
        line_buffer[strcspn(line_buffer, "\n")] = 0;
        int len = strlen(line_buffer);
        
        int raw_current_indent = get_raw_indentation_level(line_buffer);
        char *trimmed_line = line_buffer + raw_current_indent; // Pointer to the actual text after initial indent

        // ATX Headings
        if (len > 0 && trimmed_line[0] == '#') {
            int level = 0;
            while (level < strlen(trimmed_line) && trimmed_line[level] == '#') {
                level++;
            }
            if (level > 0 && level <= MAX_HEADING_LEVEL && (trimmed_line[level] == ' ' || trimmed_line[level] == '\0')) {
                add_parsed_line(TYPE_HEADING, level, trimmed_line + level + (trimmed_line[level] == ' ' ? 1 : 0), 0);
                prev_line_type = TYPE_HEADING;
                prev_line_idx = num_lines - 1;
                continue;
            }
        }

        // Setext Headings
        if (len > 0 && (trimmed_line[0] == '=' || trimmed_line[0] == '-')) {
            bool is_setext = true;
            char marker = trimmed_line[0];
            for (int i = 1; i < strlen(trimmed_line); i++) {
                if (trimmed_line[i] != marker) {
                    is_setext = false;
                    break;
                }
            }
            if (is_setext && prev_line_idx != -1 && lines_data[prev_line_idx].type == TYPE_CONTENT) {
                int heading_level = (marker == '=') ? 1 : 2;
                lines_data[prev_line_idx].type = TYPE_HEADING;
                lines_data[prev_line_idx].level = heading_level;
                prev_line_idx = -1;
                prev_line_type = TYPE_EMPTY; 
                continue;
            }
        }

        // Unordered List Items: *, -, or + followed by a space
        if (len > 0 && (trimmed_line[0] == '*' || trimmed_line[0] == '-' || trimmed_line[0] == '+') &&
            (strlen(trimmed_line) > 1 && trimmed_line[1] == ' ')) {
            add_parsed_line(TYPE_UNORDERED_LIST_ITEM, raw_current_indent, trimmed_line + 2, 0); // Store raw indent
            prev_line_type = TYPE_UNORDERED_LIST_ITEM;
            prev_line_idx = num_lines - 1;
            continue;
        }

        // Ordered List Items: Number followed by . and a space
        if (len > 0 && isdigit(trimmed_line[0])) {
            int i = 0;
            int list_num = 0;
            while (i < strlen(trimmed_line) && isdigit(trimmed_line[i])) {
                list_num = list_num * 10 + (trimmed_line[i] - '0');
                i++;
            }
            if (i > 0 && i < strlen(trimmed_line) && trimmed_line[i] == '.' && 
                (strlen(trimmed_line) > i + 1 && trimmed_line[i+1] == ' ')) {
                add_parsed_line(TYPE_ORDERED_LIST_ITEM, raw_current_indent, trimmed_line + i + 2, list_num); // Store raw indent
                prev_line_type = TYPE_ORDERED_LIST_ITEM;
                prev_line_idx = num_lines - 1;
                continue;
            }
        }

        // Check if trimmed_line is actually empty
        bool is_empty = true;
        for (int i = 0; trimmed_line[i] != '\0'; i++) {
            if (!isspace((unsigned char)trimmed_line[i])) {
                is_empty = false;
                break;
            }
        }

        // Regular content or empty line
        if (!is_empty) {
            add_parsed_line(TYPE_CONTENT, raw_current_indent, line_buffer, 0); // Store content with its raw indent
            prev_line_type = TYPE_CONTENT;
            prev_line_idx = num_lines - 1;
        } else {
            if (max_level_filter == MAX_AWK_LEVEL) {
                 add_parsed_line(TYPE_EMPTY, 0, "", 0);
                 prev_line_type = TYPE_EMPTY;
                 prev_line_idx = num_lines - 1;
            } else {
                 prev_line_type = TYPE_EMPTY; 
                 prev_line_idx = -1;
            }
        }
    }
    fclose(fp);

    // --- Second Pass: Print formatted output ---

    // last_sibling_status[level] is true if the item at 'level' is the last sibling,
    // meaning subsequent items at higher levels (deeper indent) should use 'INDENT_STR' instead of 'PIPE_STR'
    bool last_sibling_status[MAX_HEADING_LEVEL + 1]; 
    for (int i = 0; i <= MAX_HEADING_LEVEL; i++) {
        last_sibling_status[i] = false;
    }
    
    for (int i = 0; i < num_lines; i++) {
        ParsedLine *current_line = &lines_data[i];

        char prefix[MAX_LINE_LENGTH] = "";
        int current_logical_level = 0; // Represents the indentation level we are currently at (0-based)
        bool is_last_sibling_in_current_scope = true;

        // Filtering logic
        if (current_line->type == TYPE_HEADING && current_line->level > max_level_filter) {
            continue;
        }
        if ((current_line->type != TYPE_HEADING) && max_level_filter != MAX_AWK_LEVEL) {
            continue;
        }
        if (current_line->type == TYPE_EMPTY && strlen(current_line->text) == 0 && max_level_filter != MAX_AWK_LEVEL) {
             continue; 
        }

        if (current_line->type == TYPE_HEADING) {
            current_logical_level = current_line->level - 1;

            // Determine if current heading is the last sibling at its level
            is_last_sibling_in_current_scope = true;
            for (int k = i + 1; k < num_lines; k++) {
                ParsedLine *next_line = &lines_data[k];
                if (next_line->type == TYPE_HEADING) {
                    if (next_line->level == current_line->level) {
                        is_last_sibling_in_current_scope = false;
                        break;
                    } else if (next_line->level < current_line->level) {
                        break; // Scope ended, so it is the last sibling
                    }
                }
            }
            
            // Build the prefix based on parent heading status
            for (int j = 0; j < current_logical_level; j++) {
                if (last_sibling_status[j]) {
                    strcat(prefix, INDENT_STR);
                } else {
                    strcat(prefix, PIPE_STR);
                }
            }

            // Append current item's tree symbol
            if (is_last_sibling_in_current_scope) {
                strcat(prefix, ELBOW_STR);
            } else {
                strcat(prefix, TEE_STR);
            }
            
            printf("%s", prefix);
            // Apply colors based on heading level
            switch (current_line->level) {
                case 1: print_formatted_text(current_line->text, COLOR_BOLD COLOR_BRIGHT_YELLOW, COLOR_RESET); break;
                case 2: print_formatted_text(current_line->text, COLOR_BOLD COLOR_BRIGHT_CYAN, COLOR_RESET); break;
                case 3: print_formatted_text(current_line->text, COLOR_GREEN, COLOR_RESET); break;
                case 4: print_formatted_text(current_line->text, COLOR_MAGENTA, COLOR_RESET); break;
                case 5: print_formatted_text(current_line->text, COLOR_BLUE, COLOR_RESET); break;
                case 6: print_formatted_text(current_line->text, COLOR_RED, COLOR_RESET); break;
                default: print_formatted_text(current_line->text, COLOR_RESET, COLOR_RESET); break;
            }
            printf("\n");

            // Update last_sibling_status for the current level and reset deeper levels
            last_sibling_status[current_logical_level] = is_last_sibling_in_current_scope;
            for (int j = current_logical_level + 1; j <= MAX_HEADING_LEVEL; j++) {
                last_sibling_status[j] = false;
            }

        } else { // TYPE_UNORDERED_LIST_ITEM, TYPE_ORDERED_LIST_ITEM, TYPE_CONTENT, TYPE_EMPTY
            // Find the closest preceding heading's level
            int parent_heading_level = 0;
            for (int k = i - 1; k >= 0; k--) {
                ParsedLine *prev_line = &lines_data[k];
                if (prev_line->type == TYPE_HEADING) {
                    parent_heading_level = prev_line->level;
                    break;
                }
            }
            current_logical_level = parent_heading_level; // Base level for indentation

            if (current_line->type == TYPE_EMPTY) {
                // Ignore empty lines to prevent dangling lines extending
                continue;
            }

            // Build base prefix based on parent heading status
            for (int j = 0; j < current_logical_level; j++) {
                if (last_sibling_status[j]) {
                    strcat(prefix, INDENT_STR);
                } else {
                    strcat(prefix, PIPE_STR);
                }
            }

            // Check if this item is the last non-empty child in its parent scope
            bool is_last_child = true;
            for (int k = i + 1; k < num_lines; k++) {
                ParsedLine *next_line = &lines_data[k];
                if (next_line->type == TYPE_HEADING && next_line->level <= parent_heading_level) {
                    break; // Reached end of parent scope
                }
                if (next_line->type != TYPE_EMPTY) {
                    // There is another item in this scope, so we are not the last
                    is_last_child = false;
                    break;
                }
            }

            // Add list indentation
            int current_raw_indent_blocks = current_line->level / 4;
            
            if (current_line->type == TYPE_CONTENT) {
                if (is_last_child) {
                    strcat(prefix, ELBOW_STR);
                } else {
                    strcat(prefix, TEE_STR);
                }
                // Add list item indentation spacing if content is nested inside list
                for (int j = 0; j < current_raw_indent_blocks; j++) {
                    strcat(prefix, INDENT_STR);
                }
                printf("%s", prefix); 
                print_formatted_text(current_line->text, COLOR_BRIGHT_WHITE, COLOR_RESET);
                printf("\n");
            } else if (current_line->type == TYPE_UNORDERED_LIST_ITEM) {
                // List items have no tree connectors, but need the vertical line if not the last child
                if (is_last_child) {
                    strcat(prefix, INDENT_STR);
                } else {
                    strcat(prefix, PIPE_STR);
                }
                for (int j = 0; j < current_raw_indent_blocks; j++) {
                    strcat(prefix, INDENT_STR);
                }
                printf("%s%s", prefix, BULLET_POINT); 
                print_formatted_text(current_line->text, COLOR_BRIGHT_WHITE, COLOR_RESET); 
                printf("\n");
            } else if (current_line->type == TYPE_ORDERED_LIST_ITEM) {
                if (is_last_child) {
                    strcat(prefix, INDENT_STR);
                } else {
                    strcat(prefix, PIPE_STR);
                }
                for (int j = 0; j < current_raw_indent_blocks; j++) {
                    strcat(prefix, INDENT_STR);
                }
                printf("%s%s%d.%s ", prefix, COLOR_BRIGHT_BLUE, current_line->list_number, COLOR_RESET); 
                print_formatted_text(current_line->text, COLOR_BRIGHT_WHITE, COLOR_RESET); 
                printf("\n");
            }
            
            // Update last_sibling_status for content/list items:
            // This needs to mark the level *after* the parent heading.
            // Simplified for now: if this item is the "last_sibling_in_current_scope",
            // then the line for its immediate parent should stop.
            // The `last_sibling_status` array should track if the *vertical line* at that depth should continue.
            // This is implicitly handled by how `prefix` is built.
            // The `last_sibling_status` array is more strictly for heading levels.
            // We need a more granular way to track branch endings for content/list blocks.
            // For now, let's just ensure headings' status is handled correctly.
        }
    }

    return EXIT_SUCCESS;
}
