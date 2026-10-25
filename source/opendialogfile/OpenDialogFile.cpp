#include "OpenDialogFile.hpp"
#include <windows.h>
#include <commdlg.h>

namespace fileDialog {

std::filesystem::path OpenFileDialog() {
  OPENFILENAMEW ofn; /// Структура для хранения параметров диалогового окна
  wchar_t szFile[MAX_PATH] = {0}; /// Массив для хранения пути к файлу

  ZeroMemory(&ofn, sizeof(ofn)); /// Обнуление структуры
  ofn.lStructSize = sizeof(ofn); /// Размер структуры
  ofn.hwndOwner = NULL;          /// Владелец окна
  ofn.lpstrFile = szFile;        /// Пусть к файлу
  ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t); /// Максимальный размер пути
  ofn.lpstrFilter =
      L"Markdown & Text Files (*.md;*.txt)\0*.md;*.markdown;*.txt\0Markdown Files (*.md)\0*.md;*.markdown\0Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
  ofn.nFilterIndex = 1;
  ofn.Flags = OFN_PATHMUSTEXIST   /// Путь должен существовать
              | OFN_FILEMUSTEXIST /// Файл должен существовать
              | OFN_NOCHANGEDIR;  /// Не менять текущую директорию

  if (GetOpenFileNameW(&ofn)) {
    return std::filesystem::path(szFile);
  }
  return {};
}

} // namespace fileDialog