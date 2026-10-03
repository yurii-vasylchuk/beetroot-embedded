#pragma once

#include "notes.h"

namespace sounds {

constexpr uint16_t BPM = 240; // 200-240

constexpr Note SAFE_UNLOCKED_NOTES[] = {
    {C5, Duration::Sixteenth},
    {E5, Duration::Sixteenth},
    {G5, Duration::Sixteenth},
    {C6, Duration::Quarter},
};

constexpr Note SAFE_LOCKED_NOTES[] = {
    {C6, Duration::Sixteenth},
    {G5, Duration::Sixteenth},
    {E5, Duration::Sixteenth},
    {C5, Duration::Eighth},
};

constexpr Note SAFE_BLOCKED_NOTES[] = {
    {E3, Duration::Eighth}, {C3, Duration::Eighth},  {E3, Duration::Eighth},
    {C3, Duration::Eighth}, {E3, Duration::Quarter},
};

constexpr Note DIGIT_INPUT_NOTES[] = {
    {C6, Duration::Sixteenth},
};

constexpr Note NEXT_DIGIT_NOTES[] = {
    {G6, Duration::Eighth},
};

constexpr Note WRONG_INPUT_NOTES[] = {
    {E5, Duration::Sixteenth},
    {DS5, Duration::Sixteenth},
    {C5, Duration::Quarter},
};

const Riff SAFE_UNLOCKED = {
    .length = std::size(SAFE_UNLOCKED_NOTES),
    .notes = SAFE_UNLOCKED_NOTES,
};

const Riff SAFE_LOCKED = {
    .length = std::size(SAFE_LOCKED_NOTES),
    .notes = SAFE_LOCKED_NOTES,
};

const Riff SAFE_BLOCKED = {
    .length = std::size(SAFE_BLOCKED_NOTES),
    .notes = SAFE_BLOCKED_NOTES,
};

const Riff DIGIT_INPUT = {
    .length = std::size(DIGIT_INPUT_NOTES),
    .notes = DIGIT_INPUT_NOTES,
};

const Riff NEXT_DIGIT = {
    .length = std::size(NEXT_DIGIT_NOTES),
    .notes = NEXT_DIGIT_NOTES,
};

const Riff WRONG_INPUT = {
    .length = std::size(WRONG_INPUT_NOTES),
    .notes = WRONG_INPUT_NOTES,
};

} // namespace sounds
