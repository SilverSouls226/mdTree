#ifndef TEXTTABLE_H
#define TEXTTABLE_H

#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cstring>
#include "types.h"

class TextTable {
public:
    TextTable() {
        if (g_ascii_tree) {
            _h = "-"; _v = "|";
            _tl = "+"; _tr = "+"; _bl = "+"; _br = "+";
            _tm = "+"; _bm = "+"; _lm = "+"; _rm = "+"; _mm = "+";
        } else {
            _h = "═"; _v = "║";
            _tl = "╔"; _tr = "╗"; _bl = "╚"; _br = "╝";
            _tm = "╦"; _bm = "╩"; _lm = "╠"; _rm = "╣"; _mm = "╬";
        }
    }

    void add(const std::string &content) {
        _current.push_back(content);
    }

    void endOfRow() {
        _rows.push_back(_current);
        _current.clear();
    }

    static unsigned int visible_length(const std::string &content) {
        unsigned int len = 0;
        const char *current = content.c_str();
        while (*current != '\0') {
            if (*current == '\\' && *(current + 1) != '\0') {
                len++;
                current += 2;
            } else if (*current == '`') {
                const char *end = strchr(current + 1, '`');
                if (end) {
                    len += (end - current - 1);
                    current = end + 1;
                } else {
                    len++;
                    current++;
                }
            } else if (*current == '!' && *(current + 1) == '[') {
                const char *close_bracket = strchr(current + 2, ']');
                if (close_bracket && *(close_bracket + 1) == '(') {
                    const char *close_paren = strchr(close_bracket + 2, ')');
                    if (close_paren) {
                        len += 2; 
                        len += (close_bracket - (current + 2)); 
                        current = close_paren + 1;
                        continue;
                    }
                }
                len++;
                current++;
            } else if (*current == '[') {
                const char *close_bracket = strchr(current + 1, ']');
                if (close_bracket && *(close_bracket + 1) == '(') {
                    const char *close_paren = strchr(close_bracket + 2, ')');
                    if (close_paren) {
                        len += (close_bracket - (current + 1)); 
                        current = close_paren + 1;
                        continue;
                    }
                }
                len++;
                current++;
            } else if (strncmp(current, "**", 2) == 0) {
                const char *end = strstr(current + 2, "**");
                if (end) {
                    len += (end - current - 2);
                    current = end + 2;
                } else {
                    len += 2;
                    current += 2;
                }
            } else if (strncmp(current, "==", 2) == 0) {
                const char *end = strstr(current + 2, "==");
                if (end) {
                    len += (end - current - 2);
                    current = end + 2;
                } else {
                    len += 2;
                    current += 2;
                }
            } else if (strncmp(current, "~~", 2) == 0) {
                const char *end = strstr(current + 2, "~~");
                if (end) {
                    len += (end - current - 2);
                    current = end + 2;
                } else {
                    len += 2;
                    current += 2;
                }
            } else {
                if ((*current & 0xC0) != 0x80) {
                    len++;
                }
                current++;
            }
        }
        return len;
    }

    struct MarkdownSpan {
        size_t start;
        size_t end;
        std::string marker;
    };

    static std::vector<MarkdownSpan> find_spans(const std::string& text) {
        std::vector<MarkdownSpan> spans;
        size_t pos = 0;
        while (pos < text.length()) {
            if (text[pos] == '\\' && pos + 1 < text.length()) {
                pos += 2;
                continue;
            }
            if (text[pos] == '`') {
                size_t end = text.find('`', pos + 1);
                if (end != std::string::npos) {
                    spans.push_back({pos, end, "`"});
                    pos = end + 1;
                    continue;
                }
            }
            if (pos + 1 < text.length()) {
                std::string marker = text.substr(pos, 2);
                if (marker == "**" || marker == "==" || marker == "~~") {
                    size_t end = text.find(marker, pos + 2);
                    if (end != std::string::npos) {
                        spans.push_back({pos, end + 1, marker});
                        pos = end + 2;
                        continue;
                    }
                }
            }
            pos++;
        }
        return spans;
    }

