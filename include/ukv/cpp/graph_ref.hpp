/**
 * @file graph_ref.hpp
 * @author Ashot Vardanian
 * @date 30 Jun 2022
 * @brief C++ bindings for @see "ukv/graph.h".
 */

#pragma once
#include "ukv/graph.h"
#include "ukv/cpp/types.hpp"
#include "ukv/cpp/edges_stream.hpp"
#include "ukv/cpp/members_ref.hpp"

namespace unum::ukv {

/**
 * @brief Wraps relational/linking operations with cleaner type system.
 * Controls mainly just the inverted index collection and keeps a local
 * memory buffer (tape) for read operations, so isn't thread-safe.
 * You can have one such object in every working thread, even for the
 * same graph collection. Supports updates/reads from within a transaction.
 */
class graph_ref_t {
    ukv_t db_ = nullptr;
    ukv_txn_t txn_ = nullptr;
    ukv_col_t col_ = ukv_col_main_k;
    ukv_arena_t* arena_ = nullptr;

  public:
    graph_ref_t(ukv_t db, ukv_txn_t txn, ukv_col_t col, ukv_arena_t* arena) noexcept
        : db_(db), txn_(txn), col_(col), arena_(arena) {}

    graph_ref_t(graph_ref_t&&) = default;
    graph_ref_t& operator=(graph_ref_t&&) = default;
    graph_ref_t(graph_ref_t const&) = default;
    graph_ref_t& operator=(graph_ref_t const&) = default;

    graph_ref_t& on(arena_t& arena) noexcept {
        arena_ = arena.member_ptr();
        return *this;
    }

    status_t upsert(edges_view_t const& edges) noexcept {
        status_t status;
        ukv_graph_upsert_edges( //
            db_,
            txn_,
            edges.size(),
            &col_,
            0,
            edges.edge_ids.begin().get(),
            edges.edge_ids.stride(),
            edges.source_ids.begin().get(),
            edges.source_ids.stride(),
            edges.target_ids.begin().get(),
            edges.target_ids.stride(),
            ukv_options_default_k,
            arena_,
            status.member_ptr());
        return status;
    }

    status_t remove(edges_view_t const& edges) noexcept {
        status_t status;
        ukv_graph_remove_edges( //
            db_,
            txn_,
            edges.size(),
            &col_,
            0,
            edges.edge_ids.begin().get(),
            edges.edge_ids.stride(),
            edges.source_ids.begin().get(),
            edges.source_ids.stride(),
            edges.target_ids.begin().get(),
            edges.target_ids.stride(),
            ukv_options_default_k,
            arena_,
            status.member_ptr());
        return status;
    }

    status_t upsert(edge_t const& edge) noexcept { return upsert(edges_view_t {&edge, &edge + 1}); }
    status_t remove(edge_t const& edge) noexcept { return remove(edges_view_t {&edge, &edge + 1}); }

    status_t remove( //
        ukv_key_t const vertex,
        ukv_vertex_role_t const role = ukv_vertex_role_any_k,
        bool flush = false) noexcept {
        return remove({&vertex}, {&role}, flush);
    }

    status_t remove( //
        strided_range_gt<ukv_key_t const> vertices,
        strided_range_gt<ukv_vertex_role_t const> roles = {},
        bool flush = false) noexcept {

        status_t status;
        ukv_options_t options = flush ? ukv_option_write_flush_k : ukv_options_default_k;

        ukv_graph_remove_vertices( //
            db_,
            txn_,
            vertices.count(),
            &col_,
            0,
            vertices.begin().get(),
            vertices.stride(),
            roles.begin().get(),
            roles.stride(),
            options,
            arena_,
            status.member_ptr());
        return status;
    }

    expected_gt<ukv_vertex_degree_t> degree( //
        ukv_key_t vertex,
        ukv_vertex_role_t role = ukv_vertex_role_any_k,
        bool track = false) noexcept {

        auto maybe_degrees = degrees({&vertex}, {&role}, track);
        if (!maybe_degrees)
            return maybe_degrees.release_status();
        auto degrees = *maybe_degrees;
        return ukv_vertex_degree_t(degrees[0]);
    }

    expected_gt<indexed_range_gt<ukv_vertex_degree_t*>> degrees( //
        strided_range_gt<ukv_key_t const> vertices,
        strided_range_gt<ukv_vertex_role_t const> roles = {},
        bool track = false) noexcept {

        status_t status;
        ukv_vertex_degree_t* degrees_per_vertex = nullptr;
        ukv_key_t* neighborships_per_vertex = nullptr;
        ukv_options_t options = static_cast<ukv_options_t>((track ? ukv_option_read_track_k : ukv_options_default_k) |
                                                           ukv_option_read_lengths_k);

        ukv_graph_find_edges( //
            db_,
            txn_,
            vertices.count(),
            &col_,
            0,
            vertices.begin().get(),
            vertices.stride(),
            roles.begin().get(),
            roles.stride(),
            options,
            &degrees_per_vertex,
            &neighborships_per_vertex,
            arena_,
            status.member_ptr());
        if (!status)
            return status;

        return indexed_range_gt<ukv_vertex_degree_t*> {degrees_per_vertex, degrees_per_vertex + vertices.size()};
    }

    expected_gt<bool> contains(ukv_key_t vertex, bool track = false) noexcept {
        return members_ref_gt<col_key_field_t>(db_, txn_, ckf(col_, vertex), arena_).present(track);
    }

    /**
     * @brief Checks if certain vertices are present in the graph.
     * They maybe disconnected from everything else.
     */
    expected_gt<strided_range_gt<bool>> contains( //
        strided_range_gt<ukv_key_t const> const& vertices,
        bool track = false) noexcept {
        keys_arg_t arg;
        arg.cols_begin = {&col_, 0};
        arg.keys_begin = vertices.begin();
        arg.count = vertices.count();
        return members_ref_gt<keys_arg_t>(db_, txn_, arg, arena_).present(track);
    }

