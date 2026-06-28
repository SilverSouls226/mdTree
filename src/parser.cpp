#include "parser.h"
#include "utils.h"
#include <ctype.h>
#include <regex.h>

ParsedLine *lines_data = NULL;
int num_lines = 0;
int capacity_lines = 0;

LintWarning *warnings = NULL;
int num_warnings = 0;
int capacity_warnings = 0;

void add_warning(int line_number, const char *msg) {
    if (num_warnings >= capacity_warnings) {
        capacity_warnings = (capacity_warnings == 0) ? 10 : capacity_warnings * 2;
        warnings = (LintWarning*)realloc(warnings, capacity_warnings * sizeof(LintWarning));
    }
    warnings[num_warnings].line_number = line_number;
    strncpy(warnings[num_warnings].message, msg, 255);
    warnings[num_warnings].message[255] = '\0';
    num_warnings++;
}

typedef struct {
    char *text;
    int first_line;
    int *duplicate_lines;
    int num_duplicates;
    int capacity_duplicates;
} SeenHeading;

SeenHeading *seen_headings = NULL;
int num_seen_headings = 0;
int capacity_seen_headings = 0;

void add_seen_heading(const char *text, int line_num) {
    if (num_seen_headings >= capacity_seen_headings) {
        capacity_seen_headings = (capacity_seen_headings == 0) ? 10 : capacity_seen_headings * 2;
        seen_headings = (SeenHeading*)realloc(seen_headings, capacity_seen_headings * sizeof(SeenHeading));
    }
    seen_headings[num_seen_headings].text = strdup(text);
    seen_headings[num_seen_headings].first_line = line_num;
    seen_headings[num_seen_headings].duplicate_lines = NULL;
    seen_headings[num_seen_headings].num_duplicates = 0;
    seen_headings[num_seen_headings].capacity_duplicates = 0;
    num_seen_headings++;
}

bool check_and_add_duplicate(const char *text, int line_num) {
    for (int i = 0; i < num_seen_headings; i++) {
        if (strcmp(seen_headings[i].text, text) == 0) {
            SeenHeading *sh = &seen_headings[i];
            if (sh->num_duplicates >= sh->capacity_duplicates) {
                sh->capacity_duplicates = (sh->capacity_duplicates == 0) ? 5 : sh->capacity_duplicates * 2;
                sh->duplicate_lines = (int*)realloc(sh->duplicate_lines, sh->capacity_duplicates * sizeof(int));
            }
            sh->duplicate_lines[sh->num_duplicates++] = line_num;
            return true;
        }
    }
    return false;
}

int *h1_lines = NULL;
int num_h1s = 0;
int capacity_h1s = 0;

void add_h1_line(int line_num) {
    if (num_h1s >= capacity_h1s) {
        capacity_h1s = (capacity_h1s == 0) ? 10 : capacity_h1s * 2;
        h1_lines = (int*)realloc(h1_lines, capacity_h1s * sizeof(int));
    }
    h1_lines[num_h1s++] = line_num;
}

void cleanup_warnings() {
    if (warnings) free(warnings);
    for (int i = 0; i < num_seen_headings; i++) {
        free(seen_headings[i].text);
        if (seen_headings[i].duplicate_lines) free(seen_headings[i].duplicate_lines);
    }
    if (seen_headings) free(seen_headings);
    if (h1_lines) free(h1_lines);
    warnings = NULL;
    num_warnings = 0;
    capacity_warnings = 0;
    seen_headings = NULL;
    num_seen_headings = 0;
    capacity_seen_headings = 0;
    h1_lines = NULL;
    num_h1s = 0;
    capacity_h1s = 0;
}


