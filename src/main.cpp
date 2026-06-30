#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <getopt.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string>

#include "types.h"
#include "utils.h"
#include "parser.h"

int filter_md(const struct dirent *entry) {
    if (entry->d_name[0] == '.') return 0; // Ignore dot files
    return 1;
}

void process_directory(const char *dirpath, const char *global_prefix, Config *config);

void process_directory(const char *dirpath, const char *global_prefix, Config *config) {
    struct dirent **namelist;
    int n = scandir(dirpath, &namelist, filter_md, alphasort);
    if (n < 0) {
        perror("scandir");
        return;
    }
    
    int valid_count = 0;
    for (int i = 0; i < n; i++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dirpath, namelist[i]->d_name);
        struct stat st;
        if (stat(path, &st) == 0) {
            if (S_ISDIR(st.st_mode) || strstr(namelist[i]->d_name, ".md") != NULL) {
                valid_count++;
            } else {
                free(namelist[i]);
                namelist[i] = NULL;
            }
        } else {
            free(namelist[i]);
            namelist[i] = NULL;
        }
    }
    
    int processed = 0;
    for (int i = 0; i < n; i++) {
        if (namelist[i] == NULL) continue;
        
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dirpath, namelist[i]->d_name);
        struct stat st;
        stat(path, &st);
        
        bool is_last = (processed == valid_count - 1);
        char next_prefix[MAX_LINE_LENGTH];
        snprintf(next_prefix, sizeof(next_prefix), "%s%s", global_prefix, is_last ? "    " : (config->ascii_tree ? "|   " : "│   "));
        
        char item_prefix[MAX_LINE_LENGTH];
        snprintf(item_prefix, sizeof(item_prefix), "%s%s", global_prefix, is_last ? (config->ascii_tree ? "`-- " : "└── ") : (config->ascii_tree ? "|-- " : "├── "));
        
        if (S_ISDIR(st.st_mode)) {
            printf("%s%s\n", item_prefix, namelist[i]->d_name);
            process_directory(path, next_prefix, config);
        } else {
            process_markdown_file(path, next_prefix, config, item_prefix, namelist[i]->d_name);
        }
        
        free(namelist[i]);
        processed++;
    }
    free(namelist);
}
int main(int argc, char *argv[]) {

    Config config = { MAX_AWK_LEVEL, false, false, "", false, false, false, false, false, false, "", false, "", false };
    int opt;
    int option_index = 0;
    static struct option long_options[] = {
        {"help", no_argument, 0, 'h' },
        {"depth", required_argument, 0, 'd' },
        {"line-numbers", no_argument, 0, 'n' },
        {"no-warnings", no_argument, 0, 'w' },
        {"version", no_argument, 0, 'v' },
        {"find", required_argument, 0, 'f' },
        {"case-insensitive", no_argument, 0, 'i' },
        {"regex", required_argument, 0, 'r' },
        {"no-color", no_argument, 0, 'c' },
        {"ascii", no_argument, 0, 'a' },
        {"stats", no_argument, 0, 's' },
        {"ignore", required_argument, 0, 'I' },
        {"headings-only", no_argument, 0, 'H' },
        {"show-hr", no_argument, 0, 'R' },
        {"focus", required_argument, 0, 'F' },
        {"no-pager", no_argument, 0, 'P' },
        {0,      0,           0,   0  }
    };

    while ((opt = getopt_long(argc, argv, "d:hnwvf:ir:casI:HRF:P", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'd':
                config.max_level_filter = atoi(optarg);
                if (config.max_level_filter < 1 || config.max_level_filter > MAX_AWK_LEVEL) {
                    fprintf(stderr, "Error: Invalid level for -d/--depth. Must be between 1 and %d.\n", MAX_AWK_LEVEL);
                    display_help();
                    return EXIT_FAILURE;
                }
                break;
            case 'n':
                config.show_line_numbers = true;
                break;
            case 'w':
                config.suppress_warnings = true;
                break;
            case 'v':
                printf("mdtree version 1.0.0\n");
                return EXIT_SUCCESS;
            case 'f':
                config.search_query = optarg;
                config.use_regex = false;
                break;
            case 'i':
                config.case_insensitive_search = true;
                break;
            case 'r':
                config.search_query = optarg;
                config.use_regex = true;
                break;
            case 'c':
                config.no_color = true;
                break;
            case 'a':
                config.ascii_tree = true;
                break;
            case 's':
                config.show_stats = true;
                break;
            case 'I':
                config.ignore_query = optarg;
                break;
            case 'H':
                config.headings_only = true;
                break;
            case 'R':
                config.show_hr = true;
                break;
            case 'F':
                config.focus_query = optarg;
                break;
            case 'P':
                config.no_pager = true;
                break;
            case 'h':
                display_help();
                return EXIT_SUCCESS;
            case '?':
                fprintf(stderr, "Error: Unknown option.\n");
                display_help();
                return EXIT_FAILURE;
            case ':':
                fprintf(stderr, "Error: Option requires an argument.\n");
                display_help();
                return EXIT_FAILURE;
        }
    }

    const char *target_path = ".";
    if (optind < argc) {
        target_path = argv[optind];
    }

    if (optind < argc - 1) {
        fprintf(stderr, "Error: Too many arguments provided.\n");
        fprintf(stderr, "If you are trying to use a long flag like --find, make sure to use two dashes (--).\n");
        display_help();
        return EXIT_FAILURE;
    }

    apply_config(&config);

    struct stat st;
    if (stat(target_path, &st) != 0) {
        perror("Error accessing path");
        return EXIT_FAILURE;
    }
    
    bool use_pager = false;
    FILE *temp_out = NULL;
    int original_stdout_fd = -1;

    if (isatty(STDOUT_FILENO) && !config.no_pager) {
        use_pager = true;
        temp_out = tmpfile();
        if (temp_out != NULL) {
            original_stdout_fd = dup(STDOUT_FILENO);
            dup2(fileno(temp_out), STDOUT_FILENO);
        } else {
            use_pager = false;
        }
    }

    if (S_ISDIR(st.st_mode)) {
        printf("%s\n", target_path);
        process_directory(target_path, "", &config);
    } else {
        process_markdown_file(target_path, "", &config, "", target_path);
    }
    
    if (config.show_stats) {
        printf("\n\n────────────────────────────── Statistics ──────────────────────────────\n");
        printf("Files parsed:      %d\n", g_stats.files_parsed);
        printf("Characters:        %d\n", g_stats.characters);
        printf("Words:             %d (approx. %d min reading time)\n", g_stats.words, (g_stats.words / 200 > 0 ? g_stats.words / 200 : 1));
        printf("Total lines:       %d (%d non-empty, %d empty)\n", g_stats.non_empty_lines + g_stats.empty_lines, g_stats.non_empty_lines, g_stats.empty_lines);
        printf("\n");
        printf("Headings:          %d\n", g_stats.headings);
        printf("Lists:             %d\n", g_stats.lists);
        if (g_stats.completed_tasks > 0 || g_stats.incomplete_tasks > 0) {
            int total_tasks = g_stats.completed_tasks + g_stats.incomplete_tasks;
            printf("Tasks:             %d (%d completed, %d incomplete - %.1f%%)\n", total_tasks, g_stats.completed_tasks, g_stats.incomplete_tasks, (g_stats.completed_tasks * 100.0) / total_tasks);
        }
        printf("Code blocks:       %d\n", g_stats.code_blocks);
        printf("Tables:            %d rows\n", g_stats.tables);
        printf("Blockquotes:       %d\n", g_stats.blockquotes);
        printf("Links:             %d\n", g_stats.links);
        printf("Images:            %d\n", g_stats.images);
    }

    if (use_pager && temp_out != NULL) {
        fflush(stdout);
        dup2(original_stdout_fd, STDOUT_FILENO);
        close(original_stdout_fd);

        rewind(temp_out);

        std::string output_content;
        char buf[4096];
        int line_count = 0;
        while (fgets(buf, sizeof(buf), temp_out) != NULL) {
            output_content += buf;
        }

        for (size_t i = 0; i < output_content.length(); i++) {
            if (output_content[i] == '\n') {
                line_count++;
            }
        }

        int term_height = 24;
        struct winsize w;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_row > 0) {
            term_height = w.ws_row;
        }

        if (line_count > term_height) {
            FILE *pager = popen("less -R", "w");
            if (pager != NULL) {
                fwrite(output_content.c_str(), 1, output_content.length(), pager);
                pclose(pager);
            } else {
                fwrite(output_content.c_str(), 1, output_content.length(), stdout);
            }
        } else {
            fwrite(output_content.c_str(), 1, output_content.length(), stdout);
        }

        fclose(temp_out);
    }

    return EXIT_SUCCESS;
}