#include "checkuserdata.h"

#include <cctype>
#include <fstream>
#include <iostream>
#include <random>
#include <regex>
#include <sstream>
#include <windows.h>

namespace userdata {

namespace {

// Вспомогательные функции преобразования UTF-8 <-> UTF-16 для корректной работы
// с кириллицей в Windows
std::wstring utf8ToWide(const std::string &str) {
  if (str.empty())
    return L"";
  int sizeNeeded = MultiByteToWideChar(
      CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
  std::wstring wstr(sizeNeeded, 0);
  MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()),
                      &wstr[0], sizeNeeded);
  return wstr;
}

std::string wideToUtf8(const std::wstring &wstr) {
  if (wstr.empty())
    return "";
  int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.data(),
                                       static_cast<int>(wstr.size()), nullptr,
                                       0, nullptr, nullptr);
  std::string str(sizeNeeded, 0);
  WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()),
                      &str[0], sizeNeeded, nullptr, nullptr);
  return str;
}

// Вспомогательные функции преобразования регистра и проверки букв для кириллицы
// и латиницы
wchar_t toLowerUnicode(wchar_t ch) {
  if (ch >= L'A' && ch <= L'Z')
    return ch + (L'a' - L'A');
  if (ch >= 0x0410 && ch <= 0x042F)
    return ch + 0x20; // А-Я -> а-я
  if (ch == 0x0401)
    return 0x0451; // Ё -> ё
  return ch;
}

wchar_t toUpperUnicode(wchar_t ch) {
  if (ch >= L'a' && ch <= L'z')
    return ch - (L'a' - L'A');
  if (ch >= 0x0430 && ch <= 0x044F)
    return ch - 0x20; // а-я -> А-Я
  if (ch == 0x0451)
    return 0x0401; // ё -> Ё
  return ch;
}

