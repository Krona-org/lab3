#include "scanfile.h"

#include <io.h>
#include <windows.h>
#include <memoryapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <regex>
#include <sstream>
#include <string_view>
#include <thread>
#include <vector>

namespace scanfile {
namespace {

/// @brief Ограничивает Working Set процесса, чтобы потребление оперативной памяти не превышало 4 ГБ
void limitWorkingSet() {
  HANDLE hProcess = GetCurrentProcess();
  // Устанавливаем рабочий диапазон Working Set: минимум 64 МБ, максимум ~3.5 ГБ (с запасом под 4 ГБ)
  SIZE_T minWs = 64 * 1024 * 1024;
  SIZE_T maxWs = static_cast<SIZE_T>(3ULL * 1024 * 1024 * 1024 + 512 * 1024 * 1024);
  SetProcessWorkingSetSize(hProcess, minWs, maxWs);
}

/// @brief Сообщает Windows, что страницы в данном диапазоне адресов можно освободить из RAM
inline void discardRange(const void *ptr, size_t length) {
  if (!ptr || length == 0) return;
  // Сбрасываем блокировку страниц (если была) и возвращаем страницы ОС
  VirtualUnlock(const_cast<LPVOID>(ptr), length);
}

/// @brief Шаблоны регулярных выражений
namespace re {
const std::regex header{R"(^(#{1,6})\s+(.*))"};
const std::regex bulletList{R"(^\s*[-*+]\s+(.*))"};
const std::regex numberList{R"(^\s*(\d+)\.\s+(.*))"};
const std::regex codeBlock{R"(^```(\w*))"};
const std::regex inlineLink{R"(\[([^\]]+)\]\(([^)]+)\))"};
const std::regex html{
    R"(<\/?(html|head|body|div|p|script|style|table|tr|td|span|h[1-6])\b[^>]*>)",
    std::regex::icase};
const std::regex latex{
    R"(\\(documentclass|begin|section|subsection|usepackage)\b)"};
const std::regex rawCode{R"(\w+\(.*\)|;\s*$)"};
} // namespace re

/// @brief Дескриптор файла и его проекции (не занимает RAM сам по себе)
class FileMappingHandle {
public:
  explicit FileMappingHandle(const std::filesystem::path &path) {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    granularity_ = sysInfo.dwAllocationGranularity ? sysInfo.dwAllocationGranularity : 65536;

    file_ = CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file_ == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(file_, &sz) || sz.QuadPart == 0) return;
    fileSize_ = static_cast<uint64_t>(sz.QuadPart);
    mapping_ = CreateFileMappingW(file_, nullptr, PAGE_READONLY, 0, 0, nullptr);
  }

  ~FileMappingHandle() {
    if (mapping_) CloseHandle(mapping_);
    if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
  }

  FileMappingHandle(const FileMappingHandle &) = delete;
  FileMappingHandle &operator=(const FileMappingHandle &) = delete;

  bool ok() const { return mapping_ != nullptr && fileSize_ > 0; }
  uint64_t size() const { return fileSize_; }
  DWORD granularity() const { return granularity_; }
  HANDLE mapping() const { return mapping_; }

private:
  HANDLE file_{INVALID_HANDLE_VALUE};
  HANDLE mapping_{nullptr};
  uint64_t fileSize_{0};
  DWORD granularity_{65536};
};

/// @brief Временное скользящее окно в памяти (128 МБ), освобождаемое через UnmapViewOfFile
class MappedWindow {
public:
  MappedWindow(HANDLE mapping, DWORD granularity, uint64_t offset, size_t length, uint64_t totalFileSize) {
    if (!mapping || offset >= totalFileSize) return;
    uint64_t alignedOffset = (offset / granularity) * granularity;
    size_t delta = static_cast<size_t>(offset - alignedOffset);
    size_t mapSize = length + delta;
    if (alignedOffset + mapSize > totalFileSize) {
      mapSize = static_cast<size_t>(totalFileSize - alignedOffset);
    }

    ULARGE_INTEGER ul;
    ul.QuadPart = alignedOffset;
    base_ = MapViewOfFile(mapping, FILE_MAP_READ, ul.HighPart, ul.LowPart, mapSize);
    if (base_) {
      const char *startPtr = static_cast<const char *>(base_) + delta;
      size_t actualLen = mapSize - delta;
      data_ = std::string_view(startPtr, actualLen);
    }
  }

