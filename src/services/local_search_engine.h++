#pragma once
#include "services/search_engine.h++"
#include <filesystem>
#include <mutex>
#include "lucene++/Lucene.h"

namespace Ludwig {
  class LocalSearchEngine : public SearchEngine {
  public:
    LocalSearchEngine(std::filesystem::path index_dir);
    ~LocalSearchEngine();
    auto index(uint64_t id, const User& user) -> void;
    auto index(uint64_t id, const Board& board) -> void;
    auto index(uint64_t id, const Thread& thread, std::optional<std::reference_wrapper<const LinkCard>> card_opt) -> void;
    auto index(uint64_t id, const Comment& comment) -> void;
    auto unindex(uint64_t id, SearchResultType type) -> void;
    auto search(SearchQuery query) -> std::shared_ptr<CompletableOnce<std::vector<SearchResult>>>;
  private:
    // Lucene++ handles are reference-counted smart pointers; keep them as members
    Lucene::DirectoryPtr directory_;
    Lucene::AnalyzerPtr analyzer_;
    Lucene::IndexWriterPtr writer_;
    std::mutex writer_mutex_;

    auto make_doc_key(uint64_t id, SearchResultType type) const -> Lucene::String;
    auto add_common_fields(const Lucene::DocumentPtr& doc, uint64_t id, SearchResultType type, std::optional<uint64_t> board_id = {}) -> void;
    auto index_document(const Lucene::DocumentPtr& doc) -> void;
  };
}