#include "ToDoStore.h"

#include <algorithm>
#include <cstring>
#include <ctime>

namespace {
constexpr size_t MAX_TODO_ITEMS = 128;
constexpr size_t MAX_TODO_TEXT = 256;
}

int64_t ToDoStore::nowEpoch() {
  const time_t now = time(nullptr);
  return now > 100000 ? static_cast<int64_t>(now) : 0;
}

ToDoItem* ToDoStore::findMutable(const uint32_t id) {
  const auto it = std::find_if(items.begin(), items.end(), [id](const ToDoItem& item) { return item.id == id; });
  return it == items.end() ? nullptr : &*it;
}

const ToDoItem* ToDoStore::find(const uint32_t id) const {
  const auto it = std::find_if(items.begin(), items.end(), [id](const ToDoItem& item) { return item.id == id; });
  return it == items.end() ? nullptr : &*it;
}

void ToDoStore::normalizeOrder() {
  for (size_t i = 0; i < items.size(); ++i) items[i].order = static_cast<uint32_t>(i);
}

void ToDoStore::toJson(JsonDocument& doc) const {
  doc["nextId"] = nextId;
  JsonArray array = doc["items"].to<JsonArray>();
  for (const auto& item : items) {
    JsonObject out = array.add<JsonObject>();
    out["id"] = item.id;
    out["text"] = item.text;
    out["completed"] = item.completed;
    out["priority"] = item.priority;
    out["order"] = item.order;
    out["createdAt"] = item.createdAt;
    out["updatedAt"] = item.updatedAt;
  }
}

bool ToDoStore::fromJson(JsonVariantConst doc) {
  items.clear();
  nextId = std::max<uint32_t>(1, doc["nextId"] | static_cast<uint32_t>(1));
  JsonArrayConst array = doc["items"].as<JsonArrayConst>();
  for (JsonObjectConst raw : array) {
    const char* text = raw["text"] | "";
    if (!text[0]) continue;
    ToDoItem item;
    item.id = raw["id"] | nextId++;
    if (item.id >= nextId) nextId = item.id + 1;
    item.text.assign(text, std::min(strlen(text), MAX_TODO_TEXT));
    item.completed = raw["completed"] | false;
    item.priority = raw["priority"] | false;
    item.order = raw["order"] | static_cast<uint32_t>(items.size());
    item.createdAt = raw["createdAt"] | static_cast<int64_t>(0);
    item.updatedAt = raw["updatedAt"] | item.createdAt;
    items.push_back(std::move(item));
    if (items.size() >= MAX_TODO_ITEMS) break;
  }
  std::sort(items.begin(), items.end(), [](const ToDoItem& a, const ToDoItem& b) {
    return a.order == b.order ? a.id < b.id : a.order < b.order;
  });
  normalizeOrder();
  return true;
}

bool ToDoStore::add(const std::string& text, uint32_t* idOut) {
  if (items.size() >= MAX_TODO_ITEMS || text.empty()) return false;
  ToDoItem item;
  item.id = nextId++;
  if (nextId == 0) nextId = 1;
  item.text = text.substr(0, MAX_TODO_TEXT);
  item.order = static_cast<uint32_t>(items.size());
  item.createdAt = nowEpoch();
  item.updatedAt = item.createdAt;
  items.push_back(std::move(item));
  if (idOut) *idOut = items.back().id;
  return saveToFile();
}

bool ToDoStore::update(const uint32_t id, const std::string& text, const bool completed) {
  ToDoItem* item = findMutable(id);
  if (!item || text.empty()) return false;
  const std::string clippedText = text.substr(0, MAX_TODO_TEXT);
  if (item->text == clippedText && item->completed == completed) return true;
  item->text = clippedText;
  item->completed = completed;
  item->updatedAt = nowEpoch();
  return saveToFile();
}

bool ToDoStore::toggle(const uint32_t id) {
  ToDoItem* item = findMutable(id);
  if (!item) return false;
  item->completed = !item->completed;
  item->updatedAt = nowEpoch();
  return saveToFile();
}

bool ToDoStore::togglePriority(const uint32_t id) {
  ToDoItem* item = findMutable(id);
  if (!item) return false;
  item->priority = !item->priority;
  item->updatedAt = nowEpoch();
  return saveToFile();
}

bool ToDoStore::remove(const uint32_t id) {
  const auto it = std::find_if(items.begin(), items.end(), [id](const ToDoItem& item) { return item.id == id; });
  if (it == items.end()) return false;
  items.erase(it);
  normalizeOrder();
  return saveToFile();
}

bool ToDoStore::move(const uint32_t id, const int direction) {
  const auto it = std::find_if(items.begin(), items.end(), [id](const ToDoItem& item) { return item.id == id; });
  if (it == items.end() || direction == 0) return false;
  const auto index = static_cast<int>(it - items.begin());
  const int target = index + (direction < 0 ? -1 : 1);
  if (target < 0 || target >= static_cast<int>(items.size())) return true;
  std::iter_swap(items.begin() + index, items.begin() + target);
  normalizeOrder();
  items[static_cast<size_t>(target)].updatedAt = nowEpoch();
  return saveToFile();
}

bool ToDoStore::clearCompleted() {
  const auto completed = std::find_if(items.begin(), items.end(), [](const ToDoItem& item) { return item.completed; });
  if (completed == items.end()) return true;
  items.erase(std::remove_if(items.begin(), items.end(), [](const ToDoItem& item) { return item.completed; }), items.end());
  normalizeOrder();
  return saveToFile();
}
