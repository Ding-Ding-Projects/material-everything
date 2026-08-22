#include "chat_ui.hpp"

namespace me::chat {

BubbleStyle BubbleViewData::style() const {
    return direction == MessageDirection::Sent ? BubbleStyle::SentFilledPrimary
                                               : BubbleStyle::ReceivedSurfaceVariant;
}

ChatUiModel::ChatUiModel(ChatModule& module) : module_(&module) {}

const std::vector<NavigationRailItem>& ChatUiModel::navigation_rail() const {
    static const std::vector<NavigationRailItem> rail{
        {"Conversations", "forum"}, {"Attachments", "attach_file"}, {"Emoji", "emoji_emotions"}};
    return rail;
}

std::string ChatUiModel::typing_label(const std::string& id) const {
    return module_->is_typing(id) ? "Typing…" : "";
}

std::vector<BubbleViewData> ChatUiModel::bubbles(const std::string& id) const {
    std::vector<BubbleViewData> result;
    for (const auto& message : module_->messages_for(id))
        result.push_back({message.body, message.attachment_names, message.direction});
    return result;
}

std::vector<std::string> ChatUiModel::emoji_picker_items() const {
    return {"😀", "😂", "🥰", "😎", "🎉", "👍", "❤️", "🤔"};
}

} // namespace me::chat