    using adjacency_range_t = range_gt<edges_stream_t>;

    expected_gt<adjacency_range_t> edges(
        std::size_t vertices_read_ahead = keys_stream_t::default_read_ahead_k) const noexcept {

        edges_stream_t b {db_, col_, vertices_read_ahead, txn_};
        edges_stream_t e {db_, col_, vertices_read_ahead, txn_};
        status_t status = b.seek_to_first();
        if (!status)
            return status;
        status = e.seek(ukv_key_unknown_k);
        if (!status)
            return status;

        adjacency_range_t result {std::move(b), std::move(e)};
        return result;
    }

    expected_gt<edges_span_t> edges( //
        ukv_key_t vertex,
        ukv_vertex_role_t role = ukv_vertex_role_any_k,
        bool track = false) noexcept {

        status_t status;
        ukv_vertex_degree_t* degrees_per_vertex = nullptr;
        ukv_key_t* neighborships_per_vertex = nullptr;

        ukv_graph_find_edges( //
            db_,
            txn_,
            1,
            &col_,
            0,
            &vertex,
            0,
            &role,
            0,
            track ? ukv_option_read_track_k : ukv_options_default_k,
            &degrees_per_vertex,
            &neighborships_per_vertex,
            arena_,
            status.member_ptr());
        if (!status)
            return status;

        ukv_vertex_degree_t edges_count = degrees_per_vertex[0];
        if (edges_count == ukv_vertex_degree_missing_k)
            return edges_span_t {};

        auto edges_begin = reinterpret_cast<edge_t*>(neighborships_per_vertex);
        return edges_span_t {edges_begin, edges_begin + edges_count};
    }

    expected_gt<edges_span_t> edges(ukv_key_t source, ukv_key_t target, bool track = false) noexcept {
        auto maybe_all = edges(source, ukv_vertex_source_k, track);
        if (!maybe_all)
            return maybe_all;

        edges_span_t all = *maybe_all;
        auto begin_and_end = std::equal_range(all.target_ids.begin(), all.target_ids.end(), target);
        auto begin_offset = begin_and_end.first - all.target_ids.begin();
        auto count = begin_and_end.second - begin_and_end.first;
        all.source_ids = all.source_ids.subspan(begin_offset, count);
        all.target_ids = all.target_ids.subspan(begin_offset, count);
        all.edge_ids = all.edge_ids.subspan(begin_offset, count);
        return all;
    }

    /**
     * @brief Finds all the edges, that have any of the supplied nodes in allowed roles.
     * In undirected graphs, some edges may come with inverse duplicates.
     */
    expected_gt<edges_span_t> edges_containing( //
        strided_range_gt<ukv_key_t const> vertices,
        strided_range_gt<ukv_vertex_role_t const> roles = {},
        bool track = false) noexcept {

        status_t status;
        ukv_vertex_degree_t* degrees_per_vertex = nullptr;
        ukv_key_t* neighborships_per_vertex = nullptr;

        ukv_graph_find_edges( //
            db_,
            txn_,
            vertices.count(),
            &col_,
            0,
            vertices.begin().get(),
            vertices.stride(),
            roles.begin().get(),
            roles.stride(),
            track ? ukv_option_read_track_k : ukv_options_default_k,
            &degrees_per_vertex,
            &neighborships_per_vertex,
            arena_,
            status.member_ptr());
        if (!status)
            return status;

        auto edges_begin = reinterpret_cast<edge_t*>(neighborships_per_vertex);
        auto edges_count = transform_reduce_n(degrees_per_vertex, vertices.size(), 0ul, [](ukv_vertex_degree_t deg) {
            return deg == ukv_vertex_degree_missing_k ? 0 : deg;
        });

        return edges_span_t {edges_begin, edges_begin + edges_count};
    }

    expected_gt<strided_range_gt<ukv_key_t>> successors(ukv_key_t vertex) noexcept {
        auto maybe = edges(vertex, ukv_vertex_source_k);
        if (!maybe)
            return maybe.release_status();
        return strided_range_gt<ukv_key_t> {maybe->target_ids};
    }

    expected_gt<strided_range_gt<ukv_key_t>> predecessors(ukv_key_t vertex) noexcept {
        auto maybe = edges(vertex, ukv_vertex_target_k);
        if (!maybe)
            return maybe.release_status();
        return strided_range_gt<ukv_key_t> {maybe->source_ids};
    }

    expected_gt<strided_range_gt<ukv_key_t>> neighbors(ukv_key_t vertex) noexcept {
        // Retrieving neighbors in directed graphs is trickier than just `successors` or `predecessors`.
        // We are receiving an adjacency list, where both incoming an edges exist.
        // So the stride/offset is not uniform across the entire list.
        auto maybe = edges(vertex, ukv_vertex_role_any_k);
        if (!maybe)
            return maybe.release_status();

        // We can gobble the contents a little bit by swapping the members of some
        // edges to make it uniform.
        edges_span_t es = *maybe;
        auto count = es.size();
        for (std::size_t i = 0; i != count; ++i) {
            ukv_key_t& u = es.source_ids[i];
            ukv_key_t& v = es.target_ids[i];
            if (u == vertex)
                std::swap(u, v);
        }

        return strided_range_gt<ukv_key_t> {es.target_ids};
    }

    status_t export_adjacency_list(std::string const& path,
                                   std::string_view column_separator,
                                   std::string_view line_delimiter);

    status_t import_adjacency_list(std::string const& path,
                                   std::string_view column_separator,
                                   std::string_view line_delimiter);
};

} // namespace unum::ukv
