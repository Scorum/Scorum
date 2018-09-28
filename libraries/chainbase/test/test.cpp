#define BOOST_TEST_MODULE chainbase test

#include <boost/test/unit_test.hpp>
#include <chainbase/chainbase.hpp>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/member.hpp>

#include <iostream>

#include <scorum/typeid/get_object_types.hpp>

using namespace boost::multi_index;

// BOOST_TEST_SUITE( serialization_tests, database_default_integration_fixture )

struct book : public chainbase::object<0, book>
{
    CHAINBASE_DEFAULT_CONSTRUCTOR(book)

    id_type id;
    int a = 0;
    int b = 1;
};

typedef fc::shared_multi_index_container<book,
                                         indexed_by<ordered_unique<member<book, book::id_type, &book::id>>,
                                                    ordered_non_unique<BOOST_MULTI_INDEX_MEMBER(book, int, a)>,
                                                    ordered_non_unique<BOOST_MULTI_INDEX_MEMBER(book, int, b)>>>
    book_index;

CHAINBASE_SET_INDEX_TYPE(book, book_index)

class moc_database : public chainbase::database
{
    typedef chainbase::database _Base;

public:
    ~moc_database()
    {
    }

    void undo()
    {
        for_each_index([&](chainbase::abstract_generic_index_i& item) { item.undo(); });
    }

    // TODO (if chainbase::database became private)
};

BOOST_AUTO_TEST_CASE(open_and_create)
{
    boost::filesystem::path temp = boost::filesystem::unique_path();
    try
    {
        std::cerr << temp.native() << " \n";

        moc_database db;
        BOOST_CHECK_THROW(db.open(temp), std::runtime_error); /// temp does not exist

        db.open(temp, scorum::to_underlying(moc_database::open_flags::read_write), 1024 * 1024 * 8);

        moc_database db2; /// open an already created db
        db2.open(temp);
        BOOST_CHECK_THROW(db2.add_index<book_index>(),
                          std::runtime_error); /// index does not exist in read only database

        db.add_index<book_index>();
        BOOST_CHECK_THROW(db.add_index<book_index>(), std::logic_error); /// cannot add same index twice

        db2.add_index<book_index>(); /// index should exist now

        BOOST_TEST_MESSAGE("Creating book");
        const auto& new_book = db.create<book>([](book& b) {
            b.a = 3;
            b.b = 4;
        });
        const auto& copy_new_book = db2.get(book::id_type(0));
        BOOST_REQUIRE(&new_book != &copy_new_book); ///< these are mapped to different address ranges

        BOOST_REQUIRE_EQUAL(new_book.a, copy_new_book.a);
        BOOST_REQUIRE_EQUAL(new_book.b, copy_new_book.b);

        db.modify(new_book, [&](book& b) {
            b.a = 5;
            b.b = 6;
        });
        BOOST_REQUIRE_EQUAL(new_book.a, 5);
        BOOST_REQUIRE_EQUAL(new_book.b, 6);

        BOOST_REQUIRE_EQUAL(new_book.a, copy_new_book.a);
        BOOST_REQUIRE_EQUAL(new_book.b, copy_new_book.b);

        {
            auto session = db.start_undo_session();
            db.modify(new_book, [&](book& b) {
                b.a = 7;
                b.b = 8;
            });

            BOOST_REQUIRE_EQUAL(new_book.a, 7);
            BOOST_REQUIRE_EQUAL(new_book.b, 8);
        }
        BOOST_REQUIRE_EQUAL(new_book.a, 5);
        BOOST_REQUIRE_EQUAL(new_book.b, 6);

        {
            auto session = db.start_undo_session();
            const auto& book2 = db.create<book>([&](book& b) {
                b.a = 9;
                b.b = 10;
            });

            BOOST_REQUIRE_EQUAL(new_book.a, 5);
            BOOST_REQUIRE_EQUAL(new_book.b, 6);
            BOOST_REQUIRE_EQUAL(book2.a, 9);
            BOOST_REQUIRE_EQUAL(book2.b, 10);
        }
        BOOST_CHECK_THROW(db2.get(book::id_type(1)), std::out_of_range);
        BOOST_REQUIRE_EQUAL(new_book.a, 5);
        BOOST_REQUIRE_EQUAL(new_book.b, 6);

        {
            auto session = db.start_undo_session();
            db.modify(new_book, [&](book& b) {
                b.a = 7;
                b.b = 8;
            });

            BOOST_REQUIRE_EQUAL(new_book.a, 7);
            BOOST_REQUIRE_EQUAL(new_book.b, 8);
            session->push();
        }
        BOOST_REQUIRE_EQUAL(new_book.a, 7);
        BOOST_REQUIRE_EQUAL(new_book.b, 8);
        db.undo();
        BOOST_REQUIRE_EQUAL(new_book.a, 5);
        BOOST_REQUIRE_EQUAL(new_book.b, 6);

        BOOST_REQUIRE_EQUAL(new_book.a, copy_new_book.a);
        BOOST_REQUIRE_EQUAL(new_book.b, copy_new_book.b);
    }
    catch (...)
    {
        boost::filesystem::remove_all(temp);
        throw;
    }
}

// BOOST_AUTO_TEST_SUITE_END()
