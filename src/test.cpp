/**
 * @file test.cpp
 * @author Ashot Vardanian
 * @date 2022-07-06
 *
 * @brief A set of tests implemented using Google Test.
 */

#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "ukv/ukv.hpp"

using namespace unum::ukv;
using namespace unum;

#define macro_concat_(prefix, suffix) prefix##suffix
#define macro_concat(prefix, suffix) macro_concat_(prefix, suffix)
#define _ [[maybe_unused]] auto macro_concat(_, __LINE__)
#define M_EXPECT_EQ_JSON(str1, str2) EXPECT_EQ(json_t::parse((str1)), json_t::parse((str2)));
#define M_EXPECT_EQ_MSG(str1, str2) \
    EXPECT_EQ(json_t::from_msgpack((str1).c_str(), (str1).c_str() + (str1).size()), json_t::parse((str2)));

TEST(db, intro) {

    db_t db;
    EXPECT_TRUE(db.open());

    // Try getting the main collection
    EXPECT_TRUE(db.collection());
    col_t main = *db.collection();

    // Single-element access
    main[42] = "purpose of life";
    main.at(42) = "purpose of life";
    EXPECT_EQ(*main[42].value(), "purpose of life");
    _ = main[42].clear();

    // Mapping multiple keys to same values
    main[{43, 44}] = "same value";

    // Operations on smart-references
    _ = main[{43, 44}].clear();
    _ = main[{43, 44}].erase();
    _ = main[{43, 44}].present();
    _ = main[{43, 44}].length();
    _ = main[{43, 44}].value();
    _ = main[std::array<ukv_key_t, 3> {65, 66, 67}];
    _ = main[std::vector<ukv_key_t> {65, 66, 67, 68}];
    // for (value_view_t value : *main[{100, 101}].value())
    //     (void)value;

    // Accessing named collections
    col_t prefixes = *db.collection("prefixes");
    prefixes.at(42) = "purpose";
    db["articles"]->at(42) = "of";
    db["suffixes"]->at(42) = "life";

    // Reusable memory
    // This interface not just more performant, but also provides nicer interface:
    //  expected_gt<tape_view_t> tapes = main[{100, 101}].on(arena);
    arena_t arena(db);
    _ = main[{43, 44}].on(arena).clear();
    _ = main[{43, 44}].on(arena).erase();
    _ = main[{43, 44}].on(arena).present();
    _ = main[{43, 44}].on(arena).length();
    _ = main[{43, 44}].on(arena).value();

    // Batch-assignment: many keys to many values
    // main[std::array<ukv_key_t, 3> {65, 66, 67}] = std::array {"A", "B", "C"};
    // main[std::array {ckf(prefixes, 65), ckf(66), ckf(67)}] = std::array {"A", "B", "C"};

    // Iterating over collections
    for (ukv_key_t key : main.keys())
        (void)key;
    for (ukv_key_t key : main.keys(100, 200))
        (void)key;

    _ = main.members(100, 200).size_estimates()->cardinality;

    // Supporting options
    _ = main[{43, 44}].on(arena).clear(/*flush:*/ false);
    _ = main[{43, 44}].on(arena).erase(/*flush:*/ false);
    _ = main[{43, 44}].on(arena).present(/*track:*/ false);
    _ = main[{43, 44}].on(arena).length(/*track:*/ false);
    _ = main[{43, 44}].on(arena).value(/*track:*/ false);

    // Working with sub documents
    main[56] = R"( {"hello": "world", "answer": 42} )"_json.dump().c_str();
    _ = main[ckf(56, "hello")].value() == "world";
}

