#pragma once

#include <memory>

#include <scorum/protocol/types.hpp>
#include <scorum/tags/tags_api_objects.hpp>

namespace chainbase {
class database_guard;
}

namespace scorum {
namespace tags {

class tags_api_impl;

/**
 * @brief Provide api for quick search for posts, comments, tags
 *
 * Require: tags_plugin
 *
 * @ingroup api
 * @ingroup tags_plugin
 * @addtogroup tags_api Tags API
 */
class tags_api : public std::enable_shared_from_this<tags_api>
{
    std::unique_ptr<tags_api_impl> _impl;

    std::shared_ptr<chainbase::database_guard> _guard;

    chainbase::database_guard& guard() const;

public:
    tags_api(const app::api_context& ctx);
    ~tags_api();

    void on_api_startup();

    /// @name Public API
    /// @addtogroup tags_api
    /// @{

    /**
     * @brief Return trending tags with pagination
     * @param after_tag used for pagination
     * @param limit query limit
     * @return array of tags_api_obj
     */
    std::vector<api::tag_api_obj> get_trending_tags(const std::string& after_tag, uint32_t limit) const;

    std::vector<std::pair<std::string, uint32_t>> get_tags_used_by_author(const std::string& author) const;

    /**
     * @brief Returns all tags which where used sometime with specified category and domain. The result is ordered by
     * frequency of use.
     * @param domain
     * @param category
     * @return <tag_name, frequency> pairs where 'tag_name' it's a tag which was used sometime with specified category
     * and domain and 'frequency' shows how many times this tag was used with specified category.
     */
    std::vector<std::pair<std::string, uint32_t>> get_tags_by_category(const std::string& domain,
                                                                       const std::string& category) const;

    std::vector<api::discussion> get_discussions_by_trending(const api::discussion_query& query) const;
    std::vector<api::discussion> get_discussions_by_created(const api::discussion_query& query) const;
    std::vector<api::discussion> get_discussions_by_hot(const api::discussion_query& query) const;

    /// This is obsolete method, use get_contents
    api::discussion get_content(const std::string& author, const std::string& permlink) const;

    /**
     * @brief Returns all discussions by author/permlink pairs
     * @param queries vector of @ref api::content_query struct that includes author/permlink pair
     */
    std::vector<api::discussion> get_contents(const std::vector<api::content_query>& queries) const;

    /**
     * @brief Returns all children for certain comment
     * @param parent_author
     * @param parent_permlink
     * @param depth limit
     */
    std::vector<api::discussion> get_comments(const std::string& parent_author,
                                              const std::string& parent_permlink,
                                              uint32_t depth = SCORUM_MAX_COMMENT_DEPTH) const;

    /**
     * @brief Returns all parents for certain comment
     * @param query
     */
    std::vector<api::discussion> get_parents(const api::content_query& query) const;

    /**
     * @brief This method is used to fetch all posts by author that occur after start_permlink with up to limit being
     * returned.
     * @param query
     * @warning query parameter accepts only following @ref api::discussion_query fields: \n
     *          \e start_author, \e start_permlink, \e limit
     *
     * If start_permlink is empty then discussions are returned from the beginning. This
     * should allow easy pagination.
     */
    std::vector<api::discussion> get_discussions_by_author(const api::discussion_query& query) const;

    /**
     * @brief Returns an array of posts and comments belonging to the given author that have reached cashout time and it
     * is reward > 0
     * @param query
     * @warning query parameter accepts only following @ref api::discussion_query fields: \n
     *          \e start_author, \e start_permlink, \e limit, \e truncate_body
     *
     * If start_permlink is empty then discussions are returned from the beginning. This
     * should allow easy pagination.
     */
    std::vector<api::discussion> get_paid_posts_comments_by_author(const api::discussion_query& query) const;

    /**
     * @brief This method is used to fetch all posts and comments  with up to limit being returned.
     *
     * If start_permlink and start_author is empty then discussions are returned from the beginning. This
     * should allow easy pagination.
     */
    std::vector<api::discussion> get_posts_and_comments(const api::discussion_query& query) const;

    /// @}
};

} // namespace tags
} // namespace scorum

// clang-format off
FC_API(scorum::tags::tags_api,
       (get_trending_tags)
       (get_tags_used_by_author)
       (get_tags_by_category)

       (get_discussions_by_trending)
       (get_discussions_by_created)
       (get_discussions_by_hot)

       // content
       (get_content)
       (get_contents)
       (get_comments)
       (get_parents)
       (get_discussions_by_author)
       (get_paid_posts_comments_by_author)
       (get_posts_and_comments))
// clang-format on
