#pragma once

#include "Menu.hpp"
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace scanfile {
struct MarkdownStats;
}

namespace userdata {
struct UserData;
}

namespace app {

/// @brief Контроллер консольного приложения (управление меню, состоянием и сценариями)
class Application {
public:
  Application();
  ~Application() = default;

  /// @brief Запуск цикла меню приложения
  void run();

private:
  /// @brief Инициализация и привязка пунктов меню
  void setupMenu();

  // Обработчики сценариев работы приложения (действия пунктов меню)
  void onSelectAndValidateMarkdown();
  void onAnalyzeMarkdown();
  void onSelectUserDataFile();
  void onManualUserDataInput();
  void onGenerateRandomUserData();

  // Вспомогательные методы отображения и форматирования результатов
  static void printMarkdownStats(const std::filesystem::path &filePath,
                                 const scanfile::MarkdownStats &stats);

  static void printUserDataValidation(const userdata::UserData &input,
                                      const userdata::UserData &normalized,
                                      const std::vector<std::string> &errors);

  static size_t promptCount(std::string_view prompt, size_t defaultValue);

  std::filesystem::path currentMarkdownPath_;
  std::filesystem::path currentUserDataPath_;
  menu::ConsoleMenu menu_;
};

} // namespace app