bool isAlphaUnicode(wchar_t ch) {
  if ((ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z'))
    return true;
  if ((ch >= 0x0410 && ch <= 0x044F) || ch == 0x0401 || ch == 0x0451)
    return true;
  return false;
}

// Словари для генерации реалистичных данных (мужские и женские)
const std::vector<std::string> maleLastNames = {
    "Иванов",  "Смирнов", "Кузнецов", "Попов",    "Васильев",
    "Петров",  "Соколов", "Михайлов", "Новиков",  "Федоров",
    "Морозов", "Волков",  "Алексеев", "Лебедев",  "Семенов",
    "Егоров",  "Павлов",  "Козлов",   "Степанов", "Николаев"};

const std::vector<std::string> femaleLastNames = {
    "Иванова",  "Смирнова", "Кузнецова", "Попова",    "Васильева",
    "Петрова",  "Соколова", "Михайлова", "Новикова",  "Федорова",
    "Морозова", "Волкова",  "Алексеева", "Лебедева",  "Семенова",
    "Егорова",  "Павлова",  "Козлова",   "Степанова", "Николаева"};

const std::vector<std::string> maleFirstNames = {
    "Александр", "Михаил", "Максим", "Артем",  "Даниил",
    "Дмитрий",   "Иван",   "Матвей", "Илья",   "Роман",
    "Кирилл",    "Егор",   "Никита", "Андрей", "Алексей"};

const std::vector<std::string> femaleFirstNames = {
    "Анастасия", "Мария",     "Анна",    "Виктория", "Полина",
    "Елизавета", "Екатерина", "Ксения",  "Дарья",    "Александра",
    "София",     "Алиса",     "Варвара", "Вероника", "Валерия"};

const std::vector<std::string> maleMiddleNames = {
    "Иванович",   "Петрович",   "Сергеевич",  "Александрович", "Дмитриевич",
    "Алексеевич", "Андреевич",  "Михайлович", "Владимирович",  "Николаевич",
    "Павлович",   "Максимович", "Егорович",   "Артемович"};

const std::vector<std::string> femaleMiddleNames = {
    "Ивановна",   "Петровна",   "Сергеевна",  "Александровна", "Дмитриевна",
    "Алексеевна", "Андреевна",  "Михайловна", "Владимировна",  "Николаевна",
    "Павловна",   "Максимовна", "Егоровна",   "Артемовна"};

const std::vector<std::string> emailDomains = {
    "pochta.ru", "mail.ru", "yandex.ru", "gmail.com", "bk.ru", "rambler.ru"};

// Дополняет число ведущим нулём до двух знаков: 3 -> "03"
std::string pad2(int value) {
  return (value < 10 ? "0" : "") + std::to_string(value);
}

} // namespace

std::string UserData::toString() const {
  return lastName + " " + firstName + " " + middleName + " " + birthDate + " " +
         phone + " " + email;
}

/// @brief Разбор строки на 6 полей анкеты (поддерживает номера телефонов с пробелами)
UserData UserDataProcessor::parseLine(const std::string &rawLine) {
  UserData data;
  std::istringstream iss(rawLine);
  std::vector<std::string> tokens;
  std::string token;
  while (iss >> token) {
    tokens.push_back(token);
  }

  if (tokens.empty()) {
    return data;
  }

  if (tokens.size() >= 6) {
    data.lastName = tokens[0];
    data.firstName = tokens[1];
    data.middleName = tokens[2];
    data.birthDate = tokens[3];

    // Ищем токен email: по умолчанию последний токен, либо токен с '@'
    size_t emailIdx = tokens.size() - 1;
    for (size_t i = 4; i < tokens.size(); ++i) {
      if (tokens[i].find('@') != std::string::npos) {
        emailIdx = i;
        break;
      }
    }

    data.email = tokens[emailIdx];

    // Все токены между датой рождения (индекс 3) и email - это телефон
    std::string phone;
    for (size_t i = 4; i < tokens.size(); ++i) {
      if (i == emailIdx) continue;
      if (!phone.empty()) phone += ' ';
      phone += tokens[i];
    }
    data.phone = phone;
  } else {
    if (tokens.size() > 0) data.lastName = tokens[0];
    if (tokens.size() > 1) data.firstName = tokens[1];
    if (tokens.size() > 2) data.middleName = tokens[2];
    if (tokens.size() > 3) data.birthDate = tokens[3];
    if (tokens.size() > 4) data.phone = tokens[4];
  }

  return data;
}

/// @brief Нормализация отдельной части имени (фамилия/имя/отчество)
/// Удаляет цифры и лишние знаки
/// приводит к регистру: первая заглавная, остальные строчные.
bool UserDataProcessor::normalizeNamePart(const std::string &raw,
                                          std::string &normalized) {
  if (raw.empty())
    return false;

  std::wstring wide = utf8ToWide(raw);
  std::wstring cleaned;

  // Оставляем только буквы и дефис для составных фамилий/имен
  for (wchar_t ch : wide) {
    if (isAlphaUnicode(ch) || ch == L'-') {
      cleaned.push_back(ch);
    }
  }

  if (cleaned.empty())
    return false;

  // Капитализация: первая заглавная, остальные строчные (с учетом дефисов)
  bool capitalizeNext = true;
  for (size_t i = 0; i < cleaned.size(); ++i) {
    if (cleaned[i] == L'-') {
      capitalizeNext = true;
    } else if (capitalizeNext) {
      cleaned[i] = toUpperUnicode(cleaned[i]);
      capitalizeNext = false;
    } else {
      cleaned[i] = toLowerUnicode(cleaned[i]);
    }
  }

  normalized = wideToUtf8(cleaned);
  return true;
}

/// @brief Нормализация даты рождения к целевому шаблону DD.MM.YYYY
/// Поддерживает форматы: YYYY-MM-DD, YYYY.MM.DD, YYYY/MM/DD, DD.MM.YYYY,
/// DD/MM/YYYY, DD-MM-YYYY
bool UserDataProcessor::normalizeBirthDate(const std::string &raw,
                                           std::string &normalized) {
  if (raw.empty())
    return false;

  static const std::regex ymdRegex(R"(^(\d{4})[-/.](\d{1,2})[-/.](\d{1,2})$)");
  static const std::regex dmyRegex(R"(^(\d{1,2})[-/.](\d{1,2})[-/.](\d{4})$)");

  std::smatch match;
  int day = 0, month = 0, year = 0;

  if (std::regex_match(raw, match, ymdRegex)) {
    year = std::stoi(match[1].str());
    month = std::stoi(match[2].str());
    day = std::stoi(match[3].str());
  } else if (std::regex_match(raw, match, dmyRegex)) {
    day = std::stoi(match[1].str());
    month = std::stoi(match[2].str());
    year = std::stoi(match[3].str());
  } else {
    return false;
  }

  // Проверка корректности даты
  if (year < 1900 || year > 2026 || month < 1 || month > 12 || day < 1 ||
      day > 31) {
    return false;
  }

  // Проверка дней в месяце
  static const int daysInMonth[] = {0,  31, 29, 31, 30, 31, 30,
                                    31, 31, 30, 31, 30, 31};
  if (day > daysInMonth[month]) {
    return false;
  }

  normalized = pad2(day) + "." + pad2(month) + "." + std::to_string(year);
  return true;
}

/// @brief Нормализация номера телефона к шаблону +7XXXXXXXXXX
/// Обрабатывает: +7996-8432-431, 8 (996) 843-24-31, 89968432431, +7 996 843 24
/// 31, 79968432431
bool UserDataProcessor::normalizePhone(const std::string &raw,
                                       std::string &normalized) {
  if (raw.empty())
    return false;

  // Очистка от знаков разделителей (скобки, дефисы, пробелы, знаки плюс)
  static const std::regex nonDigits(R"([^\d])");
  std::string digits = std::regex_replace(raw, nonDigits, "");

  if (digits.length() == 11) {
    if (digits.front() == '8' || digits.front() == '7') {
      normalized = "+7" + digits.substr(1);
      return true;
    }
  } else if (digits.length() == 10) {
    normalized = "+7" + digits;
    return true;
  }

  return false;
}

/// @brief Нормализация email к нижнему регистру и проверка регулярным
/// выражением
bool UserDataProcessor::normalizeEmail(const std::string &raw,
                                       std::string &normalized) {
  if (raw.empty())
    return false;

  static const std::regex emailRegex(
      R"(^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$)");

  if (!std::regex_match(raw, emailRegex)) {
    return false;
  }

  std::string lower = raw;
  for (char &c : lower) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }

  normalized = lower;
  return true;
}

