#include "local_search_engine.h++"
#include "lucene++/Lucene.h"
#include "util/rich_text.h++"
#include "util/common.h++"

// Lucene++ public headers
#include "lucene++/FSDirectory.h"
#include "lucene++/IndexWriter.h"
#include "lucene++/StandardAnalyzer.h"
#include "lucene++/Document.h"
#include "lucene++/Field.h"
#include "lucene++/NumericField.h"
#include "lucene++/IndexSearcher.h"
#include "lucene++/BooleanQuery.h"
#include "lucene++/BooleanClause.h"
#include "lucene++/TermQuery.h"
#include "lucene++/Term.h"
#include "lucene++/TopScoreDocCollector.h"
#include "lucene++/TopDocs.h"
#include "lucene++/ScoreDoc.h"
#include "lucene++/StringUtils.h"
#include "lucene++/IndexReader.h"

#include <limits>
#include <cstdio>
#include <cstring>

using namespace Lucene;
using std::optional;

namespace Ludwig {

static constexpr auto FIELD_ID = L"id";
static constexpr auto FIELD_TYPE = L"type";
static constexpr auto FIELD_BOARD = L"board";
static constexpr auto FIELD_KEY = L"key"; // unique doc key for delete/update
static constexpr auto FIELD_TEXT = L"text"; // aggregated text

static inline auto to_lucene(const std::string_view sv) -> String {
  return StringUtils::toUnicode(reinterpret_cast<const uint8_t*>(sv.data()), (int32_t)sv.size());
}

static inline auto to_lucene(const std::string& s) -> String {
  return StringUtils::toUnicode(reinterpret_cast<const uint8_t*>(s.data()), (int32_t)s.size());
}

static inline auto type_to_int(SearchResultType t) -> int32_t {
  return static_cast<int32_t>(t);
}

LocalSearchEngine::LocalSearchEngine(std::filesystem::path index_dir) {
  // Open/create FSDirectory and an IndexWriter with StandardAnalyzer
  directory_ = FSDirectory::open(to_lucene(index_dir.string()));
  analyzer_ = newLucene<StandardAnalyzer>(LuceneVersion::LUCENE_CURRENT);
  // Determine whether to create a brand new index
  bool create = true;
  try {
    auto files = FSDirectory::listAll(to_lucene(index_dir.string()));
    create = files.empty();
  } catch (...) {
    create = true;
  }
  writer_ = newLucene<IndexWriter>(directory_, analyzer_, create, IndexWriter::MaxFieldLengthLIMITED);
}

LocalSearchEngine::~LocalSearchEngine() {
  try {
    std::lock_guard<std::mutex> lock(writer_mutex_);
    if (writer_) {
      writer_->commit();
      writer_->close();
      writer_.reset();
    }
    if (directory_) {
      directory_->close();
      directory_.reset();
    }
  } catch (...) {
  }
}

auto LocalSearchEngine::make_doc_key(uint64_t id, SearchResultType type) const -> String {
  // key = type:id in hex for uniqueness
  char buf[2 + 16 + 1];
  snprintf(buf, sizeof(buf), "%02x%016llx", (unsigned)type_to_int(type), (unsigned long long)id);
  return StringUtils::toUnicode(reinterpret_cast<const uint8_t*>(buf), (int32_t)strlen(buf));
}

auto LocalSearchEngine::add_common_fields(const DocumentPtr& doc, uint64_t id, SearchResultType type, optional<uint64_t> board_id) -> void {
  // Unique key field for deletes, not analyzed, stored
  doc->add(newLucene<Field>(FIELD_KEY, make_doc_key(id, type), Field::STORE_YES, Field::INDEX_NOT_ANALYZED));
  // Store numeric fields with NumericField for range/sort and a stored string copy
  doc->add(newLucene<NumericField>(FIELD_ID, Field::STORE_YES, true)->setLongValue((int64_t)id));
  doc->add(newLucene<NumericField>(FIELD_TYPE, Field::STORE_YES, true)->setIntValue(type_to_int(type)));
  if (board_id.has_value()) {
    doc->add(newLucene<NumericField>(FIELD_BOARD, Field::STORE_YES, true)->setLongValue((int64_t)board_id.value()));
  }
}

auto LocalSearchEngine::index_document(const DocumentPtr& doc) -> void {
  std::lock_guard<std::mutex> lock(writer_mutex_);
  writer_->addDocument(doc);
  writer_->commit();
}

auto LocalSearchEngine::index(uint64_t id, const User& user) -> void {
  DocumentPtr doc = newLucene<Document>();
  add_common_fields(doc, id, SearchResultType::User, {});
  // Aggregate searchable text
  String text;
  text += to_lucene(user.name()->string_view());
  if (user.display_name_type() && user.display_name_type()->size()) {
    text += L"\n" + to_lucene(rich_text_to_plain_text(user.display_name_type(), user.display_name()));
  }
  if (user.bio_type() && user.bio_type()->size()) {
    text += L"\n" + to_lucene(rich_text_to_plain_text(user.bio_type(), user.bio()));
  }
  doc->add(newLucene<Field>(FIELD_TEXT, text, Field::STORE_NO, Field::INDEX_ANALYZED));
  index_document(doc);
}

auto LocalSearchEngine::index(uint64_t id, const Board& board) -> void {
  DocumentPtr doc = newLucene<Document>();
  add_common_fields(doc, id, SearchResultType::Board, {});
  String text;
  text += to_lucene(board.name()->string_view());
  if (board.display_name_type() && board.display_name_type()->size()) {
    text += L"\n" + to_lucene(rich_text_to_plain_text(board.display_name_type(), board.display_name()));
  }
  if (board.description_type() && board.description_type()->size()) {
    text += L"\n" + to_lucene(rich_text_to_plain_text(board.description_type(), board.description()));
  }
  doc->add(newLucene<Field>(FIELD_TEXT, text, Field::STORE_NO, Field::INDEX_ANALYZED));
  index_document(doc);
}

auto LocalSearchEngine::index(uint64_t id, const Thread& thread, optional<std::reference_wrapper<const LinkCard>> card_opt) -> void {
  DocumentPtr doc = newLucene<Document>();
  add_common_fields(doc, id, SearchResultType::Thread, optional<uint64_t>(thread.board()));
  String text;
  text += to_lucene(rich_text_to_plain_text(thread.title_type(), thread.title()));
  if (thread.content_text_type() && thread.content_text_type()->size()) {
    text += L"\n" + to_lucene(rich_text_to_plain_text(thread.content_text_type(), thread.content_text()));
  }
  if (card_opt) {
    const auto& card = card_opt->get();
    if (card.title()) text += L"\n" + to_lucene(card.title()->string_view());
    if (card.description()) text += L"\n" + to_lucene(card.description()->string_view());
  }
  doc->add(newLucene<Field>(FIELD_TEXT, text, Field::STORE_NO, Field::INDEX_ANALYZED));
  index_document(doc);
}

auto LocalSearchEngine::index(uint64_t id, const Comment& comment) -> void {
  DocumentPtr doc = newLucene<Document>();
  // Do not set board for comments; we don't have it here without a DB lookup
  add_common_fields(doc, id, SearchResultType::Comment, {});
  String text = to_lucene(rich_text_to_plain_text(comment.content_type(), comment.content()));
  doc->add(newLucene<Field>(FIELD_TEXT, text, Field::STORE_NO, Field::INDEX_ANALYZED));
  index_document(doc);
}

auto LocalSearchEngine::unindex(uint64_t id, SearchResultType type) -> void {
  try {
    std::lock_guard<std::mutex> lock(writer_mutex_);
    TermPtr term = newLucene<Term>(FIELD_KEY, make_doc_key(id, type));
    IndexReaderPtr reader = IndexReader::open(directory_, false);
    reader->deleteDocuments(term);
    reader->close();
    writer_->commit();
  } catch (...) {
    spdlog::warn("Search index delete failed for {:x}", id);
  }
}

auto LocalSearchEngine::search(SearchQuery query) -> std::shared_ptr<CompletableOnce<std::vector<SearchResult>>> {
  try {
    SearcherPtr searcher = newLucene<IndexSearcher>(directory_, true);

    spdlog::info("Searching for {} (users={}, boards={}, threads={}, comments={}, board_id={:x})", query.query, query.include_users, query.include_boards, query.include_threads, query.include_comments, query.board_id);

    // Build a BooleanQuery combining text search with type and board filters
    BooleanQueryPtr boolQuery = newLucene<BooleanQuery>();
    
    // Add text search query if there's search text
    if (!query.query.empty()) {
      // Create a TermQuery for the text field
      TermPtr textTerm = newLucene<Term>(FIELD_TEXT, to_lucene(std::string(query.query)));
      QueryPtr textQuery = newLucene<TermQuery>(textTerm);
      boolQuery->add(textQuery, BooleanClause::MUST);
    }
    
    // Add missing type filters as MUST_NOT clauses
    if (!query.include_users) {
      TermPtr userTerm = newLucene<Term>(FIELD_TYPE, StringUtils::toString(type_to_int(SearchResultType::User)));
      QueryPtr userQuery = newLucene<TermQuery>(userTerm);
      boolQuery->add(userQuery, BooleanClause::MUST_NOT);
    }
    if (!query.include_boards) {
      TermPtr boardTerm = newLucene<Term>(FIELD_TYPE, StringUtils::toString(type_to_int(SearchResultType::Board)));
      QueryPtr boardQuery = newLucene<TermQuery>(boardTerm);
      boolQuery->add(boardQuery, BooleanClause::MUST_NOT);
    }
    if (!query.include_threads) {
      TermPtr threadTerm = newLucene<Term>(FIELD_TYPE, StringUtils::toString(type_to_int(SearchResultType::Thread)));
      QueryPtr threadQuery = newLucene<TermQuery>(threadTerm);
      boolQuery->add(threadQuery, BooleanClause::MUST_NOT);
    }
    if (!query.include_comments) {
      TermPtr commentTerm = newLucene<Term>(FIELD_TYPE, StringUtils::toString(type_to_int(SearchResultType::Comment)));
      QueryPtr commentQuery = newLucene<TermQuery>(commentTerm);
      boolQuery->add(commentQuery, BooleanClause::MUST_NOT);
    }
    
    // Add board filter if specified (only applies to threads)
    if (query.board_id) {
      TermPtr boardFilterTerm = newLucene<Term>(FIELD_BOARD, StringUtils::toString((int64_t)query.board_id));
      QueryPtr boardFilterQuery = newLucene<TermQuery>(boardFilterTerm);
      boolQuery->add(boardFilterQuery, BooleanClause::MUST);
    }

    // Collect top N with some headroom: offset+limit
    const int32_t need = (int32_t)std::min<size_t>(std::numeric_limits<int32_t>::max(), query.offset + query.limit);
    TopDocsPtr top = searcher->search(boolQuery, std::max(1, need));
    auto& hits = top->scoreDocs;

    spdlog::info("Search results: {:d} hits (offset: {:d}, limit: {:d})", hits.size(), query.offset, query.limit);

    std::vector<SearchResult> results;
    if ((size_t)hits.size() > query.offset) {
      const size_t end = std::min((size_t)hits.size(), query.offset + query.limit);
      results.reserve(end - query.offset);
      for (size_t i = query.offset; i < end; ++i) {
        const int32_t doc_id = hits[i]->doc;
        DocumentPtr d = searcher->doc(doc_id);
        const int32_t type_i = StringUtils::toInt(d->get(FIELD_TYPE));
        const auto t = static_cast<SearchResultType>(type_i);
        const uint64_t entity_id = (uint64_t)StringUtils::toLong(d->get(FIELD_ID));
        spdlog::info("Search result {}: {:x} {}", i, entity_id, type_i);
        results.push_back(SearchResult{ t, entity_id });
      }
    }

    searcher->close();
    return std::make_shared<CompletableOnce<std::vector<SearchResult>>>(std::move(results));
  } catch (const LuceneException& e) {
    spdlog::warn("Search query failed: {}", StringUtils::toUTF8(e.getError()));
    return std::make_shared<CompletableOnce<std::vector<SearchResult>>>(std::vector<SearchResult>{});
  }
}

} // namespace Ludwig

