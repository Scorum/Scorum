#include <scorum/tags/tags_api.hpp>

#include <scorum/tags/tags_api_impl.hpp>

namespace scorum {
namespace tags {

using namespace scorum::chain;
using namespace scorum::protocol;
using namespace scorum::tags::api;

chainbase::database_guard& tags_api::guard() const
{
    return *_guard;
}

tags_api::tags_api(const app::api_context& ctx)
    : _impl(new tags_api_impl(*ctx.app.chain_database()))
    , _guard(ctx.app.chain_database())
{
}

tags_api::~tags_api()
{
}

void tags_api::on_api_startup()
{
}

std::vector<tag_api_obj> tags_api::get_trending_tags(const std::string& after_tag, uint32_t limit) const
{
    return guard().with_read_lock([&]() { return _impl->get_trending_tags(after_tag, limit); });
}

std::vector<std::pair<std::string, uint32_t>> tags_api::get_tags_used_by_author(const std::string& author) const
{
    return guard().with_read_lock([&]() { return _impl->get_tags_used_by_author(author); });
}

std::vector<std::pair<std::string, uint32_t>> tags_api::get_tags_by_category(const std::string& category) const
{
    return guard().with_read_lock([&]() { return _impl->get_tags_by_category(category); });
}

std::vector<discussion> tags_api::get_discussions_by_payout(const discussion_query& query) const
{
    return guard().with_read_lock([&]() { return _impl->get_discussions_by_payout(query); });
}

std::vector<discussion> tags_api::get_post_discussions_by_payout(const discussion_query& query) const
{
    return guard().with_read_lock([&]() { return _impl->get_post_discussions_by_payout(query); });
}

std::vector<discussion> tags_api::get_comment_discussions_by_payout(const discussion_query& query) const
{
    return guard().with_read_lock([&]() { return _impl->get_comment_discussions_by_payout(query); });
}

std::vector<discussion> tags_api::get_discussions_by_trending(const discussion_query& query) const
{
    return guard().with_read_lock([&]() { return _impl->get_comment_discussions_by_payout(query); });
}

std::vector<discussion> tags_api::get_discussions_by_created(const discussion_query& query) const
{
    return guard().with_read_lock([&]() { return _impl->get_discussions_by_created(query); });
}

std::vector<discussion> tags_api::get_discussions_by_active(const discussion_query& query) const
{
    return guard().with_read_lock([&]() { return _impl->get_discussions_by_active(query); });
}

std::vector<discussion> tags_api::get_discussions_by_cashout(const discussion_query& query) const
{
    return guard().with_read_lock([&]() { return _impl->get_discussions_by_cashout(query); });
}

std::vector<discussion> tags_api::get_discussions_by_votes(const discussion_query& query) const
{
    return guard().with_read_lock([&]() { return _impl->get_discussions_by_votes(query); });
}

std::vector<discussion> tags_api::get_discussions_by_children(const discussion_query& query) const
{
    return guard().with_read_lock([&]() { return _impl->get_discussions_by_children(query); });
}

std::vector<discussion> tags_api::get_discussions_by_hot(const discussion_query& query) const
{
    return guard().with_read_lock([&]() { return _impl->get_discussions_by_hot(query); });
}

std::vector<discussion> tags_api::get_discussions_by_comments(const discussion_query& query) const
{
    return guard().with_read_lock([&]() { return _impl->get_discussions_by_comments(query); });
}

std::vector<discussion> tags_api::get_discussions_by_promoted(const discussion_query& query) const
{
    return guard().with_read_lock([&]() { return _impl->get_discussions_by_promoted(query); });
}

discussion tags_api::get_content(const std::string& author, const std::string& permlink) const
{
    return guard().with_read_lock([&]() { return _impl->get_content(author, permlink); });
}

std::vector<discussion> tags_api::get_content_replies(const std::string& parent,
                                                      const std::string& parent_permlink) const
{
    return guard().with_read_lock([&]() { return _impl->get_content_replies(parent, parent_permlink); });
}

std::vector<discussion>
tags_api::get_comments(const std::string& parent_author, const std::string& parent_permlink, uint32_t depth) const
{
    return guard().with_read_lock([&]() { return _impl->get_comments(parent_author, parent_permlink, depth); });
}

std::vector<discussion>
tags_api::get_discussions_by_author(const std::string& author, const std::string& start_permlink, uint32_t limit) const
{
    try
    {
        // clang-format off
        return guard().with_read_lock([&]() {
            return _impl->get_discussions_by_author(author, start_permlink, limit);
        });
        // clang-format on
    }
    FC_CAPTURE_AND_RETHROW((author)(start_permlink)(limit))
}

state tags_api::get_state(std::string path) const
{
    try
    {
        // clang-format off
        return guard().with_read_lock([&]() {
            return _impl->get_state(path);
        });
        // clang-format on
    }
    FC_CAPTURE_AND_RETHROW((path))
}

} // namespace tags
} // namespace scorum
