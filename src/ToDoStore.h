#pragma once

#include <PersistableStore.h>

#include <cstdint>
#include <string>
#include <vector>

struct ToDoItem {
  uint32_t id = 0;
  std::string text;
  bool completed = false;
  bool priority = false;
  uint32_t order = 0;
  int64_t createdAt = 0;
  int64_t updatedAt = 0;
};

class ToDoStore final : public PersistableStore<ToDoStore> {
  ToDoStore() = default;
  friend class PersistableStore<ToDoStore>;

  std::vector<ToDoItem> items;
  uint32_t nextId = 1;

  ToDoItem* findMutable(uint32_t id);
  void normalizeOrder();
  static int64_t nowEpoch();

 public:
  static const char* getFilePath() { return "/.crosspoint/todo.json"; }

  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  const std::vector<ToDoItem>& getItems() const { return items; }
  ToDoItem* find(uint32_t id) { return findMutable(id); }
  const ToDoItem* find(uint32_t id) const;

  bool add(const std::string& text, uint32_t* idOut = nullptr);
  bool update(uint32_t id, const std::string& text, bool completed);
  bool toggle(uint32_t id);
  bool togglePriority(uint32_t id);
  bool remove(uint32_t id);
  bool move(uint32_t id, int direction);
  bool clearCompleted();
};

#define TODO_STORE ToDoStore::getInstance()
