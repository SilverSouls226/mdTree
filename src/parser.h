#ifndef PARSER_H
#define PARSER_H

#include "types.h"

bool process_markdown_file(const char *md_file_path, const char *global_prefix, Config *config, const char *item_prefix, const char *filename);

#endif
