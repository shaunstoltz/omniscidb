/*
 * Copyright 2021 OmniSci, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ForeignDataWrapperFactory.h"
#include "FsiJsonUtils.h"

#include "CsvDataWrapper.h"
#include "ForeignDataWrapper.h"
#include "InternalCatalogDataWrapper.h"
#include "InternalMemoryStatsDataWrapper.h"
#include "InternalStorageStatsDataWrapper.h"
#ifdef ENABLE_IMPORT_PARQUET
#include "ParquetDataWrapper.h"
#include "ParquetImporter.h"
#endif
#include "Catalog/os/UserMapping.h"
#include "RegexParserDataWrapper.h"
#include "Shared/misc.h"

namespace {

bool is_s3_uri(const std::string& file_path) {
  const std::string s3_prefix = "s3://";
  return file_path.find(s3_prefix) != std::string::npos;
}


bool is_valid_data_wrapper(const std::string& data_wrapper_type) {
  return
#ifdef ENABLE_IMPORT_PARQUET
      data_wrapper_type == foreign_storage::DataWrapperType::PARQUET ||
#endif
      data_wrapper_type == foreign_storage::DataWrapperType::CSV ||
      data_wrapper_type == foreign_storage::DataWrapperType::REGEX_PARSER;
}

}  // namespace

namespace foreign_storage {

void validate_regex_parser_options(const import_export::CopyParams& copy_params) {
  if (copy_params.line_regex.empty()) {
    throw std::runtime_error{"Regex parser options must contain a line regex."};
  }
}

bool is_valid_source_type(const import_export::CopyParams& copy_params) {
  return
#ifdef ENABLE_IMPORT_PARQUET
      copy_params.source_type == import_export::SourceType::kParquetFile ||
#endif
      copy_params.source_type == import_export::SourceType::kDelimitedFile ||
      copy_params.source_type == import_export::SourceType::kRegexParsedFile;
}

std::string bool_to_option_value(const bool value) {
  return value ? "TRUE" : "FALSE";
}

std::unique_ptr<ForeignDataWrapper> ForeignDataWrapperFactory::createForGeneralImport(
    const std::string& data_wrapper_type,
    const int db_id,
    const ForeignTable* foreign_table,
    const UserMapping* user_mapping) {
  CHECK(is_valid_data_wrapper(data_wrapper_type));

  if (data_wrapper_type == DataWrapperType::CSV) {
    return std::make_unique<CsvDataWrapper>(
        db_id, foreign_table, user_mapping, /*disable_cache=*/true);
  } else if (data_wrapper_type == DataWrapperType::REGEX_PARSER) {
    return std::make_unique<RegexParserDataWrapper>(
        db_id, foreign_table, user_mapping, true);
  }
#ifdef ENABLE_IMPORT_PARQUET
  else if (data_wrapper_type == DataWrapperType::PARQUET) {
    return std::make_unique<ParquetDataWrapper>(
        db_id, foreign_table, user_mapping, /*do_metadata_stats_validation=*/false);
  }
#endif

  return {};
}

std::unique_ptr<ForeignDataWrapper> ForeignDataWrapperFactory::createForImport(
    const std::string& data_wrapper_type,
    const int db_id,
    const ForeignTable* foreign_table,
    const UserMapping* user_mapping) {
#ifdef ENABLE_IMPORT_PARQUET
  // only supported for parquet import path currently
  CHECK(data_wrapper_type == DataWrapperType::PARQUET);
  return std::make_unique<ParquetImporter>(db_id, foreign_table, user_mapping);
#else
  return {};
#endif
}

std::unique_ptr<UserMapping>
ForeignDataWrapperFactory::createUserMappingProxyIfApplicable(
    const int db_id,
    const int user_id,
    const std::string& file_path,
    const import_export::CopyParams& copy_params,
    const ForeignServer* server) {
  return {};
}

