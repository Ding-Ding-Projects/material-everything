#pragma once

#include "clipboard_manager.hpp"

#include <functional>
#include <string>

namespace material_everything::modules {

// Material Design 3 list adapter: the host app supplies the widget primitives,
// this class drives state and layout decisions so the module stays toolkit-free.
class ClipboardManagerUi final {
public:
    using ReCopyFn = std::function<void(const ClipboardEntry&)>;

    explicit ClipboardManagerUi(ClipboardManagerModule& model);

    struct ListRow {
        std::string id;
        std::string title;
        std::string subtitle;
        std::string kind_icon;
        bool pinned;
    };

    std::vector<ListRow> build_rows(const std::string& search_query) const;
    std::string preview_text(const ClipboardEntry& entry) const;
    bool has_thumbnail(const ClipboardEntry& entry) const;
    void activate_row(const std::string& row_id, const ReCopyFn& re_copy);

private:
    ClipboardManagerModule& model_;
};

}  // namespace material_everything::modules
