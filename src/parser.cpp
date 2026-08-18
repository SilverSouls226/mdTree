#include "parser.h"
#include "TextTable.h"
#include "utils.h"
#include <ctype.h>
#include <regex.h>

std::vector<ParsedLine> lines_data;



std::vector<LintWarning> warnings;



void add_warning(int line_number, const char *msg) {
    LintWarning w;
    w.line_number = line_number;
    w.message = msg ? msg : "";
    warnings.push_back(w);
}

struct SeenHeading {
    std::string text;
    int first_line;
    std::vector<int> duplicate_lines;
};

std::vector<SeenHeading> seen_headings;

void add_seen_heading(const char *text, int line_num) {
    SeenHeading sh;
    sh.text = text ? text : "";
    sh.first_line = line_num;
    seen_headings.push_back(sh);
}

bool check_and_add_duplicate(const char *text, int line_num) {
    std::string t = text ? text : "";
    for (size_t i = 0; i < seen_headings.size(); i++) {
        if (seen_headings[i].text == t) {
            seen_headings[i].duplicate_lines.push_back(line_num);
            return true;
        }
    }
    return false;
}

std::vector<int> h1_lines;



void add_h1_line(int line_num) {
    h1_lines.push_back(line_num);
}

void cleanup_warnings() {
    warnings.clear();
    seen_headings.clear();
    h1_lines.clear();
}


void add_parsed_line(LineType type, int level, const char *text, int list_number, int line_number) {
    ParsedLine pl;
    pl.type = type;
    pl.level = level;
    pl.text = text ? text : "";
    pl.list_number = list_number;
    pl.original_line_num = line_number;
    lines_data.push_back(pl);
}

static bool is_line_filtered(Config *config, bool *should_print_cache, bool *is_ignored, ParsedLine *line, int line_idx) {
    if (is_ignored && is_ignored[line_idx]) return true;
    if (config->headings_only && line->type != TYPE_HEADING) return true;
    if (should_print_cache && !should_print_cache[line_idx]) return true;
    if (line->type == TYPE_HEADING && line->level > config->max_level_filter) return true;
    if (line->type != TYPE_HEADING && config->max_level_filter != MAX_AWK_LEVEL) return true;
    if (line->type == TYPE_EMPTY && line->text.length() == 0 && config->max_level_filter != MAX_AWK_LEVEL) return true;
    if (line->type == TYPE_EMPTY) return true;
    if (line->type == TYPE_HORIZONTAL_RULE && !config->show_hr) return true;
    return false;
}

void cleanup() {
    lines_data.clear();
    warnings.clear();
    seen_headings.clear();
    h1_lines.clear();
}

static int get_horizontal_rule_level(int line_idx) {
    // Check if there is any heading after this horizontal line till EOF
    bool has_heading_after = false;
    for (size_t i = line_idx + 1; i < lines_data.size(); i++) {
        if (lines_data[i].type == TYPE_HEADING) {
            has_heading_after = true;
            break;
        }
    }
    
    if (!has_heading_after) {
        return 2; // Under H1 heading (level 2)
    }

    for (int k = line_idx - 1; k >= 0; k--) {
        if (lines_data[k].type == TYPE_HEADING) {
            return lines_data[k].level;
        }
    }
    return 1; // Default to level 1 if no preceding heading
}

static int get_actual_parent_level(int line_idx) {
    ParsedLine *current_line = &lines_data[line_idx];
    int current_level = (current_line->type == TYPE_HEADING) ? current_line->level : get_horizontal_rule_level(line_idx);
    for (int k = line_idx - 1; k >= 0; k--) {
        ParsedLine *prev_line = &lines_data[k];
        if (prev_line->type == TYPE_HEADING) {
            if (prev_line->level < current_level) {
                return prev_line->level;
            }
        } else if (prev_line->type == TYPE_HORIZONTAL_RULE) {
            int hr_level = get_horizontal_rule_level(k);
            if (hr_level < current_level) {
                return hr_level;
            }
        }
    }
    return 0;
}

static int get_parent_heading_level_for_content(int line_idx) {
    for (int k = line_idx - 1; k >= 0; k--) {
        ParsedLine *prev_line = &lines_data[k];
        if (prev_line->type == TYPE_HORIZONTAL_RULE) {
            int hr_parent = get_horizontal_rule_level(k) - 1;
            if (hr_parent < 0) hr_parent = 0;
            return hr_parent;
        }
        if (prev_line->type == TYPE_HEADING) {
            return prev_line->level;
        }
    }
    return 0; // Root scope
}

