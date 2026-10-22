#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "../Activity.h"
#include "ReadingStatsStore.h"
#include "util/ButtonNavigator.h"

// Year-in-review for reading stats: totals, goal days, longest streak, best
// day, top book, favourite weekday and a monthly bar chart for
// one calendar year. Previous/Next switch between years that have reading data.
class ReadingWrappedActivity final : public Activity {
  struct YearSummary {
    uint64_t totalReadingMs = 0;
    uint32_t daysRead = 0;
    uint32_t goalDays = 0;
    uint32_t longestGoalStreak = 0;
    uint32_t booksRead = 0;
    uint32_t booksFinished = 0;
    uint32_t bestDayOrdinal = 0;
    uint64_t bestDayMs = 0;
    const ReadingBookStats* topBook = nullptr;
    uint64_t topBookMs = 0;
    int topWeekday = -1;  // Monday = 0
    std::array<uint64_t, 12> monthMs{};
  };

  ButtonNavigator buttonNavigator;
  std::vector<int> years;
  int yearIndex = 0;
  YearSummary summary;

  void loadYears();
  void computeSummary();
  void selectYear(int index);

 public:
  explicit ReadingWrappedActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReadingWrapped", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  uint8_t getUiTransitionRefreshWeight() const override { return UI_TRANSITION_REFRESH_WEIGHT_DENSE; }
};