    static std::vector<std::string> wrap_text(const std::string& text, int max_width) {
        std::vector<std::string> lines;
        std::string current_line = "";
        
        std::vector<MarkdownSpan> spans = find_spans(text);
        
        size_t pos = 0;

        while (pos < text.length()) {
            size_t space_pos = text.find(' ', pos);
            if (space_pos == std::string::npos) space_pos = text.length();
            
            std::string word = text.substr(pos, space_pos - pos);
            std::string candidate = current_line + (current_line.empty() ? "" : " ") + word;
            
            if (visible_length(candidate) <= (unsigned int)max_width) {
                current_line = candidate;
            } else {
                if (!current_line.empty()) {
                    // The split is happening BEFORE 'word'. The boundary index is `pos`.
                    size_t split_pos = pos > 0 ? pos - 1 : 0;
                    
                    std::string closing_markers = "";
                    std::string opening_markers = "";
                    
                    for (const auto& span : spans) {
                        // Span is active if it started before the split and ends at or after the split.
                        if (span.start < split_pos && span.end >= split_pos) {
                            closing_markers = span.marker + closing_markers; 
                            opening_markers += span.marker; 
                        }
                    }
                    lines.push_back(current_line + closing_markers);
                    current_line = opening_markers + word;
                } else {
                    // Force wrap mid-word
                    lines.push_back(word);
                    current_line = "";
                }
            }
            pos = space_pos + 1;
        }
        if (!current_line.empty()) {
            std::string closing_markers = "";
            for (const auto& span : spans) {
                if (span.start < text.length() && span.end >= text.length()) {
                    closing_markers = span.marker + closing_markers;
                }
            }
            lines.push_back(current_line + closing_markers);
        }
        if (lines.empty()) lines.push_back("");
        
        for (auto& l : lines) {
            if (!l.empty() && l.back() == ' ') {
                l.pop_back();
            }
        }
        return lines;
    }

    std::string str(int max_total_width = -1) const {
        if (_rows.empty()) return "";
        int num_cols = 0;
        for (const auto& row : _rows) {
            if ((int)row.size() > num_cols) num_cols = row.size();
        }
        if (num_cols == 0) return "";

        std::vector<int> max_widths(num_cols, 0);
        std::vector<int> min_widths(num_cols, 1);
        for (const auto &row : _rows) {
            for (size_t i = 0; i < row.size(); i++) {
                max_widths[i] = std::max(max_widths[i], (int)visible_length(row[i]));
                
                size_t pos = 0;
                while (pos < row[i].length()) {
                    size_t space_pos = row[i].find(' ', pos);
                    if (space_pos == std::string::npos) space_pos = row[i].length();
                    std::string word = row[i].substr(pos, space_pos - pos);
                    min_widths[i] = std::max(min_widths[i], (int)visible_length(word));
                    pos = space_pos + 1;
                }
            }
        }

        int total_natural_width = 1;
        for (int w : max_widths) total_natural_width += w + 3;

        std::vector<int> col_widths = max_widths;
        if (max_total_width > 0 && total_natural_width > max_total_width) {
            int available = max_total_width - 1 - (num_cols * 3); 
            if (available < num_cols) available = num_cols; 
            
            int total_max = 0;
            for (int w : max_widths) total_max += w;
            
            if (total_max > 0) {
                int allocated = 0;
                for (int i = 0; i < num_cols; i++) {
                    col_widths[i] = std::max(min_widths[i], (max_widths[i] * available) / total_max);
                    allocated += col_widths[i];
                }
                if (allocated < available) {
                    col_widths[num_cols - 1] += (available - allocated);
                }
            }
        }

        std::string out;
        auto printBorder = [&](const std::string& left, const std::string& mid, const std::string& right) {
            out += left;
            for (int i = 0; i < num_cols; i++) {
                for (int j = 0; j < col_widths[i] + 2; j++) out += _h;
                if (i < num_cols - 1) out += mid;
                else out += right;
            }
            out += "\n";
        };

        printBorder(_tl, _tm, _tr);

        for (size_t r = 0; r < _rows.size(); r++) {
            auto &row = _rows[r];
            std::vector<std::vector<std::string>> cell_lines(num_cols);
            int max_sub_lines = 1;
            for (int i = 0; i < num_cols; i++) {
                std::string content = i < (int)row.size() ? row[i] : "";
                cell_lines[i] = wrap_text(content, col_widths[i]);
                if ((int)cell_lines[i].size() > max_sub_lines) max_sub_lines = cell_lines[i].size();
            }

            for (int sub = 0; sub < max_sub_lines; sub++) {
                out += _v;
                for (int i = 0; i < num_cols; i++) {
                    std::string content = sub < (int)cell_lines[i].size() ? cell_lines[i][sub] : "";
                    int padding = std::max(0, col_widths[i] - (int)visible_length(content));
                    
                    out += " " + content;
                    for (int p = 0; p < padding; p++) out += " ";
                    out += " " + _v;
                }
                out += "\n";
            }
            
            if (r < _rows.size() - 1) {
                printBorder(_lm, _mm, _rm);
            }
        }
        
        printBorder(_bl, _bm, _br);

        return out;
    }

private:
    std::string _h, _v, _tl, _tr, _bl, _br, _tm, _bm, _lm, _rm, _mm;
    std::vector<std::vector<std::string>> _rows;
    std::vector<std::string> _current;
};
#endif