void add_parsed_line(LineType type, int level, const char *text, int list_number, int line_number) {
    if (num_lines >= capacity_lines) {
        capacity_lines = (capacity_lines == 0) ? 100 : capacity_lines * 2;
        ParsedLine *new_lines_data = (ParsedLine*)realloc(lines_data, capacity_lines * sizeof(ParsedLine));
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
    lines_data[num_lines].original_line_num = line_number;
    num_lines++;
}

static bool is_line_filtered(Config *config, bool *should_print_cache, bool *is_ignored, ParsedLine *line, int line_idx) {
    if (is_ignored && is_ignored[line_idx]) return true;
    if (config->headings_only && line->type != TYPE_HEADING) return true;
    if (should_print_cache && !should_print_cache[line_idx]) return true;
    if (line->type == TYPE_HEADING && line->level > config->max_level_filter) return true;
    if (line->type != TYPE_HEADING && config->max_level_filter != MAX_AWK_LEVEL) return true;
    if (line->type == TYPE_EMPTY && strlen(line->text) == 0 && config->max_level_filter != MAX_AWK_LEVEL) return true;
    if (line->type == TYPE_EMPTY) return true;
    return false;
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

static int get_actual_parent_level(int line_idx) {
    ParsedLine *current_line = &lines_data[line_idx];
    for (int k = line_idx - 1; k >= 0; k--) {
        ParsedLine *prev_line = &lines_data[k];
        if (prev_line->type == TYPE_HEADING) {
            if (prev_line->level < current_line->level) {
                return prev_line->level;
            }
        }
    }
    return 0;
}

static bool is_last_sibling_for_level(int line_idx, int target_level, Config *config, bool *should_print_cache, bool *is_ignored) {
    for (int k = line_idx + 1; k < num_lines; k++) {
        ParsedLine *next_line = &lines_data[k];
        if (next_line->type == TYPE_HEADING) {
            if (is_line_filtered(config, should_print_cache, is_ignored, next_line, k)) {
                continue;
            }
            int apl = get_actual_parent_level(k);
            if (apl < target_level) {
                if (target_level == apl + 1) {
                    return false; // Found a sibling that attaches here
                } else {
                    return true; // Scope ended (jumped out horizontally or vertically)
                }
            }
        }
    }
    return true; // Reached end of file
}

bool process_markdown_file(const char *md_file_path, const char *global_prefix, Config *config, const char *item_prefix, const char *filename) {
    FILE *fp = fopen(md_file_path, "r");
    if (config->show_stats) {
        g_stats.files_parsed++;
    }
    if (fp == NULL) {
        perror("Error opening Markdown file");
        return false;
    }


    char line_buffer[MAX_LINE_LENGTH];
    int prev_line_idx = -1;
    bool in_code_block = false;

    int current_line_num = 0;


    cleanup_warnings();
    
    int last_heading_level = 0;
    int total_headings = 0;

    // --- First Pass: Parse file and store data ---

    while (fgets(line_buffer, sizeof(line_buffer), fp) != NULL) {
        current_line_num++;

        // Trailing whitespace warning
        int original_len = strlen(line_buffer);
        // fgets includes the newline character, so the last char is \n (unless it's the last line without one).
        int last_char_idx = (original_len > 0 && line_buffer[original_len - 1] == '\n') ? original_len - 2 : original_len - 1;
        if (last_char_idx >= 0 && (line_buffer[last_char_idx] == ' ' || line_buffer[last_char_idx] == '\t')) {
            add_warning(current_line_num, "Trailing whitespace detected");
        }
        
        line_buffer[strcspn(line_buffer, "\n")] = 0;

        int len = strlen(line_buffer);
        
        int raw_current_indent = get_raw_indentation_level(line_buffer);
        char *trimmed_line = line_buffer + raw_current_indent; // Pointer to the actual text after initial indent

        // Check for code block fences
        if (strncmp(trimmed_line, "```", 3) == 0) {
            in_code_block = !in_code_block;
            if (in_code_block) {
                // Starting a new code block, add an empty ParsedLine for it
                add_parsed_line(TYPE_CODE_BLOCK_CONTENT, raw_current_indent, "", 0, current_line_num);
            }
            continue; // Do not render the backticks themselves
        }

        if (in_code_block) {
            ParsedLine *last_line = &lines_data[num_lines - 1];
            int old_len = strlen(last_line->text);
            int new_len = old_len + strlen(line_buffer) + 2; // +1 for \n, +1 for \0
            char *new_text = (char*)malloc(new_len);
            strcpy(new_text, last_line->text);
            if (old_len > 0) {
                strcat(new_text, "\n");
            }
            strcat(new_text, line_buffer);
            free(last_line->text);
            last_line->text = new_text;
            continue;
        }


        // Check for level > 6
        if (len > 0 && trimmed_line[0] == '#') {
            int level = 0;
            while ((size_t)level < strlen(trimmed_line) && trimmed_line[level] == '#') {
                level++;
            }
            if (level > 6 && (trimmed_line[level] == ' ' || trimmed_line[level] == '\0')) {
                add_warning(current_line_num, "Heading level exceeds 6");
            }
        }
        
        // ATX Headings

        if (len > 0 && trimmed_line[0] == '#') {
            int level = 0;
            while ((size_t)level < strlen(trimmed_line) && trimmed_line[level] == '#') {
                level++;
            }
            if (level > 0 && level <= MAX_HEADING_LEVEL && (trimmed_line[level] == ' ' || trimmed_line[level] == '\0')) {
                const char *heading_text = trimmed_line + level + (trimmed_line[level] == ' ' ? 1 : 0);
                
                // Linting logic
                total_headings++;
                if (level == 1) add_h1_line(current_line_num);
                if (last_heading_level > 0 && level > last_heading_level + 1) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Skipped heading level (H%d to H%d)", last_heading_level, level);
                    add_warning(current_line_num, msg);
                }
                last_heading_level = level;
                
                if (strlen(heading_text) == 0) {
                    add_warning(current_line_num, "Empty heading");
                } else {
                    if (!check_and_add_duplicate(heading_text, current_line_num)) {
                        add_seen_heading(heading_text, current_line_num);
                    }
                }

                add_parsed_line(TYPE_HEADING, level, heading_text, 0, current_line_num);
                prev_line_idx = num_lines - 1;
                continue;
            }
        }

        // Setext Headings and Horizontal Rules
        if (len > 0 && (trimmed_line[0] == '=' || trimmed_line[0] == '-' || trimmed_line[0] == '*' || trimmed_line[0] == '_')) {
            bool is_uniform = true;
            char marker = trimmed_line[0];
            int count = 1;
            for (size_t i = 1; i < strlen(trimmed_line); i++) {
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
                    
                    // Linting logic for setext
                    total_headings++;
                    if (heading_level == 1) add_h1_line(current_line_num);
                    if (last_heading_level > 0 && heading_level > last_heading_level + 1) {
                        char msg[256];
                        snprintf(msg, sizeof(msg), "Skipped heading level (H%d to H%d)", last_heading_level, heading_level);
                        add_warning(current_line_num, msg);
                    }
                    last_heading_level = heading_level;
                    
                    const char *heading_text = lines_data[prev_line_idx].text;
                    if (strlen(heading_text) == 0) {
                        add_warning(current_line_num, "Empty heading");
                    } else {
                        if (!check_and_add_duplicate(heading_text, current_line_num)) {
                            add_seen_heading(heading_text, current_line_num);
                        }
                    }
                    prev_line_idx = -1;
                    continue;
                } else if (count >= 3 && (marker == '-' || marker == '*' || marker == '_')) {
                    add_parsed_line(TYPE_HORIZONTAL_RULE, raw_current_indent, "", 0, current_line_num);
                    prev_line_idx = num_lines - 1;
                    continue;
                }
            }
        }

        // Blockquotes
        if (len > 0 && trimmed_line[0] == '>') {
            if (prev_line_idx != -1 && lines_data[prev_line_idx].type == TYPE_BLOCKQUOTE) {
                ParsedLine *last_line = &lines_data[prev_line_idx];
                int old_len = strlen(last_line->text);
                int new_len = old_len + strlen(trimmed_line) + 2;
                char *new_text = (char*)malloc(new_len);
                strcpy(new_text, last_line->text);
                strcat(new_text, "\n");
                strcat(new_text, trimmed_line);
                free(last_line->text);
                last_line->text = new_text;
            } else {
                add_parsed_line(TYPE_BLOCKQUOTE, raw_current_indent, trimmed_line, 0, current_line_num);
                prev_line_idx = num_lines - 1;
            }
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

            add_parsed_line(item_type, raw_current_indent, list_content, 0, current_line_num); // Store raw indent
            prev_line_idx = num_lines - 1;
            continue;
        }

        // Ordered List Items: Number followed by . and a space
        if (len > 0 && isdigit(trimmed_line[0])) {
            size_t i = 0;
            int list_num = 0;
            while (i < strlen(trimmed_line) && isdigit(trimmed_line[i])) {
                list_num = list_num * 10 + (trimmed_line[i] - '0');
                i++;
            }
            if (i > 0 && i < strlen(trimmed_line) && trimmed_line[i] == '.' && 
                (strlen(trimmed_line) > (size_t)(i + 1) && trimmed_line[i+1] == ' ')) {
                add_parsed_line(TYPE_ORDERED_LIST_ITEM, raw_current_indent, trimmed_line + i + 2, list_num, current_line_num); // Store raw indent
                prev_line_idx = num_lines - 1;
                continue;
            }
        }

        // Table row parsing
        if (len > 0 && trimmed_line[0] == '|') {
            if (prev_line_idx != -1 && lines_data[prev_line_idx].type == TYPE_TABLE_CONTENT) {
                ParsedLine *last_line = &lines_data[prev_line_idx];
                int old_len = strlen(last_line->text);
                int new_len = old_len + strlen(line_buffer) + 2;
                char *new_text = (char*)malloc(new_len);
                strcpy(new_text, last_line->text);
                if (old_len > 0) strcat(new_text, "\n");
                strcat(new_text, line_buffer);
                free(last_line->text);
                last_line->text = new_text;
            } else {
                add_parsed_line(TYPE_TABLE_CONTENT, raw_current_indent, line_buffer, 0, current_line_num);
                prev_line_idx = num_lines - 1;
            }
            continue;
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
            add_parsed_line(TYPE_CONTENT, raw_current_indent, line_buffer, 0, current_line_num); // Store content with its raw indent
            prev_line_idx = num_lines - 1;
        } else {
            if (config->max_level_filter == MAX_AWK_LEVEL) {
                 add_parsed_line(TYPE_EMPTY, 0, "", 0, current_line_num);
                 prev_line_idx = num_lines - 1;
            } else {
                 prev_line_idx = -1;
            }
        }
    }

    fclose(fp);

    if (total_headings == 0 && current_line_num > 0) {
        add_warning(current_line_num, "File contains no headings");
    }


    // --- Second Pass: Print formatted output ---
    
    bool *is_ignored = (bool*)calloc(num_lines, sizeof(bool));
    if (config->ignore_query) {
        regex_t ignore_regex;
        bool ignore_regex_compiled = false;
        int cflags = REG_EXTENDED;
        if (config->case_insensitive_search) cflags |= REG_ICASE;
        if (regcomp(&ignore_regex, config->ignore_query, cflags) == 0) {
            ignore_regex_compiled = true;
        } else {
            fprintf(stderr, "Warning: Failed to compile ignore regex '%s'\n", config->ignore_query);
        }

        int current_ignored_level = -1;
        for (int i = 0; i < num_lines; i++) {
            ParsedLine *line = &lines_data[i];
            if (line->type == TYPE_HEADING) {
                if (current_ignored_level != -1 && line->level <= current_ignored_level) {
                    current_ignored_level = -1;
                }
                if (current_ignored_level == -1 && ignore_regex_compiled) {
                    if (regexec(&ignore_regex, line->text, 0, NULL, 0) == 0) {
                        current_ignored_level = line->level;
                    }
                }
            }
            if (current_ignored_level != -1) {
                is_ignored[i] = true;
            }
        }
        if (ignore_regex_compiled) regfree(&ignore_regex);
    }

    bool *should_print_cache = NULL;
    if (config->search_query) {
        regex_t regex;
        bool regex_compiled = false;
        if (config->use_regex) {
            int cflags = REG_EXTENDED;
            if (config->case_insensitive_search) cflags |= REG_ICASE;
            if (regcomp(&regex, config->search_query, cflags) == 0) {
                regex_compiled = true;
            } else {
                fprintf(stderr, "Warning: Failed to compile regex '%s'\n", config->search_query);
            }
        }

        should_print_cache = (bool*)calloc(num_lines, sizeof(bool));
        for (int i = 0; i < num_lines; i++) {
            if (is_ignored[i]) continue;
            if (config->headings_only && lines_data[i].type != TYPE_HEADING) continue;
            char *text_to_search = lines_data[i].text;
            bool found = false;
            if (config->use_regex) {
                if (regex_compiled && regexec(&regex, text_to_search, 0, NULL, 0) == 0) {
                    found = true;
                }
            } else if (config->case_insensitive_search) {
                if (find_substring_case_insensitive(text_to_search, config->search_query)) {
                    found = true;
                }
            } else {
                if (strstr(text_to_search, config->search_query) != NULL) {
                    found = true;
                }
            }
            
            if (found) {
                should_print_cache[i] = true;
                
                // Tracing back to mark parent headings
                int target_level = -1;
                if (lines_data[i].type == TYPE_HEADING) {
                    target_level = lines_data[i].level - 1;
                } else {
                    // For content, find the nearest preceding heading
                    for (int j = i - 1; j >= 0; j--) {
                        if (lines_data[j].type == TYPE_HEADING) {
                            target_level = lines_data[j].level;
                            break;
                        }
                    }
                }
                
                for (int j = i - 1; j >= 0; j--) {
                    if (lines_data[j].type == TYPE_HEADING && lines_data[j].level <= target_level) {
                        should_print_cache[j] = true;
                        target_level = lines_data[j].level - 1;
                        if (target_level <= 0) break;
                    }
                }
            }
        }
        if (config->use_regex && regex_compiled) {
            regfree(&regex);
        }
    }

    if (config->search_query) {
        bool has_matches = false;
        for (int i = 0; i < num_lines; i++) {
            if (should_print_cache[i]) {
                has_matches = true;
                break;
            }
        }
        if (!has_matches) {
            if (is_ignored) free(is_ignored);
            free(should_print_cache);
            cleanup();
            return false;
        }
    }

    if (filename) {
        printf("%s%s\n", item_prefix ? item_prefix : "", filename);
    }
    
    // Calculate max digits
    int max_digits = 1;
    int temp = current_line_num;
    while (temp > 9) {
        max_digits++;
        temp /= 10;
    }



    for (int i = 0; i < num_lines; i++) {
        ParsedLine *current_line = &lines_data[i];

        char prefix[MAX_LINE_LENGTH] = "";
        char full_prefix[MAX_LINE_LENGTH];
        snprintf(full_prefix, sizeof(full_prefix), "%s%s", global_prefix, prefix);
        int current_logical_level = 0; // Represents the indentation level we are currently at (0-based)
        bool is_last_sibling_in_current_scope = true;

        // Filtering logic
        if (is_line_filtered(config, should_print_cache, is_ignored, current_line, i)) continue;

        if (config->show_stats) {
            if (current_line->type == TYPE_HEADING) {
                g_stats.headings++;
                g_stats.words += count_words(current_line->text);
            } else if (current_line->type == TYPE_CONTENT || current_line->type == TYPE_BLOCKQUOTE || current_line->type == TYPE_CODE_BLOCK_CONTENT || current_line->type == TYPE_TABLE_CONTENT) {
                g_stats.words += count_words(current_line->text);
            } else if (current_line->type == TYPE_UNORDERED_LIST_ITEM || current_line->type == TYPE_ORDERED_LIST_ITEM || current_line->type == TYPE_TASK_LIST_ITEM_CHECKED || current_line->type == TYPE_TASK_LIST_ITEM_UNCHECKED) {
                g_stats.lists++;
                g_stats.words += count_words(current_line->text);
            }
        }

        if (current_line->type == TYPE_HEADING) {
            current_logical_level = current_line->level - 1;

            // Determine actual parent level to handle jumps
            int actual_parent_level = get_actual_parent_level(i);

            // Determine if current heading is the last sibling at its level
            is_last_sibling_in_current_scope = is_last_sibling_for_level(i, current_line->level, config, should_print_cache, is_ignored);
            
            bool has_skipped_levels = (actual_parent_level < current_line->level - 1);

            // Build the prefix based on parent heading status
            for (int j = 0; j < current_logical_level; j++) {
                int target_level = j + 1;
                if (target_level <= actual_parent_level) {
                    if (is_last_sibling_for_level(i, target_level, config, should_print_cache, is_ignored)) {
                        strcat(prefix, INDENT_STR);
                    } else {
                        strcat(prefix, PIPE_STR);
                    }
                } else {
                    if (target_level == actual_parent_level + 1) {
                        if (is_last_sibling_for_level(i, target_level, config, should_print_cache, is_ignored)) {
                            strcat(prefix, g_ascii_tree ? "`---" : "└───");
                        } else {
                            strcat(prefix, g_ascii_tree ? "|---" : "├───");
                        }
                    } else {
                        strcat(prefix, g_ascii_tree ? "----" : "────");
                    }
                }
            }

            // Append current item's tree symbol
            if (has_skipped_levels) {
                strcat(prefix, g_ascii_tree ? "--- " : "─── ");
            } else {
                if (is_last_sibling_in_current_scope) {
                    strcat(prefix, ELBOW_STR);
                } else {
                    strcat(prefix, TEE_STR);
                }
            }
            
            if (config->show_line_numbers) { printf("%s%*d%s %s ", COLOR_DIM, max_digits, current_line->original_line_num, COLOR_RESET, VLINE_STR); } snprintf(full_prefix, sizeof(full_prefix), "%s%s", global_prefix, prefix); printf("%s", full_prefix);
            // Apply colors based on heading level
            switch (current_line->level) {
                case 1: print_formatted_text(current_line->text, COLOR_BOLD_BRIGHT_YELLOW, COLOR_RESET); break;
                case 2: print_formatted_text(current_line->text, COLOR_BOLD_BRIGHT_CYAN, COLOR_RESET); break;
                case 3: print_formatted_text(current_line->text, COLOR_GREEN, COLOR_RESET); break;
                case 4: print_formatted_text(current_line->text, COLOR_MAGENTA, COLOR_RESET); break;
                case 5: print_formatted_text(current_line->text, COLOR_BLUE, COLOR_RESET); break;
                case 6: print_formatted_text(current_line->text, COLOR_RED, COLOR_RESET); break;
                default: print_formatted_text(current_line->text, COLOR_RESET, COLOR_RESET); break;
            }
            printf("\n");

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
                if (is_last_sibling_for_level(i, j + 1, config, should_print_cache, is_ignored)) {
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
                if (!is_line_filtered(config, should_print_cache, is_ignored, next_line, k)) {
                    if (next_line->type == TYPE_CONTENT || next_line->type == TYPE_CODE_BLOCK_CONTENT || 
                        next_line->type == TYPE_BLOCKQUOTE || next_line->type == TYPE_HORIZONTAL_RULE || next_line->type == TYPE_TABLE_CONTENT) {
                        // Only a normal line, code block, blockquote, or HR causes the vertical line to continue
                        is_last_child = false;
                        break;
                    }
                }
            }

            // Add list indentation
            int current_raw_indent_blocks = current_line->level / 4;
            
            if (current_line->type == TYPE_CONTENT || current_line->type == TYPE_CODE_BLOCK_CONTENT ||
                current_line->type == TYPE_BLOCKQUOTE || current_line->type == TYPE_HORIZONTAL_RULE || current_line->type == TYPE_TABLE_CONTENT) {
                char base_prefix[MAX_LINE_LENGTH];
                strcpy(base_prefix, prefix);

                if (current_line->type == TYPE_HORIZONTAL_RULE) {
                    if (is_last_child) {
                        strcat(prefix, g_ascii_tree ? "`---" : "└───");
                    } else {
                        strcat(prefix, g_ascii_tree ? "|---" : "├───");
                    }
                } else {
                    if (is_last_child) {
                        strcat(prefix, ELBOW_STR);
                    } else {
                        strcat(prefix, TEE_STR);
                    }
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

                    if (config->show_line_numbers) { printf("%s%*d%s %s ", COLOR_DIM, max_digits, current_line->original_line_num, COLOR_RESET, VLINE_STR); } snprintf(full_prefix, sizeof(full_prefix), "%s%s", global_prefix, prefix); printf("%s", full_prefix);
                    char *code_text = current_line->text;
                    char *newline_pos;
                    bool first_line = true;
                    int current_cb_line = current_line->original_line_num;
                    
                    if (code_text[0] == '\0') {
                        printf("%s%s\n", COLOR_CYAN, COLOR_RESET);
                    } else {
                        while ((newline_pos = strchr(code_text, '\n')) != NULL) {
                            *newline_pos = '\0';
                            if (first_line) {
                                current_cb_line++;
                                printf("%s%s%s\n", COLOR_CYAN, code_text, COLOR_RESET);
                                first_line = false;
                            } else {
                                char full_sub_prefix[MAX_LINE_LENGTH]; snprintf(full_sub_prefix, sizeof(full_sub_prefix), "%s%s", global_prefix, subsequent_prefix); if (config->show_line_numbers) { printf("%s%*d%s %s ", COLOR_DIM, max_digits, current_cb_line++, COLOR_RESET, VLINE_STR); } printf("%s%s%s%s\n", full_sub_prefix, COLOR_CYAN, code_text, COLOR_RESET);
                            }
                            *newline_pos = '\n';
                            code_text = newline_pos + 1;
                        }
                        if (*code_text != '\0' || !first_line) {
                            if (first_line) {
                                current_cb_line++;
                                printf("%s%s%s\n", COLOR_CYAN, code_text, COLOR_RESET);
                            } else if (*code_text != '\0') {
                                char full_sub_prefix[MAX_LINE_LENGTH]; snprintf(full_sub_prefix, sizeof(full_sub_prefix), "%s%s", global_prefix, subsequent_prefix); if (config->show_line_numbers) { printf("%s%*d%s %s ", COLOR_DIM, max_digits, current_cb_line++, COLOR_RESET, VLINE_STR); } printf("%s%s%s%s\n", full_sub_prefix, COLOR_CYAN, code_text, COLOR_RESET);
                            }
                        }
                    }
                } else if (current_line->type == TYPE_TABLE_CONTENT || current_line->type == TYPE_BLOCKQUOTE) {
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

                    if (config->show_line_numbers) { printf("%s%*d%s %s ", COLOR_DIM, max_digits, current_line->original_line_num, COLOR_RESET, VLINE_STR); } snprintf(full_prefix, sizeof(full_prefix), "%s%s", global_prefix, prefix); printf("%s", full_prefix);
                    char *code_text = current_line->text;
                    char *newline_pos;
                    bool first_line = true;
                    int current_cb_line = current_line->original_line_num;
                    
                    const char *color_code = (current_line->type == TYPE_BLOCKQUOTE) ? COLOR_BRIGHT_GREEN : COLOR_BRIGHT_WHITE;
                    
                    if (code_text[0] == '\0') {
                        printf("\n");
                    } else {
                        while ((newline_pos = strchr(code_text, '\n')) != NULL) {
                            *newline_pos = '\0';
                            
                            char *display_text = code_text;
                            int bq_level = 0;
                            if (current_line->type == TYPE_BLOCKQUOTE) {
                                while (*display_text == '>' || *display_text == ' ') {
                                    if (*display_text == '>') bq_level++;
                                    display_text++;
                                }
                                if (bq_level < 1) bq_level = 1;
                            }
                            
                            if (first_line) {
                                current_cb_line++;
                                if (current_line->type == TYPE_BLOCKQUOTE) {
                                    for (int i = 0; i < bq_level - 1; i++) printf("%s", INDENT_STR);
                                    printf("%s>%s ", COLOR_DIM, COLOR_RESET);
                                    print_formatted_text(display_text, color_code, COLOR_RESET);
                                } else {
                                    print_formatted_text(code_text, color_code, COLOR_RESET);
                                }
                                printf("\n");
                                first_line = false;
                            } else {
                                char full_sub_prefix[MAX_LINE_LENGTH]; snprintf(full_sub_prefix, sizeof(full_sub_prefix), "%s%s", global_prefix, subsequent_prefix); if (config->show_line_numbers) { printf("%s%*d%s %s ", COLOR_DIM, max_digits, current_cb_line++, COLOR_RESET, VLINE_STR); } printf("%s", full_sub_prefix);
                                if (current_line->type == TYPE_BLOCKQUOTE) {
                                    for (int i = 0; i < bq_level - 1; i++) printf("%s", INDENT_STR);
                                    printf("%s>%s ", COLOR_DIM, COLOR_RESET);
                                    print_formatted_text(display_text, color_code, COLOR_RESET);
                                } else {
                                    print_formatted_text(code_text, color_code, COLOR_RESET);
                                }
                                printf("\n");
                            }
                            *newline_pos = '\n';
                            code_text = newline_pos + 1;
                        }
                        if (*code_text != '\0' || !first_line) {
                            char *display_text = code_text;
                            int bq_level = 0;
                            if (current_line->type == TYPE_BLOCKQUOTE && *code_text != '\0') {
                                while (*display_text == '>' || *display_text == ' ') {
                                    if (*display_text == '>') bq_level++;
                                    display_text++;
                                }
                                if (bq_level < 1) bq_level = 1;
                            }
                            
                            if (first_line) {
                                current_cb_line++;
                                if (current_line->type == TYPE_BLOCKQUOTE && *code_text != '\0') {
                                    for (int i = 0; i < bq_level - 1; i++) printf("%s", INDENT_STR);
                                    printf("%s>%s ", COLOR_DIM, COLOR_RESET);
                                    print_formatted_text(display_text, color_code, COLOR_RESET);
                                } else if (*code_text != '\0') {
                                    print_formatted_text(code_text, color_code, COLOR_RESET);
                                }
                                printf("\n");
                            } else if (*code_text != '\0') {
                                char full_sub_prefix[MAX_LINE_LENGTH]; snprintf(full_sub_prefix, sizeof(full_sub_prefix), "%s%s", global_prefix, subsequent_prefix); if (config->show_line_numbers) { printf("%s%*d%s %s ", COLOR_DIM, max_digits, current_cb_line++, COLOR_RESET, VLINE_STR); } printf("%s", full_sub_prefix);
                                if (current_line->type == TYPE_BLOCKQUOTE) {
                                    for (int i = 0; i < bq_level - 1; i++) printf("%s", INDENT_STR);
                                    printf("%s>%s ", COLOR_DIM, COLOR_RESET);
                                    print_formatted_text(display_text, color_code, COLOR_RESET);
                                } else {
                                    print_formatted_text(code_text, color_code, COLOR_RESET);
                                }
                                printf("\n");
                            }
                        }
                    }
                } else if (current_line->type == TYPE_HORIZONTAL_RULE) {
                    if (config->show_line_numbers) { printf("%s%*d%s %s ", COLOR_DIM, max_digits, current_line->original_line_num, COLOR_RESET, VLINE_STR); } snprintf(full_prefix, sizeof(full_prefix), "%s%s", global_prefix, prefix); printf("%s%s\n", full_prefix, HLINE_STR);
                } else {
                    if (config->show_line_numbers) { printf("%s%*d%s %s ", COLOR_DIM, max_digits, current_line->original_line_num, COLOR_RESET, VLINE_STR); } snprintf(full_prefix, sizeof(full_prefix), "%s%s", global_prefix, prefix); printf("%s", full_prefix); 
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
                    char full_item_prefix[MAX_LINE_LENGTH]; snprintf(full_item_prefix, sizeof(full_item_prefix), "%s%s", global_prefix, prefix); if (config->show_line_numbers) { printf("%s%*d%s %s ", COLOR_DIM, max_digits, current_line->original_line_num, COLOR_RESET, VLINE_STR); } printf("%s%s[%s✓%s%s] ", full_item_prefix, COLOR_DIM, COLOR_BRIGHT_GREEN, COLOR_RESET, COLOR_DIM);
                    print_formatted_text(current_line->text, COLOR_BRIGHT_WHITE, COLOR_RESET); 
                } else if (current_line->type == TYPE_TASK_LIST_ITEM_UNCHECKED) {
                    char full_item_prefix[MAX_LINE_LENGTH]; snprintf(full_item_prefix, sizeof(full_item_prefix), "%s%s", global_prefix, prefix); if (config->show_line_numbers) { printf("%s%*d%s %s ", COLOR_DIM, max_digits, current_line->original_line_num, COLOR_RESET, VLINE_STR); } printf("%s%s[ ] %s", full_item_prefix, COLOR_DIM, COLOR_RESET);
                    print_formatted_text(current_line->text, COLOR_BRIGHT_WHITE, COLOR_RESET); 
                } else {
                    snprintf(full_prefix, sizeof(full_prefix), "%s%s", global_prefix, prefix); if (config->show_line_numbers) { printf("%s%*d%s %s ", COLOR_DIM, max_digits, current_line->original_line_num, COLOR_RESET, VLINE_STR); } printf("%s%s", full_prefix, BULLET_POINT); 
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
                snprintf(full_prefix, sizeof(full_prefix), "%s%s", global_prefix, prefix); if (config->show_line_numbers) { printf("%s%*d%s %s ", COLOR_DIM, max_digits, current_line->original_line_num, COLOR_RESET, VLINE_STR); } printf("%s%s%d.%s ", full_prefix, COLOR_BRIGHT_BLUE, current_line->list_number, COLOR_RESET); 
                print_formatted_text(current_line->text, COLOR_BRIGHT_WHITE, COLOR_RESET); 
                printf("\n");
            }
            
        }
    }


    // Print Warnings
    bool has_any_warnings = (num_warnings > 0) || (num_h1s > 1);
    for (int i = 0; i < num_seen_headings; i++) {
        if (seen_headings[i].num_duplicates > 0) {
            has_any_warnings = true;
            break;
        }
    }

    if (!config->suppress_warnings && has_any_warnings) {
        printf("\n%s%s Linter Warnings %s%s\n", COLOR_BRIGHT_YELLOW, HLINE_STR, HLINE_STR, COLOR_RESET); //, COLOR_BRIGHT_YELLOW, COLOR_RESET);
        for (int j = 0; j < num_warnings; j++) {
            printf("%sLine %d: %s%s\n", COLOR_YELLOW, warnings[j].line_number, warnings[j].message, COLOR_RESET);
        }
        
        if (num_h1s > 1) {
            printf("%sLine %d: Multiple H1 headings detected (also at lines ", COLOR_YELLOW, h1_lines[0]);
            for (int i = 1; i < num_h1s; i++) {
                printf("%d%s", h1_lines[i], i == num_h1s - 1 ? "" : ", ");
            }
            printf(")%s\n", COLOR_RESET);
        }
        
        for (int i = 0; i < num_seen_headings; i++) {
            if (seen_headings[i].num_duplicates > 0) {
                printf("%sLine %d: Duplicate heading: '%s' (also at lines ", COLOR_YELLOW, seen_headings[i].first_line, seen_headings[i].text);
                for (int j = 0; j < seen_headings[i].num_duplicates; j++) {
                    printf("%d%s", seen_headings[i].duplicate_lines[j], j == seen_headings[i].num_duplicates - 1 ? "" : ", ");
                }
                printf(")%s\n", COLOR_RESET);
            }
        }
    }

    if (is_ignored) free(is_ignored);
    if (should_print_cache) free(should_print_cache);
    cleanup();
    return true;
}