/// @brief 3.b. Полная валидация и нормализация анкеты
bool UserDataProcessor::normalizeAndValidate(const UserData &input,
                                             UserData &output,
                                             std::vector<std::string> &errors) {
  errors.clear();
  bool ok = true;

  if (!normalizeNamePart(input.lastName, output.lastName)) {
    errors.push_back("Некорректная фамилия: \"" + input.lastName + "\"");
    ok = false;
  }
  if (!normalizeNamePart(input.firstName, output.firstName)) {
    errors.push_back("Некорректное имя: \"" + input.firstName + "\"");
    ok = false;
  }
  if (!normalizeNamePart(input.middleName, output.middleName)) {
    errors.push_back("Некорректное отчество: \"" + input.middleName + "\"");
    ok = false;
  }
  if (!normalizeBirthDate(input.birthDate, output.birthDate)) {
    errors.push_back("Некорректная дата рождения: \"" + input.birthDate +
                     "\" (ожидаются форматы YYYY-MM-DD, DD.MM.YYYY и др.)");
    ok = false;
  }
  if (!normalizePhone(input.phone, output.phone)) {
    errors.push_back("Некорректный номер телефона: \"" + input.phone +
                     "\" (ожидается 10-11 цифр)");
    ok = false;
  }
  if (!normalizeEmail(input.email, output.email)) {
    errors.push_back("Некорректный email: \"" + input.email + "\"");
    ok = false;
  }

  return ok;
}

/// @brief 3.a. Функция ввода пользовательских данных из консоли
UserData UserDataProcessor::inputUserData() {
  std::cout << "\n--- Ввод пользовательских данных ---\n";
  std::cout << "Выберите формат ввода:\n";
  std::cout << "  1. Ввести все данные одной строкой (пример: Сидоров ИваН "
               "ПетРОви1ч 2002-03-01 +7996-8432-431 sidorovVV@pochta.ru)\n";
  std::cout << "  2. Ввести каждое поле по отдельности\n";
  std::cout << "Ваш выбор: ";

  int mode = 1;
  if (!(std::cin >> mode)) {
    std::cin.clear();
    std::cin.ignore(10000, '\n');
    mode = 1;
  }
  std::cin.ignore(10000, '\n'); // очистка остатка строки

  UserData data;
  if (mode == 1) {
    std::cout << "\nВведите данные анкеты:\n> ";
    std::string line;
    std::getline(std::cin, line);
    data = parseLine(line);
  } else {
    std::cout << "Фамилия: ";
    std::getline(std::cin, data.lastName);
    std::cout << "Имя: ";
    std::getline(std::cin, data.firstName);
    std::cout << "Отчество: ";
    std::getline(std::cin, data.middleName);
    std::cout << "Дата рождения (например, 2002-03-01 или 01.03.2002): ";
    std::getline(std::cin, data.birthDate);
    std::cout << "Телефон (например, +7996-8432-431 или 8(996)843-24-31): ";
    std::getline(std::cin, data.phone);
    std::cout << "Email (например, sidorovVV@pochta.ru): ";
    std::getline(std::cin, data.email);
  }

  return data;
}