  ~MappedWindow() {
    if (base_) {
      UnmapViewOfFile(base_);
    }
  }

  MappedWindow(const MappedWindow &) = delete;
  MappedWindow &operator=(const MappedWindow &) = delete;

  bool ok() const { return base_ != nullptr; }
  std::string_view data() const { return data_; }

private:
  void *base_{nullptr};
  std::string_view data_;
};

/// @brief Извлекает очередную строку без аллокаций
inline bool nextLine(std::string_view &text, std::string_view &line) {
  if (text.empty())
    return false;
  const void *res = memchr(text.data(), '\n', text.size());
  size_t eol =
      res ? (static_cast<const char *>(res) - text.data()) : text.size();
  line = text.substr(0, eol);
  text.remove_prefix(eol < text.size() ? eol + 1 : eol);
  if (!line.empty() && line.back() == '\r') {
    line.remove_suffix(1);
  }
  return true;
}

inline bool isBlank(std::string_view s) {
  return s.find_first_not_of(" \t\r\v\f") == std::string_view::npos;
}

inline bool isWordChar(char c) {
  return (static_cast<unsigned char>(c) >= 'a' &&
          static_cast<unsigned char>(c) <= 'z') ||
         (static_cast<unsigned char>(c) >= 'A' &&
          static_cast<unsigned char>(c) <= 'Z') ||
         (static_cast<unsigned char>(c) >= '0' &&
          static_cast<unsigned char>(c) <= '9') ||
         c == '_';
}

inline bool isCodeFence(std::string_view s) {
  if (s.size() < 3 || s[0] != '`' || s[1] != '`' || s[2] != '`')
    return false;
  for (size_t i = 3; i < s.size(); ++i) {
    char c = s[i];
    if (!isWordChar(c)) {
      return std::regex_match(s.data(), s.data() + s.size(), re::codeBlock);
    }
  }
  return true;
}

inline bool isHeader(std::string_view s) {
  if (s.empty() || s[0] != '#')
    return false;
  size_t hashes = 0;
  while (hashes < s.size() && s[hashes] == '#') {
    ++hashes;
  }
  if (hashes < 1 || hashes > 6 || hashes >= s.size())
    return false;
  char next = s[hashes];
  if (next != ' ' && next != '\t')
    return false;
  return std::regex_match(s.data(), s.data() + s.size(), re::header);
}

inline bool isListItem(std::string_view s) {
  size_t i = s.find_first_not_of(" \t");
  if (i == std::string_view::npos)
    return false;
  char c = s[i];
  if (c == '-' || c == '*' || c == '+') {
    if (i + 1 < s.size() && (s[i + 1] == ' ' || s[i + 1] == '\t')) {
      return std::regex_match(s.data(), s.data() + s.size(), re::bulletList);
    }
    return false;
  }
  if (c >= '0' && c <= '9') {
    size_t d = i + 1;
    while (d < s.size() && s[d] >= '0' && s[d] <= '9') {
      ++d;
    }
    if (d < s.size() && s[d] == '.' && d + 1 < s.size() &&
        (s[d + 1] == ' ' || s[d + 1] == '\t')) {
      return std::regex_match(s.data(), s.data() + s.size(), re::numberList);
    }
    return false;
  }
  return false;
}

inline bool hasLink(std::string_view s) {
  size_t openBracket = s.find('[');
  if (openBracket == std::string_view::npos)
    return false;
  size_t linkJoin = s.find("](", openBracket + 1);
  if (linkJoin == std::string_view::npos)
    return false;
  return std::regex_search(s.data(), s.data() + s.size(), re::inlineLink);
}

inline bool hasHtml(std::string_view s) {
  size_t p = s.find('<');
  while (p != std::string_view::npos) {
    if (p + 1 < s.size()) {
      char c = s[p + 1];
      if (c == '/' && p + 2 < s.size()) {
        char c2 = s[p + 2];
        if ((c2 >= 'a' && c2 <= 'z') || (c2 >= 'A' && c2 <= 'Z')) {
          return std::regex_search(s.data(), s.data() + s.size(), re::html);
        }
      } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
        return std::regex_search(s.data(), s.data() + s.size(), re::html);
      }
    }
    p = s.find('<', p + 1);
  }
  return false;
}

