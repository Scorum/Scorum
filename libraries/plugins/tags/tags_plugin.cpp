#include <scorum/tags/tags_plugin.hpp>
#include <scorum/tags/tags_api.hpp>
#include <scorum/tags/tags_objects.hpp>

#include <scorum/protocol/config.hpp>

#include <scorum/chain/database/database.hpp>
#include <scorum/chain/hardfork.hpp>
#include <scorum/chain/operation_notification.hpp>
#include <scorum/chain/schema/account_objects.hpp>
#include <scorum/chain/schema/comment_objects.hpp>
#include <scorum/chain/services/account.hpp>
#include <scorum/chain/services/comment.hpp>

#include <fc/smart_ref_impl.hpp>
#include <fc/thread/thread.hpp>
#include <fc/io/json.hpp>
#include <fc/string.hpp>

#include <boost/range/iterator_range.hpp>
#include <boost/algorithm/string.hpp>

namespace scorum {
namespace tags {

namespace detail {

using namespace scorum::protocol;

class tags_plugin_impl
{
public:
    tags_plugin_impl(tags_plugin& _plugin)
        : _self(_plugin)
    {
    }
    virtual ~tags_plugin_impl();

    scorum::chain::database& database()
    {
        return _self.database();
    }

    void pre_operation(const operation_notification& note);
    void post_operation(const operation_notification& note);

    tags_plugin& _self;
};

tags_plugin_impl::~tags_plugin_impl()
{
    return;
}

class category_stats_service : public scorum::chain::dbs_base
{
    friend class chain::dbservice_dbs_factory;

public:
    explicit category_stats_service(database& db)
        : _base_type(db)
    {
    }

    void exclude_from_category_stats(const fc::shared_string& category, const comment_metadata& meta)
    {
        const auto& idx = db_impl().get_index<scorum::tags::category_stats_index>().indices().get<by_category>();
        auto it_pair = idx.equal_range(category);

        std::vector<std::reference_wrapper<const category_stats_object>> stats;
        std::copy(it_pair.first, it_pair.second, std::back_inserter(stats));

        for (const category_stats_object& s : stats)
        {
            auto found_it = std::find(meta.tags.begin(), meta.tags.end(), s.tag);
            if (found_it == meta.tags.end())
                continue;

            if (s.tags_count == 1)
                db_impl().remove(s);
            else if (s.tags_count > 1)
                db_impl().modify(s, [&](category_stats_object& s) { --s.tags_count; });
        }
    }

    void include_into_category_stats(const fc::shared_string& category, const comment_metadata& meta)
    {
        for (const std::string& tag : meta.tags)
        {
            const auto& idx
                = db_impl().get_index<scorum::tags::category_stats_index>().indices().get<by_tag_category>();

            auto found_it = idx.find(std::make_tuple(category, tag_name_type(tag)));
            if (found_it == idx.end())
            {
                db_impl().create<category_stats_object>([&](category_stats_object& s) {
                    s.category = category;
                    s.tag = tag;
                    s.tags_count = 1;
                });
            }
            else
            {
                db_impl().modify(*found_it, [&](category_stats_object& s) { ++s.tags_count; });
            }
        }
    }
};

struct category_stats_pre_operation_visitor
{
    typedef void result_type;

    database& _db;
    category_stats_service& _category_stats_service;

    category_stats_pre_operation_visitor(database& db)
        : _db(db)
        , _category_stats_service(_db.obtain_service<category_stats_service>())
    {
    }

    void operator()(const comment_operation& op) const
    {
        const comment_object* c
            = _db.obtain_service<dbs_comment>().find_by<by_permlink>(std::make_tuple(op.author, op.permlink));

        if (c != nullptr)
            _category_stats_service.exclude_from_category_stats(c->category, comment_metadata::parse(c->json_metadata));
    }

    void operator()(const delete_comment_operation& op) const
    {
        const comment_object& c = _db.obtain_service<dbs_comment>().get(op.author, op.permlink);

        _category_stats_service.exclude_from_category_stats(c.category, comment_metadata::parse(c.json_metadata));
    }

