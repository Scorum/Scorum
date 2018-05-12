#pragma once

#include <boost/multi_index/composite_key.hpp>

#include <scorum/protocol/types.hpp>

#include <scorum/chain/database/database.hpp>
#include <scorum/chain/schema/comment_objects.hpp>

#include <scorum/app/plugin.hpp>

namespace scorum {
namespace tags {

using namespace scorum::chain;
using namespace boost::multi_index;

using chainbase::object;
using chainbase::oid;

//
// Plugins should #define their SPACE_ID's so plugins with
// conflicting SPACE_ID assignments can be compiled into the
// same binary (by simply re-assigning some of the conflicting #defined
// SPACE_ID's in a build script).
//
// Assignment of SPACE_ID's cannot be done at run-time because
// various template automagic depends on them being known at compile
// time.
//
#ifndef TAG_SPACE_ID
#define TAG_SPACE_ID 5
#endif

#define TAGS_PLUGIN_NAME "tags"

typedef fc::fixed_string_32 tag_name_type;

// Plugins need to define object type IDs such that they do not conflict
// globally. If each plugin uses the upper 8 bits as a space identifier,
// with 0 being for chain, then the lower 8 bits are free for each plugin
// to define as they see fit.
enum
{
    tag_object_type = (TAG_SPACE_ID << 8),
    tag_stats_object_type,
    peer_stats_object_type,
    author_tag_stats_object_type,
    category_stats_object_type
};

/**
 *  The purpose of the tag object is to allow the generation and listing of
 *  all top level posts by a string tag.  The desired sort orders include:
 *
 *  1. created - time of creation
 *  2. maturing - about to receive a payout
 *  3. active - last reply the post or any child of the post
 *  4. netvotes - individual accounts voting for post minus accounts voting against it
 *
 *  When ever a comment is modified, all tag_objects for that comment are updated to match.
 */
class tag_object : public object<tag_object_type, tag_object>
{
public:
    CHAINBASE_DEFAULT_CONSTRUCTOR(tag_object)

    id_type id;

    tag_name_type tag;
    time_point_sec created;
    time_point_sec active;
    time_point_sec cashout;
    int64_t net_rshares = 0;
    int32_t net_votes = 0;
    int32_t children = 0;
    double hot = 0;
    double trending = 0;
    share_type promoted_balance = 0;

    account_id_type author;
    comment_id_type parent;
    comment_id_type comment;