std::unique_ptr<ForeignServer> ForeignDataWrapperFactory::createForeignServerProxy(
    const int db_id,
    const int user_id,
    const std::string& file_path,
    const import_export::CopyParams& copy_params) {
  CHECK(is_valid_source_type(copy_params));

  auto foreign_server = std::make_unique<foreign_storage::ForeignServer>();

  foreign_server->id = -1;
  foreign_server->user_id = user_id;
  if (copy_params.source_type == import_export::SourceType::kDelimitedFile) {
    foreign_server->data_wrapper_type = DataWrapperType::CSV;
  } else if (copy_params.source_type == import_export::SourceType::kRegexParsedFile) {
    foreign_server->data_wrapper_type = DataWrapperType::REGEX_PARSER;
#ifdef ENABLE_IMPORT_PARQUET
  } else if (copy_params.source_type == import_export::SourceType::kParquetFile) {
    foreign_server->data_wrapper_type = DataWrapperType::PARQUET;
#endif
  } else {
    UNREACHABLE();
  }
  foreign_server->name = "import_proxy_server";

  if (copy_params.source_type == import_export::SourceType::kOdbc) {
    throw std::runtime_error("ODBC storage not supported");
  } else if (is_s3_uri(file_path)) {
    throw std::runtime_error("AWS storage not supported");
  } else {
    foreign_server->options[AbstractFileStorageDataWrapper::STORAGE_TYPE_KEY] =
        AbstractFileStorageDataWrapper::LOCAL_FILE_STORAGE_TYPE;
  }

  return foreign_server;
}

