#pragma once

#include "notes.hpp"

static constexpr Note MARIO_INTRO_NOTES[] = {
    {E5, Duration::Eighth},  {E5, Duration::Eighth},  {P0, Duration::Eighth},
    {E5, Duration::Eighth},

    {P0, Duration::Eighth},  {C5, Duration::Eighth},  {E5, Duration::Quarter},

    {G5, Duration::Quarter}, {P0, Duration::Quarter},

    {G4, Duration::Quarter}, {P0, Duration::Quarter},
};

static constexpr Note MARIO_MAIN_A_NOTES[] = {
    {C5, Duration::Quarter}, {P0, Duration::Eighth},

    {G4, Duration::Eighth},  {P0, Duration::Quarter},

    {E4, Duration::Quarter}, {P0, Duration::Eighth},

    {A4, Duration::Quarter}, {B4, Duration::Quarter},

    {AS4, Duration::Eighth}, {A4, Duration::Quarter},
};

static constexpr Note MARIO_MAIN_B_NOTES[] = {
    {G4, Duration::Eighth},

    {E5, Duration::Eighth},  {G5, Duration::Eighth}, {A5, Duration::Quarter},

    {F5, Duration::Eighth},  {G5, Duration::Eighth},

    {P0, Duration::Eighth},

    {E5, Duration::Quarter},

    {C5, Duration::Eighth},  {D5, Duration::Eighth}, {B4, Duration::Quarter},

    {P0, Duration::Eighth},
};

static constexpr Note MARIO_BRIDGE_A_NOTES[] = {
    {G5, Duration::Eighth}, {FS5, Duration::Eighth}, {F5, Duration::Eighth},

    {D5, Duration::Eighth}, {E5, Duration::Eighth},

    {P0, Duration::Eighth},

    {G4, Duration::Eighth}, {A4, Duration::Eighth},

    {C5, Duration::Eighth}, {P0, Duration::Eighth},

    {A4, Duration::Eighth}, {C5, Duration::Eighth},  {D5, Duration::Eighth},

    {P0, Duration::Eighth},
};

static constexpr Note MARIO_BRIDGE_END_A_NOTES[] = {
    {G5, Duration::Eighth},  {FS5, Duration::Eighth}, {F5, Duration::Eighth},

    {D5, Duration::Eighth},  {E5, Duration::Eighth},

    {P0, Duration::Eighth},

    {C6, Duration::Eighth},  {C6, Duration::Eighth},  {C6, Duration::Quarter},

    {P0, Duration::Quarter},
};

static constexpr Note MARIO_BRIDGE_END_B_NOTES[] = {
    {DS5, Duration::Eighth}, {D5, Duration::Eighth}, {C5, Duration::Eighth},

    {P0, Duration::Eighth},

    {C5, Duration::Eighth},  {C5, Duration::Eighth}, {C5, Duration::Quarter},

    {P0, Duration::Quarter},
};

static constexpr Note MARIO_TRANSITION_A_NOTES[] = {
    {C5, Duration::Eighth},  {D5, Duration::Eighth},  {E5, Duration::Eighth},

    {C5, Duration::Eighth},

    {A4, Duration::Eighth},  {G4, Duration::Quarter},

    {P0, Duration::Quarter},
};

static constexpr Note MARIO_TRANSITION_B_NOTES[] = {
    {C5, Duration::Eighth},  {C5, Duration::Eighth}, {C5, Duration::Quarter},

    {P0, Duration::Eighth},

    {C5, Duration::Eighth},  {D5, Duration::Eighth}, {E5, Duration::Quarter},

    {P0, Duration::Quarter},
};

static constexpr Note MARIO_ENDING_A_NOTES[] = {
    {E5, Duration::Eighth},  {C5, Duration::Eighth},

    {G4, Duration::Quarter}, {P0, Duration::Eighth},

    {G4, Duration::Eighth},  {A4, Duration::Eighth},

    {F5, Duration::Eighth},  {F5, Duration::Eighth},

    {A4, Duration::Quarter}, {P0, Duration::Eighth},
};

static constexpr Note MARIO_ENDING_B_NOTES[] = {
    {B4, Duration::Eighth},

    {A5, Duration::Eighth},  {A5, Duration::Eighth},  {A5, Duration::Eighth},

    {G5, Duration::Eighth},  {F5, Duration::Eighth},

    {E5, Duration::Eighth},  {C5, Duration::Eighth},

    {A4, Duration::Eighth},  {G4, Duration::Quarter},

    {P0, Duration::Quarter},
};