    bool is_post() const
    {
        return parent == comment_id_type();
    }
};

typedef oid<tag_object> tag_id_type;

struct by_cashout; /// all posts regardless of depth
struct by_net_rshares; /// all comments regardless of depth
struct by_parent_created;
struct by_parent_active;
struct by_parent_promoted;
struct by_parent_net_rshares; /// all top level posts by direct pending payout
struct by_parent_net_votes; /// all top level posts by direct votes
struct by_parent_trending;
struct by_parent_children; /// all top level posts with the most discussion (replies at all levels)
struct by_parent_hot;
struct by_author_parent_created; /// all blog posts by author with tag
struct by_author_comment;
struct by_reward_fund_net_rshares;
struct by_comment;
struct by_tag;

// clang-format off
typedef shared_multi_index_container<
    tag_object,
    indexed_by<
        ordered_unique<tag<by_id>, member<tag_object, tag_id_type, &tag_object::id>>,
        ordered_unique<tag<by_comment>,
                       composite_key<tag_object,
                                     member<tag_object, comment_id_type, &tag_object::comment>,
                                     member<tag_object, tag_id_type, &tag_object::id>>,
                       composite_key_compare<std::less<comment_id_type>,
                                             std::less<tag_id_type>>>,
        ordered_unique<tag<by_author_comment>,
                       composite_key<tag_object,
                                     member<tag_object, account_id_type, &tag_object::author>,
                                     member<tag_object, comment_id_type, &tag_object::comment>,
                                     member<tag_object, tag_id_type, &tag_object::id>>,
                       composite_key_compare<std::less<account_id_type>,
                                             std::less<comment_id_type>,
                                             std::less<tag_id_type>>>,
        ordered_unique<tag<by_parent_created>,
                       composite_key<tag_object,
                                     member<tag_object, tag_name_type, &tag_object::tag>,
                                     member<tag_object, comment_id_type, &tag_object::parent>,
                                     member<tag_object, time_point_sec, &tag_object::created>,
                                     member<tag_object, tag_id_type, &tag_object::id>>,
                       composite_key_compare<std::less<tag_name_type>,
                                             std::less<comment_id_type>,
                                             std::greater<time_point_sec>,
                                             std::less<tag_id_type>>>,
        ordered_unique<tag<by_parent_active>,
                       composite_key<tag_object,
                                     member<tag_object, tag_name_type, &tag_object::tag>,
                                     member<tag_object, comment_id_type, &tag_object::parent>,
                                     member<tag_object, time_point_sec, &tag_object::active>,
                                     member<tag_object, tag_id_type, &tag_object::id>>,
                       composite_key_compare<std::less<tag_name_type>,
                                             std::less<comment_id_type>,
                                             std::greater<time_point_sec>,
                                             std::less<tag_id_type>>>,
        ordered_unique<tag<by_parent_promoted>,
                       composite_key<tag_object,
                                     member<tag_object, tag_name_type, &tag_object::tag>,
                                     member<tag_object, comment_id_type, &tag_object::parent>,
                                     member<tag_object, share_type, &tag_object::promoted_balance>,
                                     member<tag_object, tag_id_type, &tag_object::id>>,
                       composite_key_compare<std::less<tag_name_type>,
                                             std::less<comment_id_type>,
                                             std::greater<share_type>,
                                             std::less<tag_id_type>>>,
        ordered_unique<tag<by_parent_net_rshares>,
                       composite_key<tag_object,
                                     member<tag_object, tag_name_type, &tag_object::tag>,
                                     member<tag_object, comment_id_type, &tag_object::parent>,
                                     member<tag_object, int64_t, &tag_object::net_rshares>,
                                     member<tag_object, tag_id_type, &tag_object::id>>,
                       composite_key_compare<std::less<tag_name_type>,
                                             std::less<comment_id_type>,
                                             std::greater<int64_t>,
                                             std::less<tag_id_type>>>,
        ordered_unique<tag<by_parent_net_votes>,
                       composite_key<tag_object,
                                     member<tag_object, tag_name_type, &tag_object::tag>,
                                     member<tag_object, comment_id_type, &tag_object::parent>,
                                     member<tag_object, int32_t, &tag_object::net_votes>,
                                     member<tag_object, tag_id_type, &tag_object::id>>,
                       composite_key_compare<std::less<tag_name_type>,
                                             std::less<comment_id_type>,
                                             std::greater<int32_t>,
                                             std::less<tag_id_type>>>,
        ordered_unique<tag<by_parent_children>,
                       composite_key<tag_object,
                                     member<tag_object, tag_name_type, &tag_object::tag>,
                                     member<tag_object, comment_id_type, &tag_object::parent>,
                                     member<tag_object, int32_t, &tag_object::children>,
                                     member<tag_object, tag_id_type, &tag_object::id>>,
                       composite_key_compare<std::less<tag_name_type>,
                                             std::less<comment_id_type>,
                                             std::greater<int32_t>,
                                             std::less<tag_id_type>>>,
        ordered_unique<tag<by_parent_hot>,
                       composite_key<tag_object,
                                     member<tag_object, tag_name_type, &tag_object::tag>,
                                     member<tag_object, comment_id_type, &tag_object::parent>,
                                     member<tag_object, double, &tag_object::hot>,
                                     member<tag_object, tag_id_type, &tag_object::id>>,
                       composite_key_compare<std::less<tag_name_type>,
                                             std::less<comment_id_type>,
                                             std::greater<double>,
                                             std::less<tag_id_type>>>,
        ordered_unique<tag<by_parent_trending>,
                       composite_key<tag_object,
                                     member<tag_object, tag_name_type, &tag_object::tag>,
                                     member<tag_object, comment_id_type, &tag_object::parent>,
                                     member<tag_object, double, &tag_object::trending>,
                                     member<tag_object, tag_id_type, &tag_object::id>>,
                       composite_key_compare<std::less<tag_name_type>,
                                             std::less<comment_id_type>,
                                             std::greater<double>,
                                             std::less<tag_id_type>>>,
        ordered_unique<
            tag<by_cashout>,
            composite_key<tag_object,
                          member<tag_object, tag_name_type, &tag_object::tag>,
                          member<tag_object, time_point_sec, &tag_object::cashout>,
                          member<tag_object, tag_id_type, &tag_object::id>>,
            composite_key_compare<std::less<tag_name_type>, std::less<time_point_sec>, std::less<tag_id_type>>>,
        ordered_unique<tag<by_net_rshares>,
                       composite_key<tag_object,
                                     member<tag_object, tag_name_type, &tag_object::tag>,
                                     member<tag_object, int64_t, &tag_object::net_rshares>,
                                     member<tag_object, tag_id_type, &tag_object::id>>,
                       composite_key_compare<std::less<tag_name_type>, std::greater<int64_t>, std::less<tag_id_type>>>,
        ordered_unique<tag<by_author_parent_created>,
                       composite_key<tag_object,
                                     member<tag_object, tag_name_type, &tag_object::tag>,
                                     member<tag_object, account_id_type, &tag_object::author>,
                                     member<tag_object, time_point_sec, &tag_object::created>,
                                     member<tag_object, tag_id_type, &tag_object::id>>,
                       composite_key_compare<std::less<tag_name_type>,
                                             std::less<account_id_type>,
                                             std::greater<time_point_sec>,
                                             std::less<tag_id_type>>>,
        ordered_unique<tag<by_reward_fund_net_rshares>,
                       composite_key<tag_object,
                                     member<tag_object, tag_name_type, &tag_object::tag>,
                                     const_mem_fun<tag_object, bool, &tag_object::is_post>,
                                     member<tag_object, int64_t, &tag_object::net_rshares>,
                                     member<tag_object, tag_id_type, &tag_object::id>>,
                       composite_key_compare<std::less<tag_name_type>,
                                             std::less<bool>,
                                             std::greater<int64_t>,
                                             std::less<tag_id_type>>>>
    >
    tag_index;
// clang-format on

/**
 *  The purpose of this index is to quickly identify how popular various tags by maintaining variou sums over
 *  all posts under a particular tag
 */
class tag_stats_object : public object<tag_stats_object_type, tag_stats_object>
{
public:
    CHAINBASE_DEFAULT_CONSTRUCTOR(tag_stats_object)

