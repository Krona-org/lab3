Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "Сборка библиотек и приложения через g++..." -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan

if (-not (Test-Path "lib")) { New-Item -ItemType Directory -Path "lib" | Out-Null }

Write-Host "[1/5] Сборка статической библиотеки menu (libmenu.a)..."
g++ -std=c++17 -c source/menu/Menu.cpp -Isource/menu -o source/menu/Menu.o
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
ar rcs lib/libmenu.a source/menu/Menu.o
Remove-Item source/menu/Menu.o

Write-Host "[2/5] Сборка статической библиотеки opendialogfile (libopendialogfile.a)..."
g++ -std=c++17 -c source/opendialogfile/OpenDialogFile.cpp -Isource/opendialogfile -o source/opendialogfile/OpenDialogFile.o
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
ar rcs lib/libopendialogfile.a source/opendialogfile/OpenDialogFile.o
Remove-Item source/opendialogfile/OpenDialogFile.o

Write-Host "[3/5] Сборка динамической библиотеки checkuserdata (libcheckuserdata.dll)..."
g++ -std=c++17 -shared -DCHECKUSERDATA_EXPORTS source/checkuserdata/checkuserdata.cpp -Isource/checkuserdata -o lib/libcheckuserdata.dll -Wl,--out-implib,lib/libcheckuserdata.dll.a
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "[4/5] Сборка динамической библиотеки scanfile (libscanfile.dll)..."
g++ -std=c++17 -shared -DSCANFILE_EXPORTS source/scanfile/scanfile.cpp -Isource/scanfile -o lib/libscanfile.dll -Wl,--out-implib,lib/libscanfile.dll.a
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "[5/5] Сборка приложения app.exe..."
g++ -std=c++17 `
    source/app/Application.cpp `
    source/app/main.cpp `
    -Isource `
    -Isource/menu `
    -Isource/opendialogfile `
    -Llib `
    -lmenu `
    -lopendialogfile `
    -lcheckuserdata `
    -lscanfile `
    -lcomdlg32 `
    -o app.exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Копирование DLL рядом с app.exe для запуска..."
Copy-Item lib/libcheckuserdata.dll . -Force
Copy-Item lib/libscanfile.dll . -Force

Write-Host "==========================================" -ForegroundColor Green
Write-Host "[УСПЕХ] Проект полностью собран!" -ForegroundColor Green
Write-Host "Исполняемый файл: app.exe" -ForegroundColor Green
Write-Host "Библиотеки находятся в: lib/" -ForegroundColor Green
Write-Host "==========================================" -ForegroundColor Green