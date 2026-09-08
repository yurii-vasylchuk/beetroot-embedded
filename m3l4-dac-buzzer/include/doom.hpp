#pragma once

#include "notes.hpp"

static constexpr Note DOOM_A_NOTES[] = {
    {E3, Duration::Sixteenth}, {E3, Duration::Sixteenth},
    {E4, Duration::Eighth},

    {E3, Duration::Sixteenth}, {E3, Duration::Sixteenth},
    {D4, Duration::Eighth},

    {E3, Duration::Sixteenth}, {E3, Duration::Sixteenth},
    {C4, Duration::Eighth},

    {E3, Duration::Sixteenth}, {E3, Duration::Sixteenth},
    {AS3, Duration::Eighth},

    {E3, Duration::Sixteenth}, {E3, Duration::Sixteenth},
    {B3, Duration::Sixteenth}, {C4, Duration::Sixteenth},

    {E3, Duration::Sixteenth}, {E3, Duration::Sixteenth},
    {E4, Duration::Eighth},

    {E3, Duration::Sixteenth}, {E3, Duration::Sixteenth},
    {D4, Duration::Eighth},

    {E3, Duration::Sixteenth}, {E3, Duration::Sixteenth},
    {C4, Duration::Eighth},

    {E3, Duration::Sixteenth}, {E3, Duration::Sixteenth},
    {AS3, Duration::Quarter},
};

static constexpr Note DOOM_B_NOTES[] = {
    {A3, Duration::Sixteenth}, {A3, Duration::Sixteenth},
    {A4, Duration::Eighth},

    {A3, Duration::Sixteenth}, {A3, Duration::Sixteenth},
    {G4, Duration::Eighth},

    {A3, Duration::Sixteenth}, {A3, Duration::Sixteenth},
    {F4, Duration::Eighth},

    {A3, Duration::Sixteenth}, {A3, Duration::Sixteenth},
    {DS4, Duration::Eighth},

    {A3, Duration::Sixteenth}, {A3, Duration::Sixteenth},
    {E4, Duration::Sixteenth}, {F4, Duration::Sixteenth},

    {A3, Duration::Sixteenth}, {A3, Duration::Sixteenth},
    {A4, Duration::Eighth},

    {A3, Duration::Sixteenth}, {A3, Duration::Sixteenth},
    {G4, Duration::Eighth},

    {A3, Duration::Sixteenth}, {A3, Duration::Sixteenth},
    {F4, Duration::Eighth},

    {A3, Duration::Sixteenth}, {A3, Duration::Sixteenth},
    {DS4, Duration::Quarter},
};

static constexpr Note DOOM_C_NOTES[] = {
    {CS4, Duration::Sixteenth}, {CS4, Duration::Sixteenth},
    {CS5, Duration::Eighth},

    {CS4, Duration::Sixteenth}, {CS4, Duration::Sixteenth},
    {B4, Duration::Eighth},

    {CS4, Duration::Sixteenth}, {CS4, Duration::Sixteenth},
    {A4, Duration::Eighth},

    {CS4, Duration::Sixteenth}, {CS4, Duration::Sixteenth},
    {G4, Duration::Eighth},

    {CS4, Duration::Sixteenth}, {CS4, Duration::Sixteenth},
    {GS4, Duration::Sixteenth}, {A4, Duration::Sixteenth},

    {B3, Duration::Sixteenth},  {B3, Duration::Sixteenth},
    {B4, Duration::Eighth},

    {B3, Duration::Sixteenth},  {B3, Duration::Sixteenth},
    {A4, Duration::Eighth},

    {B3, Duration::Sixteenth},  {B3, Duration::Sixteenth},
    {G4, Duration::Eighth},

    {A3, Duration::Sixteenth},  {A3, Duration::Sixteenth},
    {F4, Duration::Quarter},
};

static constexpr Riff DOOM_A = {
    .length = std::size(DOOM_A_NOTES),
    .notes = DOOM_A_NOTES,
};

static constexpr Riff DOOM_B = {
    .length = std::size(DOOM_B_NOTES),
    .notes = DOOM_B_NOTES,
};

static constexpr Riff DOOM_C = {
    .length = std::size(DOOM_C_NOTES),
    .notes = DOOM_C_NOTES,
};

static constexpr Riff DOOM_SONG_RIFS[] = {
    //    doom_riff_a, doom_riff_a, doom_riff_a, doom_riff_a,
    DOOM_A, DOOM_A, DOOM_B, DOOM_A, DOOM_A, DOOM_A, DOOM_C, DOOM_A, DOOM_A,
    DOOM_A, DOOM_A, DOOM_A, DOOM_B, DOOM_A, DOOM_A, DOOM_A, DOOM_C, DOOM_A};

static constexpr Song DOOM_SONG = {
    .length = std::size(DOOM_SONG_RIFS),
    .riffs = DOOM_SONG_RIFS,
};
