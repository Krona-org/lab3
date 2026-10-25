#include "app/Application.h"
#include <windows.h>

int main() {
  SetConsoleCP(65001);
  SetConsoleOutputCP(65001);

  app::Application app;
  app.run();

  return 0;
}

// Ветка fast для многопоточного анализа больших файлов