    id_type id;

    tag_name_type tag;
    asset total_payout_scr = asset(0, SCORUM_SYMBOL);
    asset total_payout_sp = asset(0, SP_SYMBOL);
    int32_t net_votes = 0;
    uint32_t top_posts = 0;
    uint32_t comments = 0;
    fc::uint128 total_trending = 0;
};

typedef oid<tag_stats_object> tag_stats_id_type;

struct by_comments;
struct by_top_posts;
struct by_trending;

// clang-format off
typedef shared_multi_index_container<
    tag_stats_object,
    indexed_by<
        ordered_unique<tag<by_id>, member<tag_stats_object, tag_stats_id_type, &tag_stats_object::id>>,
        ordered_unique<tag<by_tag>, member<tag_stats_object, tag_name_type, &tag_stats_object::tag>>,
        ordered_non_unique<tag<by_trending>,
                           composite_key<tag_stats_object,
                                         member<tag_stats_object, fc::uint128, &tag_stats_object::total_trending>,
                                         member<tag_stats_object, tag_name_type, &tag_stats_object::tag>>,
                           composite_key_compare<std::greater<fc::uint128>, std::less<tag_name_type>>>>
    >
    tag_stats_index;
// clang-format on

/**
 *  The purpose of this object is to track the relationship between accounts based upon
 * how a user votes. Every time
 *  a user votes on a post, the relationship between voter and author increases direct
 * rshares.
 */
class peer_stats_object : public object<peer_stats_object_type, peer_stats_object>
{
public:
    CHAINBASE_DEFAULT_CONSTRUCTOR(peer_stats_object)

    id_type id;

    account_id_type voter;
    account_id_type peer;
    int32_t direct_positive_votes = 0;
    int32_t direct_votes = 1;

    int32_t indirect_positive_votes = 0;
    int32_t indirect_votes = 1;

    float rank = 0;

