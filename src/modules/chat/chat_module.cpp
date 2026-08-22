#include "chat_module.hpp"

#include <algorithm>
#include <utility>

namespace me::chat {

namespace {
std::string next_id(const std::string& prefix) {
    static unsigned long long counter = 0;
    return prefix + '-' + std::to_string(++counter);
}
}

struct ChatModule::State {
    std::shared_ptr<ChatBackend> backend;
    std::vector<Conversation> conversations;
    std::vector<std::vector<ChatMessage>> messages_by_conversation;
    std::string selected;
    std::vector<std::pair<std::string, bool>> typing;

    std::size_t index_for(const std::string& id) const {
        for (std::size_t i = 0; i < conversations.size(); ++i)
            if (conversations[i].id == id) return i;
        return conversations.size();
    }
};

ChatModule::ChatModule() : state_(std::make_unique<State>()) {}

ChatModule::~ChatModule() = default;
ChatModule::ChatModule(ChatModule&&) noexcept = default;
ChatModule& ChatModule::operator=(ChatModule&&) noexcept = default;

void ChatModule::set_backend(std::shared_ptr<ChatBackend> backend) {
    state_->backend = std::move(backend);
    if (state_->backend)
        state_->backend->set_message_handler(
            [this](const ChatMessage& incoming) { receive_local_echo(incoming.conversation_id, incoming.body); });
}

std::shared_ptr<ChatBackend> ChatModule::backend() const { return state_->backend; }

std::string ChatModule::active_protocol() const {
    return state_->backend ? state_->backend->protocol_name() : "local";
}

void ChatModule::add_conversation(Conversation conversation) {
    for (const auto& existing : state_->conversations)
        if (existing.id == conversation.id) return;
    state_->messages_by_conversation.emplace_back();
    state_->conversations.push_back(std::move(conversation));
    if (state_->selected.empty()) select_conversation(state_->conversations.front().id);
}

const std::vector<Conversation>& ChatModule::conversations() const {
    return state_->conversations;
}

const std::vector<ChatMessage>& ChatModule::messages_for(const std::string& id) const {
    auto it = std::find_if(state_->conversations.begin(), state_->conversations.end(),
                           [&](const Conversation& c) { return c.id == id; });
    static const std::vector<ChatMessage> empty;
    if (it == state_->conversations.end()) return empty;
    return state_->messages_by_conversation[it - state_->conversations.begin()];
}

bool ChatModule::select_conversation(const std::string& id) {
    for (const auto& conversation : state_->conversations)
        if (conversation.id == id) { state_->selected = id; return true; }
    return false;
}

bool ChatModule::send_message(const std::string& id, const std::string& body,
                              const std::vector<std::string>& attachments) {
    if (!select_conversation(id) || body.empty()) return false;
    ChatMessage message{next_id("msg"), id, body, attachments,
                        MessageDirection::Sent, DeliveryState::Pending};
    if (state_->backend && !state_->backend->send(message)) message.delivery = DeliveryState::Failed;
    else message.delivery = DeliveryState::Delivered;
    state_->messages_by_conversation[state_->index_for(id)].push_back(message);
    return true;
}

void ChatModule::receive_local_echo(const std::string& id, const std::string& body) {
    if (!select_conversation(id) || body.empty()) return;
    const auto index = state_->index_for(id);
    if (index == state_->conversations.size()) return;
    state_->messages_by_conversation[index].push_back(ChatMessage{
        next_id("in"), id, body, {}, MessageDirection::Received, DeliveryState::Delivered});
}

bool ChatModule::is_typing(const std::string& id) const {
    for (const auto& [conversation_id, typing] : state_->typing)
        if (conversation_id == id) return typing;
    return false;
}

void ChatModule::set_typing(const std::string& id, bool typing) {
    for (auto& entry : state_->typing)
        if (entry.first == id) { entry.second = typing; return; }
    state_->typing.emplace_back(id, typing);
}

} // namespace me::chat
