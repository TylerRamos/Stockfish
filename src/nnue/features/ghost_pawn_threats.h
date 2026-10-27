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

// Potential pawn destinations controlled by individual pieces, including kings.
#ifndef NNUE_FEATURES_GHOST_PAWN_THREATS_INCLUDED
#define NNUE_FEATURES_GHOST_PAWN_THREATS_INCLUDED

#include <array>
#include "../../misc.h"
#include "../../types.h"
#include "../nnue_common.h"

namespace Stockfish {
class Position;
}
namespace Stockfish::Eval::NNUE::Features {
class GhostPawnThreats {
   public:
    static constexpr u32 HashValue = 0x71c9e5a3u;
    static constexpr IndexType Edges = 7504;
    static constexpr IndexType Dimensions = COLOR_NB * Edges;
    // At most 32 pieces, each with <=27 attacks, for two ghost colors.
    static constexpr IndexType MaxActiveDimensions = 2048;
    using IndexList = ValueList<u16, MaxActiveDimensions>;
    using ChangedList                              = ValueList<u16, DirtyGhosts::MaxSize>;

    // Ghost squares without en passant, which depends on the side to move
    static std::array<Bitboard, COLOR_NB> destinations(const Position& pos);
    static Square                         ep_destination(const Position& pos);
    // Ghost squares that appeared or disappeared in the move, with their new attackers
    static void      compute_delta(const Position&                       pos,
                                   const std::array<Bitboard, COLOR_NB>& oldGhosts,
                                   DirtyGhosts&                          dirty);
    static IndexType make_index(Color perspective,
                                Color ghostColor,
                                Piece attacker,
                                Square from,
                                Square to,
                                Square ksq);
    static void append_active_indices(Color perspective, const Position& pos, IndexList& active);
    static void append_changed_indices(Color              perspective,
                                       Square             ksq,
                                       const DirtyGhosts& diff,
                                       ChangedList&       removed,
                                       ChangedList&       added,
                                       const i8*          prefetchBase);
    static void append_changed_indices_both(Square             whiteKsq,
                                            Square             blackKsq,
                                            const DirtyGhosts& diff,
                                            ChangedList        removed[COLOR_NB],
                                            ChangedList        added[COLOR_NB],
                                            const i8*          prefetchBase);
};
}
#endif
