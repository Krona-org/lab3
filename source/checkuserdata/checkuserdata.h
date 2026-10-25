#pragma once

#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
  #if defined(CHECKUSERDATA_STATIC)
    #define CHECKUSERDATA_API
  #elif defined(CHECKUSERDATA_EXPORTS)
    #define CHECKUSERDATA_API __declspec(dllexport)
  #else
    #define CHECKUSERDATA_API __declspec(dllimport)
  #endif
#else
  #define CHECKUSERDATA_API
#endif

namespace userdata {

/// @brief Структура для хранения данных пользователя (анкеты)
struct CHECKUSERDATA_API UserData {
  std::string lastName;   ///< Фамилия
  std::string firstName;  ///< Имя
  std::string middleName; ///< Отчество
  std::string birthDate;  ///< Дата рождения
  std::string phone;      ///< Телефон
  std::string email;      ///< Электронная почта

  /// @brief Преобразование в единую строку через пробел
  std::string toString() const;
};

/// @brief Класс для ввода, валидации, нормализации и генерации данных анкеты
class CHECKUSERDATA_API UserDataProcessor {
public:
  /// @brief 3.a. Функция ввода пользовательских данных из консоли
  static UserData inputUserData();

  /// @brief Разбор входной строки на компоненты UserData
  static UserData parseLine(const std::string &rawLine);

  /// @brief 3.b. Функция проверки и приведения введённых данных к целевому шаблону
  /// @param input Исходные данные
  /// @param output Нормализованные данные, соответствующие шаблону
  /// @param errors Список ошибок, если данные некорректны
  /// @return true, если данные успешно нормализованы
  static bool normalizeAndValidate(const UserData &input, UserData &output,
                                   std::vector<std::string> &errors);

  /// @brief Нормализация части ФИО (удаление цифр/мусора, регистр: первая заглавная)
  static bool normalizeNamePart(const std::string &raw, std::string &normalized);

  /// @brief Нормализация даты рождения к формату DD.MM.YYYY
  static bool normalizeBirthDate(const std::string &raw, std::string &normalized);

  /// @brief Нормализация номера телефона к формату +7XXXXXXXXXX
  static bool normalizePhone(const std::string &raw, std::string &normalized);

  /// @brief Нормализация email (валидация и нижний регистр)
  static bool normalizeEmail(const std::string &raw, std::string &normalized);

  /// @brief 3.c. Генерация случайной анкеты с использованием словарей.
  /// Добавляет реалистичные погрешности и различные форматы (для проверки п. 3.b)
  static UserData generateRandomUserData();

  /// @brief Генерация набора случайных анкет
  static std::vector<UserData> generateRandomDataset(size_t count);

  /// @brief Обработка файла с анкетами: валидация, нормализация и вывод
  static void processFile(const std::filesystem::path &filePath);
};

} // namespace userdata