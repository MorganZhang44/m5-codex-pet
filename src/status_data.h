#pragma once

#include "pet_state.h"

struct Usage {
  // -1 represents unavailable; supplied values are USED percent, 0..100.
  double fiveHour = -1;
  double sevenDay = -1;
  bool stale = false;

  bool operator==(const Usage& other) const {
    return fiveHour == other.fiveHour && sevenDay == other.sevenDay &&
           stale == other.stale;
  }
};

enum class LastResult { NONE, SUCCESS, FAILED };

struct StatusData {
  PetState state = PetState::SLEEP;
  LastResult lastResult = LastResult::NONE;
  Usage usage;
};
