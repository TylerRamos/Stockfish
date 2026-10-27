/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2026 The Stockfish developers (see AUTHORS file)
  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "ghost_pawn_threats.h"
#include "full_threats.h"
#include "../../position.h"
#include "../../attacks.h"
#include "../../bitboard.h"
#include "../nnue_architecture.h"

namespace Stockfish::Eval::NNUE::Features {
namespace {
struct Offsets {
    std::array<std::array<u16, SQUARE_NB>, PIECE_NB> squares{};
    IndexType total = 0;
};
constexpr Bitboard geometry(Piece piece, Square from) {
    const int index = type_of(piece) == PAWN ? int(color_of(piece)) : int(type_of(piece));
    return Attacks::PseudoAttacks[index][from];
}
constexpr auto offsets = [] {
    Offsets table;
    for (Color c : {WHITE, BLACK})
        for (PieceType pt = PAWN; pt <= KING; ++pt)
            for (Square from = SQ_A1; from <= SQ_H8; ++from)
            {
                const Piece pc = make_piece(c, pt);
                table.squares[pc][from] = table.total;
                if (pt != PAWN || (from >= SQ_A2 && from <= SQ_H7))
                    table.total += constexpr_popcount(geometry(pc, from));
            }
    return table;
}();
static_assert(offsets.total == GhostPawnThreats::Edges);
}

std::array<Bitboard, COLOR_NB> GhostPawnThreats::destinations(const Position& pos) {
    std::array<Bitboard, COLOR_NB> result{};
    const Bitboard empty = ~pos.pieces();

    for (Color us : {WHITE, BLACK})
    {
        const Bitboard pinned      = pos.blockers_for_king(us);
        const Square   ksq         = pos.square<KING>(us);
        const Bitboard pawns       = pos.pieces(us, PAWN);
        const Bitboard enemies     = pos.pieces(~us);
        const Direction up         = pawn_push(us);
        const Bitboard unpinned    = pawns & ~pinned;
        Bitboard       pinnedPawns = pawns & pinned;

        const Bitboard sp   = shift(unpinned, up) & empty;
        const Bitboard dp   = shift(sp & (us == WHITE ? Rank3BB : Rank6BB), up) & empty;
        const Bitboard capR = shift(unpinned, us == WHITE ? NORTH_EAST : SOUTH_WEST) & enemies;
        const Bitboard capL = shift(unpinned, us == WHITE ? NORTH_WEST : SOUTH_EAST) & enemies;
        Bitboard       ghosts = sp | dp | capR | capL;

        while (pinnedPawns)
        {
            const Square   from = pop_lsb(pinnedPawns);
            const Bitboard ray  = Attacks::line_bb(ksq, from);
            const Square   s1   = from + up;
            if ((ray & s1) && (empty & s1))
            {
                ghosts |= s1;
                // Only form the second square on the starting rank.
                if (relative_rank(us, from) == RANK_2)
                {
                    const Square s2 = s1 + up;
                    if ((ray & s2) && (empty & s2))
                        ghosts |= s2;
                }
            }
            // Bitboard attacks avoid wrapping captures across the a/h files.
            ghosts |= Attacks::attacks_bb(PAWN, from, us) & enemies & ray;
        }

        result[us] = ghosts;
    }
    return result;
}

Square GhostPawnThreats::ep_destination(const Position& pos) {
    const Square ep = pos.ep_square();
    if (ep != SQ_NONE)
    {
        const Color us  = pos.side_to_move();
        Bitboard    epP = Attacks::attacks_bb(PAWN, ep, ~us) & pos.pieces(us, PAWN);
        while (epP)
            if (pos.legal(Move::make<EN_PASSANT>(pop_lsb(epP), ep)))
                return ep;
    }
    return SQ_NONE;
}


IndexType GhostPawnThreats::make_index(Color perspective, Color ghostColor, Piece attacker,
                                      Square from, Square to, Square ksq) {
    const int orientation = FullThreats::OrientTBL[ksq] ^ (56 * perspective);
    from = Square(int(from) ^ orientation);
    to = Square(int(to) ^ orientation);
    attacker = Piece(int(attacker) ^ (8 * perspective));
    return (ghostColor ^ perspective) * Edges + offsets.squares[attacker][from]
         + popcount(geometry(attacker, from) & ((Bitboard(1) << to) - 1));
}

void GhostPawnThreats::append_active_indices(Color           perspective,
                                             const Position& pos,
                                             IndexList&      active) {
    const Square ksq = pos.square<KING>(perspective);
    for (Color gc : {WHITE, BLACK})
        for (Bitboard targets = pos.ghost_squares(gc); targets;)
        {
            const Square to = pop_lsb(targets);
            for (Bitboard attackers = pos.attackers_to(to); attackers;)
            {
                const Square from = pop_lsb(attackers);
                active.push_back(make_index(perspective, gc, pos.piece_on(from), from, to, ksq));
            }
        }
}

void GhostPawnThreats::compute_delta(const Position&                       pos,
                                     const std::array<Bitboard, COLOR_NB>& oldGhosts,
                                     DirtyGhosts&                          dirty) {
    for (Color gc : {WHITE, BLACK})
        for (Bitboard b = pos.ghost_squares(gc) ^ oldGhosts[gc]; b;)
        {
            const Square t   = pop_lsb(b);
            const bool   add = pos.ghost_squares(gc) & t;
            for (Bitboard attackers = pos.attackers_to(t); attackers;)
            {
                const Square s = pop_lsb(attackers);
                dirty.push(DirtyGhosts::encode(add, gc, pos.piece_on(s), s, t));
            }
        }
}

namespace {
struct Edge {
    Bitboard geometry;
    u32      offset;
};

alignas(64) constexpr auto edges = [] {
    std::array<std::array<Edge, SQUARE_NB>, PIECE_NB> table{};
    for (Color c : {WHITE, BLACK})
        for (PieceType pt = PAWN; pt <= KING; ++pt)
            for (Square from = SQ_A1; from <= SQ_H8; ++from)
            {
                const Piece pc  = make_piece(c, pt);
                table[pc][from] = {geometry(pc, from), offsets.squares[pc][from]};
            }
    return table;
}();

// Same as make_index() for a DirtyGhosts entry, orientation = OrientTBL[ksq] ^ (56 * perspective)
sf_always_inline IndexType raw_index(u32 raw, unsigned orientation, unsigned perspective) {
    const unsigned to   = (raw & 0xff) ^ orientation;
    const unsigned from = ((raw >> 8) & 0xff) ^ orientation;
    const auto&    e    = edges[((raw >> 16) & 0xf) ^ (8 * perspective)][from];
    return (((raw >> 24) & 1) ^ perspective) * GhostPawnThreats::Edges + e.offset
         + popcount(e.geometry & ((Bitboard(1) << to) - 1));
}
}

void GhostPawnThreats::append_changed_indices(Color              perspective,
                                              Square             ksq,
                                              const DirtyGhosts& diff,
                                              ChangedList&       removed,
                                              ChangedList&       added,
                                              const i8*          prefetchBase) {
    const unsigned orientation = u8(FullThreats::OrientTBL[ksq]) ^ (56 * perspective);
    for (int i = 0; i < diff.size; ++i)
    {
        const IndexType idx = raw_index(diff.list[i], orientation, perspective);
        prefetch<PrefetchRw::READ, PrefetchLoc::LOW>(prefetchBase + usize(idx) * L1);
        (diff.list[i] >> 31 ? added : removed).push_back(idx);
    }
}

void GhostPawnThreats::append_changed_indices_both(Square             whiteKsq,
                                                   Square             blackKsq,
                                                   const DirtyGhosts& diff,
                                                   ChangedList        removed[COLOR_NB],
                                                   ChangedList        added[COLOR_NB],
                                                   const i8*          prefetchBase) {
    const unsigned orientation[COLOR_NB] = {u8(FullThreats::OrientTBL[whiteKsq]),
                                            u8(FullThreats::OrientTBL[blackKsq]) ^ 56u};
    for (int i = 0; i < diff.size; ++i)
        for (Color c : {WHITE, BLACK})
        {
            const IndexType idx = raw_index(diff.list[i], orientation[c], c);
            prefetch<PrefetchRw::READ, PrefetchLoc::LOW>(prefetchBase + usize(idx) * L1);
            (diff.list[i] >> 31 ? added[c] : removed[c]).push_back(idx);
        }
}
}
