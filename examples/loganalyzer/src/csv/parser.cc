#include "csv/parser.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace csv {

Row parse_line(std::string_view line) {
  Row fields;
  std::string current;
  bool in_quotes = false;

  for (size_t i = 0; i < line.size(); ++i) {
    char c = line[i];

    if (in_quotes) {
      if (c == '"') {
        // Peek: escaped quote "" or end of quoted field.
        if (i + 1 < line.size() && line[i + 1] == '"') {
          current += '"';
          ++i;  // skip second quote
        } else {
          in_quotes = false;
        }
      } else {
        current += c;
      }
    } else {
      if (c == '"') {
        in_quotes = true;
      } else if (c == ',') {
        fields.push_back(std::move(current));
        current.clear();
      } else if (c == '\r') {
        // skip carriage return
      } else {
        current += c;
      }
    }
  }
  fields.push_back(std::move(current));
  return fields;
}

Table parse(std::string_view text, bool has_header) {
  Table table;
  std::istringstream stream{ std::string(text) };
  std::string line;

  bool first = true;
  while (std::getline(stream, line)) {
    if (line.empty())
      continue;
    auto row = parse_line(line);
    if (first && has_header) {
      table.header = std::move(row);
      first = false;
    } else {
      table.rows.push_back(std::move(row));
      first = false;
    }
  }
  return table;
}

Table parse_file(const std::string& path, bool has_header) {
  std::ifstream file(path);
  if (!file.is_open()) {
    throw std::runtime_error("cannot open file: " + path);
  }

  std::ostringstream buf;
  buf << file.rdbuf();
  return parse(buf.str(), has_header);
}

}  // namespace csv
