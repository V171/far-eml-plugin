# Far Manager EML Plugin

Плагин для Far Manager, позволяющий открывать файлы `.eml` (электронные письма) как обычные архивы. 
Письмо отображается в виде папки, внутри которой находятся текстовое содержимое (`message.txt`), HTML-представление (`message.html`) и все вложения (картинки, документы и т.д.) с корректно декодированными именами.

## Возможности
- Открытие `.eml` по `Enter` (через ассоциации Far).
- Просмотр содержимого по `F3` (встроенный просмотрщик Far).
- Извлечение файлов по `F5` (с запросом перезаписи и обработкой ошибок доступа).
- Выход из "архива" по `Esc` или `..`.
- Полная поддержка декодирования Base64 и Quoted-Printable.
- Декодирование RFC 2047 имен файлов (поддержка UTF-8, KOI8-R, Windows-1251 и др.).

## Установка
1. Скопируйте скомпилированную DLL в следующую папку:
   - `%FARHOME%\Plugins\eml_plugin\`
2. Перезапустите Far Manager.

## Настройка ассоциаций (чтобы открывать по Enter)
1. В Far Manager нажмите `F9` -> `Options` (Конфигурация) -> `File associations` (Ассоциации файлов).
2. Нажмите `Ins`, чтобы создать новую ассоциацию.
3. В поле **Маска** (Mask) введите:
   `*.eml`
4. В поле **Команда выполнения** (Execute command) введите (скопируйте отсюда):
   `eml_plugin:"!\!.!"`
5. Сохраните настройки (Esc/Ok).

Теперь при нажатии `Enter` на любом файле `.eml` он откроется как папка с содержимым письма.

## Сборка из исходников (MSVC)
Потребуется установленный Visual Studio (с C++) и Far Manager PluginSDK.

**Сборка для x64:**
```cmd
cl /LD /MT /EHsc /I "C:\Program Files\Far Manager\PluginSDK\Headers.c" eml_plugin.cpp /link /NOENTRY /DEF:eml_plugin.def /OUT:eml_plugin.dll kernel32.lib user32.lib libcmt.lib libcpmt.lib vcruntime.lib ucrt.lib legacy_stdio_definitions.lib
```

**Сборка для x86:**
```cmd
cl /LD /MT /EHsc /I "C:\Program Files\Far Manager (x86)\PluginSDK\Headers.c" eml_plugin.cpp /link /NOENTRY /DEF:eml_plugin.def /OUT:eml_plugin.dll kernel32.lib user32.lib libcmt.lib libcpmt.lib vcruntime.lib ucrt.lib legacy_stdio_definitions.lib
```