std::unique_ptr<ForeignTable> ForeignDataWrapperFactory::createForeignTableProxy(
    const int db_id,
    const TableDescriptor* table,
    const std::string& copy_from_source,
    const import_export::CopyParams& copy_params,
    const ForeignServer* server) {
  CHECK(is_valid_source_type(copy_params));

  auto catalog = Catalog_Namespace::SysCatalog::instance().getCatalog(db_id);
  auto foreign_table = std::make_unique<ForeignTable>();

  *static_cast<TableDescriptor*>(foreign_table.get()) =
      *table;  // copy table related values

  CHECK(server);
  foreign_table->foreign_server = server;

  // populate options for regex filtering of file-paths in supported data types
  if (copy_params.source_type == import_export::SourceType::kRegexParsedFile ||
      copy_params.source_type == import_export::SourceType::kDelimitedFile ||
      copy_params.source_type == import_export::SourceType::kParquetFile) {
    if (copy_params.regex_path_filter.has_value()) {
      foreign_table->options[AbstractFileStorageDataWrapper::REGEX_PATH_FILTER_KEY] =
          copy_params.regex_path_filter.value();
    }
    if (copy_params.file_sort_order_by.has_value()) {
      foreign_table->options[AbstractFileStorageDataWrapper::FILE_SORT_ORDER_BY_KEY] =
          copy_params.file_sort_order_by.value();
    }
    if (copy_params.file_sort_regex.has_value()) {
      foreign_table->options[AbstractFileStorageDataWrapper::FILE_SORT_REGEX_KEY] =
          copy_params.file_sort_regex.value();
    }
  }

  if (copy_params.source_type == import_export::SourceType::kRegexParsedFile) {
    CHECK(!copy_params.line_regex.empty());
    foreign_table->options[RegexFileBufferParser::LINE_REGEX_KEY] =
        copy_params.line_regex;
    if (!copy_params.line_start_regex.empty()) {
      foreign_table->options[RegexFileBufferParser::LINE_START_REGEX_KEY] =
          copy_params.line_start_regex;
    }
  }

  // setup data source options based on various criteria
  if (copy_params.source_type == import_export::SourceType::kOdbc) {
    throw std::runtime_error("ODBC storage not supported");
  } else if (is_s3_uri(copy_from_source)) {
    throw std::runtime_error("AWS storage not supported");
  } else {
    foreign_table->options["FILE_PATH"] = copy_from_source;
  }

  // for CSV import
  if (copy_params.source_type == import_export::SourceType::kDelimitedFile) {
    foreign_table->options[CsvFileBufferParser::DELIMITER_KEY] = copy_params.delimiter;
    foreign_table->options[CsvFileBufferParser::NULLS_KEY] = copy_params.null_str;
    switch (copy_params.has_header) {
      case import_export::ImportHeaderRow::kNoHeader:
        foreign_table->options[CsvFileBufferParser::HEADER_KEY] = "FALSE";
        break;
      case import_export::ImportHeaderRow::kHasHeader:
      case import_export::ImportHeaderRow::kAutoDetect:
        foreign_table->options[CsvFileBufferParser::HEADER_KEY] = "TRUE";
        break;
      default:
        CHECK(false);
    }
    foreign_table->options[CsvFileBufferParser::QUOTED_KEY] =
        bool_to_option_value(copy_params.quoted);
    foreign_table->options[CsvFileBufferParser::QUOTE_KEY] = copy_params.quote;
    foreign_table->options[CsvFileBufferParser::ESCAPE_KEY] = copy_params.escape;
    foreign_table->options[CsvFileBufferParser::LINE_DELIMITER_KEY] =
        copy_params.line_delim;
    foreign_table->options[CsvFileBufferParser::ARRAY_DELIMITER_KEY] =
        copy_params.array_delim;
    const std::array<char, 3> array_marker{
        copy_params.array_begin, copy_params.array_end, 0};
    foreign_table->options[CsvFileBufferParser::ARRAY_MARKER_KEY] = array_marker.data();
    foreign_table->options[CsvFileBufferParser::LONLAT_KEY] =
        bool_to_option_value(copy_params.lonlat);
    foreign_table->options[CsvFileBufferParser::GEO_ASSIGN_RENDER_GROUPS_KEY] =
        bool_to_option_value(copy_params.geo_assign_render_groups);
    if (copy_params.geo_explode_collections) {
      throw std::runtime_error(
          "geo_explode_collections is not yet supported for FSI CSV import");
    }
    foreign_table->options[CsvFileBufferParser::GEO_EXPLODE_COLLECTIONS_KEY] =
        bool_to_option_value(copy_params.geo_explode_collections);
    foreign_table->options[CsvFileBufferParser::BUFFER_SIZE_KEY] =
        std::to_string(copy_params.buffer_size);
  }

  foreign_table->initializeOptions();
  return foreign_table;
}

std::unique_ptr<ForeignDataWrapper> ForeignDataWrapperFactory::create(
    const std::string& data_wrapper_type,
    const int db_id,
    const ForeignTable* foreign_table) {
  std::unique_ptr<ForeignDataWrapper> data_wrapper;
  if (data_wrapper_type == DataWrapperType::CSV) {
    if (CsvDataWrapper::validateAndGetIsS3Select(foreign_table)) {
      UNREACHABLE();
    } else {
      data_wrapper = std::make_unique<CsvDataWrapper>(db_id, foreign_table);
    }
#ifdef ENABLE_IMPORT_PARQUET
  } else if (data_wrapper_type == DataWrapperType::PARQUET) {
    data_wrapper = std::make_unique<ParquetDataWrapper>(db_id, foreign_table);
#endif
  } else if (data_wrapper_type == DataWrapperType::REGEX_PARSER) {
    data_wrapper = std::make_unique<RegexParserDataWrapper>(db_id, foreign_table);
  } else if (data_wrapper_type == DataWrapperType::INTERNAL_CATALOG) {
    data_wrapper = std::make_unique<InternalCatalogDataWrapper>(db_id, foreign_table);
  } else if (data_wrapper_type == DataWrapperType::INTERNAL_MEMORY_STATS) {
    data_wrapper = std::make_unique<InternalMemoryStatsDataWrapper>(db_id, foreign_table);
  } else if (data_wrapper_type == DataWrapperType::INTERNAL_STORAGE_STATS) {
    data_wrapper =
        std::make_unique<InternalStorageStatsDataWrapper>(db_id, foreign_table);
  } else {
    throw std::runtime_error("Unsupported data wrapper");
  }
  return data_wrapper;
}