template <typename locations_at>
void check_length(members_ref_gt<locations_at>& ref, ukv_val_len_t expected_length) {

    EXPECT_TRUE(ref.value()) << "Failed to fetch missing keys";

    auto const expects_missing = expected_length == ukv_val_len_missing_k;
    using extractor_t = keys_arg_extractor_gt<locations_at>;

    // Validate that values match
    expected_gt<tape_view_t> retrieved_and_arena = ref.value();
    tape_view_t const& retrieved = *retrieved_and_arena;
    ukv_size_t count = extractor_t {}.count(ref.locations());
    EXPECT_EQ(retrieved.size(), count);

    // Check views
    tape_iterator_t it = retrieved.begin();
    for (std::size_t i = 0; i != count; ++i, ++it) {
        EXPECT_EQ((*it).size(), expects_missing ? 0 : expected_length);
    }

    // Check length estimates
    auto maybe_lengths_and_arena = ref.length();
    EXPECT_TRUE(maybe_lengths_and_arena);
    for (std::size_t i = 0; i != count; ++i) {
        EXPECT_EQ(maybe_lengths_and_arena->at(i), expected_length);
    }

    // Check boolean indicators
    auto maybe_indicators_and_arena = ref.present();
    EXPECT_TRUE(maybe_indicators_and_arena);
    for (std::size_t i = 0; i != count; ++i) {
        EXPECT_EQ(maybe_indicators_and_arena->at(i), !expects_missing);
    }
}

template <typename locations_at>
void check_equalities(members_ref_gt<locations_at>& ref, values_arg_t values) {

    EXPECT_TRUE(ref.value()) << "Failed to fetch present keys";
    using extractor_t = keys_arg_extractor_gt<locations_at>;

    // Validate that values match
    expected_gt<tape_view_t> retrieved_and_arena = ref.value();
    tape_view_t const& retrieved = *retrieved_and_arena;
    EXPECT_EQ(retrieved.size(), extractor_t {}.count(ref.locations()));

    tape_iterator_t it = retrieved.begin();
    for (std::size_t i = 0; i != extractor_t {}.count(ref.locations()); ++i, ++it) {
        auto expected_len = static_cast<std::size_t>(values.lengths_begin[i]);
        auto expected_begin = reinterpret_cast<byte_t const*>(values.contents_begin[i]) + values.offsets_begin[i];

        value_view_t val_view = *it;
        value_view_t expected_view(expected_begin, expected_begin + expected_len);
        EXPECT_EQ(val_view.size(), expected_len);
        EXPECT_EQ(val_view, expected_view);
    }
}

template <typename locations_at>
void round_trip(members_ref_gt<locations_at>& ref, values_arg_t values) {
    EXPECT_TRUE(ref.assign(values)) << "Failed to assign";
    check_equalities(ref, values);
}

TEST(db, basic) {

    db_t db;
    EXPECT_TRUE(db.open(""));

    // Try getting the main collection
    EXPECT_TRUE(db.collection());
    col_t col = *db.collection();

    std::vector<ukv_key_t> keys {34, 35, 36};
    ukv_val_len_t val_len = sizeof(std::uint64_t);
    std::vector<std::uint64_t> vals {34, 35, 36};
    std::vector<ukv_val_len_t> offs {0, val_len, val_len * 2};
    auto vals_begin = reinterpret_cast<ukv_val_ptr_t>(vals.data());

    auto ref = col[keys];
    values_arg_t values {
        .contents_begin = {&vals_begin, 0},
        .offsets_begin = {offs.data(), sizeof(ukv_val_len_t)},
        .lengths_begin = {&val_len, 0},
    };
    round_trip(ref, values);

    // Overwrite those values with same size integers and try again
    for (auto& val : vals)
        val += 100;
    round_trip(ref, values);

    // Overwrite with empty values, but check for existence
    EXPECT_TRUE(ref.clear());
    check_length(ref, 0);

    // Check scans
    keys_range_t present_keys = col.keys();
    keys_stream_t present_it = present_keys.begin();
    auto expected_it = keys.begin();
    for (; expected_it != keys.end(); ++present_it, ++expected_it) {
        EXPECT_EQ(*expected_it, *present_it);
    }
    EXPECT_TRUE(present_it.is_end());

    // Remove all of the values and check that they are missing
    EXPECT_TRUE(ref.erase());
    check_length(ref, ukv_val_len_missing_k);
    status_t status = db.clear();
}

