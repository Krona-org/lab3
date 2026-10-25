#include "Menu.hpp"
#include <limits>

namespace menu {

void ConsoleMenu::showMenu(std::ostream &os, std::istream &is) {
  while (true) {
    os << "\n" << title_ << "\n";
    for (const auto &item : items_) {
      os << "  " << item.choice << ". " << item.title << "\n";
    }
    os << "Выберите пункт: ";

    // Проверка на корректность ввода числа
    int choice = -1;
    if (!(is >> choice)) {
      is.clear();
      is.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      os << "Ошибка: введите корректный номер пункта!\n";
      continue;
    }

    const MenuItem *item = find(choice);

    // Если выбран пункт выхода (0)
    if (choice == 0) {
      if (item && item->action) item->action();
      os << "Выход из программы.\n";
      break;
    }

    if (item && item->action) {
      os << "\n";
      item->action();
    } else {
      os << "Неверный пункт меню! Попробуйте снова.\n";
    }
  }
}

} // namespace menu