inline bool hasLatex(std::string_view s) {
  size_t p = s.find('\\');
  while (p != std::string_view::npos) {
    if (p + 1 < s.size()) {
      char c = s[p + 1];
      if (c == 'd' || c == 'b' || c == 's' || c == 'u') {
        return std::regex_search(s.data(), s.data() + s.size(), re::latex);
      }
    }
    p = s.find('\\', p + 1);
  }
  return false;
}

inline bool hasRawCode(std::string_view s) {
  size_t last = s.find_last_not_of(" \t\r\v\f");
  if (last != std::string_view::npos && s[last] == ';') {
    return std::regex_search(s.data(), s.data() + s.size(), re::rawCode);
  }
  size_t p = s.find('(');
  while (p != std::string_view::npos) {
    if (p > 0 && isWordChar(s[p - 1])) {
      return std::regex_search(s.data(), s.data() + s.size(), re::rawCode);
    }
    p = s.find('(', p + 1);
  }
  return false;
}

/// @brief Консольный анимированный прогресс-бар
class ProgressBar {
public:
  ProgressBar(uint64_t totalBytes, std::string_view title, unsigned int threads)
      : totalBytes_(totalBytes), title_(title), threads_(threads),
        enabled_(totalBytes >= 1024 * 1024),
        startTime_(std::chrono::steady_clock::now()),
        isTty_(_isatty(_fileno(stdout))) {
    if (enabled_) {
      active_.store(true);
      monitorThread_ = std::thread([this]() { run(); });
    }
  }

  ~ProgressBar() { stop(); }

  void add(uint64_t bytes) {
    if (enabled_) {
      processedBytes_.fetch_add(bytes, std::memory_order_relaxed);
    }
  }

  void stop() {
    if (enabled_ && active_.exchange(false)) {
      if (monitorThread_.joinable()) {
        monitorThread_.join();
      }
      render(true);
      std::cout << "\n";
    }
  }

private:
  void run() {
    while (active_.load()) {
      render(false);
      std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
  }

  void render(bool finish) {
    uint64_t current =
        finish ? totalBytes_ : processedBytes_.load(std::memory_order_relaxed);
    if (current > totalBytes_)
      current = totalBytes_;

    double progress =
        totalBytes_ > 0 ? static_cast<double>(current) / totalBytes_ : 1.0;
    auto now = std::chrono::steady_clock::now();
    double elapsedSec = std::chrono::duration<double>(now - startTime_).count();
    double speedMBs =
        (elapsedSec > 0.05)
            ? (static_cast<double>(current) / (1024.0 * 1024.0)) / elapsedSec
            : 0.0;
    double remainingSec =
        (speedMBs > 0.01 && progress < 0.999)
            ? (static_cast<double>(totalBytes_ - current) / (1024.0 * 1024.0)) /
                  speedMBs
            : 0.0;

    int barWidth = 30;
    int filled = static_cast<int>(progress * barWidth);
    if (filled > barWidth)
      filled = barWidth;

    std::ostringstream ss;
    ss << title_ << " [";
    for (int i = 0; i < barWidth; ++i) {
      if (i < filled)
        ss << "=";
      else if (i == filled)
        ss << ">";
      else
        ss << " ";
    }
    ss << "] " << std::fixed << std::setprecision(1) << (progress * 100.0)
       << "% | " << std::setprecision(2)
       << (static_cast<double>(current) / (1024.0 * 1024.0 * 1024.0)) << " / "
       << (static_cast<double>(totalBytes_) / (1024.0 * 1024.0 * 1024.0))
       << " ГБ | " << std::setprecision(1) << speedMBs << " МБ/с | ";

    if (finish) {
      ss << "Завершено за " << std::setprecision(1) << elapsedSec << "с";
    } else {
      ss << "ETA: " << static_cast<int>(remainingSec) << "с [" << threads_
         << " потоков]";
    }

    if (isTty_) {
      std::cout << "\r" << ss.str() << std::flush;
    } else if (finish) {
      std::cout << ss.str() << std::endl;
    }
  }

  uint64_t totalBytes_{0};
  std::string title_;
  unsigned int threads_{1};
  std::atomic<uint64_t> processedBytes_{0};
  std::atomic<bool> active_{false};
  std::chrono::steady_clock::time_point startTime_;
  bool isTty_{false};
  bool enabled_{false};
  std::thread monitorThread_;
};

struct Chunk {
  size_t index{0};
  const char *start{nullptr};
  const char *end{nullptr};
  size_t startLineNumber{1};
  size_t fenceCount{0};
  bool startInCodeBlock{false};
  MarkdownStats stats;

