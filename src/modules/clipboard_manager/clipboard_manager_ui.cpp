#include "clipboard_manager_ui.hpp"

namespace material_everything::modules {

ClipboardManagerUi::ClipboardManagerUi(ClipboardManagerModule& model) : model_(model) {}

std::vector<ClipboardManagerUi::ListRow> ClipboardManagerUi::build_rows(
    const std::string& search_query) const {
    std::vector<ListRow> rows;
    rows.reserve(32);
    for (const auto& entry : model_.entries(search_query)) {
        ListRow row;
        row.id = entry.id;
        row.pinned = entry.pinned;
        switch (entry.kind) {
            case ClipboardEntryKind::Text:
                row.kind_icon = "content_paste";
                row.title = preview_text(entry);
                break;
            case ClipboardEntryKind::Image:
                row.kind_icon = "image";
                row.title = "Image";
                break;
            case ClipboardEntryKind::Files:
                row.kind_icon = "folder_open";
                row.title = entry.file_paths.empty() ? "File(s)"
                                                     : entry.file_paths.front();
                break;
        }
        row.subtitle = std::to_string(entry.image_width) + "×" +
                       std::to_string(entry.image_height);
        rows.push_back(std::move(row));
    }
    return rows;
}

std::string ClipboardManagerUi::preview_text(const ClipboardEntry& entry) const {
    constexpr std::size_t kPreviewLength = 120;
    if (entry.text_preview.size() <= kPreviewLength) return entry.text_preview;
    return entry.text_preview.substr(0, kPreviewLength) + "…";
}

bool ClipboardManagerUi::has_thumbnail(const ClipboardEntry& entry) const {
    return entry.kind == ClipboardEntryKind::Image && !entry.image_bytes.empty() &&
           entry.image_width > 0 && entry.image_height > 0;
}

void ClipboardManagerUi::activate_row(const std::string& row_id, const ReCopyFn& re_copy) {
    const ClipboardEntry* entry = model_.find(row_id);
    if (entry && re_copy) re_copy(*entry);
}

}  // namespace material_everything::modules
