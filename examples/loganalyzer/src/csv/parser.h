#ifndef LOGANALYZER_CSV_PARSER_H
#define LOGANALYZER_CSV_PARSER_H

#include <string>
#include <string_view>
#include <vector>

namespace csv {

/// A single parsed row: vector of field strings.
using Row = std::vector<std::string>;

/// Parse a single CSV line into fields.
/// Handles quoted fields (double-quote escaping).
Row parse_line(std::string_view line);

/// Parse an entire CSV string (multiple lines) into rows.
/// First row is treated as header if `has_header` is true.
struct Table {
  Row header;
  std::vector<Row> rows;
};

Table parse(std::string_view text, bool has_header = true);

/// Read and parse a CSV file. Throws on I/O error.
Table parse_file(const std::string& path, bool has_header = true);

}  // namespace csv

#endif  // LOGANALYZER_CSV_PARSER_H
