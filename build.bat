@echo off
chcp 65001 > nul
echo Сборка через g++...

if not exist "lib" mkdir "lib"

echo [1/5] Сборка статической библиотеки menu (libmenu.a)...
g++ -std=c++17 -c source/menu/Menu.cpp -Isource/menu -o source/menu/Menu.o
ar rcs lib/libmenu.a source/menu/Menu.o
del source\menu\Menu.o

echo [2/5] Сборка статической библиотеки opendialogfile (libopendialogfile.a)...
g++ -std=c++17 -c source/opendialogfile/OpenDialogFile.cpp -Isource/opendialogfile -o source/opendialogfile/OpenDialogFile.o
ar rcs lib/libopendialogfile.a source/opendialogfile/OpenDialogFile.o
del source\opendialogfile\OpenDialogFile.o

echo [3/5] Сборка динамической библиотеки checkuserdata (libcheckuserdata.dll)...
g++ -std=c++17 -shared -DCHECKUSERDATA_EXPORTS source/checkuserdata/checkuserdata.cpp -Isource/checkuserdata -o lib/libcheckuserdata.dll -Wl,--out-implib,lib/libcheckuserdata.dll.a

echo [4/5] Сборка динамической библиотеки scanfile (libscanfile.dll)...
g++ -std=c++17 -shared -DSCANFILE_EXPORTS source/scanfile/scanfile.cpp -Isource/scanfile -o lib/libscanfile.dll -Wl,--out-implib,lib/libscanfile.dll.a

echo [5/5] Сборка приложения app.exe...
g++ -std=c++17 source/app/Application.cpp source/app/main.cpp -Isource -Isource/menu -Isource/opendialogfile -Llib -lmenu -lopendialogfile -lcheckuserdata -lscanfile -lcomdlg32 -o app.exe

echo Копирование DLL рядом с app.exe для запуска...
copy /Y lib\libcheckuserdata.dll . > nul
copy /Y lib\libscanfile.dll . > nul


pause
