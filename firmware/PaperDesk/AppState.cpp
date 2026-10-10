#include "AppState.h"

#include <FFat.h>

namespace paperdesk {

constexpr char AppState::kPhotoPath[];
constexpr char AppState::kPhotoTempPath[];
constexpr char AppState::kTemplatePath[];
constexpr char AppState::kTemplateTempPath[];

bool AppState::begin() {
  fileSystemReady_ = FFat.begin(true);
  if (!load()) {
    setDefaults();
    save();
  } else if (mode() == Mode::kClock) {
    // Clock is now selected by physical landscape orientation. Persisted mode
    // only chooses which content the portrait UI opens.
    data_.mode = static_cast<uint8_t>(Mode::kBadge);
    save();
  }
  loadTemplates();
  if (fileSystemReady_) {
    File photo = FFat.open(kPhotoPath, FILE_READ);
    photoReady_ = photo && photo.size() == kPhotoBytes;
    photo.close();
  }
  return fileSystemReady_;
}

void AppState::setDefaults() {
  memset(&data_, 0, sizeof(data_));
  data_.magic = kMagic;
  data_.version = kVersion;
  data_.mode = static_cast<uint8_t>(Mode::kBadge);
  data_.nextTodoId = 1;
  copyText(data_.badge.name, sizeof(data_.badge.name), "PaperDesk");
  copyText(data_.badge.role, sizeof(data_.badge.role), "Your role");
  copyText(data_.badge.contact, sizeof(data_.badge.contact), "contact@example.com");
}

bool AppState::load() {
  if (!preferences_.begin("paperdesk_app", true)) return false;
  const size_t length = preferences_.getBytesLength("state");
  if (length == sizeof(data_)) {
    const size_t read = preferences_.getBytes("state", &data_, sizeof(data_));
    preferences_.end();
    if (read != sizeof(data_) || data_.magic != kMagic ||
        data_.mode > static_cast<uint8_t>(Mode::kTodo) || data_.todoCount > kMaxTodos) {
      return false;
    }
    if (data_.version == kVersion) return true;
    // V2 has the same compact layout; only its version marker needs updating.
    if (data_.version == 2) {
      data_.version = kVersion;
      return save();
    }
    return false;
  }

  struct LegacyDataV3 {
    uint32_t magic;
    uint16_t version;
    uint8_t mode;
    uint8_t todoCount;
    uint32_t nextTodoId;
    Badge badge;
    Todo todos[kMaxTodos];
    uint8_t templateCount;
    uint32_t nextTemplateId;
    TodoTemplate templates[kMaxTodoTemplates];
  };
  // Keep the large one-time migration buffer out of the Arduino loop task's
  // comparatively small stack. A stack allocation here can corrupt the NVS
  // reader before the first preference lookup completes.
  static LegacyDataV3 legacyV3;
  memset(&legacyV3, 0, sizeof(legacyV3));
  if (length == sizeof(legacyV3)) {
    const size_t read = preferences_.getBytes("state", &legacyV3, sizeof(legacyV3));
    preferences_.end();
    if (read != sizeof(legacyV3) || legacyV3.magic != kMagic || legacyV3.version != 3 ||
        legacyV3.mode > static_cast<uint8_t>(Mode::kTodo) || legacyV3.todoCount > kMaxTodos ||
        legacyV3.templateCount > kMaxTodoTemplates) return false;
    for (uint8_t i = 0; i < legacyV3.templateCount; ++i) {
      if (legacyV3.templates[i].itemCount > kMaxTemplateItems) return false;
    }
    setDefaults();
    data_.mode = legacyV3.mode;
    data_.todoCount = legacyV3.todoCount;
    data_.nextTodoId = legacyV3.nextTodoId;
    data_.badge = legacyV3.badge;
    memcpy(data_.todos, legacyV3.todos, sizeof(legacyV3.todos));
    memset(&templateData_, 0, sizeof(templateData_));
    templateData_.magic = kTemplateMagic;
    templateData_.version = kTemplateVersion;
    templateData_.templateCount = legacyV3.templateCount;
    templateData_.nextTemplateId = legacyV3.nextTemplateId;
    memcpy(templateData_.templates, legacyV3.templates, sizeof(legacyV3.templates));
    if (fileSystemReady_ && !saveTemplates()) return false;
    return save();
  }

  struct LegacyBadgeV1 {
    char name[49];
    char role[65];
    char contact[97];
    uint8_t layout;
  };
  struct LegacyDataV1 {
    uint32_t magic;
    uint16_t version;
    uint8_t mode;
    uint8_t todoCount;
    uint32_t nextTodoId;
    LegacyBadgeV1 badge;
    Todo todos[kMaxTodos];
  };
  LegacyDataV1 legacy{};
  const size_t read = length == sizeof(legacy)
                          ? preferences_.getBytes("state", &legacy, sizeof(legacy))
                          : 0;
  preferences_.end();
  if (read != sizeof(legacy) || legacy.magic != kMagic || legacy.version != 1 ||
      legacy.mode > static_cast<uint8_t>(Mode::kTodo) || legacy.todoCount > kMaxTodos) {
    return false;
  }

  memset(&data_, 0, sizeof(data_));
  data_.magic = kMagic;
  data_.version = kVersion;
  data_.mode = legacy.mode;
  data_.todoCount = legacy.todoCount;
  data_.nextTodoId = legacy.nextTodoId;
  copyText(data_.badge.name, sizeof(data_.badge.name), legacy.badge.name);
  copyText(data_.badge.role, sizeof(data_.badge.role), legacy.badge.role);
  copyText(data_.badge.contact, sizeof(data_.badge.contact), legacy.badge.contact);
  // Old layout 2 was the asymmetric photo card, which is closest to B.
  data_.badge.layout = legacy.badge.layout == 2 ? 1 : min<uint8_t>(legacy.badge.layout, 1);
  memcpy(data_.todos, legacy.todos, sizeof(legacy.todos));
  return save();
}

bool AppState::save() {
  if (!preferences_.begin("paperdesk_app", false)) return false;
  const size_t written = preferences_.putBytes("state", &data_, sizeof(data_));
  preferences_.end();
  return written == sizeof(data_);
}

bool AppState::loadTemplates() {
  if (!fileSystemReady_) return false;
  File file = FFat.open(kTemplatePath, FILE_READ);
  if (file && file.size() == sizeof(templateData_) &&
      file.read(reinterpret_cast<uint8_t*>(&templateData_), sizeof(templateData_)) ==
          sizeof(templateData_) &&
      templateData_.magic == kTemplateMagic && templateData_.version == kTemplateVersion &&
      templateData_.templateCount <= kMaxTodoTemplates) {
    file.close();
    for (uint8_t i = 0; i < templateData_.templateCount; ++i) {
      if (templateData_.templates[i].itemCount > kMaxTemplateItems) {
        memset(&templateData_, 0, sizeof(templateData_));
        break;
      }
    }
    if (templateData_.magic == kTemplateMagic) return true;
  } else if (file) {
    file.close();
  }
  memset(&templateData_, 0, sizeof(templateData_));
  templateData_.magic = kTemplateMagic;
  templateData_.version = kTemplateVersion;
  templateData_.nextTemplateId = 1;
  return false;
}

bool AppState::saveTemplates() {
  if (!fileSystemReady_) return false;
  FFat.remove(kTemplateTempPath);
  File file = FFat.open(kTemplateTempPath, FILE_WRITE);
  if (!file) return false;
  const size_t written = file.write(reinterpret_cast<const uint8_t*>(&templateData_),
                                    sizeof(templateData_));
  file.flush();
  file.close();
  if (written != sizeof(templateData_)) {
    FFat.remove(kTemplateTempPath);
    return false;
  }
  FFat.remove(kTemplatePath);
  return FFat.rename(kTemplateTempPath, kTemplatePath);
}

bool AppState::setMode(const String& value) {
  Mode selected;
  if (value == "badge") selected = Mode::kBadge;
  else if (value == "todo") selected = Mode::kTodo;
  else return false;
  data_.mode = static_cast<uint8_t>(selected);
  return save();
}

bool AppState::updateBadge(const String& name, const String& role, const String& contact,
                           const String& company, const String& number, int layout) {
  if (name.isEmpty() || name.length() > 48 || role.length() > 64 ||
      contact.length() > 96 || company.length() > 64 || number.length() > 32 ||
      layout < 0 || layout > 5) return false;
  copyText(data_.badge.name, sizeof(data_.badge.name), name);
  copyText(data_.badge.role, sizeof(data_.badge.role), role);
  copyText(data_.badge.contact, sizeof(data_.badge.contact), contact);
  copyText(data_.badge.company, sizeof(data_.badge.company), company);
  copyText(data_.badge.number, sizeof(data_.badge.number), number);
  data_.badge.layout = static_cast<uint8_t>(layout);
  return save();
}

bool AppState::addTodo(const String& text) {
  if (text.isEmpty() || text.length() > 96 || data_.todoCount >= kMaxTodos) return false;
  Todo& item = data_.todos[data_.todoCount++];
  memset(&item, 0, sizeof(item));
  item.id = data_.nextTodoId++;
  copyText(item.text, sizeof(item.text), text);
  return save();
}

int AppState::findTodo(uint32_t id) const {
  for (uint8_t i = 0; i < data_.todoCount; ++i) {
    if (data_.todos[i].id == id) return i;
  }
  return -1;
}

bool AppState::deleteTodo(uint32_t id) {
  const int index = findTodo(id);
  if (index < 0) return false;
  for (uint8_t i = index; i + 1 < data_.todoCount; ++i) data_.todos[i] = data_.todos[i + 1];
  --data_.todoCount;
  memset(&data_.todos[data_.todoCount], 0, sizeof(Todo));
  return save();
}

bool AppState::moveTodo(uint32_t id, int direction) {
  const int index = findTodo(id);
  const int target = index + (direction < 0 ? -1 : 1);
  if (index < 0 || target < 0 || target >= data_.todoCount) return false;
  const Todo temporary = data_.todos[index];
  data_.todos[index] = data_.todos[target];
  data_.todos[target] = temporary;
  return save();
}

bool AppState::setTodoCompleted(uint32_t id, bool completed) {
  const int index = findTodo(id);
  if (index < 0) return false;
  data_.todos[index].completed = completed ? 1 : 0;
  return save();
}

int AppState::findTodoTemplate(uint32_t id) const {
  for (uint8_t i = 0; i < templateData_.templateCount; ++i) {
    if (templateData_.templates[i].id == id) return i;
  }
  return -1;
}

bool AppState::saveTodoTemplate(uint32_t id, const String& name, const String& items) {
  String cleanName = name;
  cleanName.trim();
  if (cleanName.isEmpty() || cleanName.length() > 48) return false;

  TodoTemplate candidate{};
  candidate.id = id;
  copyText(candidate.name, sizeof(candidate.name), cleanName);
  size_t start = 0;
  while (start <= items.length()) {
    const int found = items.indexOf('\n', start);
    String item = found < 0 ? items.substring(start) : items.substring(start, found);
    item.trim();
    if (!item.isEmpty()) {
      if (item.length() > 96 || candidate.itemCount >= kMaxTemplateItems) return false;
      copyText(candidate.items[candidate.itemCount], sizeof(candidate.items[0]), item);
      ++candidate.itemCount;
    }
    if (found < 0) break;
    start = static_cast<size_t>(found) + 1;
  }
  if (candidate.itemCount == 0) return false;

  int index = id == 0 ? -1 : findTodoTemplate(id);
  if (id != 0 && index < 0) return false;
  const uint8_t previousCount = templateData_.templateCount;
  const uint32_t previousNextId = templateData_.nextTemplateId;
  if (id == 0) {
    if (templateData_.templateCount >= kMaxTodoTemplates) return false;
    index = templateData_.templateCount++;
    candidate.id = templateData_.nextTemplateId++;
    if (candidate.id == 0) candidate.id = templateData_.nextTemplateId++;
  }
  const TodoTemplate previous = templateData_.templates[index];
  templateData_.templates[index] = candidate;
  if (saveTemplates()) return true;
  templateData_.templates[index] = previous;
  templateData_.templateCount = previousCount;
  templateData_.nextTemplateId = previousNextId;
  return false;
}

bool AppState::deleteTodoTemplate(uint32_t id) {
  const int index = findTodoTemplate(id);
  if (index < 0) return false;
  const TodoTemplate removed = templateData_.templates[index];
  for (uint8_t i = index; i + 1 < templateData_.templateCount; ++i) {
    templateData_.templates[i] = templateData_.templates[i + 1];
  }
  --templateData_.templateCount;
  memset(&templateData_.templates[templateData_.templateCount], 0, sizeof(TodoTemplate));
  if (saveTemplates()) return true;
  for (uint8_t i = templateData_.templateCount; i > index; --i) {
    templateData_.templates[i] = templateData_.templates[i - 1];
  }
  templateData_.templates[index] = removed;
  ++templateData_.templateCount;
  return false;
}

int AppState::importTodoTemplates(const String& ids) {
  uint32_t selected[kMaxTodoTemplates]{};
  uint8_t selectedCount = 0;
  uint8_t itemTotal = 0;
  size_t start = 0;
  while (start <= ids.length()) {
    const int found = ids.indexOf(',', start);
    String value = found < 0 ? ids.substring(start) : ids.substring(start, found);
    value.trim();
    if (!value.isEmpty()) {
      const uint32_t id = strtoul(value.c_str(), nullptr, 10);
      const int index = findTodoTemplate(id);
      if (id == 0 || index < 0 || selectedCount >= kMaxTodoTemplates) return -1;
      bool duplicate = false;
      for (uint8_t i = 0; i < selectedCount; ++i) duplicate |= selected[i] == id;
      if (!duplicate) {
        selected[selectedCount++] = id;
        itemTotal += templateData_.templates[index].itemCount;
      }
    }
    if (found < 0) break;
    start = static_cast<size_t>(found) + 1;
  }
  if (selectedCount == 0) return -1;
  if (data_.todoCount + itemTotal > kMaxTodos) return -2;

  const uint8_t previousCount = data_.todoCount;
  const uint32_t previousNextId = data_.nextTodoId;
  for (uint8_t i = 0; i < selectedCount; ++i) {
    const TodoTemplate& source = templateData_.templates[findTodoTemplate(selected[i])];
    for (uint8_t j = 0; j < source.itemCount; ++j) {
      Todo& item = data_.todos[data_.todoCount++];
      memset(&item, 0, sizeof(item));
      item.id = data_.nextTodoId++;
      copyText(item.text, sizeof(item.text), source.items[j]);
    }
  }
  if (save()) return itemTotal;
  while (data_.todoCount > previousCount) {
    --data_.todoCount;
    memset(&data_.todos[data_.todoCount], 0, sizeof(Todo));
  }
  data_.nextTodoId = previousNextId;
  return -1;
}

bool AppState::beginPhotoWrite() {
  if (!fileSystemReady_) return false;
  abortPhotoWrite();
  FFat.remove(kPhotoTempPath);
  photoFile_ = FFat.open(kPhotoTempPath, FILE_WRITE);
  photoWritten_ = 0;
  return static_cast<bool>(photoFile_);
}

bool AppState::appendPhoto(const uint8_t* bytes, size_t length) {
  if (!photoFile_ || photoWritten_ + length > kPhotoBytes) return false;
  const size_t written = photoFile_.write(bytes, length);
  photoWritten_ += written;
  return written == length;
}

bool AppState::finishPhotoWrite() {
  if (!photoFile_) return false;
  photoFile_.flush();
  photoFile_.close();
  if (photoWritten_ != kPhotoBytes) {
    FFat.remove(kPhotoTempPath);
    return false;
  }
  FFat.remove(kPhotoPath);
  photoReady_ = FFat.rename(kPhotoTempPath, kPhotoPath);
  return photoReady_;
}

void AppState::abortPhotoWrite() {
  if (photoFile_) photoFile_.close();
  if (fileSystemReady_) FFat.remove(kPhotoTempPath);
  photoWritten_ = 0;
}

const char* AppState::modeName(Mode mode) {
  switch (mode) {
    case Mode::kClock: return "clock";
    case Mode::kBadge: return "badge";
    case Mode::kTodo: return "todo";
  }
  return "clock";
}

void AppState::copyText(char* target, size_t capacity, const String& value) {
  if (capacity == 0) return;
  const size_t count = min(capacity - 1, value.length());
  memcpy(target, value.c_str(), count);
  target[count] = '\0';
}

}  // namespace paperdesk