TEST(db, named) {
    db_t db;
    EXPECT_TRUE(db.open(""));

    col_t col1 = *(db["col1"]);
    col_t col2 = *(db["col2"]);

    ukv_val_len_t val_len = sizeof(std::uint64_t);
    std::vector<ukv_key_t> keys {44, 45, 46};
    std::vector<std::uint64_t> vals {44, 45, 46};
    std::vector<ukv_val_len_t> offs {0, val_len, val_len * 2};
    auto vals_begin = reinterpret_cast<ukv_val_ptr_t>(vals.data());

    values_arg_t values {
        .contents_begin = {&vals_begin, 0},
        .offsets_begin = {offs.data(), sizeof(ukv_val_len_t)},
        .lengths_begin = {&val_len, 0},
    };

    auto ref1 = col1[keys];
    auto ref2 = col2[keys];
    EXPECT_TRUE(*db.contains("col1"));
    EXPECT_TRUE(*db.contains("col2"));
    EXPECT_FALSE(*db.contains("unknown_col"));
    round_trip(ref1, values);
    round_trip(ref2, values);

    // Check scans
    keys_range_t present_keys1 = col1.keys();
    keys_range_t present_keys2 = col2.keys();
    keys_stream_t present_it1 = present_keys1.begin();
    keys_stream_t present_it2 = present_keys2.begin();
    auto expected_it1 = keys.begin();
    auto expected_it2 = keys.begin();
    for (; expected_it1 != keys.end() && expected_it2 != keys.end();
         ++present_it1, ++expected_it1, ++present_it2, ++expected_it2) {
        EXPECT_EQ(*expected_it1, *present_it1);
        EXPECT_EQ(*expected_it2, *present_it2);
    }
    EXPECT_TRUE(present_it1.is_end());
    EXPECT_TRUE(present_it2.is_end());

    pairs_range_t present_item = col1.items();
    pairs_stream_t present_item_it = present_item.begin();
    auto expected_key_it = keys.begin();
    for (size_t i = 0; expected_key_it != keys.end(); ++i, ++present_item_it, ++expected_key_it) {
        EXPECT_EQ(*expected_key_it, present_item_it.key());

        auto expected_len = static_cast<std::size_t>(values.lengths_begin[i]);
        auto expected_begin = reinterpret_cast<byte_t const*>(values.contents_begin[i]) + values.offsets_begin[i];

        value_view_t val_view = present_item_it.value();
        value_view_t expected_view(expected_begin, expected_begin + expected_len);
        EXPECT_EQ(val_view.size(), expected_len);
        EXPECT_EQ(val_view, expected_view);
    }
    EXPECT_TRUE(present_item_it.is_end());

    _ = db.remove("col1");
    _ = db.remove("col2");
    EXPECT_FALSE(*db.contains("col1"));
    EXPECT_FALSE(*db.contains("col2"));
    status_t status = db.clear();
}

TEST(db, docs) {
    using json_t = nlohmann::json;
    db_t db;
    EXPECT_TRUE(db.open(""));

    // JSON
    col_t col = *db.collection("docs", ukv_format_json_k);
    auto json = R"( {"person": "Davit", "age": 24} )"_json.dump();
    col[1] = json.c_str();
    M_EXPECT_EQ_JSON(col[1].value()->c_str(), json.c_str());
    M_EXPECT_EQ_JSON(col[ckf(1, "person")].value()->c_str(), "\"Davit\"");
    M_EXPECT_EQ_JSON(col[ckf(1, "age")].value()->c_str(), "24");

    // MsgPack
    col.as(ukv_format_msgpack_k);
    M_EXPECT_EQ_MSG(*col[1].value(), json.c_str());
    M_EXPECT_EQ_MSG(*col[ckf(1, "person")].value(), "\"Davit\"");
    M_EXPECT_EQ_MSG(*col[ckf(1, "age")].value(), "24");

    // Binary
    col.as(ukv_format_binary_k);
    auto maybe_person = col[ckf(1, "person")].value();
    EXPECT_EQ(std::string_view(maybe_person->c_str(), maybe_person->size()), std::string_view("Davit"));

    // JSON-Patching
    col.as(ukv_format_json_patch_k);
    auto json_patch =
        R"( [
            { "op": "replace", "path": "/person", "value": "Ashot" },
            { "op": "add", "path": "/hello", "value": ["world"] },
            { "op": "remove", "path": "/age" }
            ] )"_json.dump();
    auto expected_json = R"( {"person": "Ashot", "hello": ["world"]} )"_json.dump();
    col[1] = json_patch.c_str();
    auto patch_result = col[1].value();
    M_EXPECT_EQ_JSON(patch_result->c_str(), expected_json.c_str());
    M_EXPECT_EQ_JSON(col[ckf(1, "person")].value()->c_str(), "\"Ashot\"");
    M_EXPECT_EQ_JSON(col[ckf(1, "/hello/0")].value()->c_str(), "\"world\"");

    // JSON-Patch Merging
    col.as(ukv_format_json_merge_patch_k);
    auto json_to_merge = R"( {"person": "Darvin", "age": 28} )"_json.dump();
    expected_json = R"( {"person": "Darvin", "hello": ["world"], "age": 28} )"_json.dump();
    col[1] = json_to_merge.c_str();
    auto merge_result = col[1].value();
    M_EXPECT_EQ_JSON(merge_result->c_str(), expected_json.c_str());
    M_EXPECT_EQ_JSON(col[ckf(1, "person")].value()->c_str(), "\"Darvin\"");
    M_EXPECT_EQ_JSON(col[ckf(1, "/hello/0")].value()->c_str(), "\"world\"");
    M_EXPECT_EQ_JSON(col[ckf(1, "age")].value()->c_str(), "28");
    status_t status = db.clear();
}

