# Far Manager EML Plugin

[Русская версия](README_RU.md)

A plugin for Far Manager that allows opening `.eml` (email) files as regular archives. 
The email is displayed as a folder containing the plain text body (`message.txt`), HTML representation (`message.html`), and all attachments (images, documents, etc.) with correctly decoded names.

## Features
- Open `.eml` via `Enter` (using Far Manager associations).
- View contents via `F3` (built-in Far viewer).
- Extract files via `F5` (with overwrite prompts and access error handling).
- Exit the "archive" via `Esc` or `..`.
- Full support for Base64 and Quoted-Printable decoding.
- RFC 2047 filename decoding (UTF-8, KOI8-R, Windows-1251, etc.).

## Installation
1. Place the compiled DLL into the following folder:
   - `%FARHOME%\Plugins\eml_plugin\`
2. Restart Far Manager.

## Configuring File Associations (to open via Enter)
1. In Far Manager, press `F9` -> `Options` -> `File associations`.
2. Press `Ins` to create a new association.
3. In the **Mask** field, enter:
   `*.eml`
4. In the **Execute command** field, enter (copy exactly):
   `eml_plugin:"!\!.!"`
5. Save the settings (Esc/Ok).

Now, pressing `Enter` on any `.eml` file will open it as a folder containing the email contents.

## Building from Source (MSVC)
Requires Visual Studio (with C++) and Far Manager PluginSDK.

**Build for x64:**
```cmd
cl /LD /MT /EHsc /I "C:\Program Files\Far Manager\PluginSDK\Headers.c" eml_plugin.cpp /link /NOENTRY /DEF:eml_plugin.def /OUT:eml_plugin.dll kernel32.lib user32.lib libcmt.lib libcpmt.lib vcruntime.lib ucrt.lib legacy_stdio_definitions.lib
```

**Build for x86:**
```cmd
cl /LD /MT /EHsc /I "C:\Program Files (x86)\Far Manager\PluginSDK\Headers.c" eml_plugin.cpp /link /NOENTRY /DEF:eml_plugin.def /OUT:eml_plugin.x86.dll kernel32.lib user32.lib libcmt.lib libcpmt.lib vcruntime.lib ucrt.lib legacy_stdio_definitions.lib
```

## License
Licensed under the Apache 2.0 License. See LICENSE file for details.
Copyright (c) 2026 V171.