  // Данные для склейки элементов на стыках чанков
  bool startsWithBlank{false};
  bool endsWithBlank{false};
  std::string leadingParagraph;
  std::string trailingParagraph;
  bool startsWithList{false};
  bool endsWithList{false};
};

/// @brief Нарезает файл на выровненные по \n блоки
std::vector<Chunk> makeChunks(std::string_view data, size_t numChunks) {
  std::vector<Chunk> chunks;
  chunks.reserve(numChunks);
  const char *base = data.data();
  size_t total = data.size();
  size_t nominal = total / numChunks;

  const char *curr = base;
  const char *fileEnd = base + total;

  for (size_t i = 0; i < numChunks; ++i) {
    const char *chunkStart = curr;
    const char *chunkEnd = fileEnd;
    if (i + 1 < numChunks) {
      const char *target = base + (i + 1) * nominal;
      if (target < fileEnd) {
        const void *nl = memchr(target, '\n', fileEnd - target);
        if (nl) {
          chunkEnd = static_cast<const char *>(nl) + 1;
        }
      }
    }
    chunks.push_back({i,
                      chunkStart,
                      chunkEnd,
                      1,
                      0,
                      false,
                      {},
                      false,
                      false,
                      "",
                      "",
                      false,
                      false});
    curr = chunkEnd;
  }
  return chunks;
}

/// @brief Быстрый параллельный расчет четности блоков кода (```) для каждого
/// чанка
void computeCodeFenceParity(std::vector<Chunk> &chunks,
                            unsigned int numThreads) {
  std::vector<std::thread> workers;
  std::atomic<size_t> chunkIdx{0};

  auto worker = [&]() {
    while (true) {
      size_t idx = chunkIdx.fetch_add(1, std::memory_order_relaxed);
      if (idx >= chunks.size())
        break;
      auto &c = chunks[idx];
      const char *p = c.start;
      const char *end = c.end;
      size_t count = 0;

      // Проверка первой строки чанка
      if (end - p >= 3 && p[0] == '`' && p[1] == '`' && p[2] == '`') {
        const char *eol = static_cast<const char *>(memchr(p, '\n', end - p));
        size_t len = eol ? (eol - p) : (end - p);
        std::string_view line(p, len);
        if (!line.empty() && line.back() == '\r')
          line.remove_suffix(1);
        if (isCodeFence(line))
          ++count;
      }

      while (p < end) {
        const void *res = memchr(p, '\n', end - p);
        if (!res)
          break;
        const char *nl = static_cast<const char *>(res);
        p = nl + 1;
        if (end - p >= 3 && p[0] == '`' && p[1] == '`' && p[2] == '`') {
          const char *eol = static_cast<const char *>(memchr(p, '\n', end - p));
          size_t len = eol ? (eol - p) : (end - p);
          std::string_view line(p, len);
          if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
          if (isCodeFence(line))
            ++count;
        }
      }
      c.fenceCount = count;
    }
  };

  for (unsigned int i = 0; i < numThreads; ++i) {
    workers.emplace_back(worker);
  }
  for (auto &w : workers) {
    w.join();
  }

  // Расчет четности перед каждым чанком
  size_t totalFences = 0;
  for (auto &c : chunks) {
    c.startInCodeBlock = (totalFences % 2 != 0);
    totalFences += c.fenceCount;
  }
}

} // namespace