const ForeignDataWrapper& ForeignDataWrapperFactory::createForValidation(
    const std::string& data_wrapper_type,
    const ForeignTable* foreign_table) {
  bool is_s3_select_wrapper{false};
  std::string data_wrapper_type_key{data_wrapper_type};
  constexpr const char* S3_SELECT_WRAPPER_KEY = "CSV_S3_SELECT";
  if (foreign_table && data_wrapper_type == DataWrapperType::CSV &&
      CsvDataWrapper::validateAndGetIsS3Select(foreign_table)) {
    is_s3_select_wrapper = true;
    data_wrapper_type_key = S3_SELECT_WRAPPER_KEY;
  }

  if (validation_data_wrappers_.find(data_wrapper_type_key) ==
      validation_data_wrappers_.end()) {
    if (data_wrapper_type == DataWrapperType::CSV) {
      if (is_s3_select_wrapper) {
        UNREACHABLE();
      } else {
        validation_data_wrappers_[data_wrapper_type_key] =
            std::make_unique<CsvDataWrapper>();
      }
#ifdef ENABLE_IMPORT_PARQUET
    } else if (data_wrapper_type == DataWrapperType::PARQUET) {
      validation_data_wrappers_[data_wrapper_type_key] =
          std::make_unique<ParquetDataWrapper>();
#endif
    } else if (data_wrapper_type == DataWrapperType::REGEX_PARSER) {
      validation_data_wrappers_[data_wrapper_type_key] =
          std::make_unique<RegexParserDataWrapper>();
    } else if (data_wrapper_type == DataWrapperType::INTERNAL_CATALOG) {
      validation_data_wrappers_[data_wrapper_type_key] =
          std::make_unique<InternalCatalogDataWrapper>();
    } else if (data_wrapper_type == DataWrapperType::INTERNAL_MEMORY_STATS) {
      validation_data_wrappers_[data_wrapper_type_key] =
          std::make_unique<InternalMemoryStatsDataWrapper>();
    } else if (data_wrapper_type == DataWrapperType::INTERNAL_STORAGE_STATS) {
      validation_data_wrappers_[data_wrapper_type_key] =
          std::make_unique<InternalStorageStatsDataWrapper>();
    } else {
      UNREACHABLE();
    }
  }
  CHECK(validation_data_wrappers_.find(data_wrapper_type_key) !=
        validation_data_wrappers_.end());
  return *validation_data_wrappers_[data_wrapper_type_key];
}

void ForeignDataWrapperFactory::validateDataWrapperType(
    const std::string& data_wrapper_type) {
  const auto& supported_wrapper_types = DataWrapperType::supported_data_wrapper_types;
  if (std::find(supported_wrapper_types.begin(),
                supported_wrapper_types.end(),
                data_wrapper_type) == supported_wrapper_types.end()) {
    std::vector<std::string_view> user_facing_wrapper_types;
    for (const auto& type : supported_wrapper_types) {
      if (!shared::contains(DataWrapperType::INTERNAL_DATA_WRAPPERS, type)) {
        user_facing_wrapper_types.emplace_back(type);
      }
    }
    throw std::runtime_error{"Invalid data wrapper type \"" + data_wrapper_type +
                             "\". Data wrapper type must be one of the following: " +
                             join(user_facing_wrapper_types, ", ") + "."};
  }
}

std::map<std::string, std::unique_ptr<ForeignDataWrapper> >
    ForeignDataWrapperFactory::validation_data_wrappers_;
}  // namespace foreign_storage
