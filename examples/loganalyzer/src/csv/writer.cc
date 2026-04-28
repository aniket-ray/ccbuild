#include "csv/writer.h"

#include <sstream>

namespace csv {

static bool needs_quoting(const std::string& field) {
  for (char c : field) {
    if (c == ',' || c == '"' || c == '\n')
      return true;
  }
  return false;
}

std::string format_row(const Row& row) {
  std::ostringstream out;
  for (size_t i = 0; i < row.size(); ++i) {
    if (i > 0)
      out << ',';
    if (needs_quoting(row[i])) {
      out << '"';
      for (char c : row[i]) {
        if (c == '"')
          out << "\"\"";
        else
          out << c;
      }
      out << '"';
    } else {
      out << row[i];
    }
  }
  return out.str();
}

void write(std::ostream& out, const Table& table) {
  if (!table.header.empty()) {
    out << format_row(table.header) << '\n';
  }
  for (const auto& row : table.rows) {
    out << format_row(row) << '\n';
  }
}

}  // namespace csv