TEST(db, docs_table) {
    using json_t = nlohmann::json;
    db_t db;
    EXPECT_TRUE(db.open(""));

    // Inject basic data
    col_t col = *db.collection("", ukv_format_json_k);
    auto json_ashot = R"( { "person": "Ashot", "age": 27, "height": 1 } )"_json.dump();
    auto json_darvin = R"( { "person": "Darvin", "age": "27", "weight": 2 } )"_json.dump();
    auto json_davit = R"( { "person": "Davit", "age": 24 } )"_json.dump();
    col[1] = json_ashot.c_str();
    col[2] = json_darvin.c_str();
    col[3] = json_davit.c_str();
    M_EXPECT_EQ_JSON(*col[1].value(), json_ashot.c_str());
    M_EXPECT_EQ_JSON(*col[2].value(), json_darvin.c_str());

    // Single cell
    {
        docs_layout_t layout {1, 1};
        layout.index(0).key = 1;
        layout.header(0) = field_type_t {"age", ukv_type_u32_k};

        auto maybe_table = col.as_table().gather(layout);
        auto table = *maybe_table;
        auto col0 = table.column(0).as<std::uint32_t>();

        EXPECT_EQ(col0[0].value, 27);
        EXPECT_FALSE(col0[0].converted);
    }

    // Single row
    {
        docs_layout_t layout {1, 3};
        layout.index(0).key = 1;
        layout.header(0) = field_type_t {"age", ukv_type_u32_k};
        layout.header(1) = field_type_t {"age", ukv_type_i32_k};
        layout.header(2) = field_type_t {"age", ukv_type_str_k};

        auto maybe_table = col.as_table().gather(layout);
        auto table = *maybe_table;
        auto col0 = table.column(0).as<std::uint32_t>();
        auto col1 = table.column(1).as<std::int32_t>();
        auto col2 = table.column(2).as<value_view_t>();

        EXPECT_EQ(col0[0].value, 27);
        EXPECT_FALSE(col0[0].converted);
        EXPECT_EQ(col1[0].value, 27);
        EXPECT_TRUE(col1[0].converted);
        EXPECT_STREQ(col2[0].value.c_str(), "27");
        EXPECT_TRUE(col2[0].converted);
    }

    // Single column
    {
        docs_layout_t layout {4, 1};
        layout.index(0).key = 1;
        layout.index(1).key = 2;
        layout.index(2).key = 3;
        layout.index(3).key = 123456;
        layout.header(0) = field_type_t {"age", ukv_type_i32_k};

        auto maybe_table = col.as_table().gather(layout);
        auto table = *maybe_table;
        auto col0 = table.column(0).as<std::int32_t>();

        EXPECT_EQ(col0[0].value, 27);
        EXPECT_EQ(col0[1].value, 27);
        EXPECT_TRUE(col0[1].converted);
        EXPECT_EQ(col0[2].value, 24);
    }

    // Multi-column
    {
        docs_layout_t layout {5, 6};
        layout.index(0).key = 1;
        layout.index(1).key = 2;
        layout.index(2).key = 3;
        layout.index(3).key = 123456;
        layout.index(4).key = 654321;
        layout.header(0) = field_type_t {"age", ukv_type_i32_k};
        layout.header(1) = field_type_t {"age", ukv_type_str_k};
        layout.header(2) = field_type_t {"person", ukv_type_str_k};
        layout.header(3) = field_type_t {"person", ukv_type_f32_k};
        layout.header(4) = field_type_t {"height", ukv_type_i32_k};
        layout.header(5) = field_type_t {"weight", ukv_type_u64_k};

        auto maybe_table = col.as_table().gather(layout);
        auto table = *maybe_table;
        auto col0 = table.column(0).as<std::int32_t>();
        auto col1 = table.column(1).as<value_view_t>();
        auto col2 = table.column(2).as<value_view_t>();
        auto col3 = table.column(3).as<float>();
        auto col4 = table.column(4).as<std::int32_t>();
        auto col5 = table.column(5).as<std::uint64_t>();

        EXPECT_EQ(col0[0].value, 27);
        EXPECT_EQ(col0[1].value, 27);
        EXPECT_TRUE(col0[1].converted);
        EXPECT_EQ(col0[2].value, 24);

        EXPECT_STREQ(col1[0].value.c_str(), "27");
        EXPECT_TRUE(col1[0].converted);
        EXPECT_STREQ(col1[1].value.c_str(), "27");
        EXPECT_STREQ(col1[2].value.c_str(), "24");
    }
    status_t status = db.clear();
}