/// @brief 3.c. Генератор случайных пользовательских данных со словарями
UserData UserDataProcessor::generateRandomUserData() {
  static std::mt19937 rng(std::random_device{}());
  auto rnd = [](int from, int to) { return std::uniform_int_distribution<int>(from, to)(rng); };
  auto pick = [&](const std::vector<std::string> &v) { return v[rnd(0, static_cast<int>(v.size()) - 1)]; };

  const bool isMale = rnd(0, 1) == 1;

  UserData user;
  user.lastName = pick(isMale ? maleLastNames : femaleLastNames);
  std::string fName = pick(isMale ? maleFirstNames : femaleFirstNames);
  std::string mName = pick(isMale ? maleMiddleNames : femaleMiddleNames);

  // Вносим реалистичные искажения регистра и опечатки (как в примере задания)
  // Например: "Сидоров ИваН ПетРОви1ч"
  std::wstring wFName = utf8ToWide(fName);
  if (wFName.size() > 2) {
    wFName.back() = toUpperUnicode(wFName.back()); // последняя буква заглавная: "ИваН"
  }
  user.firstName = wideToUtf8(wFName);

  std::wstring wMName = utf8ToWide(mName);
  if (wMName.size() > 4) {
    wMName[3] = toUpperUnicode(wMName[3]);
    wMName[4] = toUpperUnicode(wMName[4]);
    wMName.insert(wMName.end() - 1, L'1'); // цифра-опечатка как в "ПетРОви1ч"
  }
  user.middleName = wideToUtf8(wMName);

  // Дата в случайном формате
  const std::string y = std::to_string(rnd(1980, 2006));
  const std::string m = pad2(rnd(1, 12));
  const std::string d = pad2(rnd(1, 28));
  switch (rnd(0, 3)) {
  case 0: user.birthDate = y + "-" + m + "-" + d; break; // YYYY-MM-DD
  case 1: user.birthDate = d + "." + m + "." + y; break; // DD.MM.YYYY
  case 2: user.birthDate = y + "/" + m + "/" + d; break; // YYYY/MM/DD
  default: user.birthDate = d + "/" + m + "/" + y; break; // DD/MM/YYYY
  }

  // Телефон в случайном формате
  const std::string code = std::to_string(rnd(900, 999));
  const std::string n1 = std::to_string(rnd(100, 999));
  const std::string n2 = std::to_string(rnd(10, 99));
  const std::string n3 = std::to_string(rnd(10, 99));
  switch (rnd(0, 3)) {
  case 0: user.phone = "+7" + code + "-" + n1 + "-" + n2 + n3; break;
  case 1: user.phone = "8 (" + code + ") " + n1 + "-" + n2 + "-" + n3; break;
  case 2: user.phone = "+7 " + code + " " + n1 + " " + n2 + " " + n3; break;
  default: user.phone = "8" + code + n1 + n2 + n3; break;
  }

  // Email со смешанным регистром: "UsEr923@..."
  user.email = "UsEr" + code + "@" + pick(emailDomains);

  return user;
}

/// @brief Генерация массива случайных анкет
std::vector<UserData> UserDataProcessor::generateRandomDataset(size_t count) {
  std::vector<UserData> dataset;
  dataset.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    dataset.push_back(generateRandomUserData());
  }
  return dataset;
}

/// @brief Обработка анкет из файла
void UserDataProcessor::processFile(const std::filesystem::path &filePath) {
  if (filePath.empty()) {
    std::cout << "Файл не выбран!\n";
    return;
  }

  std::ifstream file(filePath);
  if (!file.is_open()) {
    std::cout << "Не удалось открыть файл: " << filePath.string() << "\n";
    return;
  }

  std::cout << "\n=== Обработка файла анкет: " << filePath.filename().u8string()
            << " ===\n";

  std::string line;
  size_t total = 0;
  size_t valid = 0;

  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }

    total++;
    UserData raw = parseLine(line);
    UserData normalized;
    std::vector<std::string> errors;

    std::cout << "\n[Запись #" << total << "]\n";
    std::cout << "  Входные данные:  " << line << "\n";

    if (normalizeAndValidate(raw, normalized, errors)) {
      std::cout << "  Выходные данные: " << normalized.toString() << "\n";
      std::cout << "  Статус: [УСПЕШНО НОРМАЛИЗОВАНО]\n";
      valid++;
    } else {
      std::cout << "  Статус: [ОШИБКИ ВАЛИДАЦИИ]\n";
      for (const auto &err : errors) {
        std::cout << "    * " << err << "\n";
      }
    }
  }

  std::cout << "\nИтого обработано: " << total << ", корректных: " << valid
            << ", с ошибками: " << (total - valid) << "\n";
}

} // namespace userdata
