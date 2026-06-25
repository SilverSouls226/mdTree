#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <getopt.h>

// --- Constants and Symbols ---
#define MAX_LINE_LENGTH 2048
#define MAX_HEADING_LEVEL 6
#define MAX_AWK_LEVEL 7 // Our custom -L 7 to show all text

const char *PIPE_STR = "│   ";
const char *ELBOW_STR = "└── ";
const char *TEE_STR = "├── ";
const char *INDENT_STR = "    ";

// --- ANSI Color Codes ---
// Define your desired color scheme here
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
#define COLOR_BRIGHT_BLACK  "\033[90m"
#define COLOR_BRIGHT_RED    "\033[91m"
#define COLOR_BRIGHT_GREEN  "\033[92m"
#define COLOR_BRIGHT_YELLOW "\033[93m"
#define COLOR_BRIGHT_BLUE   "\033[94m"
#define COLOR_BRIGHT_MAGENTA "\033[95m"
#define COLOR_BRIGHT_CYAN   "\033[96m"
#define COLOR_BRIGHT_WHITE  "\033[97m"


// --- Data Structures ---
typedef enum {
    TYPE_HEADING,
    TYPE_CONTENT,
    TYPE_EMPTY
} LineType;

typedef struct {
    LineType type;
    int level; // 1-6 for headings, 0 for content/empty
    char *text; // Dynamically allocated string for the line content
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
void add_parsed_line(LineType type, int level, const char *text) {
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
    num_lines++;
}

// Function to free allocated memory
void cleanup() {
    for (int i = 0; i < num_lines; i++) {
        free(lines_data[i].text);
    }
    free(lines_data);
}

// --- Main Logic ---

int main(int argc, char *argv[]) {
    int max_level_filter = MAX_AWK_LEVEL; // Default to show all headings and text
    const char *md_file_path = NULL;
    int opt;

    // Use getopt for command-line options
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
            case '?': // Unknown option
                fprintf(stderr, "Error: Unknown option '-%c'.\n", optopt);
                display_help();
                return EXIT_FAILURE;
            case ':': // Missing argument for an option
                fprintf(stderr, "Error: Option '-%c' requires an argument.\n", optopt);
                display_help();
                return EXIT_FAILURE;
        }
    }

    // Remaining argument should be the Markdown file path
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

    atexit(cleanup); // Register cleanup function to free memory on exit

    char line_buffer[MAX_LINE_LENGTH];
    LineType prev_line_type = TYPE_EMPTY;
    int prev_line_idx = -1; // Index of the previous line added to lines_data

    // --- First Pass: Parse file and store data ---
    while (fgets(line_buffer, sizeof(line_buffer), fp) != NULL) {
        // Remove trailing newline character
        line_buffer[strcspn(line_buffer, "\n")] = 0;

        int len = strlen(line_buffer);

        // ATX Headings: # Heading
        if (len > 0 && line_buffer[0] == '#') {
            int level = 0;
            while (level < len && line_buffer[level] == '#') {
                level++;
            }
            if (level > 0 && level <= MAX_HEADING_LEVEL && (line_buffer[level] == ' ' || line_buffer[level] == '\0')) {
                // Valid ATX heading
                add_parsed_line(TYPE_HEADING, level, line_buffer + level + (line_buffer[level] == ' ' ? 1 : 0));
                prev_line_type = TYPE_HEADING;
                prev_line_idx = num_lines - 1;
                continue;
            }
        }

        // Setext Headings: === or ---
        if (len > 0 && (line_buffer[0] == '=' || line_buffer[0] == '-')) {
            bool is_setext = true;
            char marker = line_buffer[0];
            for (int i = 1; i < len; i++) {
                if (line_buffer[i] != marker) {
                    is_setext = false;
                    break;
                }
            }

            if (is_setext && prev_line_idx != -1 && lines_data[prev_line_idx].type == TYPE_CONTENT) {
                // We found a Setext marker and the previous line was content
                // Convert the previous CONTENT line to a HEADING
                int heading_level = (marker == '=') ? 1 : 2;
                lines_data[prev_line_idx].type = TYPE_HEADING;
                lines_data[prev_line_idx].level = heading_level;
                // Don't track this marker line itself in prev_line_idx or type
                prev_line_idx = -1;
                prev_line_type = TYPE_EMPTY; 
                continue;
            }
        }

        // Regular content or empty line
        if (len > 0) {
            add_parsed_line(TYPE_CONTENT, 0, line_buffer);
            prev_line_type = TYPE_CONTENT;
            prev_line_idx = num_lines - 1;
        } else {
            // An actual empty line. Only store if MAX_AWK_LEVEL is enabled
            if (max_level_filter == MAX_AWK_LEVEL) {
                 add_parsed_line(TYPE_EMPTY, 0, "");
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

    bool last_sibling_status[MAX_HEADING_LEVEL + 1];
    for (int i = 0; i <= MAX_HEADING_LEVEL; i++) {
        last_sibling_status[i] = false;
    }

    for (int i = 0; i < num_lines; i++) {
        ParsedLine *current_line = &lines_data[i];

        // Filter based on max_level_filter
        if (current_line->type == TYPE_HEADING && current_line->level > max_level_filter) {
            continue;
        }
        // Content/Empty lines are only shown if MAX_AWK_LEVEL is active
        if ((current_line->type == TYPE_CONTENT || current_line->type == TYPE_EMPTY) && max_level_filter != MAX_AWK_LEVEL) {
            continue;
        }
        // If it's an empty line (and not showing all content via MAX_AWK_LEVEL), skip it.
        // Or if it's an empty line but it's not actually empty in terms of text (shouldn't happen now)
        if (current_line->type == TYPE_EMPTY && strlen(current_line->text) == 0 && max_level_filter != MAX_AWK_LEVEL) {
             continue; 
        }


        char prefix[MAX_LINE_LENGTH] = "";
        int current_level_indent = current_line->level - 1;

        // Build the indentation prefix
        for (int j = 0; j < current_level_indent; j++) {
            if (last_sibling_status[j]) {
                strcat(prefix, INDENT_STR);
            } else {
                strcat(prefix, PIPE_STR);
            }
        }

        bool is_last_sibling = true;
        if (current_line->type == TYPE_HEADING) {
            for (int k = i + 1; k < num_lines; k++) {
                ParsedLine *next_line = &lines_data[k];
                // Only consider other headings when determining last sibling for a heading
                if (next_line->type == TYPE_HEADING && next_line->level <= current_line->level) {
                    is_last_sibling = false;
                    break;
                }
            }
        }
        // For content/empty lines, their 'last sibling' status for prefix building is inherited from their parent heading

        if (current_line->type == TYPE_HEADING) {
            last_sibling_status[current_level_indent] = is_last_sibling;

            if (is_last_sibling) {
                strcat(prefix, ELBOW_STR);
            } else {
                strcat(prefix, TEE_STR);
            }
            
            // Apply colors based on heading level
            switch (current_line->level) {
                case 1: printf("%s%s%s%s%s\n", prefix, COLOR_BOLD, COLOR_BRIGHT_YELLOW, current_line->text, COLOR_RESET); break;
                case 2: printf("%s%s%s%s%s\n", prefix, COLOR_BOLD, COLOR_BRIGHT_CYAN, current_line->text, COLOR_RESET); break;
                case 3: printf("%s%s%s%s%s\n", prefix, COLOR_GREEN, current_line->text, COLOR_RESET); break;
                case 4: printf("%s%s%s%s\n", prefix, COLOR_MAGENTA, current_line->text, COLOR_RESET); break;
                case 5: printf("%s%s%s%s\n", prefix, COLOR_BLUE, current_line->text, COLOR_RESET); break;
                case 6: printf("%s%s%s%s\n", prefix, COLOR_RED, current_line->text, COLOR_RESET); break;
                default: printf("%s%s\n", prefix, current_line->text); break; // Fallback
            }

            // Reset status for deeper levels
            for (int j = current_level_indent + 1; j <= MAX_HEADING_LEVEL; j++) {
                last_sibling_status[j] = false;
            }
        } else if (current_line->type == TYPE_CONTENT || current_line->type == TYPE_EMPTY) {
            int parent_heading_level = 0;
            for (int k = i - 1; k >= 0; k--) {
                ParsedLine *prev_line = &lines_data[k];
                if (prev_line->type == TYPE_HEADING) {
                    parent_heading_level = prev_line->level;
                    break;
                }
            }

            char content_prefix[MAX_LINE_LENGTH] = "";
            for (int j = 0; j < parent_heading_level; j++) {
                if (last_sibling_status[j]) {
                    strcat(content_prefix, INDENT_STR);
                } else {
                    strcat(content_prefix, PIPE_STR);
                }
            }
            strcat(content_prefix, INDENT_STR); 
            
            // Content can be just default color, or dimmed
            printf("%s%s%s%s\n", content_prefix, COLOR_DIM, current_line->text, COLOR_RESET);
        }
    }

    return EXIT_SUCCESS;
}