TEST(db, txn) {
    db_t db;
    EXPECT_TRUE(db.open(""));
    EXPECT_TRUE(db.transact());
    txn_t txn = *db.transact();

    std::vector<ukv_key_t> keys {54, 55, 56};
    ukv_val_len_t val_len = sizeof(std::uint64_t);
    std::vector<std::uint64_t> vals {54, 55, 56};
    std::vector<ukv_val_len_t> offs {0, val_len, val_len * 2};
    auto vals_begin = reinterpret_cast<ukv_val_ptr_t>(vals.data());

    values_arg_t values {
        .contents_begin = {&vals_begin, 0},
        .offsets_begin = {offs.data(), sizeof(ukv_val_len_t)},
        .lengths_begin = {&val_len, 0},
    };

    auto txn_ref = txn[keys];
    round_trip(txn_ref, values);

    EXPECT_TRUE(db.collection());
    col_t col = *db.collection();
    auto col_ref = col[keys];

    // Check for missing values before commit
    check_length(col_ref, ukv_val_len_missing_k);

    auto status = txn.commit();
    status.throw_unhandled();
    status = txn.reset();
    status.throw_unhandled();

    // Validate that values match after commit
    check_equalities(col_ref, values);

    // Transaction with named collection
    EXPECT_TRUE(db.collection("named_col"));
    col_t named_col = *db.collection("named_col");
    std::vector<col_key_t> sub_keys {{named_col, 54}, {named_col, 55}, {named_col, 56}};
    auto txn_named_col_ref = txn[sub_keys];
    round_trip(txn_named_col_ref, values);

    // Check for missing values before commit
    auto named_col_ref = named_col[keys];
    check_length(named_col_ref, ukv_val_len_missing_k);

    status = txn.commit();
    status.throw_unhandled();
    status = txn.reset();
    status.throw_unhandled();

    // Validate that values match after commit
    check_equalities(named_col_ref, values);
    status = db.clear();
}

TEST(db, nested_docs) {
    db_t db;
    _ = db.open();
    col_t col = *db.collection();
}

