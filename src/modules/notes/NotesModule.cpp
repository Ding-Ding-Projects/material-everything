#include "NotesModule.hpp"

#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>

namespace material_everything {

static std::string to_lower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

std::string NotesModule::generate_id() {
    static std::mt19937_64 rng(std::random_device{}());
    uint64_t v = rng();
    std::stringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << v;
    return "n" + ss.str();
}

void NotesModule::touch(Note& n) {
    n.updated_at = std::chrono::system_clock::now();
    m_dirty_notes.insert(n.id);
}

NotesModule::NotesModule() {
    m_notebooks["default"] = {"default", "All Notes", ""};
}

Note NotesModule::create_note(const std::string& notebook_id, const std::string& title) {
    Note n;
    n.id = generate_id();
    n.title = title.empty() ? "Untitled" : title;
    n.notebook_id = m_notebooks.count(notebook_id) ? notebook_id : "default";
    n.created_at = n.updated_at = std::chrono::system_clock::now();
    m_notes[n.id] = n;
    m_dirty_notes.insert(n.id);
    return n;
}

void NotesModule::update_note(const Note& note) {
    if (!m_notes.count(note.id)) return;
    m_notes[note.id] = note;
    touch(m_notes[note.id]);
}

void NotesModule::delete_note(const std::string& note_id) {
    m_notes.erase(note_id);
    m_dirty_notes.erase(note_id);
}

const Note* NotesModule::get_note(const std::string& note_id) const {
    auto it = m_notes.find(note_id);
    return it != m_notes.end() ? &it->second : nullptr;
}

Notebook NotesModule::create_notebook(const std::string& name, const std::string& parent_id) {
    Notebook nb;
    nb.id = generate_id();
    nb.name = name.empty() ? "New Folder" : name;
    nb.parent_id = parent_id;
    m_notebooks[nb.id] = nb;
    return nb;
}

void NotesModule::rename_notebook(const std::string& notebook_id, const std::string& new_name) {
    if (m_notebooks.count(notebook_id)) m_notebooks[notebook_id].name = new_name;
}

void NotesModule::delete_notebook(const std::string& notebook_id) {
    if (notebook_id == "default") return;
    for (auto& [id, note] : m_notes)
        if (note.notebook_id == notebook_id) note.notebook_id = "default";
    for (auto& [id, nb] : m_notebooks)
        if (nb.parent_id == notebook_id) nb.parent_id = "";
    m_notebooks.erase(notebook_id);
}

std::vector<Notebook> NotesModule::get_notebooks() const {
    std::vector<Notebook> out;
    for (auto& [_, nb] : m_notebooks) out.push_back(nb);
    return out;
}

NoteTag NotesModule::create_tag(const std::string& name) {
    NoteTag t;
    t.id = generate_id();
    t.name = name;
    m_tags[t.id] = t;
    return t;
}

void NotesModule::assign_tag(const std::string& note_id, const std::string& tag_id) {
    if (!m_notes.count(note_id) || !m_tags.count(tag_id)) return;
    auto& tags = m_notes[note_id].tag_ids;
    if (std::find(tags.begin(), tags.end(), tag_id) == tags.end())
        tags.push_back(tag_id);
    touch(m_notes[note_id]);
}

void NotesModule::remove_tag(const std::string& note_id, const std::string& tag_id) {
    if (!m_notes.count(note_id)) return;
    auto& tags = m_notes[note_id].tag_ids;
    tags.erase(std::remove(tags.begin(), tags.end(), tag_id), tags.end());
    touch(m_notes[note_id]);
}

std::vector<NoteTag> NotesModule::get_tags() const {
    std::vector<NoteTag> out;
    for (auto& [_, t] : m_tags) out.push_back(t);
    return out;
}

std::vector<Note> NotesModule::search_notes(const std::string& query) const {
    std::vector<Note> results;
    if (query.empty()) {
        for (auto& [_, n] : m_notes) results.push_back(n);
    } else {
        std::string q = to_lower(query);
        for (auto& [_, n] : m_notes) {
            std::string title = to_lower(n.title);
            std::string body = to_lower(n.markdown_body);
            if (title.find(q) != std::string::npos || body.find(q) != std::string::npos)
                results.push_back(n);
        }
    }
    std::stable_sort(results.begin(), results.end(), [](const Note& a, const Note& b) {
        if (a.pinned != b.pinned) return a.pinned > b.pinned;
        return a.updated_at > b.updated_at;
    });
    return results;
}

std::vector<Note> NotesModule::search_by_tag(const std::string& tag_id) const {
    std::vector<Note> results;
    for (auto& [_, n] : m_notes) {
        if (std::find(n.tag_ids.begin(), n.tag_ids.end(), tag_id) != n.tag_ids.end())
            results.push_back(n);
    }
    return results;
}

std::vector<Note> NotesModule::notes_in_notebook(const std::string& notebook_id) const {
    std::vector<Note> results;
    for (auto& [_, n] : m_notes) {
        if (n.notebook_id == notebook_id || notebook_id.empty())
            results.push_back(n);
    }
    return results;
}

void NotesModule::toggle_pin(const std::string& note_id) {
    if (!m_notes.count(note_id)) return;
    m_notes[note_id].pinned = !m_notes[note_id].pinned;
    touch(m_notes[note_id]);
}

void NotesModule::add_checklist_item(const std::string& note_id, const std::string& text) {
    if (!m_notes.count(note_id)) return;
    NoteChecklistItem item{text, false};
    m_notes[note_id].checklist_items.push_back(item);
    m_notes[note_id].has_checklist = true;
    touch(m_notes[note_id]);
}

void NotesModule::toggle_checklist_item(const std::string& note_id, size_t index) {
    if (!m_notes.count(note_id)) return;
    auto& items = m_notes[note_id].checklist_items;
    if (index < items.size()) items[index].checked = !items[index].checked;
    touch(m_notes[note_id]);
}

void NotesModule::set_autosave_interval(std::chrono::milliseconds interval) {
    m_autosave_interval = interval;
}

void NotesModule::mark_dirty(const std::string& note_id) {
    m_dirty_notes.insert(note_id);
}

void NotesModule::autosave_now() {
    m_dirty_notes.clear(); // In production this would serialize to disk.
}

} // namespace material_everything