    template <typename Op> void operator()(Op&&) const
    {
    } /// ignore all other ops
};

struct category_stats_post_operation_visitor
{
    typedef void result_type;

    database& _db;
    category_stats_service& _category_stats_service;

    category_stats_post_operation_visitor(database& db)
        : _db(db)
        , _category_stats_service(_db.obtain_service<category_stats_service>())
    {
    }

    void operator()(const comment_operation& op) const
    {
        const comment_object& c = _db.obtain_service<dbs_comment>().get(op.author, op.permlink);

        _category_stats_service.include_into_category_stats(c.category, comment_metadata::parse(c.json_metadata));
    }

    template <typename Op> void operator()(Op&&) const
    {
    } /// ignore all other ops
};

struct post_operation_visitor
{
    typedef void result_type;

    database& _db;

    post_operation_visitor(database& db)
        : _db(db)
    {
    }

    void remove_stats(const tag_object& tag, const tag_stats_object& stats) const
    {
        _db.modify(stats, [&](tag_stats_object& s) {
            if (tag.parent == comment_id_type())
            {
                s.top_posts--;
            }
            else
            {
                s.comments--;
            }
            s.total_trending -= static_cast<uint32_t>(tag.trending);
            s.net_votes -= tag.net_votes;
        });
    }

    void add_stats(const tag_object& tag, const tag_stats_object& stats) const
    {
        _db.modify(stats, [&](tag_stats_object& s) {
            if (tag.parent == comment_id_type())
            {
                s.top_posts++;
            }
            else
            {
                s.comments++;
            }
            s.total_trending += static_cast<uint32_t>(tag.trending);
            s.net_votes += tag.net_votes;
        });
    }

    void remove_tag(const tag_object& tag) const
    {
        /// TODO: update tag stats object
        _db.remove(tag);

        const auto& idx = _db.get_index<author_tag_stats_index>().indices().get<by_author_tag_posts>();
        auto itr = idx.lower_bound(boost::make_tuple(tag.author, tag.tag));
        if (itr != idx.end() && itr->author == tag.author && itr->tag == tag.tag)
        {
            _db.modify(*itr, [&](author_tag_stats_object& stats) { stats.total_posts--; });
        }
    }

    const tag_stats_object& get_stats(const std::string& tag) const
    {
        const auto& stats_idx = _db.get_index<tag_stats_index>().indices().get<by_tag>();
        auto itr = stats_idx.find(tag);
        if (itr != stats_idx.end())
            return *itr;

        return _db.create<tag_stats_object>([&](tag_stats_object& stats) { stats.tag = tag; });
    }

    comment_metadata filter_tags(const comment_object& c) const
    {
        comment_metadata meta = comment_metadata::parse(c.json_metadata);

        // We need to write the transformed tags into a temporary
        // local container because we can't modify meta.tags concurrently
        // as we iterate over it.
        std::set<std::string> lower_tags;

        uint8_t tag_limit = 5;
        uint8_t count = 0;
        for (const std::string& tag : meta.tags)
        {
            ++count;
            if (count > tag_limit || lower_tags.size() > tag_limit)
                break;
            if (tag == "")
                continue;
            lower_tags.insert(fc::to_lower(tag));
        }

        /// the universal tag applies to everything safe for work or nsfw with a non-negative payout
        if (c.net_rshares >= 0)
        {
            lower_tags.insert(std::string()); /// add it to the universal tag
        }

        meta.tags = lower_tags; /// TODO: std::move???

        return meta;
    }

    void update_tag(const tag_object& current, const comment_object& comment, double hot, double trending) const
    {
        const auto& stats = get_stats(current.tag);
        remove_stats(current, stats);

        if (comment.cashout_time != fc::time_point_sec::maximum())
        {
            _db.modify(current, [&](tag_object& obj) {
                obj.active = comment.active;
                obj.cashout = _db.calculate_discussion_payout_time(comment);
                obj.children = comment.children;
                obj.net_rshares = comment.net_rshares.value;
                obj.net_votes = comment.net_votes;
                obj.hot = hot;
                obj.trending = trending;
                if (obj.cashout == fc::time_point_sec())
                    obj.promoted_balance = 0;
            });
            add_stats(current, stats);
        }
        else
        {
            _db.remove(current);
        }
    }

