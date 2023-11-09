#include "remote_media.h++"
#include "models/detail.h++"
#include <libxml/HTMLparser.h>
#include <static_vector.hpp>
#include "util/lambda_macros.h++"

using std::make_shared, std::optional, std::pair, std::regex, std::regex_match,
    std::runtime_error, std::shared_ptr, std::string, std::string_view,
    flatbuffers::FlatBufferBuilder;

namespace Ludwig {
  RemoteMediaController::RemoteMediaController(
    shared_ptr<DB> db,
    shared_ptr<HttpClient> http_client,
    shared_ptr<LibXmlContext> xml_ctx,
    shared_ptr<EventBus> event_bus,
    optional<shared_ptr<SearchEngine>> search_engine
  ) : db(db), http_client(http_client), xml_ctx(xml_ctx), event_bus(event_bus), search_engine(search_engine),
      sub_fetch(event_bus->on_event(Event::ThreadFetchLinkCard, [&](Event, uint64_t id){fetch_link_card_for_thread(id);})),
      small_cache(http_client, 16384, 256),
      banner_cache(http_client, 256, 960, 160) {}

  auto RemoteMediaController::user_avatar(string_view user_name, ThumbnailCache::Callback&& cb) -> void {
    auto txn = db->open_read_txn();
    const auto user =
      txn.get_user_id_by_name(user_name).and_then([&](auto id){return txn.get_user(id);});
    if (user && user->get().avatar_url()) {
      small_cache.thumbnail(user->get().avatar_url()->str(), std::move(cb));
    } else {
      cb(make_shared<optional<pair<string, uint64_t>>>());
    }
  }

  auto RemoteMediaController::user_banner(string_view user_name, ThumbnailCache::Callback&& cb) -> void {
    auto txn = db->open_read_txn();
    const auto user =
      txn.get_user_id_by_name(user_name).and_then([&](auto id){return txn.get_user(id);});
    if (user && user->get().banner_url()) {
      banner_cache.thumbnail(user->get().banner_url()->str(), std::move(cb));
    } else {
      cb(make_shared<optional<pair<string, uint64_t>>>());
    }
  }

  auto RemoteMediaController::board_icon(string_view board_name, ThumbnailCache::Callback&& cb) -> void {
    auto txn = db->open_read_txn();
    const auto board =
      txn.get_board_id_by_name(board_name).and_then([&](auto id){return txn.get_board(id);});
    if (board && board->get().icon_url()) {
      small_cache.thumbnail(board->get().icon_url()->str(), std::move(cb));
    } else {
      cb(make_shared<optional<pair<string, uint64_t>>>());
    }
  }

  auto RemoteMediaController::board_banner(string_view board_name, ThumbnailCache::Callback&& cb) -> void {
    auto txn = db->open_read_txn();
    const auto board =
      txn.get_board_id_by_name(board_name).and_then([&](auto id){return txn.get_board(id);});
    if (board && board->get().banner_url()) {
      banner_cache.thumbnail(board->get().banner_url()->str(), std::move(cb));
    } else {
      cb(make_shared<optional<pair<string, uint64_t>>>());
    }
  }

  auto RemoteMediaController::thread_link_card_image(uint64_t thread_id, ThumbnailCache::Callback&& cb) -> void {
    auto txn = db->open_read_txn();
    const auto thread = txn.get_thread(thread_id);
    if (thread && thread->get().content_url()) {
      const auto card = txn.get_link_card(thread->get().content_url()->string_view());
      if (card && card->get().image_url()) {
        small_cache.thumbnail(card->get().image_url()->str(), std::move(cb));
        return;
      }
    }
    cb(make_shared<optional<pair<string, uint64_t>>>());
  }

  const regex ws("^\\s*$");
  const regex bad_extensions("^.*[.](svgz?|avif|heif|tiff|jxl)$", regex::icase);

  struct PrioritizedLinkCardBuilder {
    const string& url;
    optional<MediaCategory> media_category;
    optional<string> title, description, image_url;
    uint8_t priority_title = 0, priority_description = 0, priority_image_url = 0;

    PrioritizedLinkCardBuilder(const string& url) : url(url) {}

    inline auto set_title(string str, uint8_t priority) noexcept {
      if (!str.empty() && priority_title < priority) {
        if (regex_match(str, ws)) return;
        title = str;
        priority_title = priority;
      }
    }

    inline auto set_description(string str, uint8_t priority) noexcept {
      if (!str.empty() && priority_description < priority) {
        if (regex_match(str, ws)) return;
        description = str;
        priority_description = priority;
      }
    }

    inline auto set_image_url(string str, uint8_t priority) noexcept {
      if (!str.empty() && priority_image_url < priority) {
        // Skip images with extensions we know we can't handle
        if (regex_match(str, bad_extensions)) return;
        // Fix relative URLs
        if (str.starts_with("/")) {
          auto base_url = Url::parse(url);
          if (!base_url) return;
          if (str.starts_with("//")) str = fmt::format("{}:{}", base_url->scheme, str);
          else str = fmt::format("{}://{}{}", base_url->scheme, base_url->host, str);
        } else if (!Url::parse(str)) return;
        image_url = str;
        priority_image_url = priority;
      }
    }

    inline auto save(WriteTxn& txn) -> void {
      auto card = txn.get_link_card(url);
      FlatBufferBuilder fbb;
      fbb.Finish(CreateLinkCardDirect(fbb,
        true,
        card.transform(λx(x.get().fetch_tries())).value_or(1),
        card.transform(λx(x.get().last_fetch_at())).value_or(now_s()),
        media_category,
        title.transform(λx(x.c_str())).value_or(nullptr),
        description.transform(λx(x.c_str())).value_or(nullptr),
        image_url.transform(λx(x.c_str())).value_or(nullptr)
      ));
      txn.set_link_card(url, fbb.GetBufferSpan());
    }
  };

