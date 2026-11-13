#include "sf/Scene.h"

namespace sf {

Object* Scene::find(uint32_t id) {
    for (auto& o : objects) if (o->st.id == id) return o.get();
    return nullptr;
}
const Object* Scene::find(uint32_t id) const {
    for (auto& o : objects) if (o->st.id == id) return o.get();
    return nullptr;
}
int Scene::indexOf(uint32_t id) const {
    for (size_t i = 0; i < objects.size(); ++i) if (objects[i]->st.id == id) return (int)i;
    return -1;
}

Mat4 Scene::worldMatrix(uint32_t id) const {
    Mat4 m;
    int guard = 0;
    for (const Object* o = find(id); o && guard < 64; o = find(o->st.parent), ++guard) {
        m = o->localMatrix() * m;
        if (o->st.parent == 0) break;
    }
    return m;
}

Object* Scene::add(Mesh mesh, const std::string& name) {
    ObjectState st;
    st.id = nextId++;
    st.name = name;
    return insert(st, std::move(mesh), -1);
}

Object* Scene::insert(const ObjectState& st, Mesh mesh, int index) {
    auto o = std::make_unique<Object>();
    o->st = st;
    o->mesh = std::move(mesh);
    o->meshChanged();
    nextId = std::max(nextId, st.id + 1);
    Object* raw = o.get();
    if (index < 0 || index > (int)objects.size()) objects.push_back(std::move(o));
    else objects.insert(objects.begin() + index, std::move(o));
    return raw;
}

bool Scene::remove(uint32_t id) {
    int i = indexOf(id);
    if (i < 0) return false;
    uint32_t parent = objects[i]->st.parent;
    for (auto& o : objects) if (o->st.parent == id) o->st.parent = parent;
    objects.erase(objects.begin() + i);
    return true;
}

bool Scene::wouldCycle(uint32_t child, uint32_t newParent) const {
    int guard = 0;
    for (uint32_t p = newParent; p != 0 && guard < 1024; ++guard) {
        if (p == child) return true;
        const Object* o = find(p);
        if (!o) return false;
        p = o->st.parent;
    }
    return false;
}

size_t Scene::totalTriangles() const {
    size_t n = 0;
    for (auto& o : objects) n += o->mesh.tcount();
    return n;
}
size_t Scene::memoryBytes() const {
    size_t n = 0;
    for (auto& o : objects) n += o->mesh.memoryBytes();
    return n;
}

// ---- history -------------------------------------------------------------------------------
size_t Command::bytes() const {
    size_t n = sizeof(Command) + diff.bytes();
    if (meshBefore) n += meshBefore->memoryBytes();
    if (meshAfter) n += meshAfter->memoryBytes();
    for (auto& c : children) n += c.bytes();
    return n;
}

size_t History::bytes() const {
    size_t n = 0;
    for (auto& c : undo_) n += c.bytes();
    for (auto& c : redo_) n += c.bytes();
    return n;
}

void History::trim() {
    while (undo_.size() > maxDepth_) undo_.erase(undo_.begin());
    while (undo_.size() > 1 && bytes() > maxBytes_) undo_.erase(undo_.begin());
}

void History::push(Command c) {
    redo_.clear();
    undo_.push_back(std::move(c));
    trim();
}

void History::apply(Scene& s, const Command& c, bool undo) {
    switch (c.kind) {
    case Command::Stroke:
        if (Object* o = s.find(c.objectId)) { c.diff.apply(o->mesh, undo); o->bvh.refit(o->mesh); }
        break;
    case Command::MeshReplace:
        if (Object* o = s.find(c.objectId)) {
            const auto& src = undo ? c.meshBefore : c.meshAfter;
            if (src) { o->mesh = *src; o->meshChanged(); }
        }
        break;
    case Command::ObjectAdd:
        if (undo) s.remove(c.objectId);
        else if (c.meshAfter) s.insert(c.after, *c.meshAfter, c.index);
        break;
    case Command::ObjectRemove:
        if (undo) { if (c.meshBefore) s.insert(c.before, *c.meshBefore, c.index); }
        else s.remove(c.objectId);
        break;
    case Command::Props:
        if (Object* o = s.find(c.objectId)) o->st = undo ? c.before : c.after;
        break;
    case Command::Group:
        if (undo) for (size_t i = c.children.size(); i-- > 0;) apply(s, c.children[i], true);
        else for (auto& ch : c.children) apply(s, ch, false);
        break;
    }
}

bool History::undo(Scene& s) {
    if (undo_.empty()) return false;
    Command c = std::move(undo_.back());
    undo_.pop_back();
    apply(s, c, true);
    redo_.push_back(std::move(c));
    return true;
}

bool History::redo(Scene& s) {
    if (redo_.empty()) return false;
    Command c = std::move(redo_.back());
    redo_.pop_back();
    apply(s, c, false);
    undo_.push_back(std::move(c));
    return true;
}

} // namespace sf