    void create_tag(const std::string& tag, const comment_object& comment, double hot, double trending) const
    {
        comment_id_type parent;
        account_id_type author = _db.obtain_service<dbs_account>().get_account(comment.author).id;

        if (comment.parent_author.size())
            parent = _db.obtain_service<dbs_comment>()
                         .get(comment.parent_author, fc::to_string(comment.parent_permlink))
                         .id;

        const auto& tag_obj = _db.create<tag_object>([&](tag_object& obj) {
            obj.tag = tag;
            obj.comment = comment.id;
            obj.parent = parent;
            obj.created = comment.created;
            obj.active = comment.active;
            obj.cashout = comment.cashout_time;
            obj.net_votes = comment.net_votes;
            obj.children = comment.children;
            obj.net_rshares = comment.net_rshares.value;
            obj.author = author;
            obj.hot = hot;
            obj.trending = trending;
        });
        add_stats(tag_obj, get_stats(tag));

        const auto& idx = _db.get_index<author_tag_stats_index>().indices().get<by_author_tag_posts>();
        auto itr = idx.lower_bound(boost::make_tuple(author, tag));
        if (itr != idx.end() && itr->author == author && itr->tag == tag)
        {
            _db.modify(*itr, [&](author_tag_stats_object& stats) { stats.total_posts++; });
        }
        else
        {
            _db.create<author_tag_stats_object>([&](author_tag_stats_object& stats) {
                stats.author = author;
                stats.tag = tag;
                stats.total_posts = 1;
            });
        }
    }

    /**
     * https://medium.com/hacking-and-gonzo/how-reddit-ranking-algorithms-work-ef111e33d0d9#.lcbj6auuw
     */
    template <int64_t S, int32_t T> double calculate_score(const share_type& score, const time_point_sec& created) const
    {
        /// new algorithm
        auto mod_score = score.value / S;

        /// reddit algorithm
        double order = log10(std::max<int64_t>(std::abs(mod_score), 1));
        int sign = 0;
        if (mod_score > 0)
            sign = 1;
        else if (mod_score < 0)
            sign = -1;

        return sign * order + double(created.sec_since_epoch()) / double(T);
    }

    inline double calculate_hot(const share_type& score, const time_point_sec& created) const
    {
        return calculate_score<10000000, 10000>(score, created);
    }

    inline double calculate_trending(const share_type& score, const time_point_sec& created) const
    {
        return calculate_score<10000000, 480000>(score, created);
    }

    /** finds tags that have been added or removed or updated */
    void update_tags(const comment_object& c, bool parse_tags = false) const
    {
        try
        {

            auto hot = calculate_hot(c.net_rshares, c.created);
            auto trending = calculate_trending(c.net_rshares, c.created);

            const auto& comment_idx = _db.get_index<scorum::tags::tag_index>().indices().get<by_comment>();

            if (parse_tags)
            {
                auto meta = filter_tags(c);
                auto citr = comment_idx.lower_bound(c.id);

                std::map<std::string, const tag_object*> existing_tags;
                std::vector<const tag_object*> remove_queue;

                while (citr != comment_idx.end() && citr->comment == c.id)
                {
                    const tag_object* tag = &*citr;
                    ++citr;

                    if (meta.tags.find(tag->tag) == meta.tags.end())
                    {
                        remove_queue.push_back(tag);
                    }
                    else
                    {
                        existing_tags[tag->tag] = tag;
                    }
                }

                for (const auto& tag : meta.tags)
                {
                    auto existing = existing_tags.find(tag);

                    if (existing == existing_tags.end())
                    {
                        create_tag(tag, c, hot, trending);
                    }
                    else
                    {
                        update_tag(*existing->second, c, hot, trending);
                    }
                }

                for (const auto& item : remove_queue)
                    remove_tag(*item);
            }
            else
            {
                auto citr = comment_idx.lower_bound(c.id);

                while (citr != comment_idx.end() && citr->comment == c.id)
                {
                    update_tag(*citr, c, hot, trending);
                    ++citr;
                }
            }

            if (c.parent_author.size())
            {
                update_tags(_db.obtain_service<dbs_comment>().get(c.parent_author, fc::to_string(c.parent_permlink)));
            }
        }
        FC_CAPTURE_LOG_AND_RETHROW((c))
    }