static bool is_last_sibling_for_level(int line_idx, int target_level, Config *config, bool *should_print_cache, bool *is_ignored) {
    for (int k = line_idx + 1; k < ((int)lines_data.size()); k++) {
        ParsedLine *next_line = &lines_data[k];
        if (is_line_filtered(config, should_print_cache, is_ignored, next_line, k)) {
            continue;
        }
        if (next_line->type == TYPE_HEADING || next_line->type == TYPE_HORIZONTAL_RULE) {
            int apl = get_actual_parent_level(k);
            if (apl < target_level) {
                if (target_level == apl + 1) {
                    return false; // Found a sibling that attaches here
                } else {
                    return true; // Scope ended (jumped out horizontally or vertically)
                }
            }
        } else if (next_line->type != TYPE_EMPTY) { // Normal content, blockquotes, lists, tables
            int content_parent = get_parent_heading_level_for_content(k);
            int content_level = content_parent + 1;
            if (content_level <= target_level) {
                if (target_level == content_level) {
                    return false; // Found a sibling (content) that attaches here!
                } else {
                    return true; // Scope ended
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

        if (config->show_stats) {
            int original_len = strlen(line_buffer);
            g_stats.characters += original_len;
            
            bool is_empty_for_stats = true;
            for (int i = 0; i < original_len; i++) {
                if (!isspace((unsigned char)line_buffer[i])) {
                    is_empty_for_stats = false;
                    break;
                }
            }
            if (is_empty_for_stats) {
                g_stats.empty_lines++;
            } else {
                g_stats.non_empty_lines++;
                g_stats.words += count_words(line_buffer);
            }
            
            const char *p = line_buffer;
            while ((p = strchr(p, '[')) != NULL) {
                if (p > line_buffer && *(p - 1) == '!') {
                    g_stats.images++;
                } else {
                    g_stats.links++;
                }
                p++;
            }
        }

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
            ParsedLine *last_line = &lines_data[((int)lines_data.size() - 1)];
            int old_len = last_line->text.length();
            if (old_len > 0) last_line->text += "\n";
            last_line->text += line_buffer;
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
                prev_line_idx = ((int)lines_data.size() - 1);
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
                    
                    const char *heading_text = lines_data[prev_line_idx].text.c_str();
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
                    prev_line_idx = ((int)lines_data.size() - 1);
                    continue;
                }
            }
        }

        // Blockquotes
        if (len > 0 && trimmed_line[0] == '>') {
            if (prev_line_idx != -1 && lines_data[prev_line_idx].type == TYPE_BLOCKQUOTE) {
                ParsedLine *last_line = &lines_data[prev_line_idx];
                int old_len = last_line->text.length();
                if (old_len > 0) last_line->text += "\n";
                last_line->text += trimmed_line;
            } else {
                add_parsed_line(TYPE_BLOCKQUOTE, raw_current_indent, trimmed_line, 0, current_line_num);
                prev_line_idx = ((int)lines_data.size() - 1);
            }
            continue;
        }

        // Unordered List Items: *, -, or + followed by a space
        if (len > 0 && (trimmed_line[0] == '*' || trimmed_line[0] == '-' || trimmed_line[0] == '+') &&
            (strlen(trimmed_line) > 1 && trimmed_line[1] == ' ')) {
            
            char *list_content = trimmed_line + 2;
            LineType item_type = TYPE_UNORDERED_LIST_ITEM;

            if (strlen(list_content) >= 4 && list_content[0] == '[' && list_content[2] == ']' && list_content[3] == ' ') {
                char mark = list_content[1];
                if (mark == ' ') {
                    item_type = TYPE_TASK_LIST_ITEM_UNCHECKED;
                    list_content += 4;
                } else if (mark == 'x' || mark == 'X') {
                    item_type = TYPE_TASK_LIST_ITEM_CHECKED;
                    list_content += 4;
                }
            }

            add_parsed_line(item_type, raw_current_indent, list_content, 0, current_line_num); // Store raw indent
            prev_line_idx = ((int)lines_data.size() - 1);
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
                prev_line_idx = ((int)lines_data.size() - 1);
                continue;
            }
        }

        // Table row parsing
        if (len > 0 && trimmed_line[0] == '|') {
            if (prev_line_idx != -1 && lines_data[prev_line_idx].type == TYPE_TABLE_CONTENT) {
                ParsedLine *last_line = &lines_data[prev_line_idx];
                if (last_line->text.length() > 0) last_line->text += "\n";
                last_line->text += line_buffer;
            } else {
                add_parsed_line(TYPE_TABLE_CONTENT, raw_current_indent, line_buffer, 0, current_line_num);
                prev_line_idx = ((int)lines_data.size() - 1);
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
            prev_line_idx = ((int)lines_data.size() - 1);
        } else {
            if (config->max_level_filter == MAX_AWK_LEVEL) {
                 add_parsed_line(TYPE_EMPTY, 0, "", 0, current_line_num);
                 prev_line_idx = ((int)lines_data.size() - 1);
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
    
    bool *is_ignored = (bool*)calloc(((int)lines_data.size()), sizeof(bool));

    // --- Focus Mode filtering ---
    if (!config->focus_query.empty()) {
        // First, mark everything as ignored
        for (int i = 0; i < (int)lines_data.size(); i++) {
            is_ignored[i] = true;
        }

        regex_t focus_regex;
        int cflags = REG_EXTENDED;
        if (config->case_insensitive_search) cflags |= REG_ICASE;
        if (regcomp(&focus_regex, config->focus_query.c_str(), cflags) != 0) {
            fprintf(stderr, "Error: Failed to compile focus regex '%s'\n", config->focus_query.c_str());
            free(is_ignored);
            cleanup();
            return false;
        }

        bool match_found = false;
        for (int i = 0; i < (int)lines_data.size(); i++) {
            if (lines_data[i].type == TYPE_HEADING) {
                if (regexec(&focus_regex, lines_data[i].text.c_str(), 0, NULL, 0) == 0) {
                    match_found = true;
                    int focus_level = lines_data[i].level;
                    int focus_end_idx = (int)lines_data.size();
                    for (int k = i + 1; k < (int)lines_data.size(); k++) {
                        if (lines_data[k].type == TYPE_HEADING && lines_data[k].level <= focus_level) {
                            focus_end_idx = k;
                            break;
                        }
                    }
                    for (int j = i; j < focus_end_idx; j++) {
                        is_ignored[j] = false;
                    }
                    // Trace back to un-ignore all parent headings
                    int curr_level = focus_level;
                    for (int k = i - 1; k >= 0; k--) {
                        if (lines_data[k].type == TYPE_HEADING && lines_data[k].level < curr_level) {
                            is_ignored[k] = false;
                            curr_level = lines_data[k].level;
                        }
                    }
                }
            }
        }

        regfree(&focus_regex);

        if (!match_found) {
            if (global_prefix == NULL || strcmp(global_prefix, "") == 0) {
                fprintf(stderr, "Error: Focus heading matching '%s' not found.\n", config->focus_query.c_str());
            }
            free(is_ignored);
            cleanup();
            return false;
        }
    }
    if (!config->ignore_query.empty()) {
        regex_t ignore_regex;
        bool ignore_regex_compiled = false;
        int cflags = REG_EXTENDED;
        if (config->case_insensitive_search) cflags |= REG_ICASE;
        if (regcomp(&ignore_regex, config->ignore_query.c_str(), cflags) == 0) {
            ignore_regex_compiled = true;
        } else {
            fprintf(stderr, "Warning: Failed to compile ignore regex '%s'\n", config->ignore_query.c_str());
        }

        int current_ignored_level = -1;
        for (int i = 0; i < ((int)lines_data.size()); i++) {
            ParsedLine *line = &lines_data[i];
            if (line->type == TYPE_HEADING) {
                if (current_ignored_level != -1 && line->level <= current_ignored_level) {
                    current_ignored_level = -1;
                }
                if (current_ignored_level == -1 && ignore_regex_compiled) {
                    if (regexec(&ignore_regex, line->text.c_str(), 0, NULL, 0) == 0) {
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
    if (!config->search_query.empty()) {
        regex_t regex;
        bool regex_compiled = false;
        if (config->use_regex) {
            int cflags = REG_EXTENDED;
            if (config->case_insensitive_search) cflags |= REG_ICASE;
            if (regcomp(&regex, config->search_query.c_str(), cflags) == 0) {
                regex_compiled = true;
            } else {
                fprintf(stderr, "Warning: Failed to compile regex '%s'\n", config->search_query.c_str());
            }
        }

        should_print_cache = (bool*)calloc(((int)lines_data.size()), sizeof(bool));
        for (int i = 0; i < ((int)lines_data.size()); i++) {
            if (is_ignored[i]) continue;
            if (config->headings_only && lines_data[i].type != TYPE_HEADING) continue;
            const char *text_to_search = lines_data[i].text.c_str();
            bool found = false;
            if (config->use_regex) {
                if (regex_compiled && regexec(&regex, text_to_search, 0, NULL, 0) == 0) {
                    found = true;
                }
            } else if (config->case_insensitive_search) {
                if (find_substring_case_insensitive(text_to_search, config->search_query.c_str())) {
                    found = true;
                }
            } else {
                if (strstr(text_to_search, config->search_query.c_str()) != NULL) {
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

    if (!config->search_query.empty()) {
        bool has_matches = false;
        for (int i = 0; i < ((int)lines_data.size()); i++) {
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



    for (int i = 0; i < ((int)lines_data.size()); i++) {
        ParsedLine *current_line = &lines_data[i];

        char prefix[MAX_LINE_LENGTH] = "";
        char full_prefix[MAX_LINE_LENGTH];
        snprintf(full_prefix, sizeof(full_prefix), "%s%s", global_prefix, prefix);
        int current_logical_level = 0; // Represents the indentation level we are currently at (0-based)
        bool is_last_sibling_in_current_scope = true;

        if (config->show_stats) {
            if (current_line->type == TYPE_HEADING) {
                g_stats.headings++;
            } else if (current_line->type == TYPE_BLOCKQUOTE) {
                g_stats.blockquotes++;
            } else if (current_line->type == TYPE_CODE_BLOCK_CONTENT) {
                g_stats.code_blocks++;
            } else if (current_line->type == TYPE_TABLE_CONTENT) {
                g_stats.tables++;
            } else if (current_line->type == TYPE_UNORDERED_LIST_ITEM || current_line->type == TYPE_ORDERED_LIST_ITEM || current_line->type == TYPE_TASK_LIST_ITEM_CHECKED || current_line->type == TYPE_TASK_LIST_ITEM_UNCHECKED) {
                g_stats.lists++;
                if (current_line->type == TYPE_TASK_LIST_ITEM_CHECKED) g_stats.completed_tasks++;
                if (current_line->type == TYPE_TASK_LIST_ITEM_UNCHECKED) g_stats.incomplete_tasks++;
            }
        }

        if (is_line_filtered(config, should_print_cache, is_ignored, current_line, i)) continue;

        if (current_line->type == TYPE_HEADING || current_line->type == TYPE_HORIZONTAL_RULE) {
            int effective_level = (current_line->type == TYPE_HEADING) ? current_line->level : get_horizontal_rule_level(i);
            current_logical_level = effective_level - 1;

            // Determine actual parent level to handle jumps
            int actual_parent_level = get_actual_parent_level(i);

            // Determine if current heading is the last sibling at its level
            is_last_sibling_in_current_scope = is_last_sibling_for_level(i, effective_level, config, should_print_cache, is_ignored);
            
            bool has_skipped_levels = (actual_parent_level < effective_level - 1);

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
            
            if (config->show_line_numbers) { printf("%s%*d%s %s ", COLOR_DIM, max_digits, current_line->original_line_num, COLOR_RESET, "|"); } 
            snprintf(full_prefix, sizeof(full_prefix), "%s%s", global_prefix, prefix); 
            
            if (current_line->type == TYPE_HORIZONTAL_RULE) {
                // Remove trailing space so the horizontal line attaches seamlessly to the tree connector
                size_t len = strlen(full_prefix);
                if (len > 0 && full_prefix[len - 1] == ' ') {
                    full_prefix[len - 1] = '\0';
                }
                
                int prefix_len = TextTable::visible_length(full_prefix);
                if (config->show_line_numbers) {
                    prefix_len += max_digits + 3;
                }
                int term_width = get_terminal_width();
                int hr_len = term_width - prefix_len;
                if (hr_len < 5) hr_len = 5;
                
                printf("%s", full_prefix);
                for (int m = 0; m < hr_len; m++) {
                    printf("%s", g_ascii_tree ? "-" : "─");
                }
                printf("\n");
            } else {
                printf("%s", full_prefix);
                // Apply colors based on heading level
                switch (effective_level) {
                    case 1: print_formatted_text(current_line->text.c_str(), COLOR_BOLD_BRIGHT_YELLOW, COLOR_RESET); break;
                    case 2: print_formatted_text(current_line->text.c_str(), COLOR_BOLD_BRIGHT_CYAN, COLOR_RESET); break;
                    case 3: print_formatted_text(current_line->text.c_str(), COLOR_GREEN, COLOR_RESET); break;
                    case 4: print_formatted_text(current_line->text.c_str(), COLOR_MAGENTA, COLOR_RESET); break;
                    case 5: print_formatted_text(current_line->text.c_str(), COLOR_BLUE, COLOR_RESET); break;
                    case 6: print_formatted_text(current_line->text.c_str(), COLOR_RED, COLOR_RESET); break;
                    default: print_formatted_text(current_line->text.c_str(), COLOR_RESET, COLOR_RESET); break;
                }
                printf("\n");
            }

        } else { // TYPE_UNORDERED_LIST_ITEM, TYPE_ORDERED_LIST_ITEM, TYPE_CONTENT, TYPE_EMPTY
            // Find the closest preceding heading's level (respecting horizontal rules as scope closers)
            int parent_heading_level = get_parent_heading_level_for_content(i);
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
            for (int k = i + 1; k < ((int)lines_data.size()); k++) {
                ParsedLine *next_line = &lines_data[k];
                if (next_line->type == TYPE_HEADING && next_line->level <= parent_heading_level) {
                    break; // Reached end of parent scope
                }
                if (config->show_hr && next_line->type == TYPE_HORIZONTAL_RULE && get_horizontal_rule_level(k) <= parent_heading_level) {
                    break; // Reached end of parent scope via HR
                }
                if (!is_line_filtered(config, should_print_cache, is_ignored, next_line, k)) {
                    if (next_line->type == TYPE_CONTENT || next_line->type == TYPE_CODE_BLOCK_CONTENT || 
                        next_line->type == TYPE_BLOCKQUOTE || next_line->type == TYPE_TABLE_CONTENT ||
                        (next_line->type == TYPE_HEADING && next_line->level > parent_heading_level)) {
                        is_last_child = false;
                        break;
                    }
                }
            }

            // Add list indentation
            int current_raw_indent_blocks = current_line->level / 4;
            
            if (current_line->type == TYPE_CONTENT || current_line->type == TYPE_CODE_BLOCK_CONTENT ||
                current_line->type == TYPE_BLOCKQUOTE || current_line->type == TYPE_TABLE_CONTENT) {
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

                    snprintf(full_prefix, sizeof(full_prefix), "%s%s", global_prefix, prefix);
                    char full_sub_prefix[MAX_LINE_LENGTH]; 
                    snprintf(full_sub_prefix, sizeof(full_sub_prefix), "%s%s", global_prefix, subsequent_prefix);
                    
                    int prefix_len = TextTable::visible_length(full_prefix);
                    if (config->show_line_numbers) prefix_len += max_digits + 3;
                    int term_width = get_terminal_width();
                    int max_width = term_width - prefix_len;
                    if (max_width < 10) max_width = 10;
                    
                    std::string code_text = current_line->text;
                    int current_cb_line = current_line->original_line_num;
                    
                    if (code_text.empty()) {
                        if (config->show_line_numbers) { printf("%s%*d%s %s ", COLOR_DIM, max_digits, current_cb_line, COLOR_RESET, "|"); } 
                        printf("%s%s%s\n", full_prefix, COLOR_CYAN, COLOR_RESET);
                    } else {
                        size_t pos = 0;
                        bool first_line = true;
                        while (pos < code_text.size()) {
                            size_t end = code_text.find('\n', pos);
                            if (end == std::string::npos) end = code_text.size();
                            std::string line = code_text.substr(pos, end - pos);
                            pos = end + 1;
                            
                            std::vector<std::string> wrapped_lines;
                            std::string remaining = line;
                            while (remaining.length() > (size_t)max_width) {
                                wrapped_lines.push_back(remaining.substr(0, max_width));
                                remaining = remaining.substr(max_width);
                            }
                            if (!remaining.empty() || wrapped_lines.empty()) {
                                wrapped_lines.push_back(remaining);
                            }
                            
                            for (size_t wi = 0; wi < wrapped_lines.size(); wi++) {
                                if (first_line && wi == 0) {
                                    if (config->show_line_numbers) { printf("%s%*d%s %s ", COLOR_DIM, max_digits, current_cb_line, COLOR_RESET, "|"); }
                                    printf("%s%s%s%s\n", full_prefix, COLOR_CYAN, wrapped_lines[wi].c_str(), COLOR_RESET);
                                    first_line = false;
                                } else if (wi == 0) {
                                    current_cb_line++;
                                    if (config->show_line_numbers) { printf("%s%*d%s %s ", COLOR_DIM, max_digits, current_cb_line, COLOR_RESET, "|"); }
                                    printf("%s%s%s%s\n", full_sub_prefix, COLOR_CYAN, wrapped_lines[wi].c_str(), COLOR_RESET);
                                } else {
                                    if (config->show_line_numbers) { printf("%*s %s ", max_digits, "", "|"); }
                                    // Use dimmed continuation prefix: ↳
                                    printf("%s%s↳ %s%s%s\n", full_sub_prefix, COLOR_DIM, COLOR_CYAN, wrapped_lines[wi].c_str(), COLOR_RESET);
                                }
                            }
                        }
                    }
                } else if (current_line->type == TYPE_TABLE_CONTENT) {
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

                    TextTable t;
                    std::string code_text = current_line->text;
                    size_t pos = 0;
                    while (pos < code_text.size()) {
                        size_t end = code_text.find('\n', pos);
                        if (end == std::string::npos) end = code_text.size();
                        std::string line = code_text.substr(pos, end - pos);
                        pos = end + 1;
                        
                        bool is_divider = true;
                        for (char c : line) {
                            if (c != '|' && c != '-' && c != ':' && c != ' ' && c != '\r') {
                                is_divider = false;
                                break;
                            }
                        }
                        if (is_divider) continue;
                        
                        size_t col_pos = 0;
                        if (line.size() > 0 && line[0] == '|') col_pos = 1;
                        while (col_pos < line.size()) {
                            size_t col_end = line.find('|', col_pos);
                            if (col_end == std::string::npos) col_end = line.size();
                            std::string cell = line.substr(col_pos, col_end - col_pos);
                            size_t start = cell.find_first_not_of(" \r\n");
                            if (start != std::string::npos) {
                                size_t end_cell = cell.find_last_not_of(" \r\n");
                                cell = cell.substr(start, end_cell - start + 1);
                            } else {
                                cell = "";
                            }
                            // Avoid adding an empty column if it's trailing due to the last '|'
                            if (!(col_end == line.size() && cell == "")) {
                                t.add(cell);
                            }
                            col_pos = col_end + 1;
                        }
                        t.endOfRow();
                    }
                    
                    snprintf(full_prefix, sizeof(full_prefix), "%s%s", global_prefix, subsequent_prefix);
                    int prefix_len = TextTable::visible_length(full_prefix);
                    if (config->show_line_numbers) prefix_len += max_digits + 3;
                    int term_width = get_terminal_width();
                    int max_total_width = term_width - prefix_len;
                    if (max_total_width < 10) max_total_width = 10; // Sanity check

                    auto table_lines = t.get_lines(max_total_width);
                    int current_cb_line = current_line->original_line_num;
                    
                    if (table_lines.empty()) {
                        printf("\n");
                    } else {
                        for (size_t i = 0; i < table_lines.size(); i++) {
                            const std::string& line = table_lines[i].first;
                            bool is_new_md_line = table_lines[i].second;
                            
                            char full_sub_prefix[MAX_LINE_LENGTH]; 
                            snprintf(full_sub_prefix, sizeof(full_sub_prefix), "%s%s", global_prefix, subsequent_prefix); 
                            
                            if (is_new_md_line) {
                                if (config->show_line_numbers) { printf("%s%*d%s %s ", COLOR_DIM, max_digits, current_cb_line++, COLOR_RESET, "|"); } 
                            } else {
                                if (config->show_line_numbers) { printf("%*s %s ", max_digits, "", "|"); } 
                            }
                            
                            printf("%s", full_sub_prefix);
                            print_formatted_text(line.c_str(), COLOR_BRIGHT_WHITE, COLOR_RESET);
                            printf("\n");
                        }
                    }
                } else if (current_line->type == TYPE_BLOCKQUOTE) {
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
                    
                    snprintf(full_prefix, sizeof(full_prefix), "%s%s", global_prefix, prefix);
                    char full_sub_prefix[MAX_LINE_LENGTH];
                    snprintf(full_sub_prefix, sizeof(full_sub_prefix), "%s%s", global_prefix, subsequent_prefix);
                    
                    int prefix_len = TextTable::visible_length(full_prefix);
                    if (config->show_line_numbers) prefix_len += max_digits + 3;
                    int term_width = get_terminal_width();
                    
                    std::string code_text = current_line->text;
                    size_t pos = 0;
                    bool first_line = true;
                    int current_cb_line = current_line->original_line_num;
                    
                    if (code_text.empty()) {
                        if (config->show_line_numbers) { printf("%s%*d%s %s ", COLOR_DIM, max_digits, current_line->original_line_num, COLOR_RESET, "|"); } 
                        printf("%s\n", full_prefix);
                    } else {
                        while (pos < code_text.size()) {
                            size_t end = code_text.find('\n', pos);
                            if (end == std::string::npos) end = code_text.size();
                            std::string line = code_text.substr(pos, end - pos);
                            pos = end + 1;
                            
                            const char *display_text = line.c_str();
                            int bq_level = 0;
                            while (*display_text == '>' || *display_text == ' ') {
                                if (*display_text == '>') bq_level++;
                                display_text++;
                            }
                            if (bq_level < 1) bq_level = 1;
                            
                            char bq_marker[64] = "";
                            for (int i = 0; i < bq_level - 1; i++) strcat(bq_marker, INDENT_STR);
                            strcat(bq_marker, COLOR_DIM);
                            strcat(bq_marker, ">");
                            strcat(bq_marker, COLOR_RESET);
                            strcat(bq_marker, " ");
                            
                            int bq_marker_len = (bq_level - 1) * 4 + 2;
                            int max_width = term_width - prefix_len - bq_marker_len;
                            if (max_width < 10) max_width = 10;
                            
                            std::vector<std::string> wrapped_lines = TextTable::wrap_text(display_text, max_width);
                            if (wrapped_lines.empty()) wrapped_lines.push_back("");
                            
                            for (size_t wi = 0; wi < wrapped_lines.size(); wi++) {
                                if (first_line && wi == 0) {
                                    if (config->show_line_numbers) { printf("%s%*d%s %s ", COLOR_DIM, max_digits, current_cb_line++, COLOR_RESET, "|"); } 
                                    printf("%s%s", full_prefix, bq_marker);
                                    print_formatted_text(wrapped_lines[wi].c_str(), COLOR_BRIGHT_GREEN, COLOR_RESET);
                                    printf("\n");
                                    first_line = false;
                                } else if (wi == 0) {
                                    if (config->show_line_numbers) { printf("%s%*d%s %s ", COLOR_DIM, max_digits, current_cb_line++, COLOR_RESET, "|"); } 
                                    printf("%s%s", full_sub_prefix, bq_marker);
                                    print_formatted_text(wrapped_lines[wi].c_str(), COLOR_BRIGHT_GREEN, COLOR_RESET);
                                    printf("\n");
                                } else {
                                    if (config->show_line_numbers) { printf("%*s %s ", max_digits, "", "|"); } 
                                    char bq_marker_sub[64] = "";
                                    for (int m = 0; m < bq_marker_len && m < 63; m++) bq_marker_sub[m] = ' ';
                                    bq_marker_sub[bq_marker_len] = '\0';
                                    
                                    printf("%s%s", full_sub_prefix, bq_marker_sub);
                                    print_formatted_text(wrapped_lines[wi].c_str(), COLOR_BRIGHT_GREEN, COLOR_RESET);
                                    printf("\n");
                                }
                            }
                        }
                    }
                } else {
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
                    
                    snprintf(full_prefix, sizeof(full_prefix), "%s%s", global_prefix, prefix);
                    char full_sub_prefix[MAX_LINE_LENGTH];
                    snprintf(full_sub_prefix, sizeof(full_sub_prefix), "%s%s", global_prefix, subsequent_prefix);
                    
                    int prefix_len = TextTable::visible_length(full_prefix);
                    if (config->show_line_numbers) {
                        prefix_len += max_digits + 3;
                    }
                    int term_width = get_terminal_width();
                    
                    int max_width_first = term_width - prefix_len;
                    if (max_width_first < 10) max_width_first = 10;
                    
                    std::vector<std::string> wrapped_lines = TextTable::wrap_text(current_line->text, max_width_first);
                    if (wrapped_lines.empty()) wrapped_lines.push_back("");
                    
                    int current_cb_line = current_line->original_line_num;
                    
                    for (size_t wi = 0; wi < wrapped_lines.size(); wi++) {
                        if (wi == 0) {
                            if (config->show_line_numbers) { printf("%s%*d%s %s ", COLOR_DIM, max_digits, current_cb_line++, COLOR_RESET, "|"); } 
                            printf("%s", full_prefix); 
                            print_formatted_text(wrapped_lines[wi].c_str(), COLOR_BRIGHT_WHITE, COLOR_RESET);
                            printf("\n");
                        } else {
                            // If first line wrapped, subsequent lines need to fit in term_width - sub_prefix_len
                            // Wait, if it wrapped, we should ideally re-wrap, but wrap_text is done in one pass.
                            // Since sub_prefix_len is usually the same length as prefix_len, it's fine.
                            if (config->show_line_numbers) { printf("%*s %s ", max_digits, "", "|"); } 
                            printf("%s", full_sub_prefix); 
                            print_formatted_text(wrapped_lines[wi].c_str(), COLOR_BRIGHT_WHITE, COLOR_RESET);
                            printf("\n");
                        }
                    }
                }
            } else if (current_line->type == TYPE_UNORDERED_LIST_ITEM || 
                       current_line->type == TYPE_TASK_LIST_ITEM_UNCHECKED || 
                       current_line->type == TYPE_TASK_LIST_ITEM_CHECKED ||
                       current_line->type == TYPE_ORDERED_LIST_ITEM) {
                
                if (is_last_child) {
                    strcat(prefix, INDENT_STR);
                } else {
                    strcat(prefix, PIPE_STR);
                }
                for (int j = 0; j < current_raw_indent_blocks; j++) {
                    strcat(prefix, INDENT_STR);
                }
                
                char item_marker[64] = "";
                char item_marker_sub[64] = "";
                if (current_line->type == TYPE_TASK_LIST_ITEM_CHECKED) {
                    snprintf(item_marker, sizeof(item_marker), "%s[%s✓%s%s] %s", COLOR_DIM, COLOR_BRIGHT_GREEN, COLOR_RESET, COLOR_DIM, COLOR_RESET);
                    snprintf(item_marker_sub, sizeof(item_marker_sub), "    ");
                } else if (current_line->type == TYPE_TASK_LIST_ITEM_UNCHECKED) {
                    snprintf(item_marker, sizeof(item_marker), "%s[ ] %s", COLOR_DIM, COLOR_RESET);
                    snprintf(item_marker_sub, sizeof(item_marker_sub), "    ");
                } else if (current_line->type == TYPE_ORDERED_LIST_ITEM) {
                    snprintf(item_marker, sizeof(item_marker), "%s%d.%s ", COLOR_BRIGHT_BLUE, current_line->list_number, COLOR_RESET);
                    int marker_len = snprintf(NULL, 0, "%d. ", current_line->list_number);
                    for (int m = 0; m < marker_len && m < 63; m++) item_marker_sub[m] = ' ';
                    item_marker_sub[marker_len] = '\0';
                } else {
                    snprintf(item_marker, sizeof(item_marker), "%s", BULLET_POINT);
                    int marker_len = TextTable::visible_length(BULLET_POINT);
                    for (int m = 0; m < marker_len && m < 63; m++) item_marker_sub[m] = ' ';
                    item_marker_sub[marker_len] = '\0';
                }
                
                snprintf(full_prefix, sizeof(full_prefix), "%s%s", global_prefix, prefix); 
                
                int prefix_len = TextTable::visible_length(full_prefix) + TextTable::visible_length(item_marker);
                if (config->show_line_numbers) {
                    prefix_len += max_digits + 3;
                }
                int term_width = get_terminal_width();
                int max_width = term_width - prefix_len;
                if (max_width < 10) max_width = 10;
                
                std::vector<std::string> wrapped_lines = TextTable::wrap_text(current_line->text, max_width);
                if (wrapped_lines.empty()) wrapped_lines.push_back("");
                
                int current_cb_line = current_line->original_line_num;
                
                const char* text_color = (current_line->type == TYPE_TASK_LIST_ITEM_CHECKED) ? COLOR_DIM : COLOR_BRIGHT_WHITE;
                for (size_t wi = 0; wi < wrapped_lines.size(); wi++) {
                    if (wi == 0) {
                        if (config->show_line_numbers) { printf("%s%*d%s %s ", COLOR_DIM, max_digits, current_cb_line, COLOR_RESET, "|"); } 
                        printf("%s%s", full_prefix, item_marker); 
                        print_formatted_text(wrapped_lines[wi].c_str(), text_color, COLOR_RESET);
                        printf("\n");
                    } else {
                        if (config->show_line_numbers) { printf("%*s %s ", max_digits, "", "|"); } 
                        printf("%s%s", full_prefix, item_marker_sub); 
                        print_formatted_text(wrapped_lines[wi].c_str(), text_color, COLOR_RESET);
                        printf("\n");
                    }
                }
            }
            
        }
    }


    // Print Warnings
    bool has_any_warnings = (warnings.size() > 0) || (h1_lines.size() > 1);
    for (size_t i = 0; i < seen_headings.size(); i++) {
        if (seen_headings[i].duplicate_lines.size() > 0) {
            has_any_warnings = true;
            break;
        }
    }

    if (!config->suppress_warnings && has_any_warnings) {
        int term_width = get_terminal_width();
        int heading_len = 17; // " Linter Warnings "
        int total_dashes = term_width - heading_len - 1; // Leave 1 char padding to prevent auto-wrap
        if (total_dashes < 4) total_dashes = 4;
        
        int left_dashes = total_dashes / 2;
        int right_dashes = total_dashes - left_dashes;
        const char* single_dash = g_ascii_tree ? "-" : "─";
        
        printf("\n%s", COLOR_BRIGHT_YELLOW);
        for (int m = 0; m < left_dashes; m++) printf("%s", single_dash);
        printf(" Linter Warnings ");
        for (int m = 0; m < right_dashes; m++) printf("%s", single_dash);
        printf("%s\n", COLOR_RESET);
        for (size_t j = 0; j < warnings.size(); j++) {
            printf("%sLine %d: %s%s\n", COLOR_YELLOW, warnings[j].line_number, warnings[j].message.c_str(), COLOR_RESET);
        }
        
        if (h1_lines.size() > 1) {
            printf("%sLine %d: Multiple H1 headings detected (also at lines ", COLOR_YELLOW, h1_lines[0]);
            for (size_t i = 1; i < h1_lines.size(); i++) {
                printf("%d%s", h1_lines[i], i == h1_lines.size() - 1 ? "" : ", ");
            }
            printf(")%s\n", COLOR_RESET);
        }
        
        for (size_t i = 0; i < seen_headings.size(); i++) {
            if (seen_headings[i].duplicate_lines.size() > 0) {
                printf("%sLine %d: Duplicate heading: '%s' (also at lines ", COLOR_YELLOW, seen_headings[i].first_line, seen_headings[i].text.c_str());
                for (size_t j = 0; j < seen_headings[i].duplicate_lines.size(); j++) {
                    printf("%d%s", seen_headings[i].duplicate_lines[j], j == seen_headings[i].duplicate_lines.size() - 1 ? "" : ", ");
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