/// @brief Метод проверки на соответствие синтаксису Markdown
/// @param path Путь к проверяемому файлу
/// @return true, если файл соответствует синтаксису Markdown, иначе false
bool Scanner::checkValidation(const std::filesystem::path &path) {
  if (path.extension() != ".md" && path.extension() != ".markdown") {
    std::cout << "[Ошибка валидации]: Неверное расширение файла ("
              << path.extension().string()
              << "). Ожидается .md или .markdown!\n";
    return false;
  }

  FileMappingHandle file(path);
  if (!file.ok()) {
    std::cout << "[Ошибка валидации]: Не удалось открыть файл для чтения: "
              << path.string() << "\n";
    return false;
  }

  limitWorkingSet();

  unsigned int numThreads = std::thread::hardware_concurrency();
  if (numThreads == 0)
    numThreads = 8;

  ProgressBar bar(file.size(), "Валидация Markdown", numThreads);

  std::atomic<bool> hasError{false};
  std::atomic<bool> hasAnyMarkdownGlobal{false};
  std::mutex errorMutex;
  std::string firstErrorMessage;

  bool currentInCodeBlock = false;
  uint64_t fileOffset = 0;
  const uint64_t totalSize = file.size();
  constexpr size_t kWindowSize = 128ULL * 1024 * 1024; // Окно 128 МБ

  while (fileOffset < totalSize && !hasError.load(std::memory_order_relaxed)) {
    size_t targetMap = static_cast<size_t>(std::min<uint64_t>(kWindowSize, totalSize - fileOffset));
    MappedWindow win(file.mapping(), file.granularity(), fileOffset, targetMap, totalSize);
    if (!win.ok()) {
      std::cout << "[Ошибка]: Не удалось отобразить сегмент файла.\n";
      return false;
    }

    std::string_view winData = win.data();
    size_t actualLength = winData.size();

    // Если это не конец файла, выравниваем конец окна по последнему \n
    if (fileOffset + actualLength < totalSize) {
      size_t lastNl = winData.rfind('\n');
      if (lastNl != std::string_view::npos) {
        actualLength = lastNl + 1;
        winData = winData.substr(0, actualLength);
      }
    }

    // Нарезаем окно на чанки для потоков
    size_t chunksCount = (numThreads == 1 || actualLength < 1024 * 1024) ? 1 : numThreads;
    auto chunks = makeChunks(winData, chunksCount);

    // Расчет четности code fence для чанков окна с учетом состояния от предыдущего окна
    computeCodeFenceParity(chunks, static_cast<unsigned int>(chunksCount));
    if (currentInCodeBlock) {
      for (auto &c : chunks) {
        c.startInCodeBlock = !c.startInCodeBlock;
      }
    }

    // Обновляем состояние блока кода для следующего окна
    size_t windowFences = 0;
    for (const auto &c : chunks) {
      windowFences += c.fenceCount;
    }
    if (windowFences % 2 != 0) {
      currentInCodeBlock = !currentInCodeBlock;
    }

    // Параллельная проверка чанков окна
    std::vector<std::thread> workers;
    std::atomic<size_t> chunkIdx{0};

    auto worker = [&]() {
      while (!hasError.load(std::memory_order_relaxed)) {
        size_t idx = chunkIdx.fetch_add(1, std::memory_order_relaxed);
        if (idx >= chunks.size())
          break;
        auto &c = chunks[idx];

        std::string_view text(c.start, c.end - c.start);
        bool inCodeBlock = c.startInCodeBlock;
        bool hasAnyMarkdown = false;
        size_t lineNumber = 0;
        std::string_view line;

        auto reportError = [&](const char *reason, std::string_view l) {
          if (!hasError.exchange(true)) {
            std::lock_guard<std::mutex> lock(errorMutex);
            std::ostringstream ss;
            ss << "[Ошибка валидации]: " << reason << ": \"" << l << "\"\n";
            firstErrorMessage = ss.str();
          }
        };

        while (!hasError.load(std::memory_order_relaxed) &&
               nextLine(text, line)) {
          ++lineNumber;

          if (isCodeFence(line)) {
            inCodeBlock = !inCodeBlock;
            hasAnyMarkdown = true;
            continue;
          }
          if (inCodeBlock || isBlank(line)) {
            continue;
          }

          if (hasHtml(line)) {
            reportError("обнаружен тег чужого языка разметки HTML", line);
            break;
          }
          if (hasLatex(line)) {
            reportError("обнаружены команды чужого языка разметки LaTeX", line);
            break;
          }

          if (isHeader(line) || isListItem(line) || hasLink(line)) {
            hasAnyMarkdown = true;
            continue;
          }

          if (hasRawCode(line)) {
            reportError("обнаружен программный код вне блока ```", line);
            break;
          }
        }

        if (hasAnyMarkdown) {
          hasAnyMarkdownGlobal.store(true, std::memory_order_relaxed);
        }
      }
    };

    for (size_t i = 0; i < chunksCount; ++i) {
      workers.emplace_back(worker);
    }
    for (auto &w : workers) {
      w.join();
    }

    bar.add(actualLength);
    fileOffset += actualLength;

    // При выходе из блока win уничтожается, вызывая UnmapViewOfFile и мгновенно освобождая RAM!
  }

  bar.stop();

  if (hasError.load()) {
    std::cout << firstErrorMessage;
    return false;
  }

  if (currentInCodeBlock) {
    std::cout << "Блок кода (```) не был закрыт до конца файла!\n";
    return false;
  }

  if (!hasAnyMarkdownGlobal.load()) {
    std::cout << "Файл не содержит ни одного элемента разметки Markdown "
                 "(заголовки, списки, ссылки, блоки кода)!\n";
    return false;
  }

  return true;
}

