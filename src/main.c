#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <getopt.h>
#include <dirent.h>
#include <sys/stat.h>

#include "types.h"
#include "utils.h"
#include "parser.h"

int filter_md(const struct dirent *entry) {
    if (entry->d_name[0] == '.') return 0; // Ignore dot files
    return 1;
}

void process_directory(const char *dirpath, const char *global_prefix, int max_level_filter);

void process_directory(const char *dirpath, const char *global_prefix, int max_level_filter) {
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
        snprintf(next_prefix, sizeof(next_prefix), "%s%s", global_prefix, is_last ? "    " : "│   ");
        
        char item_prefix[MAX_LINE_LENGTH];
        snprintf(item_prefix, sizeof(item_prefix), "%s%s", global_prefix, is_last ? "└── " : "├── ");
        
        if (S_ISDIR(st.st_mode)) {
            printf("%s%s\n", item_prefix, namelist[i]->d_name);
            process_directory(path, next_prefix, max_level_filter);
        } else {
            printf("%s%s\n", item_prefix, namelist[i]->d_name);
            process_markdown_file(path, next_prefix, max_level_filter);
        }
        
        free(namelist[i]);
        processed++;
    }
    free(namelist);
}
int main(int argc, char *argv[]) {

    int max_level_filter = MAX_AWK_LEVEL;
    int opt;
    int option_index = 0;
    static struct option long_options[] = {
        {"help", no_argument, 0,  'h' },
        {"depth", required_argument, 0, 'd' },
        {0,      0,           0,   0  }
    };

    while ((opt = getopt_long(argc, argv, "d:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'd':
                max_level_filter = atoi(optarg);
                if (max_level_filter < 1 || max_level_filter > MAX_AWK_LEVEL) {
                    fprintf(stderr, "Error: Invalid level for -d/--depth. Must be between 1 and %d.\n", MAX_AWK_LEVEL);
                    display_help();
                    return EXIT_FAILURE;
                }
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
    
    struct stat st;
    if (stat(target_path, &st) != 0) {
        perror("Error accessing path");
        return EXIT_FAILURE;
    }
    
    printf("%s\n", target_path);
    if (S_ISDIR(st.st_mode)) {
        process_directory(target_path, "", max_level_filter);
    } else {
        process_markdown_file(target_path, "", max_level_filter);
    }
    
    return EXIT_SUCCESS;
}