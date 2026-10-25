#pragma once

#include <filesystem>

namespace fileDialog {

/// @brief Функция вызова диалогового окна выбора файла
/// @return Путь к выбранному файлу
std::filesystem::path OpenFileDialog();

} // namespace fileDialog