    const peer_stats_object& get_or_create_peer_stats(account_id_type voter, account_id_type peer) const
    {
        const auto& peeridx = _db.get_index<peer_stats_index>().indices().get<by_voter_peer>();
        auto itr = peeridx.find(boost::make_tuple(voter, peer));
        if (itr == peeridx.end())
        {
            return _db.create<peer_stats_object>([&](peer_stats_object& obj) {
                obj.voter = voter;
                obj.peer = peer;
            });
        }
        return *itr;
    }

    void update_indirect_vote(account_id_type a, account_id_type b, int positive) const
    {
        if (a == b)
            return;
        const auto& ab = get_or_create_peer_stats(a, b);
        const auto& ba = get_or_create_peer_stats(b, a);
        _db.modify(ab, [&](peer_stats_object& o) {
            o.indirect_positive_votes += positive;
            o.indirect_votes++;
            o.update_rank();
        });
        _db.modify(ba, [&](peer_stats_object& o) {
            o.indirect_positive_votes += positive;
            o.indirect_votes++;
            o.update_rank();
        });
    }

    void update_peer_stats(const account_object& voter,
                           const account_object& author,
                           const comment_object& c,
                           int vote) const
    {
        if (voter.id == author.id)
            return; /// ignore votes for yourself
        if (c.parent_author.size())
            return; /// only count top level posts

        const auto& stat = get_or_create_peer_stats(voter.id, author.id);
        _db.modify(stat, [&](peer_stats_object& obj) {
            obj.direct_votes++;
            obj.direct_positive_votes += vote > 0;
            obj.update_rank();
        });

        const auto& voteidx = _db.get_index<comment_vote_index>().indices().get<by_comment_voter>();
        auto itr = voteidx.lower_bound(boost::make_tuple(comment_id_type(c.id), account_id_type()));
        while (itr != voteidx.end() && itr->comment == c.id)
        {
            update_indirect_vote(voter.id, itr->voter, (itr->vote_percent > 0) == (vote > 0));
            ++itr;
        }
    }

    void operator()(const comment_operation& op) const
    {
        const comment_object& comment = _db.obtain_service<dbs_comment>().get(op.author, op.permlink);

        update_tags(comment, true);
    }

    void operator()(const transfer_operation& op) const
    {
        // SCORUM: changed from null account
        // if( op.to == SCORUM_NULL_ACCOUNT && op.amount.symbol() == SCORUM_SYMBOL )
        if (op.to == "" && op.amount.symbol() == SCORUM_SYMBOL)
        {
            std::vector<std::string> part;
            part.reserve(4);
            auto path = op.memo;
            boost::split(part, path, boost::is_any_of("/"));
            if (part.size() > 1 && part[0].size() && part[0][0] == '@')
            {
                auto acnt = part[0].substr(1);
                auto perm = part[1];

                auto& comment_service = _db.obtain_service<dbs_comment>();
                if (comment_service.is_exists(acnt, perm))
                {
                    const auto& c = comment_service.get(acnt, perm);
                    if (c.parent_author.size() == 0)
                    {
                        const auto& comment_idx = _db.get_index<scorum::tags::tag_index>().indices().get<by_comment>();
                        auto citr = comment_idx.lower_bound(c.id);
                        while (citr != comment_idx.end() && citr->comment == c.id)
                        {
                            _db.modify(*citr, [&](tag_object& t) {
                                if (t.cashout != fc::time_point_sec::maximum())
                                    t.promoted_balance += op.amount.amount;
                            });
                            ++citr;
                        }

                        return;
                    }
                }

                ilog("unable to find body");
            }
        }
    }

