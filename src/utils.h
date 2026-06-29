#ifndef UTILS_H
#define UTILS_H

void display_help();
void apply_config(Config *config);
int count_words(const char *text);
int get_raw_indentation_level(const char *line);
void print_formatted_text(const char *text, const char *initial_color_code, const char *reset_color_code);
int get_terminal_width();
bool find_substring_case_insensitive(const char *haystack, const char *needle);

#endif