    void update_rank()
    {
        auto direct = float(direct_positive_votes) / direct_votes;
        auto indirect = float(indirect_positive_votes) / indirect_votes;
        auto direct_order = log(direct_votes);
        auto indirect_order = log(indirect_votes);

        if (!(direct_positive_votes + indirect_positive_votes))
        {
            direct_order *= -1;
            indirect_order *= -1;
        }

        direct *= direct;
        indirect *= indirect;

        direct *= direct_order * 10;
        indirect *= indirect_order;

        rank = direct + indirect;
    }
};

typedef oid<peer_stats_object> peer_stats_id_type;

struct by_rank;
struct by_voter_peer;

// clang-format off
typedef shared_multi_index_container<
    peer_stats_object,
    indexed_by<ordered_unique<tag<by_id>, member<peer_stats_object, peer_stats_id_type, &peer_stats_object::id>>,
               ordered_unique<tag<by_rank>,
                              composite_key<peer_stats_object,
                                 member<peer_stats_object, account_id_type, &peer_stats_object::voter>,
                                 member<peer_stats_object, float, &peer_stats_object::rank>,
                                 member<peer_stats_object, account_id_type, &peer_stats_object::peer>>,
                              composite_key_compare<std::less<account_id_type>, std::greater<float>, std::less<account_id_type>>>,
               ordered_unique<tag<by_voter_peer>,
                              composite_key<peer_stats_object,
                                            member<peer_stats_object, account_id_type, &peer_stats_object::voter>,
                                            member<peer_stats_object, account_id_type, &peer_stats_object::peer>>,
                              composite_key_compare<std::less<account_id_type>, std::less<account_id_type>>>>
    >
    peer_stats_index;
// clang-format on

/**
 *  This purpose of this object is to maintain stats about which tags an author uses, how frequently, and
 *  how many total earnings of all posts by author in tag.  It also allows us to answer the question of which
 *  authors earn the most in each tag category.  This helps users to discover the best bloggers to follow for
 *  particular tags.
 */
class author_tag_stats_object : public object<author_tag_stats_object_type, author_tag_stats_object>
{
public:
    CHAINBASE_DEFAULT_CONSTRUCTOR(author_tag_stats_object)

    id_type id;
    account_id_type author;
    tag_name_type tag;
    asset total_rewards = asset(0, SCORUM_SYMBOL);
    uint32_t total_posts = 0;
};

typedef oid<author_tag_stats_object> author_tag_stats_id_type;

struct by_author_tag_posts;
struct by_author_posts_tag;
struct by_author_tag_rewards;
struct by_tag_rewards_author;

// clang-format off
typedef shared_multi_index_container<
    author_tag_stats_object,
    indexed_by<
        ordered_unique<tag<by_id>,
                       member<author_tag_stats_object, author_tag_stats_id_type, &author_tag_stats_object::id>>,
        ordered_unique<tag<by_author_posts_tag>,
                       composite_key<author_tag_stats_object,
                                     member<author_tag_stats_object, account_id_type, &author_tag_stats_object::author>,
                                     member<author_tag_stats_object, uint32_t, &author_tag_stats_object::total_posts>,
                                     member<author_tag_stats_object, tag_name_type, &author_tag_stats_object::tag>>,
                       composite_key_compare<std::less<account_id_type>, std::greater<uint32_t>, std::less<tag_name_type>>>,
        ordered_unique<tag<by_author_tag_posts>,
                       composite_key<author_tag_stats_object,
                                     member<author_tag_stats_object, account_id_type, &author_tag_stats_object::author>,
                                     member<author_tag_stats_object, tag_name_type, &author_tag_stats_object::tag>,
                                     member<author_tag_stats_object, uint32_t, &author_tag_stats_object::total_posts>>,
                       composite_key_compare<std::less<account_id_type>, std::less<tag_name_type>, std::greater<uint32_t>>>,
        ordered_unique<tag<by_author_tag_rewards>,
                       composite_key<author_tag_stats_object,
                                     member<author_tag_stats_object, account_id_type, &author_tag_stats_object::author>,
                                     member<author_tag_stats_object, tag_name_type, &author_tag_stats_object::tag>,
                                     member<author_tag_stats_object, asset, &author_tag_stats_object::total_rewards>>,
                       composite_key_compare<std::less<account_id_type>, std::less<tag_name_type>, std::greater<asset>>>,
        ordered_unique<
            tag<by_tag_rewards_author>,
            composite_key<author_tag_stats_object,
                          member<author_tag_stats_object, tag_name_type, &author_tag_stats_object::tag>,
                          member<author_tag_stats_object, asset, &author_tag_stats_object::total_rewards>,
                          member<author_tag_stats_object, account_id_type, &author_tag_stats_object::author>>,
            composite_key_compare<std::less<tag_name_type>, std::greater<asset>, std::less<account_id_type>>>>>
    author_tag_stats_index;
// clang-format on

/**
 * @brief The category_stats_object class
 * This class is used to maintain stats about which tags are used within particular category and how often.
 */
class category_stats_object : public object<category_stats_object_type, category_stats_object>
{
public:
    CHAINBASE_DEFAULT_DYNAMIC_CONSTRUCTOR(category_stats_object, (category))

