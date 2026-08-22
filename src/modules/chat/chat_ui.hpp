#pragma once

#include <string>
#include <vector>

#include "chat_module.hpp"

namespace me::chat {

enum class BubbleStyle { SentFilledPrimary, ReceivedSurfaceVariant };

struct BubbleViewData {
    std::string text;
    std::vector<std::string> attachment_names;
    MessageDirection direction;
    BubbleStyle style() const;
};

struct NavigationRailItem {
    std::string label;
    std::string icon_name;
};

class ChatUiModel {
public:
    explicit ChatUiModel(ChatModule& module);

    const std::vector<NavigationRailItem>& navigation_rail() const;
    std::string typing_label(const std::string& conversation_id) const;
    std::vector<BubbleViewData> bubbles(const std::string& conversation_id) const;
    std::vector<std::string> emoji_picker_items() const;

private:
    ChatModule* module_;
};

} // namespace me::chat