static constexpr Note MARIO_ENDING_C_NOTES[] = {
    {B4, Duration::Eighth},

    {F5, Duration::Eighth},  {F5, Duration::Eighth}, {F5, Duration::Eighth},

    {E5, Duration::Eighth},  {D5, Duration::Eighth}, {C5, Duration::Quarter},

    {G4, Duration::Eighth},  {E4, Duration::Eighth}, {C4, Duration::Quarter},

    {P0, Duration::Quarter},
};

static constexpr Note MARIO_FINAL_NOTES[] = {
    {C5, Duration::Quarter}, {G4, Duration::Eighth},  {E4, Duration::Quarter},

    {P0, Duration::Eighth},

    {A4, Duration::Eighth},  {B4, Duration::Eighth},  {A4, Duration::Quarter},

    {GS4, Duration::Eighth}, {AS4, Duration::Eighth}, {GS4, Duration::Quarter},

    {G4, Duration::Eighth},  {FS4, Duration::Eighth}, {G4, Duration::Half},
};

// -----------------------------------------------------------------------------
// Riffs
// -----------------------------------------------------------------------------

static constexpr Riff MARIO_INTRO = {
    .length = std::size(MARIO_INTRO_NOTES),
    .notes = MARIO_INTRO_NOTES,
};

static constexpr Riff MARIO_MAIN_A = {
    .length = std::size(MARIO_MAIN_A_NOTES),
    .notes = MARIO_MAIN_A_NOTES,
};

static constexpr Riff MARIO_MAIN_B = {
    .length = std::size(MARIO_MAIN_B_NOTES),
    .notes = MARIO_MAIN_B_NOTES,
};

static constexpr Riff MARIO_BRIDGE_A = {
    .length = std::size(MARIO_BRIDGE_A_NOTES),
    .notes = MARIO_BRIDGE_A_NOTES,
};

static constexpr Riff MARIO_BRIDGE_END_A = {
    .length = std::size(MARIO_BRIDGE_END_A_NOTES),
    .notes = MARIO_BRIDGE_END_A_NOTES,
};

static constexpr Riff MARIO_BRIDGE_END_B = {
    .length = std::size(MARIO_BRIDGE_END_B_NOTES),
    .notes = MARIO_BRIDGE_END_B_NOTES,
};

static constexpr Riff MARIO_TRANSITION_A = {
    .length = std::size(MARIO_TRANSITION_A_NOTES),
    .notes = MARIO_TRANSITION_A_NOTES,
};

static constexpr Riff MARIO_TRANSITION_B = {
    .length = std::size(MARIO_TRANSITION_B_NOTES),
    .notes = MARIO_TRANSITION_B_NOTES,
};

static constexpr Riff MARIO_ENDING_A = {
    .length = std::size(MARIO_ENDING_A_NOTES),
    .notes = MARIO_ENDING_A_NOTES,
};

static constexpr Riff MARIO_ENDING_B = {
    .length = std::size(MARIO_ENDING_B_NOTES),
    .notes = MARIO_ENDING_B_NOTES,
};

static constexpr Riff MARIO_ENDING_C = {
    .length = std::size(MARIO_ENDING_C_NOTES),
    .notes = MARIO_ENDING_C_NOTES,
};

static constexpr Riff MARIO_FINAL = {
    .length = std::size(MARIO_FINAL_NOTES),
    .notes = MARIO_FINAL_NOTES,
};

// -----------------------------------------------------------------------------
// Song
// -----------------------------------------------------------------------------

static constexpr Riff MARIO_SONG_RIFFS[] = {
    // Intro
    MARIO_INTRO,

    // Main theme
    MARIO_MAIN_A,
    MARIO_MAIN_B,
    MARIO_MAIN_A,
    MARIO_MAIN_B,

    // Bridge
    MARIO_BRIDGE_A,
    MARIO_BRIDGE_END_A,

    MARIO_BRIDGE_A,
    MARIO_BRIDGE_END_B,

    // Transition
    MARIO_TRANSITION_A,
    MARIO_TRANSITION_B,
    MARIO_TRANSITION_A,

    // Intro reprise
    MARIO_INTRO,

    // Main theme reprise
    MARIO_MAIN_A,
    MARIO_MAIN_B,
    MARIO_MAIN_A,
    MARIO_MAIN_B,

    // Ending
    MARIO_ENDING_A,
    MARIO_ENDING_B,
    MARIO_ENDING_A,
    MARIO_ENDING_C,

    // Final phrase
    MARIO_FINAL,
};

static constexpr Song MARIO_SONG = {
    .length = std::size(MARIO_SONG_RIFFS),
    .riffs = MARIO_SONG_RIFFS,
};