    void operator()(const vote_operation& op) const
    {
        update_tags(_db.obtain_service<dbs_comment>().get(op.author, op.permlink));
        /*
        update_peer_stats( _db.obtain_service<dbs_account>().get_account(op.voter),
                           _db.obtain_service<dbs_account>().get_account(op.author),
                           _db.obtain_service<dbs_comment>().get(op.author, op.permlink),
                           op.weight );
                           */
    }

    void operator()(const delete_comment_operation& op) const
    {
        const auto& idx = _db.get_index<scorum::tags::tag_index>().indices().get<by_author_comment>();

        const auto& auth = _db.obtain_service<dbs_account>().get_account(op.author);
        auto itr = idx.lower_bound(boost::make_tuple(auth.id));
        while (itr != idx.end() && itr->author == auth.id)
        {
            const auto& tobj = *itr;
            const auto* obj = _db.find<comment_object>(itr->comment);
            ++itr;
            if (!obj)
            {
                _db.remove(tobj);
            }
        }
    }

    void operator()(const comment_reward_operation& op) const
    {
        const auto& c = _db.obtain_service<dbs_comment>().get(op.author, op.permlink);
        update_tags(c);

        comment_metadata meta = filter_tags(c);

        for (const std::string& tag : meta.tags)
        {
            _db.modify(get_stats(tag), [&](tag_stats_object& ts) {

                if (op.payout.symbol() == SCORUM_SYMBOL)
                {
                    ts.total_payout_scr += op.payout;
                }
                else
                {
                    ts.total_payout_sp += op.payout;
                }
            });
        }
    }

    void operator()(const comment_payout_update_operation& op) const
    {
        const auto& c = _db.obtain_service<dbs_comment>().get(op.author, op.permlink);
        update_tags(c);
    }

    template <typename Op> void operator()(Op&&) const
    {
    } /// ignore all other ops
};

void tags_plugin_impl::pre_operation(const operation_notification& note)
{
    try
    {
        /// plugins shouldn't ever throw
        note.op.visit(category_stats_pre_operation_visitor(database()));
    }
    catch (const fc::exception& e)
    {
        edump((e.to_detail_string()));
    }
    catch (...)
    {
        elog("unhandled exception");
    }
}

void tags_plugin_impl::post_operation(const operation_notification& note)
{
    try
    {
        /// plugins shouldn't ever throw
        note.op.visit(post_operation_visitor(database()));
        note.op.visit(category_stats_post_operation_visitor(database()));
    }
    catch (const fc::exception& e)
    {
        edump((e.to_detail_string()));
    }
    catch (...)
    {
        elog("unhandled exception");
    }
}

} // namespace detail

tags_plugin::tags_plugin(scorum::app::application* app)
    : plugin(app)
    , my(new detail::tags_plugin_impl(*this))
{
}

tags_plugin::~tags_plugin()
{
}

std::string tags_plugin::plugin_name() const
{
    return TAGS_PLUGIN_NAME;
}

void tags_plugin::plugin_set_program_options(boost::program_options::options_description& cli,
                                             boost::program_options::options_description& cfg)
{
}

void tags_plugin::plugin_initialize(const boost::program_options::variables_map& options)
{
    try
    {
        chain::database& db = database();

        db.pre_apply_operation.connect([&](const operation_notification& note) { my->pre_operation(note); });
        db.post_apply_operation.connect([&](const operation_notification& note) { my->post_operation(note); });

        db.add_plugin_index<tags::tag_index>();
        db.add_plugin_index<tag_stats_index>();
        db.add_plugin_index<peer_stats_index>();
        db.add_plugin_index<author_tag_stats_index>();
        db.add_plugin_index<category_stats_index>();
    }
    FC_LOG_AND_RETHROW()

    print_greeting();
}

void tags_plugin::plugin_startup()
{
    app().register_api_factory<tags_api>("tags_api");
}

} // namespace tags
} // namespace scorum

SCORUM_DEFINE_PLUGIN(tags, scorum::tags::tags_plugin)
