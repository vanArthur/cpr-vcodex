#include "ReadingWrappedActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "AppMetricCard.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/ReadingStatsAnalytics.h"
#include "util/TimeUtils.h"

namespace {
constexpr int CARD_HEIGHT = 76;
constexpr int CARD_GAP = 10;
constexpr int SUB_HEADER_HEIGHT = 34;
constexpr int SECTION_GAP = 8;
constexpr int MIN_CHART_HEIGHT = 90;
constexpr int CARD_COUNT = 8;

constexpr StrId WEEKDAY_NAMES[7] = {
    StrId::STR_WEEKDAY_MONDAY, StrId::STR_WEEKDAY_TUESDAY,  StrId::STR_WEEKDAY_WEDNESDAY, StrId::STR_WEEKDAY_THURSDAY,
    StrId::STR_WEEKDAY_FRIDAY, StrId::STR_WEEKDAY_SATURDAY, StrId::STR_WEEKDAY_SUNDAY};

int yearOfDayOrdinal(const uint32_t dayOrdinal) {
  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  if (dayOrdinal == 0 || !TimeUtils::getDateFromDayOrdinal(dayOrdinal, year, month, day)) {
    return 0;
  }
  return year;
}

int yearOfTimestamp(const uint32_t epochSeconds) {
  if (!TimeUtils::isClockValid(epochSeconds)) {
    return 0;
  }
  return yearOfDayOrdinal(TimeUtils::getLocalDayOrdinal(epochSeconds));
}

std::string formatShortDay(const uint32_t dayOrdinal) {
  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  if (dayOrdinal == 0 || !TimeUtils::getDateFromDayOrdinal(dayOrdinal, year, month, day)) {
    return "";
  }
  char buffer[8];
  snprintf(buffer, sizeof(buffer), "%02u/%02u", day, month);
  return buffer;
}

void drawCard(GfxRenderer& renderer, const Rect& rect, const char* label, const std::string& value) {
  AppMetricCard::Options options;
  AppMetricCard::draw(renderer, rect, label, value, options);
}

void drawMonthChart(GfxRenderer& renderer, const Rect& rect, const std::array<uint64_t, 12>& monthMs) {
  const uint64_t maxMs = *std::max_element(monthMs.begin(), monthMs.end());
  const int labelHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int barAreaHeight = rect.height - labelHeight - 4;
  const int slotWidth = rect.width / 12;
  const int barWidth = std::max(4, slotWidth - 10);

  renderer.drawLine(rect.x, rect.y + barAreaHeight, rect.x + rect.width, rect.y + barAreaHeight, true);
  for (int month = 0; month < 12; ++month) {
    const int slotX = rect.x + month * slotWidth;
    if (maxMs > 0 && monthMs[month] > 0) {
      const int barHeight =
          std::max(2, static_cast<int>((monthMs[month] * static_cast<uint64_t>(barAreaHeight)) / maxMs));
      renderer.fillRect(slotX + (slotWidth - barWidth) / 2, rect.y + barAreaHeight - barHeight, barWidth, barHeight,
                        true);
    }
    char label[4];
    snprintf(label, sizeof(label), "%d", month + 1);
    const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, label);
    renderer.drawText(SMALL_FONT_ID, slotX + (slotWidth - labelWidth) / 2, rect.y + barAreaHeight + 4, label);
  }
}
}  // namespace

void ReadingWrappedActivity::loadYears() {
  years.clear();
  for (const auto& day : READING_STATS.getReadingDays()) {
    const int year = yearOfDayOrdinal(day.dayOrdinal);
    if (year != 0 && day.readingMs > 0 && (years.empty() || years.back() != year)) {
      years.push_back(year);
    }
  }
  std::sort(years.begin(), years.end());
  years.erase(std::unique(years.begin(), years.end()), years.end());

  yearIndex = years.empty() ? 0 : static_cast<int>(years.size()) - 1;
  const int referenceYear = ReadingStatsAnalytics::getReferenceYear();
  const auto it = std::find(years.begin(), years.end(), referenceYear);
  if (it != years.end()) {
    yearIndex = static_cast<int>(it - years.begin());
  }
}

