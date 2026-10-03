#pragma once

#include <cstdint>
#include <iterator>

static constexpr uint16_t P0 = 0;

static constexpr uint16_t C3 = 131;
static constexpr uint16_t CS3 = 139;
static constexpr uint16_t D3 = 147;
static constexpr uint16_t DS3 = 156;
static constexpr uint16_t E3 = 165;
static constexpr uint16_t F3 = 175;
static constexpr uint16_t FS3 = 185;
static constexpr uint16_t G3 = 196;
static constexpr uint16_t GS3 = 208;
static constexpr uint16_t A3 = 220;
static constexpr uint16_t AS3 = 233;
static constexpr uint16_t B3 = 247;

static constexpr uint16_t C4 = 262;
static constexpr uint16_t CS4 = 277;
static constexpr uint16_t D4 = 294;
static constexpr uint16_t DS4 = 311;
static constexpr uint16_t E4 = 330;
static constexpr uint16_t F4 = 349;
static constexpr uint16_t FS4 = 370;
static constexpr uint16_t G4 = 392;
static constexpr uint16_t GS4 = 415;
static constexpr uint16_t A4 = 440;
static constexpr uint16_t AS4 = 466;
static constexpr uint16_t B4 = 494;

static constexpr uint16_t C5 = 523;
static constexpr uint16_t CS5 = 554;
static constexpr uint16_t D5 = 587;
static constexpr uint16_t DS5 = 622;
static constexpr uint16_t E5 = 659;
static constexpr uint16_t F5 = 698;
static constexpr uint16_t FS5 = 740;
static constexpr uint16_t G5 = 784;
static constexpr uint16_t GS5 = 831;
static constexpr uint16_t A5 = 880;
static constexpr uint16_t AS5 = 932;
static constexpr uint16_t B5 = 988;

static constexpr uint16_t C6 = 1047;
static constexpr uint16_t CS6 = 1109;
static constexpr uint16_t D6 = 1175;
static constexpr uint16_t DS6 = 1245;
static constexpr uint16_t E6 = 1319;
static constexpr uint16_t F6 = 1397;
static constexpr uint16_t FS6 = 1480;
static constexpr uint16_t G6 = 1568;
static constexpr uint16_t GS6 = 1661;
static constexpr uint16_t A6 = 1760;
static constexpr uint16_t AS6 = 1865;
static constexpr uint16_t B6 = 1976;

enum class Duration {
  Whole = 1,
  Half = 2,
  Quarter = 4,
  Eighth = 8,
  Sixteenth = 16,
};

struct Note {
  uint16_t frequency;
  Duration duration;
};

struct Riff {
  size_t length;
  const Note *notes;
};

struct Song {
  size_t length;
  const Riff *riffs;
};
