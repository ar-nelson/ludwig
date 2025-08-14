#include "services/local_search_engine.h++"
#include "util/rich_text.h++"
#include "fbs/records.h++"
#include "util/common.h++"
#include <iostream>
#include <memory>
#include <filesystem>
#include <future>

using namespace Ludwig;

int main() {
    TRY {
        // Create a temporary directory for the search index
        std::filesystem::path index_path = std::filesystem::temp_directory_path() / "ludwig_search_test";
        
        // Clean up any existing index
        if (std::filesystem::exists(index_path)) {
            std::filesystem::remove_all(index_path);
        }
        
        std::cout << "Creating LocalSearchEngine with index path: " << index_path << std::endl;
        
        // Create the search engine
        auto search_engine = LocalSearchEngine(index_path);
        
        // Create a simple user for testing
        flatbuffers::FlatBufferBuilder fbb;
        
        // Create a simple user with basic fields
        auto name = fbb.CreateString("testuser");
        auto bio_raw = fbb.CreateString("This is a test user for search engine testing");
        auto [bio_type, bio] = plain_text_to_rich_text(fbb, "This is a test user for search engine testing");
        auto actor_id = fbb.CreateString("https://example.com/users/testuser");
        auto inbox_url = fbb.CreateString("https://example.com/users/testuser/inbox");
        
        
        // Create the User object using UserBuilder
        UserBuilder user_builder(fbb);
        user_builder.add_name(name);
        user_builder.add_bio_raw(bio_raw);
        user_builder.add_bio_type(bio_type);
        user_builder.add_bio(bio);
        user_builder.add_actor_id(actor_id);
        user_builder.add_inbox_url(inbox_url);
        user_builder.add_instance(0);
        user_builder.add_created_at(std::time(nullptr));
        user_builder.add_salt(12345);
        user_builder.add_bot(false);
        user_builder.add_mod_state(ModState::Normal);
        
        auto user_offset = user_builder.Finish();
        fbb.Finish(user_offset);
        
        // Get the User object from the buffer
        const User* user = flatbuffers::GetRoot<User>(fbb.GetBufferPointer());
        
        std::cout << "Created test user: " << user->name()->str() << std::endl;
        std::cout << "User bio: " << user->bio_raw()->str() << std::endl;
        
        // Index the user with ID 1
        uint64_t user_id = 1;
        std::cout << "Indexing user with ID: " << user_id << std::endl;
        
        search_engine.index(user_id, *user);
        
        std::cout << "Successfully indexed user!" << std::endl;
        
        // Test search functionality
        SearchQuery query;
        query.query = "test";
        query.include_users = true;
        query.include_boards = false;
        query.include_threads = false;
        query.include_comments = false;
        query.include_cws = false;
        query.sort = SearchResultSort::Relevant;
        query.board_id = 0;
        query.offset = 0;
        query.limit = 10;
        
        std::cout << "Searching for 'test'..." << std::endl;
        
        // Use a promise to get the result from the async search
        auto promise = std::make_shared<std::promise<std::vector<SearchResult>>>();
        auto future = promise->get_future();
        
        auto search_future = search_engine.search(query);
        search_future->on_complete([&](std::vector<SearchResult> results) {
            promise->set_value(std::move(results));
        });
        
        auto results = future.get();
        
        std::cout << "Search completed. Found " << results.size() << " results." << std::endl;
        
        for (const auto& result : results) {
            std::cout << "Result - Type: " << static_cast<int>(result.type) 
                      << ", ID: " << result.id << std::endl;
        }
        
        // Clean up
        std::cout << "Cleaning up..." << std::endl;
        
        if (std::filesystem::exists(index_path)) {
            std::filesystem::remove_all(index_path);
            std::cout << "Removed test index directory." << std::endl;
        }
        
        std::cout << "Test completed successfully!" << std::endl;
        return 0;
        
    } CATCH (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        LOG_STACK_TRACE;
        return 1;
    }
} 