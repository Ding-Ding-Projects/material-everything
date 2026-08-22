#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace material_everything::spreadsheet {

// A1 notation: column letters then 1-based row (e.g. "A1", "BC42").
struct CellRef {
    int row = 0;    // 0-based internally; API helpers convert.
    int col = 0;

    static CellRef from_a1(const std::string& a1);
    std::string to_a1() const;
};

enum class NumberFormat { General, Number, Currency, Percent, Date, Scientific };

enum class CellAlignment { Left, Center, Right };

struct CellFormat {
    bool bold = false;
    bool italic = false;
    bool underline = false;
    CellAlignment alignment = CellAlignment::Left;
    NumberFormat number_format = NumberFormat::General;
    std::uint32_t text_color = 0xFF000000;     // ARGB
    std::uint32_t background_color = 0xFFFFFFFF; // ARGB
};

struct Cell {
    std::string raw;        // what the user typed ("=SUM(A1:A5)" or "hello")
    std::string cached_value;
    CellFormat format;
};

enum class SortOrder { Ascending, Descending };

struct SortSpec {
    int col = 0;
    SortOrder order = SortOrder::Ascending;
    // Rows are compared by this column first; ties keep original order.
};

struct FilterSpec {
    int col = 0;
    enum class Op { Equals, NotEquals, Contains, NotContains,
                    GreaterThan, LessThan, GreaterEqual, LessEqual } op = Op::Contains;
    std::string value;
    bool case_sensitive = false;
};

struct SheetInfo {
    std::string name;
    int row_count = 1000;
    int col_count = 26;
};

// Pure data/logic spreadsheet engine. The Qt6 shell binds this API to an
// M3-styled grid widget (QTableView subclass or custom grid).
class SpreadsheetModule {
public:
    SpreadsheetModule();

    // --- Sheets ---
    const SheetInfo& active_sheet_info() const;
    const std::vector<std::string>& sheet_names() const;
    bool add_sheet(const std::string& name);
    bool remove_sheet(const std::string& name); // refuses last sheet
    bool switch_sheet(const std::string& name);

    // --- Cells ---
    void set_cell(int row, int col, const std::string& raw);
    void set_cell_a1(const std::string& a1, const std::string& raw);
    const Cell* cell(int row, int col) const;
    std::string displayed_value(int row, int col);   // evaluates if needed
    std::string displayed_value_a1(const std::string& a1);
    double numeric_value(int row, int col);          // 0 when non-numeric
    bool is_numeric(int row, int col);

    // --- Formatting ---
    void set_format(int row, int col, const CellFormat& fmt);
    const CellFormat* format(int row, int col) const;
    void clear_format(int row, int col);

    // --- Resize metadata (the widget reads these for its layout pass) ---
    int row_height(int row) const;               // px
    void set_row_height(int row, int px);
    int col_width(int col) const;                // px
    void set_col_width(int col, int px);

    // --- Sorting & filtering ---
    // Returns the permutation applied so a UI can animate or undo.
    std::vector<int> sort_rows(const SortSpec& spec, int first_data_row = 1, int header_row = 0);
    std::vector<int> filter_rows(const FilterSpec& spec,
                                 int first_data_row = 1, int header_row = 0) const;
    void clear_filter();
    bool filter_active() const;

    // --- Import / export ---
    bool import_csv(const std::string& path_or_text);
    std::string export_csv() const;
    // XLSX: writes a minimal valid .xlsx (zip of XML parts). Uses no external
    // dependency; import parses sharedStrings + sheet1 XML.
    bool export_xlsx(const std::string& path) const;
    bool import_xlsx(const std::string& path);

    // --- Undo / redo ---
    bool can_undo() const;
    bool can_redo() const;
    void undo();
    void redo();

private:
    struct Impl;
    Impl* impl_;
};

} // namespace material_everything::spreadsheet