/// @brief Анализ содержимого файла (многопоточный MMF с динамической
/// балансировкой и скользящим окном памяти)
/// @param path Путь к Markdown-файлу
/// @return Структура со статистикой и найденными элементами MarkdownStats
MarkdownStats Scanner::analyzeMarkdown(const std::filesystem::path &path) {
  constexpr size_t kMaxGlobalStored = 10000;
  MarkdownStats totalStats;

  FileMappingHandle file(path);
  if (!file.ok())
    return totalStats;

  limitWorkingSet();

  unsigned int numThreads = std::thread::hardware_concurrency();
  if (numThreads == 0)
    numThreads = 8;

  ProgressBar bar(file.size(), "Анализ Markdown", numThreads);

  std::atomic<size_t> globalStoredParagraphs{0};
  std::atomic<size_t> globalStoredHeaders{0};
  std::atomic<size_t> globalStoredLists{0};

  bool currentInCodeBlock = false;
  std::string lastTrailingParagraph;
  bool lastEndsWithList = false;

  uint64_t fileOffset = 0;
  const uint64_t totalSize = file.size();
  constexpr size_t kWindowSize = 128ULL * 1024 * 1024; // Окно 128 МБ

  while (fileOffset < totalSize) {
    size_t targetMap = static_cast<size_t>(std::min<uint64_t>(kWindowSize, totalSize - fileOffset));
    MappedWindow win(file.mapping(), file.granularity(), fileOffset, targetMap, totalSize);
    if (!win.ok()) {
      break;
    }

    std::string_view winData = win.data();
    size_t actualLength = winData.size();

    // Выравниваем конец окна по последнему \n
    if (fileOffset + actualLength < totalSize) {
      size_t lastNl = winData.rfind('\n');
      if (lastNl != std::string_view::npos) {
        actualLength = lastNl + 1;
        winData = winData.substr(0, actualLength);
      }
    }

    // Нарезаем окно на чанки для потоков
    size_t chunksCount = (numThreads == 1 || actualLength < 1024 * 1024) ? 1 : numThreads;
    auto chunks = makeChunks(winData, chunksCount);

    // Расчет четности code fence для чанков окна
    computeCodeFenceParity(chunks, static_cast<unsigned int>(chunksCount));
    if (currentInCodeBlock) {
      for (auto &c : chunks) {
        c.startInCodeBlock = !c.startInCodeBlock;
      }
    }

    size_t windowFences = 0;
    for (const auto &c : chunks) {
      windowFences += c.fenceCount;
    }
    if (windowFences % 2 != 0) {
      currentInCodeBlock = !currentInCodeBlock;
    }

    std::vector<std::thread> workers;
    std::atomic<size_t> chunkIdx{0};

    auto worker = [&]() {
      while (true) {
        size_t idx = chunkIdx.fetch_add(1, std::memory_order_relaxed);
        if (idx >= chunks.size())
          break;
        auto &c = chunks[idx];

        std::string_view text(c.start, c.end - c.start);
        bool inCodeBlock = c.startInCodeBlock;
        bool inList = false;
        bool storingList = false;
        std::string paragraph;

        auto flushParagraph = [&]() {
          if (paragraph.empty())
            return;
          ++c.stats.paragraphsCount;
          if (globalStoredParagraphs.fetch_add(1, std::memory_order_relaxed) < kMaxGlobalStored) {
            c.stats.paragraphs.push_back(std::move(paragraph));
          }
          paragraph.clear();
        };

        std::string_view line;
        bool isFirstLine = true;

        while (nextLine(text, line)) {
          if (isCodeFence(line)) {
            flushParagraph();
            inCodeBlock = !inCodeBlock;
            inList = false;
            isFirstLine = false;
            continue;
          }
          if (inCodeBlock) {
            isFirstLine = false;
            continue;
          }

          if (isBlank(line)) {
            flushParagraph();
            inList = false;
            if (isFirstLine)
              c.startsWithBlank = true;
            isFirstLine = false;
            continue;
          }

          if (isHeader(line)) {
            flushParagraph();
            inList = false;
            ++c.stats.headersCount;
            if (globalStoredHeaders.fetch_add(1, std::memory_order_relaxed) < kMaxGlobalStored) {
              c.stats.headers.emplace_back(line);
            }
            isFirstLine = false;
            continue;
          }

          if (isListItem(line)) {
            flushParagraph();
            if (isFirstLine) {
              c.startsWithList = true;
            }
            if (!inList) {
              inList = true;
              ++c.stats.listsCount;
              storingList = (globalStoredLists.fetch_add(1, std::memory_order_relaxed) < kMaxGlobalStored);
              if (storingList)
                c.stats.lists.emplace_back();
            }
            ++c.stats.listItemsCount;
            if (storingList && c.stats.lists.back().size() < 100) {
              c.stats.lists.back().emplace_back(line);
            }
            isFirstLine = false;
            continue;
          }

          // Обычный абзац
          inList = false;
          if (isFirstLine && !c.startsWithBlank) {
            c.leadingParagraph = std::string(line);
          }
          if (!paragraph.empty())
            paragraph += ' ';
          paragraph += line;
          isFirstLine = false;
        }

        if (!paragraph.empty()) {
          c.trailingParagraph = paragraph;
        }
        c.endsWithList = inList;
        flushParagraph();
      }
    };

    for (size_t i = 0; i < chunksCount; ++i) {
      workers.emplace_back(worker);
    }
    for (auto &w : workers) {
      w.join();
    }

    // Объединяем статистику из чанков текущего окна и корректируем стыки
    for (size_t i = 0; i < chunks.size(); ++i) {
      auto &c = chunks[i];

      // Склейка абзаца на стыке чанков
      if (i == 0) {
        if (!lastTrailingParagraph.empty() && !c.leadingParagraph.empty()) {
          if (totalStats.paragraphsCount > 0)
            --totalStats.paragraphsCount;
        }
      } else if (!chunks[i - 1].trailingParagraph.empty() && !c.leadingParagraph.empty()) {
        if (totalStats.paragraphsCount > 0)
          --totalStats.paragraphsCount;
      }

      // Склейка списка на стыке чанков
      if (i == 0) {
        if (lastEndsWithList && c.startsWithList) {
          if (totalStats.listsCount > 0)
            --totalStats.listsCount;
        }
      } else if (chunks[i - 1].endsWithList && c.startsWithList) {
        if (totalStats.listsCount > 0)
          --totalStats.listsCount;
      }

      totalStats.headersCount += c.stats.headersCount;
      totalStats.paragraphsCount += c.stats.paragraphsCount;
      totalStats.listsCount += c.stats.listsCount;
      totalStats.listItemsCount += c.stats.listItemsCount;

      for (auto &h : c.stats.headers) {
        if (totalStats.headers.size() < kMaxGlobalStored)
          totalStats.headers.push_back(std::move(h));
      }
      for (auto &p : c.stats.paragraphs) {
        if (totalStats.paragraphs.size() < kMaxGlobalStored)
          totalStats.paragraphs.push_back(std::move(p));
      }
      for (auto &l : c.stats.lists) {
        if (totalStats.lists.size() < kMaxGlobalStored)
          totalStats.lists.push_back(std::move(l));
      }
    }

    if (!chunks.empty()) {
      lastTrailingParagraph = chunks.back().trailingParagraph;
      lastEndsWithList = chunks.back().endsWithList;
    }

    bar.add(actualLength);
    fileOffset += actualLength;

    // При выходе из итерации win уничтожается -> UnmapViewOfFile освобождает 128 МБ из RAM!
  }

  bar.stop();
  return totalStats;
}

} // namespace scanfile
