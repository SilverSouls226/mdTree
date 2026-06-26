#include "parser.h"
#include "utils.h"
#include <ctype.h>

ParsedLine *lines_data = NULL;
int num_lines = 0;
int capacity_lines = 0;

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

void cleanup() {
    for (int i = 0; i < num_lines; i++) {
        free(lines_data[i].text);
    }
    free(lines_data);
    lines_data = NULL;
    num_lines = 0;
    capacity_lines = 0;
}

void process_markdown_file(const char *md_file_path, const char *global_prefix, int max_level_filter) {
    FILE *fp = fopen(md_file_path, "r");
    if (fp == NULL) {
        perror("Error opening Markdown file");
        return;
    }


    char line_buffer[MAX_LINE_LENGTH];
    LineType prev_line_type = TYPE_EMPTY;
    int prev_line_idx = -1;
    bool in_code_block = false;

    // --- First Pass: Parse file and store data ---
    while (fgets(line_buffer, sizeof(line_buffer), fp) != NULL) {
        line_buffer[strcspn(line_buffer, "\n")] = 0;
        int len = strlen(line_buffer);
        
        int raw_current_indent = get_raw_indentation_level(line_buffer);
        char *trimmed_line = line_buffer + raw_current_indent; // Pointer to the actual text after initial indent

        // Check for code block fences
        if (strncmp(trimmed_line, "```", 3) == 0) {
            in_code_block = !in_code_block;
            if (in_code_block) {
                // Starting a new code block, add an empty ParsedLine for it
                add_parsed_line(TYPE_CODE_BLOCK_CONTENT, raw_current_indent, "", 0);
            }
            continue; // Do not render the backticks themselves
        }

        if (in_code_block) {
            ParsedLine *last_line = &lines_data[num_lines - 1];
            int old_len = strlen(last_line->text);
            int new_len = old_len + strlen(line_buffer) + 2; // +1 for \n, +1 for \0
            char *new_text = malloc(new_len);
            strcpy(new_text, last_line->text);
            if (old_len > 0) {
                strcat(new_text, "\n");
            }
            strcat(new_text, line_buffer);
            free(last_line->text);
            last_line->text = new_text;
            continue;
        }

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

        // Setext Headings and Horizontal Rules
        if (len > 0 && (trimmed_line[0] == '=' || trimmed_line[0] == '-' || trimmed_line[0] == '*' || trimmed_line[0] == '_')) {
            bool is_uniform = true;
            char marker = trimmed_line[0];
            int count = 1;
            for (int i = 1; i < strlen(trimmed_line); i++) {
                if (trimmed_line[i] != ' ' && trimmed_line[i] != marker) {
                    is_uniform = false;
                    break;
                }
                if (trimmed_line[i] == marker) count++;
            }
            
            if (is_uniform) {
                if ((marker == '=' || marker == '-') && prev_line_idx != -1 && lines_data[prev_line_idx].type == TYPE_CONTENT) {
                    int heading_level = (marker == '=') ? 1 : 2;
                    lines_data[prev_line_idx].type = TYPE_HEADING;
                    lines_data[prev_line_idx].level = heading_level;
                    prev_line_idx = -1;
                    prev_line_type = TYPE_EMPTY; 
                    continue;
                } else if (count >= 3 && (marker == '-' || marker == '*' || marker == '_')) {
                    add_parsed_line(TYPE_HORIZONTAL_RULE, raw_current_indent, "", 0);
                    prev_line_type = TYPE_HORIZONTAL_RULE;
                    prev_line_idx = num_lines - 1;
                    continue;
                }
            }
        }

        // Blockquotes
        if (len > 0 && trimmed_line[0] == '>') {
            int quote_indent = 1;
            while (trimmed_line[quote_indent] == '>') quote_indent++;
            if (trimmed_line[quote_indent] == ' ') quote_indent++;
            add_parsed_line(TYPE_BLOCKQUOTE, raw_current_indent, trimmed_line + quote_indent, 0);
            prev_line_type = TYPE_BLOCKQUOTE;
            prev_line_idx = num_lines - 1;
            continue;
        }

        // Unordered List Items: *, -, or + followed by a space
        if (len > 0 && (trimmed_line[0] == '*' || trimmed_line[0] == '-' || trimmed_line[0] == '+') &&
            (strlen(trimmed_line) > 1 && trimmed_line[1] == ' ')) {
            
            char *list_content = trimmed_line + 2;
            LineType item_type = TYPE_UNORDERED_LIST_ITEM;

            if (strlen(list_content) >= 4 && list_content[0] == '[' && list_content[2] == ']' && list_content[3] == ' ') {
                if (list_content[1] == ' ') {
                    item_type = TYPE_TASK_LIST_ITEM_UNCHECKED;
                } else {
                    item_type = TYPE_TASK_LIST_ITEM_CHECKED;
                }
                list_content += 4;
            }

            add_parsed_line(item_type, raw_current_indent, list_content, 0); // Store raw indent
            prev_line_type = item_type;
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
        char full_prefix[MAX_LINE_LENGTH];
        snprintf(full_prefix, sizeof(full_prefix), "%s%s", global_prefix, prefix);
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
            
            snprintf(full_prefix, sizeof(full_prefix), "%s%s", global_prefix, prefix); printf("%s", full_prefix);
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

            // Check if this item is the last child in its parent scope that requires extending the line
            bool is_last_child = true;
            for (int k = i + 1; k < num_lines; k++) {
                ParsedLine *next_line = &lines_data[k];
                if (next_line->type == TYPE_HEADING && next_line->level <= parent_heading_level) {
                    break; // Reached end of parent scope
                }
                if (next_line->type == TYPE_CONTENT || next_line->type == TYPE_CODE_BLOCK_CONTENT || 
                    next_line->type == TYPE_BLOCKQUOTE || next_line->type == TYPE_HORIZONTAL_RULE) {
                    // Only a normal line, code block, blockquote, or HR causes the vertical line to continue
                    is_last_child = false;
                    break;
                }
            }

            // Add list indentation
            int current_raw_indent_blocks = current_line->level / 4;
            
            if (current_line->type == TYPE_CONTENT || current_line->type == TYPE_CODE_BLOCK_CONTENT ||
                current_line->type == TYPE_BLOCKQUOTE || current_line->type == TYPE_HORIZONTAL_RULE) {
                char base_prefix[MAX_LINE_LENGTH];
                strcpy(base_prefix, prefix);

                if (is_last_child) {
                    strcat(prefix, ELBOW_STR);
                } else {
                    strcat(prefix, TEE_STR);
                }
                // Add list item indentation spacing if content is nested inside list
                for (int j = 0; j < current_raw_indent_blocks; j++) {
                    strcat(prefix, INDENT_STR);
                }
                
                if (current_line->type == TYPE_CODE_BLOCK_CONTENT) {
                    char subsequent_prefix[MAX_LINE_LENGTH];
                    strcpy(subsequent_prefix, base_prefix);
                    if (is_last_child) {
                        strcat(subsequent_prefix, INDENT_STR);
                    } else {
                        strcat(subsequent_prefix, PIPE_STR);
                    }
                    for (int j = 0; j < current_raw_indent_blocks; j++) {
                        strcat(subsequent_prefix, INDENT_STR);
                    }

                    snprintf(full_prefix, sizeof(full_prefix), "%s%s", global_prefix, prefix); printf("%s", full_prefix);
                    char *code_text = current_line->text;
                    char *newline_pos;
                    bool first_line = true;
                    
                    if (code_text[0] == '\0') {
                        printf("%s%s\n", COLOR_CYAN, COLOR_RESET);
                    } else {
                        while ((newline_pos = strchr(code_text, '\n')) != NULL) {
                            *newline_pos = '\0';
                            if (first_line) {
                                printf("%s%s%s\n", COLOR_CYAN, code_text, COLOR_RESET);
                                first_line = false;
                            } else {
                                char full_sub_prefix[MAX_LINE_LENGTH]; snprintf(full_sub_prefix, sizeof(full_sub_prefix), "%s%s", global_prefix, subsequent_prefix); printf("%s%s%s%s\n", full_sub_prefix, COLOR_CYAN, code_text, COLOR_RESET);
                            }
                            *newline_pos = '\n';
                            code_text = newline_pos + 1;
                        }
                        if (*code_text != '\0' || !first_line) {
                            if (first_line) {
                                printf("%s%s%s\n", COLOR_CYAN, code_text, COLOR_RESET);
                            } else if (*code_text != '\0') {
                                char full_sub_prefix[MAX_LINE_LENGTH]; snprintf(full_sub_prefix, sizeof(full_sub_prefix), "%s%s", global_prefix, subsequent_prefix); printf("%s%s%s%s\n", full_sub_prefix, COLOR_CYAN, code_text, COLOR_RESET);
                            }
                        }
                    }
                } else if (current_line->type == TYPE_BLOCKQUOTE) {
                    snprintf(full_prefix, sizeof(full_prefix), "%s%s", global_prefix, prefix); printf("%s%s> %s", full_prefix, COLOR_DIM, COLOR_RESET);
                    print_formatted_text(current_line->text, COLOR_BRIGHT_GREEN, COLOR_RESET);
                    printf("\n");
                } else if (current_line->type == TYPE_HORIZONTAL_RULE) {
                    snprintf(full_prefix, sizeof(full_prefix), "%s%s", global_prefix, prefix); printf("%s%s──────────%s\n", full_prefix, COLOR_DIM, COLOR_RESET);
                } else {
                    snprintf(full_prefix, sizeof(full_prefix), "%s%s", global_prefix, prefix); printf("%s", full_prefix); 
                    print_formatted_text(current_line->text, COLOR_BRIGHT_WHITE, COLOR_RESET);
                    printf("\n");
                }
            } else if (current_line->type == TYPE_UNORDERED_LIST_ITEM || 
                       current_line->type == TYPE_TASK_LIST_ITEM_UNCHECKED || 
                       current_line->type == TYPE_TASK_LIST_ITEM_CHECKED) {
                // List items have no tree connectors, but need the vertical line if not the last child
                if (is_last_child) {
                    strcat(prefix, INDENT_STR);
                } else {
                    strcat(prefix, PIPE_STR);
                }
                for (int j = 0; j < current_raw_indent_blocks; j++) {
                    strcat(prefix, INDENT_STR);
                }
                
                if (current_line->type == TYPE_TASK_LIST_ITEM_CHECKED) {
                    printf("%s%s[%s✓%s%s] ", prefix, COLOR_DIM, COLOR_BRIGHT_GREEN, COLOR_RESET, COLOR_DIM);
                    print_formatted_text(current_line->text, COLOR_BRIGHT_WHITE, COLOR_RESET); 
                } else if (current_line->type == TYPE_TASK_LIST_ITEM_UNCHECKED) {
                    printf("%s%s[ ] %s", prefix, COLOR_DIM, COLOR_RESET);
                    print_formatted_text(current_line->text, COLOR_BRIGHT_WHITE, COLOR_RESET); 
                } else {
                    snprintf(full_prefix, sizeof(full_prefix), "%s%s", global_prefix, prefix); printf("%s%s", full_prefix, BULLET_POINT); 
                    print_formatted_text(current_line->text, COLOR_BRIGHT_WHITE, COLOR_RESET); 
                }
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
                snprintf(full_prefix, sizeof(full_prefix), "%s%s", global_prefix, prefix); printf("%s%s%d.%s ", full_prefix, COLOR_BRIGHT_BLUE, current_line->list_number, COLOR_RESET); 
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


    cleanup();
}