#include "EmailClientModule.hpp"

#include <atomic>
#include <cctype>
#include <ctime>
#include <functional>
#include <iomanip>
#include <sstream>

namespace material_everything {

namespace {

std::atomic<unsigned long long> id_counter{1};

bool contains_case_insensitive(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    const std::string hay_lower = EmailClientModuleHelpers::lower(haystack);
    const std::string needle_lower = EmailClientModuleHelpers::lower(needle);
    return hay_lower.find(needle_lower) != std::string::npos;
}

std::string address_list_to_string(const std::vector<EmailAddress>& addresses) {
    std::string joined;
    for (size_t i = 0; i < addresses.size(); ++i) {
        if (i > 0) joined += ", ";
        joined += EmailClientModuleHelpers::format_address(addresses[i]);
    }
    return joined;
}

std::string timestamp_to_iso(const std::chrono::system_clock::time_point& tp) {
    const std::time_t tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_value{};
#ifdef _WIN32
    gmtime_s(&tm_value, &tt);
#else
    gmtime_r(&tt, &tm_value);
#endif
    std::ostringstream out;
    out << std::put_time(&tm_value, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

} // namespace

namespace EmailClientModuleHelpers {

std::string lower(const std::string& value) {
    std::string result = value;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

std::string format_address(const EmailAddress& address) {
    if (address.name.empty()) return address.email;
    return address.name + " <" + address.email + ">";
}

} // namespace EmailClientModuleHelpers

LocalEmailTransport::LocalEmailTransport(std::function<void(const EmailMessage&)> observer)
    : m_observer(std::move(observer)) {}

SendResult LocalEmailTransport::send(const EmailAccount& account,
                                     const EmailMessage& message) {
    SendResult result;
    if (!account.enabled || account.smtp_host.empty()) {
        result.protocol_error = "SMTP transport unavailable: account disabled or host missing";
        return result;
    }
    result.accepted = true;
    result.message_id = message.id;
    if (m_observer) m_observer(message);
    return result;
}

EmailClientModule::EmailClientModule() {}

std::string EmailClientModule::next_id(const char* prefix) {
    std::ostringstream out;
    out << prefix << '-' << id_counter.fetch_add(1);
    return out.str();
}

std::string EmailClientModule::lower_copy(const std::string& value) {
    return EmailClientModuleHelpers::lower(value);
}

std::string EmailClientModule::trim_copy(const std::string& value) {
    size_t begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string EmailClientModule::escape_html(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&#34;"; break;
            case '\'': out += "&#39;"; break;
            default: out.push_back(ch); break;
        }
    }
    return out;
}

std::string EmailClientModule::format_address(const EmailAddress& address) {
    return EmailClientModuleHelpers::format_address(address);
}

std::string EmailClientModule::quote_subject(const std::string& subject) {
    if (subject.rfind("Re:", 0) == 0 || subject.rfind("Fwd:", 0) == 0) return subject;
    return "Re: " + subject;
}

std::chrono::system_clock::time_point EmailClientModule::parse_rfc5322_date(const std::string&) {
    // Deterministic fallback; a socket backend will replace this parser.
    return std::chrono::system_clock::now();
}

std::string EmailClientModule::encode_header_value(const std::string& raw) {
    std::ostringstream out;
    for (unsigned char c : raw) {
        if (std::isalnum(c) || c == ' ' || c == '.' || c == '@' || c == '-') {
            out << static_cast<char>(c);
        }
    }
    return out.str();
}

EmailAccount EmailClientModule::add_account(const std::string& display_name,
                                            const std::string& address,
                                            const std::string& imap_host,
                                            const std::string& smtp_host) {
    EmailAccount account;
    account.id = next_id("acct");
    account.display_name = display_name;
    account.address = address;
    account.imap_host = imap_host;
    account.smtp_host = smtp_host;
    m_accounts[account.id] = account;
    seed_system_folders(account.id);
    return account;
}

void EmailClientModule::update_account(const EmailAccount& account) {
    auto it = m_accounts.find(account.id);
    if (it != m_accounts.end()) it->second = account;
}

void EmailClientModule::remove_account(const std::string& account_id) {
    for (auto it = m_folders.begin(); it != m_folders.end();) {
        if (it->second.account_id == account_id) it = m_folders.erase(it);
        else ++it;
    }
    for (auto it = m_messages.begin(); it != m_messages.end();) {
        if (it->second.account_id == account_id) it = m_messages.erase(it);
        else ++it;
    }
    m_accounts.erase(account_id);
}

std::vector<EmailAccount> EmailClientModule::accounts() const {
    std::vector<EmailAccount> list;
    list.reserve(m_accounts.size());
    for (const auto& [_, account] : m_accounts) list.push_back(account);
    return list;
}

const EmailAccount* EmailClientModule::find_account(const std::string& account_id) const {
    const auto it = m_accounts.find(account_id);
    return it == m_accounts.end() ? nullptr : &it->second;
}

void EmailClientModule::seed_system_folders(const std::string& account_id) {
    const std::pair<const char*, EmailFolderKind> system_folders[] = {
        {"Inbox", EmailFolderKind::Inbox},
        {"Sent", EmailFolderKind::Sent},
        {"Drafts", EmailFolderKind::Drafts},
        {"Spam", EmailFolderKind::Spam},
        {"Trash", EmailFolderKind::Trash},
    };
    for (const auto& [name, kind] : system_folders) {
        EmailFolder folder;
        folder.id = next_id("folder");
        folder.account_id = account_id;
        folder.name = name;
        folder.kind = kind;
        folder.system_folder = true;
        m_folders[folder.id] = folder;
    }
}

std::vector<EmailFolder> EmailClientModule::folders_for_account(const std::string& account_id) const {
    std::vector<EmailFolder> list;
    for (const auto& [_, folder] : m_folders) {
        if (folder.account_id == account_id) list.push_back(folder);
    }
    return list;
}

EmailFolder EmailClientModule::create_custom_folder(const std::string& account_id,
                                                    const std::string& name) {
    EmailFolder folder;
    folder.id = next_id("folder");
    folder.account_id = account_id;
    folder.name = name;
    folder.kind = EmailFolderKind::Custom;
    folder.system_folder = false;
    m_folders[folder.id] = folder;
    return folder;
}

void EmailClientModule::rename_folder(const std::string& folder_id, const std::string& new_name) {
    auto it = m_folders.find(folder_id);
    if (it != m_folders.end()) it->second.name = new_name;
}

void EmailClientModule::delete_custom_folder(const std::string& folder_id) {
    auto it = m_folders.find(folder_id);
    if (it == m_folders.end() || it->second.system_folder) return;
    const std::string trash_id = trash_folder_id(it->second.account_id);
    for (auto msg_it = m_messages.begin(); msg_it != m_messages.end();) {
        if (msg_it->second.folder_id == folder_id && !trash_id.empty()) {
            msg_it->second.folder_id = trash_id;
            recompute_unread(trash_id);
        }
        ++msg_it;
    }
    m_folders.erase(it);
}

std::vector<EmailMessage> EmailClientModule::messages_in_folder(const std::string& folder_id) const {
    std::vector<EmailMessage> list;
    for (const auto& [id, message] : m_messages) {
        if (message.folder_id == folder_id) list.push_back(message);
    }
    std::sort(list.begin(), list.end(), [](const EmailMessage& a, const EmailMessage& b) {
        return a.received_at > b.received_at;
    });
    return list;
}

const EmailMessage* EmailClientModule::find_message(const std::string& message_id) const {
    const auto it = m_messages.find(message_id);
    return it == m_messages.end() ? nullptr : &it->second;
}

EmailMessage* EmailClientModule::mutable_message(const std::string& id) {
    const auto it = m_messages.find(id);
    return it == m_messages.end() ? nullptr : &it->second;
}

void EmailClientModule::mark_read(const std::string& message_id, bool read_state) {
    if (auto* message = mutable_message(message_id)) {
        message->read_flag = read_state;
        recompute_unread(message->folder_id);
    }
}

void EmailClientModule::toggle_flag(const std::string& message_id) {
    if (auto* message = mutable_message(message_id)) message->flagged = !message->flagged;
}

bool EmailClientModule::move_message(const std::string& message_id,
                                     const std::string& target_folder_id) {
    auto* message = mutable_message(message_id);
    if (!message || m_folders.find(target_folder_id) == m_folders.end()) return false;
    const std::string old_folder = message->folder_id;
    message->folder_id = target_folder_id;
    recompute_unread(old_folder);
    recompute_unread(target_folder_id);
    return true;
}

bool EmailClientModule::delete_message_to_trash(const std::string& message_id) {
    auto* message = mutable_message(message_id);
    if (!message) return false;
    const std::string trash_id = trash_folder_id(message->account_id);
    if (trash_id.empty()) return false;
    if (message->folder_id == trash_id) {
        m_messages.erase(message_id);
        return true;
    }
    return move_message(message_id, trash_id);
}

void EmailClientModule::purge_trash() {
    for (auto it = m_messages.begin(); it != m_messages.end();) {
        const auto folder_it = m_folders.find(it->second.folder_id);
        if (folder_it != m_folders.end() && folder_it->second.kind == EmailFolderKind::Trash) {
            it = m_messages.erase(it);
        } else {
            ++it;
        }
    }
}

void EmailClientModule::recompute_unread(const std::string& folder_id) {
    auto it = m_folders.find(folder_id);
    if (it == m_folders.end()) return;
    int unread = 0;
    for (const auto& [_, message] : m_messages) {
        if (message.folder_id == folder_id && !message.read_flag) ++unread;
    }
    it->second.unread_count = unread;
}

std::string EmailClientModule::trash_folder_id(const std::string& account_id) const {
    for (const auto& [_, folder] : m_folders) {
        if (folder.account_id == account_id && folder.kind == EmailFolderKind::Trash) {
            return folder.id;
        }
    }
    return {};
}

EmailMessage EmailClientModule::create_draft(const ComposeEnvelope& envelope) {
    EmailMessage message;
    message.id = next_id("msg");
    message.account_id = envelope.account_id;
    const auto folders = folders_for_account(envelope.account_id);
    for (const auto& folder : folders) {
        if (folder.kind == EmailFolderKind::Drafts) message.folder_id = folder.id;
    }
    message.to = envelope.to;
    message.cc = envelope.cc;
    message.bcc = envelope.bcc;
    message.subject = envelope.subject;
    message.text_body = envelope.text_body;
    message.html_body = envelope.html_body;
    message.prefer_html = !envelope.html_body.empty();
    message.received_at = std::chrono::system_clock::now();
    for (const auto& attachment : envelope.attachments) {
        const std::string attachment_id = add_attachment(message.id, attachment);
        message.attachment_ids.push_back(attachment_id);
    }
    message.has_attachments = !message.attachment_ids.empty();
    m_messages[message.id] = message;
    recompute_unread(message.folder_id);
    return m_messages[message.id];
}

EmailMessage EmailClientModule::reply_all(const std::string& source_message_id,
                                          const std::string& body,
                                          bool html) {
    const EmailMessage* source = find_message(source_message_id);
    ComposeEnvelope envelope;
    if (source) {
        envelope.account_id = source->account_id;
        envelope.to = source->from;
        envelope.cc.insert(envelope.cc.end(), source->to.begin(), source->to.end());
        envelope.cc.insert(envelope.cc.end(), source->cc.begin(), source->cc.end());
        envelope.subject = quote_subject(source->subject);
        envelope.text_body = "> " + source->text_body + "\n\n" + body;
        envelope.html_body = html ? "<blockquote>" + source->html_body + "</blockquote>" + body : "";
    } else {
        envelope.text_body = body;
        envelope.html_body = html ? body : "";
    }
    return create_draft(envelope);
}

EmailMessage EmailClientModule::forward(const std::string& source_message_id,
                                        const std::vector<EmailAddress>& recipients,
                                        const std::string& body,
                                        bool html,
                                        bool include_attachments) {
    const EmailMessage* source = find_message(source_message_id);
    ComposeEnvelope envelope;
    envelope.to = recipients;
    if (source) {
        envelope.account_id = source->account_id;
        envelope.subject = "Fwd: " + source->subject;
        envelope.text_body = "---------- Forwarded ----------\n" +
                             address_list_to_string(source->from) + "\n" +
                             source->text_body + "\n\n" + body;
        envelope.html_body = html
                                 ? "<hr><p><b>Fwd:</b> " + escape_html(address_list_to_string(source->from)) +
                                       "</p>" + source->html_body + body
                                 : "";
        if (include_attachments) {
            for (const auto& attachment_id : source->attachment_ids) {
                if (const auto* attachment = get_attachment(attachment_id)) {
                    envelope.attachments.push_back(*attachment);
                }
            }
        }
    } else {
        envelope.text_body = body;
        envelope.html_body = html ? body : "";
    }
    return create_draft(envelope);
}

SendResult EmailClientModule::send_message(const std::string& draft_or_message_id) {
    EmailMessage* message = mutable_message(draft_or_message_id);
    SendResult result;
    if (!message) {
        result.protocol_error = "message not found";
        return result;
    }
    const EmailAccount* account = find_account(message->account_id);
    if (!account) {
        result.protocol_error = "account not found";
        return result;
    }
    if (!m_transport) {
        static LocalEmailTransport default_transport;
        m_transport = &default_transport;
    }
    result = m_transport->send(*account, *message);
    if (result.accepted) {
        for (const auto& folder : folders_for_account(account->id)) {
            if (folder.kind == EmailFolderKind::Sent) {
                move_message(draft_or_message_id, folder.id);
                break;
            }
        }
    }
    return result;
}

std::string EmailClientModule::add_attachment(const std::string& message_id, EmailAttachment attachment) {
    attachment.id = next_id("att");
    m_attachments[attachment.id] = attachment;
    if (auto* message = mutable_message(message_id)) {
        message->attachment_ids.push_back(attachment.id);
        message->has_attachments = true;
    }
    return attachment.id;
}

const EmailAttachment* EmailClientModule::get_attachment(const std::string& attachment_id) const {
    const auto it = m_attachments.find(attachment_id);
    return it == m_attachments.end() ? nullptr : &it->second;
}

bool EmailClientModule::remove_attachment(const std::string& attachment_id) {
    return m_attachments.erase(attachment_id) > 0;
}

std::string EmailClientModule::render_html_body(const std::string& message_id) const {
    const EmailMessage* message = find_message(message_id);
    if (!message) return {};
    if (message->prefer_html && !message->html_body.empty()) {
        return "<div class=\"email-body\">" + message->html_body + "</div>";
    }
    return "<pre class=\"email-body\">" + escape_html(message->text_body) + "</pre>";
}

std::string EmailClientModule::render_text_body(const std::string& message_id) const {
    const EmailMessage* message = find_message(message_id);
    if (!message) return {};
    return message->text_body;
}

std::vector<EmailMessage> EmailClientModule::search(const std::string& query) const {
    SearchFilter filter;
    filter.subject_contains = query;
    return this->filter(filter);
}

std::vector<EmailMessage> EmailClientModule::filter(const SearchFilter& f) const {
    std::vector<EmailMessage> results;
    for (const auto& [id, message] : m_messages) {
        if (f.account_id && *f.account_id != message.account_id) continue;
        if (f.folder_id && *f.folder_id != message.folder_id) continue;
        if (f.unread_only && *f.unread_only && message.read_flag) continue;
        if (f.flagged_only && *f.flagged_only && !message.flagged) continue;
        if (f.has_attachments_only && *f.has_attachments_only && !message.has_attachments) continue;
        if (f.use_since && message.received_at < f.since) continue;
        bool matched_subject = !f.subject_contains.has_value();
        if (f.subject_contains) {
            matched_subject = contains_case_insensitive(message.subject, *f.subject_contains) ||
                              contains_case_insensitive(message.text_body, *f.subject_contains);
        }
        bool matched_sender = !f.sender_contains.has_value();
        if (f.sender_contains) {
            for (const auto& sender : message.from) {
                if (contains_case_insensitive(sender.email, *f.sender_contains) ||
                    contains_case_insensitive(sender.name, *f.sender_contains)) {
                    matched_sender = true;
                }
            }
        }
        if (matched_subject && matched_sender) results.push_back(message);
    }
    return results;
}

ContactEntry EmailClientModule::add_contact(const std::string& name, const std::string& email) {
    ContactEntry contact;
    contact.id = next_id("contact");
    contact.name = name;
    contact.email = email;
    m_contacts[contact.id] = contact;
    return contact;
}

void EmailClientModule::update_contact(const ContactEntry& contact) {
    auto it = m_contacts.find(contact.id);
    if (it != m_contacts.end()) it->second = contact;
}

void EmailClientModule::remove_contact(const std::string& contact_id) {
    m_contacts.erase(contact_id);
}

std::vector<ContactEntry> EmailClientModule::contacts() const {
    std::vector<ContactEntry> list;
    list.reserve(m_contacts.size());
    for (const auto& [_, contact] : m_contacts) list.push_back(contact);
    return list;
}

std::vector<ContactEntry> EmailClientModule::autocomplete_contacts(const std::string& query) const {
    std::vector<ContactEntry> matches;
    for (const auto& [_, contact] : m_contacts) {
        if (contains_case_insensitive(contact.name, query) ||
            contains_case_insensitive(contact.email, query)) {
            matches.push_back(contact);
        }
    }
    std::sort(matches.begin(), matches.end(), [](const ContactEntry& a, const ContactEntry& b) {
        return a.send_count > b.send_count;
    });
    return matches;
}

void EmailClientModule::set_transport(EmailTransport* transport) {
    m_transport = transport;
}

} // namespace material_everything