  static inline auto str_eq(const char* lhs, const xmlChar* rhs) noexcept -> bool {
    if (rhs == nullptr) return false;
    return !strcmp(lhs, (char*)rhs);
  }

  static inline auto html_element_to_link_card(
    HtmlDoc& doc,
    xmlNodePtr node,
    PrioritizedLinkCardBuilder& card,
    xmlNodePtr& main
  ) noexcept -> void {
    const auto tag_name = node->name;

    if (str_eq("meta", tag_name)) {
      auto name = doc.attr(node, "property");
      if (name.empty()) name = doc.attr(node, "name");
      if (name == "og:title") card.set_title(doc.attr(node, "content"), 5);
      else if (name == "og:description") card.set_description(doc.attr(node, "content"), 5);
      else if (name == "og:image") card.set_image_url(doc.attr(node, "content"), 5);
      else if (name == "twitter:title") card.set_title(doc.attr(node, "content"), 4);
      else if (name == "twitter:description") card.set_description(doc.attr(node, "content"), 4);
      else if (name == "twitter:image") card.set_image_url(doc.attr(node, "content"), 4);
      else if (name == "description") card.set_description(doc.attr(node, "content"), 3);
    }
    else if (str_eq("title", tag_name)) card.set_title(doc.text_content(node), 2);
    else if (main == nullptr && str_eq("main", tag_name)) main = node;
    else if (main != nullptr && str_eq("p", tag_name)) card.set_description(doc.text_content(node), 1);
    else if (str_eq("img", tag_name)) {
      const auto width = doc.attr(node, "width");
      // Ignore images with a fixed with <64px, these are usually icons
      if (width.empty() || atoi(width.c_str()) >= 64) {
        card.set_image_url(doc.text_content(node), main == nullptr ? 1 : 2);
      }
    } else if (strlen((char*)tag_name) == 2 && tag_name[0] == 'h' && tag_name[1] >= '1' && tag_name[1] <= 6) {
      card.set_title(doc.text_content(node), main == nullptr ? 1 : 3);
    }

    if (main == nullptr && doc.attr(node, "role") == "main") main = node;
  }

  static inline auto next_node(xmlNodePtr node, xmlNodePtr& main) -> xmlNodePtr {
    if (node == nullptr) return nullptr;
    if (auto child = node->children) return child;
    do {
      if (node == main) main = nullptr;
      if (auto next = xmlNextElementSibling(node)) return next;
    } while ((node = node->parent));
    return nullptr;
  }

  static inline auto html_to_link_card(
    std::shared_ptr<LibXmlContext> xml_ctx,
    string_view html_src,
    string url,
    PrioritizedLinkCardBuilder& card
  ) noexcept -> bool {
    try {
      HtmlDoc doc(xml_ctx, html_src.data(), html_src.size(), url.c_str());
      xmlNodePtr main = nullptr;
      for (xmlNodePtr node = doc.root(); node != nullptr; node = next_node(node, main)) {
        if (node->type == XML_ELEMENT_NODE) {
          html_element_to_link_card(doc, node, card, main);
        }
      }
      return true;
    } catch (const runtime_error& e) {
      spdlog::debug("{}", e.what());
      return false;
    }
  }

  auto RemoteMediaController::fetch_link_card_for_thread(uint64_t thread_id) -> void {
    string url;
    try {
      auto txn = db->open_write_txn();
      const auto thread = ThreadDetail::get(txn, thread_id, {});
      if (!thread.should_fetch_card()) return;
      url = thread.thread().content_url()->str();
      const auto& card = thread.link_card();
      FlatBufferBuilder fbb;
      fbb.Finish(CreateLinkCardDirect(fbb,
        false,
        card.fetch_tries() + 1,
        now_s()
      ));
      txn.set_link_card(url, fbb.GetBufferSpan());
      txn.commit();
    } catch (const runtime_error& e) {
      spdlog::warn("Failed to set up link card fetch for thread {:x}: {}", thread_id, e.what());
      return;
    }
    http_client->get(url)
      .header("Accept", "text/html, application/xhtml+xml, image/ *")
      .dispatch([this, url, thread_id](auto&& rsp) {
        if (rsp->status() != 200) {
          spdlog::warn("Preview card failed: got HTTP {} from {}", rsp->status(), url);
          return;
        }
        PrioritizedLinkCardBuilder card(url);
        const auto content_type = rsp->header("content-type");
        if (content_type.starts_with("image/")) {
          card.media_category = MediaCategory::Image;
          if (small_cache.set_thumbnail(url, rsp->header("content-type"), rsp->body())) {
            card.image_url = url;
          }
        } else {
          html_to_link_card(xml_ctx, rsp->body(), url, card);
        }
        spdlog::debug(
          R"(Fetched card for {}: title "{}", description "{}", image "{}")",
          url, card.title.value_or(""), card.description.value_or(""), card.image_url.value_or("")
        );
        {
          auto txn = db->open_write_txn();
          card.save(txn);
          txn.commit();
        }
        if (search_engine) {
          auto txn = db->open_read_txn();
          const auto thread = txn.get_thread(thread_id);
          const auto card = txn.get_link_card(url);
          if (thread) (*search_engine)->index(thread_id, thread->get(), card);
        }
        event_bus->dispatch(Event::ThreadUpdate, thread_id);
      });
  }
}
