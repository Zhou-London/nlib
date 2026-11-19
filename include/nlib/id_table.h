#pragma once

#include <cstddef>
#include <optional>
#include <utility>

#include <nlib/map.h>
#include <nlib/typed_vector.h>

namespace nlib {

template <typename Index, typename Record>
class id_table {
 public:
  using id_type = decltype(Record::id);
  using const_iterator = typename typed_vector<Index, Record>::const_iterator;

  const Record& operator[](Index index) const noexcept { return records_[index]; }

  [[nodiscard]] bool empty() const noexcept { return records_.empty(); }
  std::size_t size() const noexcept { return records_.size(); }
  const_iterator begin() const noexcept { return records_.begin(); }
  const_iterator end() const noexcept { return records_.end(); }

  std::optional<Index> find(id_type id) const noexcept {
    const auto found = index_by_id_.find(id);
    return found == index_by_id_.end() ? std::nullopt : std::optional{found->second};
  }

  Index insert(Record&& record) {
    id_type id = record.id;
    const Index index = records_.emplace_back(std::move(record));
    index_by_id_.try_emplace(std::move(id), index);
    return index;
  }

 private:
  typed_vector<Index, Record> records_;
  map<id_type, Index> index_by_id_;
};

}  // namespace nlib