TEST(db, net) {

    db_t db;
    EXPECT_TRUE(db.open(""));

    col_t main = *db.collection();
    graph_ref_t net = main.as_graph();

    // triangle
    edge_t edge1 {1, 2, 9};
    edge_t edge2 {2, 3, 10};
    edge_t edge3 {3, 1, 11};

    EXPECT_TRUE(net.upsert(edge1));
    EXPECT_TRUE(net.upsert(edge2));
    EXPECT_TRUE(net.upsert(edge3));

    EXPECT_TRUE(*net.contains(1));
    EXPECT_TRUE(*net.contains(2));
    EXPECT_FALSE(*net.contains(9));
    EXPECT_FALSE(*net.contains(10));
    EXPECT_FALSE(*net.contains(1000));

    EXPECT_EQ(*net.degree(1), 2u);
    EXPECT_EQ(*net.degree(2), 2u);
    EXPECT_EQ(*net.degree(3), 2u);
    EXPECT_EQ(*net.degree(1, ukv_vertex_source_k), 1u);
    EXPECT_EQ(*net.degree(2, ukv_vertex_source_k), 1u);
    EXPECT_EQ(*net.degree(3, ukv_vertex_source_k), 1u);

    EXPECT_TRUE(net.edges(1));
    EXPECT_EQ(net.edges(1)->size(), 2ul);
    EXPECT_EQ(net.edges(1, ukv_vertex_source_k)->size(), 1ul);
    EXPECT_EQ(net.edges(1, ukv_vertex_target_k)->size(), 1ul);

    EXPECT_EQ(net.edges(3, ukv_vertex_target_k)->size(), 1ul);
    EXPECT_EQ(net.edges(2, ukv_vertex_source_k)->size(), 1ul);
    EXPECT_EQ((*net.edges(3, ukv_vertex_target_k))[0].source_id, 2);
    EXPECT_EQ((*net.edges(3, ukv_vertex_target_k))[0].target_id, 3);
    EXPECT_EQ((*net.edges(3, ukv_vertex_target_k))[0].id, 10);
    EXPECT_EQ(net.edges(3, 1)->size(), 1ul);
    EXPECT_EQ(net.edges(1, 3)->size(), 0ul);

    // Check scans
    EXPECT_TRUE(net.edges());
    {
        std::unordered_set<edge_t, edge_hash_t> expected_edges {edge1, edge2, edge3};
        std::unordered_set<edge_t, edge_hash_t> exported_edges;

        auto present_edges = *net.edges();
        auto present_it = std::move(present_edges).begin();
        auto count_results = 0;
        while (!present_it.is_end()) {
            exported_edges.insert(*present_it);
            ++present_it;
            ++count_results;
        }
        EXPECT_EQ(count_results, 6);
        EXPECT_EQ(exported_edges, expected_edges);
    }

    // Remove a single edge, making sure that the nodes info persists
    EXPECT_TRUE(net.remove({
        {&edge1.source_id},
        {&edge1.target_id},
        {&edge1.id},
    }));
    EXPECT_TRUE(*net.contains(1));
    EXPECT_TRUE(*net.contains(2));
    EXPECT_EQ(net.edges(1, 2)->size(), 0ul);

    // Bring that edge back
    EXPECT_TRUE(net.upsert({
        {&edge1.source_id},
        {&edge1.target_id},
        {&edge1.id},
    }));
    EXPECT_EQ(net.edges(1, 2)->size(), 1ul);

    // Remove a vertex
    ukv_key_t vertex_to_remove = 2;
    EXPECT_TRUE(net.remove(vertex_to_remove));
    EXPECT_FALSE(*net.contains(vertex_to_remove));
    EXPECT_EQ(net.edges(vertex_to_remove)->size(), 0ul);
    EXPECT_EQ(net.edges(1, vertex_to_remove)->size(), 0ul);
    EXPECT_EQ(net.edges(vertex_to_remove, 1)->size(), 0ul);

    // Bring back the whole graph
    EXPECT_TRUE(net.upsert(edge1));
    EXPECT_TRUE(net.upsert(edge2));
    EXPECT_TRUE(net.upsert(edge3));
    EXPECT_TRUE(*net.contains(vertex_to_remove));
    EXPECT_EQ(net.edges(vertex_to_remove)->size(), 2ul);
    EXPECT_EQ(net.edges(1, vertex_to_remove)->size(), 1ul);
    EXPECT_EQ(net.edges(vertex_to_remove, 1)->size(), 0ul);
    status_t status = db.clear();
}

