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

    static std::string strip_markdown(const std::string &content) {
        std::string stripped;
        const char *current = content.c_str();
        while (*current != '\0') {
            if (*current == '\\' && *(current + 1) != '\0') {
                stripped += *(current + 1);
                current += 2;
            } else if (*current == '`') {
                const char *end = strchr(current + 1, '`');
                if (end) {
                    stripped.append(current + 1, end - (current + 1));
                    current = end + 1;
                } else {
                    stripped += '`';
                    current++;
                }
            } else if (*current == '!' && *(current + 1) == '[') {
                const char *close_bracket = strchr(current + 2, ']');
                if (close_bracket && *(close_bracket + 1) == '(') {
                    const char *close_paren = strchr(close_bracket + 2, ')');
                    if (close_paren) {
                        stripped += "🖼️ ";
                        std::string inner(current + 2, close_bracket - (current + 2));
                        stripped += strip_markdown(inner);
                        current = close_paren + 1;
                        continue;
                    }
                }
                stripped += '!';
                current++;
            } else if (*current == '[') {
                const char *close_bracket = strchr(current + 1, ']');
                if (close_bracket && *(close_bracket + 1) == '(') {
                    const char *close_paren = strchr(close_bracket + 2, ')');
                    if (close_paren) {
                        std::string inner(current + 1, close_bracket - (current + 1));
                        stripped += strip_markdown(inner);
                        current = close_paren + 1;
                        continue;
                    }
                }
                stripped += '[';
                current++;
            } else if (strncmp(current, "**", 2) == 0) {
                const char *end = strstr(current + 2, "**");
                if (end) {
                    std::string inner(current + 2, end - (current + 2));
                    stripped += strip_markdown(inner);
                    current = end + 2;
                } else {
                    stripped += "**";
                    current += 2;
                }
            } else if (strncmp(current, "==", 2) == 0) {
                const char *end = strstr(current + 2, "==");
                if (end) {
                    std::string inner(current + 2, end - (current + 2));
                    stripped += strip_markdown(inner);
                    current = end + 2;
                } else {
                    stripped += "==";
                    current += 2;
                }
            } else if (strncmp(current, "~~", 2) == 0) {
                const char *end = strstr(current + 2, "~~");
                if (end) {
                    std::string inner(current + 2, end - (current + 2));
                    stripped += strip_markdown(inner);
                    current = end + 2;
                } else {
                    stripped += "~~";
                    current += 2;
                }
            } else if (*current == '*' || *current == '_') {
                char marker = *current;
                const char *end = current + 1;
                bool found = false;
                while (*end != '\0') {
                    if (*end == marker) {
                        if (marker == '*' && *(end + 1) == '*') {
                            end += 2;
                            continue;
                        }
                        found = true;
                        break;
                    }
                    if (*end == '\\' && *(end + 1) != '\0') end++;
                    end++;
                }
                if (found) {
                    std::string inner(current + 1, end - (current + 1));
                    stripped += strip_markdown(inner);
                    current = end + 1;
                } else {
                    stripped += marker;
                    current++;
                }
            } else {
                stripped += *current;
                current++;
            }
        }
        return stripped;
    }

    static unsigned int visible_length(const std::string &content) {
        std::string stripped = strip_markdown(content);

        // Calculate visual width for terminal
        unsigned int width = 0;
        unsigned int i = 0;
        while (i < stripped.length()) {
            unsigned char c = stripped[i];
            if (c < 0x80) {
                width++;
                i++;
            } else if ((c & 0xE0) == 0xC0) {
                width++;
                i += 2;
            } else if ((c & 0xF0) == 0xE0) {
                if (c == 0xE2) {
                    if (i + 1 < stripped.length()) {
                        unsigned char c2 = stripped[i + 1];
                        if (c2 >= 0x98) width += 2; // Dingbats and Misc Symbols
                        else width += 1; // Arrows, dashes, punctuation
                    } else {
                        width += 1;
                    }
                } else if (c == 0xEF) {
                    width += 2; // Often full-width chars
                } else {
                    width += 1;
                }
                i += 3;
            } else if ((c & 0xF8) == 0xF0) {
                width += 2; // Emojis are generally 2 cols
                i += 4;
            } else {
                width++;
                i++;
            }
        }
        return width;
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
                size_t split_pos = pos > 0 ? pos - 1 : 0;
                
                std::string closing_markers = "";
                std::string opening_markers = "";
                
                for (const auto& span : spans) {
                    if (span.start < split_pos && span.end >= split_pos) {
                        closing_markers = span.marker + closing_markers; 
                        opening_markers += span.marker; 
                    }
                }

                if (!current_line.empty()) {
                    lines.push_back(current_line + closing_markers);
                }
                
                if (visible_length(word) <= (unsigned int)max_width) {
                    current_line = opening_markers + word;
                } else {
                    current_line = opening_markers;
                    std::string chunk = "";
                    for (size_t c = 0; c < word.length(); c++) {
                        chunk += word[c];
                        if (c + 1 == word.length() || (word[c+1] & 0xC0) != 0x80) {
                            size_t absolute_split_pos = pos + c + 1;
                            std::string inner_closing = "";
                            std::string inner_opening = "";
                            for (const auto& span : spans) {
                                if (span.start < absolute_split_pos && span.end >= absolute_split_pos) {
                                    inner_closing = span.marker + inner_closing;
                                    inner_opening += span.marker;
                                }
                            }
                            if (visible_length(current_line + chunk + inner_closing) >= (unsigned int)max_width && c + 1 < word.length()) {
                                lines.push_back(current_line + chunk + inner_closing);
                                chunk = "";
                                current_line = inner_opening;
                            }
                        }
                    }
                    current_line += chunk;
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
        auto lines = get_lines(max_total_width);
        std::string result = "";
        for (const auto& l : lines) {
            result += l.first + "\n";
        }
        return result;
    }

    std::vector<std::pair<std::string, bool>> get_lines(int max_total_width = -1) const {
        std::vector<std::pair<std::string, bool>> output;
        if (_rows.empty()) return output;
        int num_cols = 0;
        for (const auto& row : _rows) {
            if ((int)row.size() > num_cols) num_cols = row.size();
        }
        if (num_cols == 0) return output;

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
                int remaining_available = available;
                int remaining_total_max = total_max;
                
                for (int i = 0; i < num_cols; i++) {
                    int w = (remaining_total_max > 0) ? (max_widths[i] * remaining_available) / remaining_total_max : min_widths[i];
                    col_widths[i] = std::max(min_widths[i], w);
                    allocated += col_widths[i];
                    remaining_available -= col_widths[i];
                    remaining_total_max -= max_widths[i];
                }
                
                if (allocated < available) {
                    col_widths[num_cols - 1] += (available - allocated);
                } else if (allocated > available) {
                    int excess = allocated - available;
                    
                    // Loop 1: Shrink proportionally based on current widths to prevent starving small columns
                    int total_weight = 0;
                    for (int i = 0; i < num_cols; i++) total_weight += col_widths[i];
                    
                    if (total_weight > 0) {
                        for (int i = 0; i < num_cols && excess > 0; i++) {
                            int reduction = (col_widths[i] * excess) / total_weight;
                            if (col_widths[i] - reduction < 1) reduction = col_widths[i] - 1;
                            col_widths[i] -= reduction;
                        }
                    }
                    
                    // Recalculate excess after proportional reduction
                    allocated = 0;
                    for (int i = 0; i < num_cols; i++) allocated += col_widths[i];
                    excess = allocated - available;

                    // Loop 2: Clean up any remaining rounding excess character by character
                    if (excess > 0) {
                        bool progress = true;
                        while (excess > 0 && progress) {
                            progress = false;
                            for (int i = num_cols - 1; i >= 0 && excess > 0; i--) {
                                if (col_widths[i] > 1) {
                                    col_widths[i]--;
                                    excess--;
                                    progress = true;
                                }
                            }
                        }
                    }
                }
            }
        }

        std::string top_border = _tl;
        for (int i = 0; i < num_cols; i++) {
            for (int j = 0; j < col_widths[i] + 2; j++) top_border += _h;
            if (i < num_cols - 1) top_border += _tm;
            else top_border += _tr;
        }
        output.push_back({top_border, false});

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
                std::string out = _v;
                for (int i = 0; i < num_cols; i++) {
                    std::string content = sub < (int)cell_lines[i].size() ? cell_lines[i][sub] : "";
                    int padding = std::max(0, col_widths[i] - (int)visible_length(content));
                    
                    out += " " + content;
                    for (int p = 0; p < padding; p++) out += " ";
                    out += " " + _v;
                }
                output.push_back({out, sub == 0});
            }
            
            if (r < _rows.size() - 1) {
                std::string mid_border = _lm;
                for (int i = 0; i < num_cols; i++) {
                    for (int j = 0; j < col_widths[i] + 2; j++) mid_border += _h;
                    if (i < num_cols - 1) mid_border += _mm;
                    else mid_border += _rm;
                }
                output.push_back({mid_border, true});
            }
        }
        
        std::string bot_border = _bl;
        for (int i = 0; i < num_cols; i++) {
            for (int j = 0; j < col_widths[i] + 2; j++) bot_border += _h;
            if (i < num_cols - 1) bot_border += _bm;
            else bot_border += _br;
        }
        output.push_back({bot_border, false});

        return output;
    }

private:
    std::string _h, _v, _tl, _tr, _bl, _br, _tm, _bm, _lm, _rm, _mm;
    std::vector<std::vector<std::string>> _rows;
    std::vector<std::string> _current;
};
#endif