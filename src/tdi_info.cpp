/*
 * Copyright(c) 2021 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this software except as stipulated in the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <exception>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <regex>
#include <string>
#include <vector>

#include <tdi/common/tdi_info.hpp>
#include <tdi/common/tdi_utils.hpp>

namespace tdi {

namespace {

#if 0
std::vector<std::string> splitString(const std::string &s,
                                     const std::string &delimiter) {
  size_t pos = 0;
  size_t start_pos = 0;
  std::vector<std::string> token_list;
  while ((pos = s.find(delimiter, start_pos)) != std::string::npos) {
    token_list.push_back(s.substr(start_pos, pos - start_pos));
    start_pos = pos + delimiter.length();
  }
  token_list.push_back(s.substr(start_pos, pos - start_pos));
  return token_list;
}

/*
 * @brief Generate unique names
 * Split the name using . as delimiter. Then create partially qualified
 * names.
 * tokens(pipe0.SwitchIngress.forward) = set(pipe0, SwitchIngress, forward)
 * full_name_list = [pipe0.SwitchIngress.forward, SwitchIngress.forward,
 * forward]
 */

std::set<std::string> generateUniqueNames(const std::string &obj_name) {
  auto tokens = splitString(obj_name, ".");
  std::string last_token = "";
  std::set<std::string> full_name_list;
  for (auto it = tokens.rbegin(); it != tokens.rend(); ++it) {
    auto token = *it;
    if (last_token == "") {
      full_name_list.insert(token);
      last_token = token;
    } else {
      full_name_list.insert(token + "." + last_token);
      last_token = token + "." + last_token;
    }
  }
  return full_name_list;
}

/* @brief This function converts a nameMap to a fullNameMap. A fullNameMap is a
 *mapping of
 * all possible names of a table entity to the table object's raw pointer.
 *
 * pipe0.SI.forward = <forward_table_1>
 * SI.forward       = <forward_table_1>
 *
 * pipe0.SE.forward = <forward_table_2>
 * SE.forward       = <forward_table_2>
 *
 * pipe0.SI.fib = <fib_table>
 * SI.fib       = <fib_table>
 * fib          = <fib_table>
 */
template <typename T>
void populateFullNameMap(
    const std::map<std::string, std::unique_ptr<T>> &nameMap,
    std::map<std::string, const T *> *fullNameMap) {
  std::set<std::string> names_to_remove;
  // We need to trim the possible names down since all are not possible.
  // Loop over all the tables
  for (const auto &name_pair : nameMap) {
    // Generate all possible names for all the tables. The below map will
    // contain
    // mapping of
    // a table name to all the possible table names.
    const auto possible_name_list = generateUniqueNames(name_pair.first);
    // Loop over all the possible names. If they are not present in the map,
    // then add them to the map. Else, just mark this name in a set kept to
    // remove these later
    for (const auto &prospective_name : possible_name_list) {
      if ((*fullNameMap).find(prospective_name) != (*fullNameMap).end()) {
        names_to_remove.insert(prospective_name);
      } else {
        (*fullNameMap)[prospective_name] = name_pair.second.get();
      }
    }
  }

  // Remove the marked names from the map as well.
  for (const auto &name : names_to_remove) {
    (*fullNameMap).erase(name);
  }
}
#endif

}  // anonymous namespace

std::unique_ptr<const TdiInfo> TdiInfo::makeTdiInfo(
    std::unique_ptr<TdiInfoParser> tdi_info_parser,
    const tdi::TableFactory *factory) {
  try {
    std::unique_ptr<const TdiInfo> tdi_info(
        new TdiInfo(std::move(tdi_info_parser), factory));
    return tdi_info;
  } catch (...) {
    LOG_ERROR("%s:%d Failed to create TdiInfo", __func__, __LINE__);
    return nullptr;
  }
}

TdiInfo::TdiInfo(std::unique_ptr<TdiInfoParser> tdi_info_parser,
                 const tdi::TableFactory *factory)
    : tdi_info_parser_(std::move(tdi_info_parser)) {
  // Go over all table_info and learn_info in the parser object and
  // create Table and Learn objects for them
  for (const auto &kv : tdi_info_parser_->tableInfoMapGet()) {
    if (tableMap.find(kv.first) != tableMap.end()) {
      LOG_ERROR("%s:%d Table:%s Already exists",
                __func__,
                __LINE__,
                kv.first.c_str());
    } else {
      tableMap[kv.first] = std::move(factory->makeTable(kv.second.get()));
    }
  }
}

tdi_status_t TdiInfo::tablesGet(
    std::vector<const Table *> *table_vec_ret) const {
  if (table_vec_ret == nullptr) {
    LOG_ERROR("%s:%d nullptr arg passed", __func__, __LINE__);
    return TDI_INVALID_ARG;
  }
  for (auto const &item : tableMap) {
    table_vec_ret->push_back(item.second.get());
  }
  return TDI_SUCCESS;
}

