#ifndef LOGANALYZER_CSV_WRITER_H
#define LOGANALYZER_CSV_WRITER_H

#include <ostream>
#include <string>
#include <vector>

#include "csv/parser.h"

namespace csv {

/// Format a row as a CSV line (quoting fields that contain commas/quotes).
std::string format_row(const Row& row);

/// Write an entire table to an output stream.
void write(std::ostream& out, const Table& table);

}  // namespace csv

#endif  // LOGANALYZER_CSV_WRITER_H