TEST(db, net_batch) {

    db_t db;
    EXPECT_TRUE(db.open(""));

    col_t main = *db.collection();
    graph_ref_t net = main.as_graph();

    std::vector<edge_t> triangle {
        {1, 2, 9},
        {2, 3, 10},
        {3, 1, 11},
    };

    EXPECT_TRUE(net.upsert(edges(triangle)));
    EXPECT_TRUE(*net.contains(1));
    EXPECT_TRUE(*net.contains(2));
    EXPECT_FALSE(*net.contains(9));
    EXPECT_FALSE(*net.contains(10));
    EXPECT_FALSE(*net.contains(1000));

    EXPECT_EQ(*net.degree(1), 2u);
    EXPECT_EQ(*net.degree(2), 2u);
    EXPECT_EQ(*net.degree(3), 2u);
    EXPECT_EQ(*net.degree(1, ukv_vertex_source_k), 1u);
    EXPECT_EQ(*net.degree(2, ukv_vertex_source_k), 1u);
    EXPECT_EQ(*net.degree(3, ukv_vertex_source_k), 1u);

    EXPECT_TRUE(net.edges(1));
    EXPECT_EQ(net.edges(1)->size(), 2ul);
    EXPECT_EQ(net.edges(1, ukv_vertex_source_k)->size(), 1ul);
    EXPECT_EQ(net.edges(1, ukv_vertex_target_k)->size(), 1ul);

    EXPECT_EQ(net.edges(3, ukv_vertex_target_k)->size(), 1ul);
    EXPECT_EQ(net.edges(2, ukv_vertex_source_k)->size(), 1ul);
    EXPECT_EQ((*net.edges(3, ukv_vertex_target_k))[0].source_id, 2);
    EXPECT_EQ((*net.edges(3, ukv_vertex_target_k))[0].target_id, 3);
    EXPECT_EQ((*net.edges(3, ukv_vertex_target_k))[0].id, 10);
    EXPECT_EQ(net.edges(3, 1)->size(), 1ul);
    EXPECT_EQ(net.edges(1, 3)->size(), 0ul);

    // Check scans
    EXPECT_TRUE(net.edges());
    {
        std::unordered_set<edge_t, edge_hash_t> expected_edges {triangle.begin(), triangle.end()};
        std::unordered_set<edge_t, edge_hash_t> exported_edges;

        auto present_edges = *net.edges();
        auto present_it = std::move(present_edges).begin();
        auto count_results = 0;
        while (!present_it.is_end()) {
            exported_edges.insert(*present_it);
            ++present_it;
            ++count_results;
        }
        EXPECT_EQ(count_results, triangle.size() * 2);
        EXPECT_EQ(exported_edges, expected_edges);
    }

    // Remove a single edge, making sure that the nodes info persists
    EXPECT_TRUE(net.remove(edges_view_t {
        {&triangle[0].source_id},
        {&triangle[0].target_id},
        {&triangle[0].id},
    }));
    EXPECT_TRUE(*net.contains(1));
    EXPECT_TRUE(*net.contains(2));
    EXPECT_EQ(net.edges(1, 2)->size(), 0ul);

    // Bring that edge back
    EXPECT_TRUE(net.upsert(edges_view_t {
        {&triangle[0].source_id},
        {&triangle[0].target_id},
        {&triangle[0].id},
    }));
    EXPECT_EQ(net.edges(1, 2)->size(), 1ul);

    // Remove a vertex
    ukv_key_t vertex_to_remove = 2;
    EXPECT_TRUE(net.remove(vertex_to_remove));
    EXPECT_FALSE(*net.contains(vertex_to_remove));
    EXPECT_EQ(net.edges(vertex_to_remove)->size(), 0ul);
    EXPECT_EQ(net.edges(1, vertex_to_remove)->size(), 0ul);
    EXPECT_EQ(net.edges(vertex_to_remove, 1)->size(), 0ul);

    // Bring back the whole graph
    EXPECT_TRUE(net.upsert(edges(triangle)));
    EXPECT_TRUE(*net.contains(vertex_to_remove));
    EXPECT_EQ(net.edges(vertex_to_remove)->size(), 2ul);
    EXPECT_EQ(net.edges(1, vertex_to_remove)->size(), 1ul);
    EXPECT_EQ(net.edges(vertex_to_remove, 1)->size(), 0ul);
    status_t status = db.clear();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