void ReadingWrappedActivity::computeSummary() {
  summary = YearSummary{};
  if (years.empty()) {
    return;
  }
  const int year = years[static_cast<size_t>(yearIndex)];
  const uint64_t goalMs = getDailyReadingGoalMs();

  std::array<uint64_t, 7> weekdayMs{};
  uint32_t streak = 0;
  uint32_t previousGoalDay = 0;
  // readingDays is sorted by dayOrdinal, so goal streaks can be counted in one pass.
  for (const auto& day : READING_STATS.getReadingDays()) {
    if (day.readingMs == 0 || yearOfDayOrdinal(day.dayOrdinal) != year) {
      continue;
    }
    int dayYear = 0;
    unsigned month = 0;
    unsigned dayOfMonth = 0;
    TimeUtils::getDateFromDayOrdinal(day.dayOrdinal, dayYear, month, dayOfMonth);

    summary.totalReadingMs += day.readingMs;
    summary.daysRead++;
    if (month >= 1 && month <= 12) {
      summary.monthMs[month - 1] += day.readingMs;
    }
    weekdayMs[(day.dayOrdinal + 3U) % 7U] += day.readingMs;
    if (day.readingMs > summary.bestDayMs) {
      summary.bestDayMs = day.readingMs;
      summary.bestDayOrdinal = day.dayOrdinal;
    }
    if (goalMs > 0 && day.readingMs >= goalMs) {
      summary.goalDays++;
      streak = (previousGoalDay != 0 && previousGoalDay + 1 == day.dayOrdinal) ? streak + 1 : 1;
      previousGoalDay = day.dayOrdinal;
      summary.longestGoalStreak = std::max(summary.longestGoalStreak, streak);
    }
  }

  if (summary.daysRead > 0) {
    summary.topWeekday = static_cast<int>(std::max_element(weekdayMs.begin(), weekdayMs.end()) - weekdayMs.begin());
  }

  for (const auto& book : READING_STATS.getBooks()) {
    uint64_t bookMs = 0;
    for (const auto& day : book.readingDays) {
      if (yearOfDayOrdinal(day.dayOrdinal) == year) {
        bookMs += day.readingMs;
      }
    }
    if (bookMs > 0) {
      summary.booksRead++;
      if (bookMs > summary.topBookMs) {
        summary.topBookMs = bookMs;
        summary.topBook = &book;
      }
    }
    if (book.completed && yearOfTimestamp(book.completedAt) == year) {
      summary.booksFinished++;
    }
  }
}

void ReadingWrappedActivity::selectYear(const int index) {
  if (index < 0 || index >= static_cast<int>(years.size()) || index == yearIndex) {
    return;
  }
  yearIndex = index;
  computeSummary();
  requestUpdate();
}

void ReadingWrappedActivity::onEnter() {
  Activity::onEnter();
  loadYears();
  computeSummary();
  requestUpdate();
}

void ReadingWrappedActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Left) {
    selectYear(yearIndex + 1);
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Right) {
    selectYear(yearIndex - 1);
    return;
  }

  buttonNavigator.onPreviousRelease([&]() { selectYear(yearIndex - 1); });
  buttonNavigator.onNextRelease([&]() { selectYear(yearIndex + 1); });
}

void ReadingWrappedActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int sidePadding = metrics.contentSidePadding;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;

  const std::string yearLabel = years.empty() ? std::string() : std::to_string(years[static_cast<size_t>(yearIndex)]);
  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_READING_WRAPPED),
                                      yearLabel.empty() ? nullptr : yearLabel.c_str());

  if (years.empty()) {
    renderer.drawCenteredText(UI_12_FONT_ID, (contentTop + contentBottom) / 2, tr(STR_NO_READING_STATS));
  } else {
    // Cards: 2 columns in portrait, 4 in landscape.
    const int columns = pageWidth > pageHeight ? 4 : 2;
    const int cardWidth = (pageWidth - sidePadding * 2 - CARD_GAP * (columns - 1)) / columns;
    const std::string bestDayLabel = std::string(tr(STR_BEST_DAY)) + " " + formatShortDay(summary.bestDayOrdinal);
    const uint64_t perDayMs = summary.daysRead > 0 ? summary.totalReadingMs / summary.daysRead : 0;

    const char* labels[CARD_COUNT] = {tr(STR_READING_TIME),   tr(STR_DAYS_READ),   tr(STR_GOALS_MET),
                                      tr(STR_LONGEST_STREAK), tr(STR_BOOKS_READ),  tr(STR_BOOKS_FINISHED),
                                      bestDayLabel.c_str(),   tr(STR_PER_READ_DAY)};
    const std::string values[CARD_COUNT] = {ReadingStatsAnalytics::formatDurationHm(summary.totalReadingMs),
                                            std::to_string(summary.daysRead),
                                            std::to_string(summary.goalDays),
                                            std::to_string(summary.longestGoalStreak),
                                            std::to_string(summary.booksRead),
                                            std::to_string(summary.booksFinished),
                                            ReadingStatsAnalytics::formatDurationHm(summary.bestDayMs),
                                            ReadingStatsAnalytics::formatDurationHm(perDayMs)};
    for (int i = 0; i < CARD_COUNT; ++i) {
      const int x = sidePadding + (i % columns) * (cardWidth + CARD_GAP);
      const int y = contentTop + (i / columns) * (CARD_HEIGHT + CARD_GAP);
      drawCard(renderer, Rect{x, y, cardWidth, CARD_HEIGHT}, labels[i], values[i]);
    }

    const int cardRows = (CARD_COUNT + columns - 1) / columns;
    int y = contentTop + cardRows * (CARD_HEIGHT + CARD_GAP) + SECTION_GAP;

    // Top book: duration on the sub-header, title on its own line below.
    const std::string topBookTime =
        summary.topBook ? ReadingStatsAnalytics::formatDurationHm(summary.topBookMs) : std::string(tr(STR_NOT_SET));
    GUI.drawSubHeader(renderer, Rect{0, y, pageWidth, SUB_HEADER_HEIGHT}, tr(STR_TOP_BOOK), topBookTime.c_str());
    y += SUB_HEADER_HEIGHT;
    if (summary.topBook) {
      const std::string& title = summary.topBook->title.empty() ? summary.topBook->path : summary.topBook->title;
      const std::string fitted =
          renderer.truncatedText(UI_12_FONT_ID, title.c_str(), pageWidth - sidePadding * 2, EpdFontFamily::BOLD);
      renderer.drawText(UI_12_FONT_ID, sidePadding, y, fitted.c_str(), true, EpdFontFamily::BOLD);
      y += renderer.getLineHeight(UI_12_FONT_ID) + SECTION_GAP;
    }

    const char* weekdayName = summary.topWeekday >= 0 ? I18N.get(WEEKDAY_NAMES[summary.topWeekday]) : tr(STR_NOT_SET);
    GUI.drawSubHeader(renderer, Rect{0, y, pageWidth, SUB_HEADER_HEIGHT}, tr(STR_FAVORITE_WEEKDAY), weekdayName);
    y += SUB_HEADER_HEIGHT + SECTION_GAP;

    const int chartTop = y + SUB_HEADER_HEIGHT + SECTION_GAP;
    if (contentBottom - chartTop >= MIN_CHART_HEIGHT) {
      GUI.drawSubHeader(renderer, Rect{0, y, pageWidth, SUB_HEADER_HEIGHT}, tr(STR_ANNUAL_READING), nullptr);
      drawMonthChart(renderer, Rect{sidePadding, chartTop, pageWidth - sidePadding * 2, contentBottom - chartTop},
                     summary.monthMs);
    }
  }

  const std::string previousYear = yearIndex > 0 ? std::to_string(years[static_cast<size_t>(yearIndex - 1)]) : "";
  const std::string nextYear =
      yearIndex + 1 < static_cast<int>(years.size()) ? std::to_string(years[static_cast<size_t>(yearIndex + 1)]) : "";
  const auto hints = mappedInput.mapLabels(tr(STR_BACK), "", previousYear.c_str(), nextYear.c_str());
  GUI.drawButtonHints(renderer, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
  renderer.displayBuffer();
}