    id_type id;
    tag_name_type tag;
    fc::shared_string category;
    uint32_t tags_count = 0;
};

typedef oid<category_stats_object> category_stats_id_type;

struct by_category;
struct by_tag_category;

// clang-format off
typedef shared_multi_index_container<
    category_stats_object,
    indexed_by<
        ordered_unique<tag<by_id>,
                       member<category_stats_object, category_stats_id_type, &category_stats_object::id>>,
        ordered_unique<tag<by_tag_category>,
                       composite_key<category_stats_object,
                                     member<category_stats_object, fc::shared_string, &category_stats_object::category>,
                                     member<category_stats_object, tag_name_type, &category_stats_object::tag>>,
                       composite_key_compare<fc::strcmp_less,
                                             std::less<tag_name_type>>>,
        ordered_non_unique<tag<by_category>,
                           member<category_stats_object, fc::shared_string, &category_stats_object::category>,
                           fc::strcmp_less>>>
    category_stats_index;

// clanf-format on

/**
 * Used to parse the metadata from the comment json_meta field.
 */
struct comment_metadata
{
    std::set<std::string> tags;

    static comment_metadata parse(const fc::shared_string& json_metadata)
    {
        comment_metadata meta;

        if (json_metadata.size())
        {
            try
            {
                meta = fc::json::from_string(fc::to_string(json_metadata)).as<comment_metadata>();
            }
            catch (const fc::exception&)
            {
                // Do nothing on malformed json_metadata
            }
        }

        return meta;
    }
};

} // namespace tags
} // namespace scorum

// clang-format off

FC_REFLECT(scorum::tags::tag_object,
           (id)
           (tag)
           (created)
           (active)
           (cashout)
           (net_rshares)
           (net_votes)
           (hot)
           (trending)
           (promoted_balance)
           (children)
           (author)
           (parent)
           (comment))

CHAINBASE_SET_INDEX_TYPE(scorum::tags::tag_object, scorum::tags::tag_index)

FC_REFLECT(scorum::tags::tag_stats_object,
           (id)
           (tag)
           (total_payout_scr)
           (total_payout_sp)
           (net_votes)
           (top_posts)
           (comments)
           (total_trending))

CHAINBASE_SET_INDEX_TYPE(scorum::tags::tag_stats_object, scorum::tags::tag_stats_index)

FC_REFLECT(scorum::tags::peer_stats_object,
           (id)
           (voter)
           (peer)
           (direct_positive_votes)
           (direct_votes)
           (indirect_positive_votes)
           (indirect_votes)
           (rank))

CHAINBASE_SET_INDEX_TYPE(scorum::tags::peer_stats_object, scorum::tags::peer_stats_index)

FC_REFLECT(scorum::tags::comment_metadata, (tags))

FC_REFLECT(scorum::tags::author_tag_stats_object,
           (id)
           (author)
           (tag)
           (total_posts)
           (total_rewards))

CHAINBASE_SET_INDEX_TYPE(scorum::tags::author_tag_stats_object, scorum::tags::author_tag_stats_index)

FC_REFLECT(scorum::tags::category_stats_object,
           (id)
           (tag)
           (category)
           (tags_count))

CHAINBASE_SET_INDEX_TYPE(scorum::tags::category_stats_object, scorum::tags::category_stats_index)

// clang-format on