tdi_status_t TdiInfo::tableFromNameGet(const std::string &name,
                                       const Table **table_ret) const {
  if (invalid_table_names.find(name) != invalid_table_names.end()) {
    LOG_ERROR("%s:%d Table \"%s\" was optimized out",
              __func__,
              __LINE__,
              name.c_str());
    return TDI_INVALID_ARG;
  }
  auto it = this->fullTableMap.find(name);
  if (it == this->fullTableMap.end()) {
    LOG_ERROR("%s:%d Table \"%s\" not found", __func__, __LINE__, name.c_str());
    return TDI_OBJECT_NOT_FOUND;
  } else {
    *table_ret = it->second;
    return TDI_SUCCESS;
  }
}

tdi_status_t TdiInfo::tableFromIdGet(const tdi_id_t &id,
                                     const Table **table_ret) const {
  auto it = tableIdMap.find(id);
  if (it == tableIdMap.end()) {
    LOG_ERROR("%s:%d Table_id \"%d\" not found", __func__, __LINE__, id);
    return TDI_OBJECT_NOT_FOUND;
  } else {
    return tableFromNameGet(it->second, table_ret);
  }
}

tdi_status_t TdiInfo::learnsGet(
    std::vector<const Learn *> *learn_vec_ret) const {
  if (learn_vec_ret == nullptr) {
    LOG_ERROR("%s:%d nullptr arg passed", __func__, __LINE__);
    return TDI_INVALID_ARG;
  }
  for (auto &item : learnMap) {
    learn_vec_ret->push_back(item.second.get());
  }
  return TDI_SUCCESS;
}

tdi_status_t TdiInfo::learnFromNameGet(std::string name,
                                       const Learn **learn_ret) const {
  if (this->fullLearnMap.find(name) == this->fullLearnMap.end()) {
    LOG_ERROR(
        "%s:%d Learn Obj \"%s\" not found", __func__, __LINE__, name.c_str());
    return TDI_OBJECT_NOT_FOUND;
  }
  *learn_ret = this->fullLearnMap.at(name);
  return TDI_SUCCESS;
}

tdi_status_t TdiInfo::learnFromIdGet(tdi_id_t id,
                                     const Learn **learn_ret) const {
  if (this->learnIdMap.find(id) == this->learnIdMap.end()) {
    LOG_ERROR("%s:%d Learn_id \"%d\" not found", __func__, __LINE__, id);
    return TDI_OBJECT_NOT_FOUND;
  }
  return this->learnFromNameGet(learnIdMap.at(id), learn_ret);
}

#ifdef _TDI_FROM_BFRT
// from tdi_info_impl.cpp
tdi_status_t TdiInfo::tdiInfoTablesDependentOnThisTableGet(
    const tdi_id_t &tbl_id, std::vector<tdi_id_t> *table_vec_ret) const {
  if (table_vec_ret == nullptr) {
    LOG_ERROR("%s:%d nullptr arg passed", __func__, __LINE__);
    return TDI_INVALID_ARG;
  }
  if (tables_dependent_on_this_table_map.find(tbl_id) ==
      tables_dependent_on_this_table_map.end()) {
    LOG_ERROR("%s:%d Info requested for the given table with id %d not found",
              __func__,
              __LINE__,
              tbl_id);
    return TDI_OBJECT_NOT_FOUND;
  }
  *table_vec_ret = tables_dependent_on_this_table_map.at(tbl_id);
  return TDI_SUCCESS;
}

tdi_status_t TdiInfo::tdiInfoTablesThisTableDependsOnGet(
    const tdi_id_t &tbl_id, std::vector<tdi_id_t> *table_vec_ret) const {
  if (table_vec_ret == nullptr) {
    LOG_ERROR("%s:%d nullptr arg passed", __func__, __LINE__);
    return TDI_INVALID_ARG;
  }
  const Table *table;
  auto sts = tableFromIdGet(tbl_id, &table);
  if (sts != TDI_SUCCESS) {
    LOG_ERROR(
        "%s:%d Unable to get table list that the table with id %d depends on",
        __func__,
        __LINE__,
        tbl_id);
    return sts;
  }
  //auto *table_info = reinterpret_cast<const TableInfo >(table);
  for (const auto &kv : table->table_ref_map) {
    if (kv.first != "action_data_table_refs" &&
        kv.first != "selection_table_refs" && kv.first != "other") {
      continue;
    }
    const auto &ref_list = kv.second;
    for (const auto &ref : ref_list) {
      // Only expose the tables which this table indirectly depends on
      if (ref.indirect_ref == false) {
        continue;
      }
      table_vec_ret->push_back(ref.id);
    }
  }
  return TDI_SUCCESS;
}
#endif
}  // namespace tdi
