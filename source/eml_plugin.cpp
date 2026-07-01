// ============================================================================
// Far Manager EML Plugin
// Copyright (c) 2026 V171
// Licensed under the Apache 2.0 License. See LICENSE file for details.
// ============================================================================
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <plugin.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#define PLUGIN_NAME L"EML Plugin"
#define PLUGIN_DESCRIPTION L"Открытие EML файлов как архивов"
#define MAX_FILES 1024

#ifndef FCTL_CLOSEPLUGIN
#define FCTL_CLOSEPLUGIN 0
#endif

#ifndef OPM_SILENT
#define OPM_SILENT 0x0001
#endif

struct EMLFileEntry {
    wchar_t name[256];
    char* content;
    size_t size;
    DWORD attributes;
    FILETIME creationTime;
    FILETIME lastAccessTime;
    FILETIME lastWriteTime;
};

HINSTANCE g_hInstance = NULL;
PluginStartupInfo g_psi;
FarStandardFunctions* g_fsf = NULL;

EMLFileEntry g_files[MAX_FILES];
int g_fileCount = 0;
wchar_t g_currentEML[MAX_PATH] = {0};
bool g_bOpened = false;

// ==================== Утилиты и Декодеры ====================

void DebugLog(const char* msg, ...) {
    wchar_t tempPath[MAX_PATH];
    if (GetTempPathW(MAX_PATH, tempPath) == 0) return;
    wcscat_s(tempPath, MAX_PATH, L"eml_debug.log");
    
    char buffer[2048];
    va_list args;
    va_start(args, msg);
    vsnprintf(buffer, sizeof(buffer), msg, args);
    va_end(args);
    
    HANDLE hFile = CreateFileW(tempPath, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(hFile, buffer, strlen(buffer), &written, NULL);
        WriteFile(hFile, "\r\n", 2, &written, NULL);
        CloseHandle(hFile);
    }
}

const char* stristr(const char* haystack, const char* needle) {
    if (!haystack || !needle) return NULL;
    for (; *haystack; ++haystack) {
        const char* h = haystack;
        const char* n = needle;
        while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
            h++; n++;
        }
        if (!*n) return haystack;
    }
    return NULL;
}

const char* FindNextBoundary(const char* start, const char* bLine, const char* limit) {
    size_t bLen = strlen(bLine);
    for (const char* p = start; p + bLen <= limit; ++p) {
        if (strncmp(p, bLine, bLen) == 0) {
            if (p == start || (p > start && p[-1] == '\n')) {
                return p;
            }
        }
    }
    return NULL;
}

size_t Base64Decode(const char* src, size_t len, char** out) {
    int8_t tbl[256];
    memset(tbl, -1, 256);
    for (int i = 0; i < 26; i++) tbl['A' + i] = (int8_t)i;
    for (int i = 0; i < 26; i++) tbl['a' + i] = (int8_t)(26 + i);
    for (int i = 0; i < 10; i++) tbl['0' + i] = (int8_t)(52 + i);
    tbl['+'] = 62; tbl['/'] = 63;

    size_t out_len = (len / 4 + 1) * 3;
    char* buf = (char*)malloc(out_len);
    if (!buf) return 0;
    
    int val = 0, valb = -8;
    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = src[i];
        if (c == '=') break;
        if (tbl[c] == -1) continue; 
        val = (val << 6) + tbl[c];
        valb += 6;
        if (valb >= 0) {
            buf[o++] = (char)((val >> valb) & 0xFF);
            valb -= 8;
        }
    }
    *out = buf;
    return o;
}

size_t QPDecode(const char* src, size_t len, char** out) {
    char* buf = (char*)malloc(len + 1);
    if (!buf) return 0;
    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        if (src[i] == '=') {
            if (i + 2 < len && src[i+1] == '\r' && src[i+2] == '\n') { i += 2; continue; }
            if (i + 1 < len && src[i+1] == '\n') { i += 1; continue; }
            if (i + 2 < len) {
                char hex[3] = {src[i+1], src[i+2], 0};
                if (isxdigit(hex[0]) && isxdigit(hex[1])) {
                    buf[o++] = (char)strtol(hex, NULL, 16);
                    i += 2;
                    continue;
                }
            }
        }
        buf[o++] = src[i];
    }
    *out = buf;
    return o;
}

size_t URLDecode(const char* src, size_t len, char* out) {
    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        if (src[i] == '%' && i + 2 < len) {
            char hex[3] = {src[i+1], src[i+2], 0};
            if (isxdigit(hex[0]) && isxdigit(hex[1])) {
                out[o++] = (char)strtol(hex, NULL, 16);
                i += 2;
                continue;
            }
        }
        out[o++] = src[i];
    }
    return o;
}

UINT GetCodePage(const char* charset) {
    if (!charset) return CP_ACP;
    if (_stricmp(charset, "utf-8") == 0 || _stricmp(charset, "utf8") == 0) return CP_UTF8;
    if (_stricmp(charset, "koi8-r") == 0) return 20866;
    if (_stricmp(charset, "windows-1251") == 0) return 1251;
    if (_stricmp(charset, "windows-1252") == 0) return 1252;
    if (_stricmp(charset, "iso-8859-1") == 0) return 28591;
    return CP_ACP;
}

void DecodeMimeFilename(const char* src, wchar_t* out, size_t out_size) {
    if (!src || !out || out_size == 0) return;
    const char* p = src;
    size_t out_pos = 0;
    
    while (*p && out_pos < out_size - 1) {
        if (strncmp(p, "=?", 2) == 0) {
            const char* charset_start = p + 2;
            const char* charset_end = strchr(charset_start, '?');
            if (charset_end && (charset_end - charset_start) > 0 && (charset_end - charset_start) < 32) {
                char charset[32] = {0};
                strncpy(charset, charset_start, charset_end - charset_start);
                
                const char* enc_start = charset_end + 1;
                if ((*enc_start == 'Q' || *enc_start == 'q' || *enc_start == 'B' || *enc_start == 'b') && *(enc_start+1) == '?') {
                    char enc = *enc_start;
                    const char* text_start = enc_start + 2;
                    const char* text_end = strstr(text_start, "?=");
                    if (text_end) {
                        size_t text_len = text_end - text_start;
                        char decoded[512] = {0};
                        size_t dec_len = 0;
                        
                        if (enc == 'Q' || enc == 'q') {
                            for (size_t i = 0; i < text_len && dec_len < sizeof(decoded)-1; i++) {
                                if (text_start[i] == '_') decoded[dec_len++] = ' ';
                                else if (text_start[i] == '=' && i + 2 < text_len) {
                                    char hex[3] = {text_start[i+1], text_start[i+2], 0};
                                    decoded[dec_len++] = (char)strtol(hex, NULL, 16);
                                    i += 2;
                                } else {
                                    decoded[dec_len++] = text_start[i];
                                }
                            }
                        } else if (enc == 'B' || enc == 'b') {
                            char* b64_out = NULL;
                            size_t b_len = Base64Decode(text_start, text_len, &b64_out);
                            if (b64_out && b_len > 0 && b_len < sizeof(decoded)) {
                                memcpy(decoded, b64_out, b_len);
                                dec_len = b_len;
                            }
                            if (b64_out) free(b64_out);
                        }
                        
                        UINT cp = GetCodePage(charset);
                        int wlen = MultiByteToWideChar(cp, 0, decoded, (int)dec_len, out + out_pos, (int)(out_size - out_pos - 1));
                        if (wlen > 0) out_pos += wlen;
                        
                        p = text_end + 2;
                        continue;
                    }
                }
            }
        }
        out[out_pos++] = (wchar_t)(unsigned char)*p++;
    }
    out[out_pos] = 0;
}

bool ExtractRFC2231(const char* headerVal, const char* baseParam, wchar_t* out, size_t out_size) {
    if (!headerVal || !out || out_size == 0) return false;

    char combined[2048] = {0};
    size_t combined_len = 0;
    char charset[32] = {0};
    char paramToFind[64];

    // Попытка найти multipart RFC 2231: baseParam*0*=
    sprintf(paramToFind, "%s*0*=", baseParam);
    const char* p = stristr(headerVal, paramToFind);
    if (p) {
        p += strlen(paramToFind);
        const char* quote1 = strchr(p, '\'');
        if (quote1) {
            size_t csLen = quote1 - p;
            if (csLen > 0 && csLen < sizeof(charset)) {
                strncpy(charset, p, csLen);
                charset[csLen] = 0;
            }
            const char* quote2 = strchr(quote1 + 1, '\'');
            if (quote2) {
                p = quote2 + 1;
                const char* end = p;
                while (*end && *end != ';' && *end != '\r' && *end != '\n') end++;
                if (end > p) combined_len += URLDecode(p, end - p, combined + combined_len);

                for (int i = 1; i < 10; i++) {
                    sprintf(paramToFind, "%s*%d*=", baseParam, i);
                    const char* next_p = stristr(headerVal, paramToFind);
                    if (!next_p) break;
                    next_p += strlen(paramToFind);
                    const char* end2 = next_p;
                    while (*end2 && *end2 != ';' && *end2 != '\r' && *end2 != '\n') end2++;
                    if (end2 > next_p) combined_len += URLDecode(next_p, end2 - next_p, combined + combined_len);
                }

                UINT cp = GetCodePage(charset);
                MultiByteToWideChar(cp, 0, combined, (int)combined_len, out, (int)out_size);
                return true;
            }
        }
    }

    // Попытка найти single part RFC 5987: baseParam*=
    sprintf(paramToFind, "%s*=", baseParam);
    p = stristr(headerVal, paramToFind);
    if (p) {
        p += strlen(paramToFind);
        const char* quote1 = strchr(p, '\'');
        if (quote1) {
            size_t csLen = quote1 - p;
            if (csLen > 0 && csLen < sizeof(charset)) {
                strncpy(charset, p, csLen);
                charset[csLen] = 0;
            }
            const char* quote2 = strchr(quote1 + 1, '\'');
            if (quote2) {
                p = quote2 + 1;
                const char* end = p;
                while (*end && *end != ';' && *end != '\r' && *end != '\n') end++;

                char decoded[2048];
                size_t dec_len = URLDecode(p, end - p, decoded);

                UINT cp = GetCodePage(charset);
                MultiByteToWideChar(cp, 0, decoded, (int)dec_len, out, (int)out_size);
                return true;
            }
        }
    }

    return false;
}

void GetHeader(const char* headers, size_t headersLen, const char* name, char* out, size_t out_size) {
    out[0] = 0;
    size_t name_len = strlen(name);
    for (size_t i = 0; i < headersLen; ) {
        if (_strnicmp(headers + i, name, name_len) == 0) {
            if (i == 0 || headers[i-1] == '\n') {
                size_t p = i + name_len;
                while (p < headersLen && (headers[p] == ':' || headers[p] == ' ' || headers[p] == '\t')) p++;
                
                size_t o = 0;
                while (p < headersLen && headers[p] != '\r' && headers[p] != '\n') {
                    if (o < out_size - 1) out[o++] = headers[p];
                    p++;
                }
                out[o] = 0;
                
                while (p < headersLen && (headers[p] == '\r' || headers[p] == '\n')) {
                    size_t next_p = p;
                    if (headers[next_p] == '\r') next_p++;
                    if (next_p < headersLen && headers[next_p] == '\n') next_p++;
                    if (next_p < headersLen && (headers[next_p] == ' ' || headers[next_p] == '\t')) {
                        next_p++;
                        if (o < out_size - 1) out[o++] = ' ';
                        while (next_p < headersLen && headers[next_p] != '\r' && headers[next_p] != '\n') {
                            if (o < out_size - 1) out[o++] = headers[next_p];
                            next_p++;
                        }
                        out[o] = 0;
                        p = next_p;
                    } else {
                        break;
                    }
                }
                return;
            }
        }
        i++;
    }
}

void ExtractParam(const char* headerVal, const char* param, char* out, size_t out_size) {
    out[0] = 0;
    const char* p = stristr(headerVal, param);
    if (!p) return;
    p += strlen(param);
    while (*p == ' ' || *p == '\t' || *p == '=') p++;
    
    if (*p == '"') {
        p++;
        size_t o = 0;
        while (*p && *p != '"' && o < out_size - 1) out[o++] = *p++;
        out[o] = 0;
    } else {
        size_t o = 0;
        while (*p && *p != ';' && *p != ' ' && *p != '\r' && *p != '\n' && o < out_size - 1) {
            out[o++] = *p++;
        }
        out[o] = 0;
    }
}

// ==================== Управление файлами ====================

void ClearFiles() {
    for (int i = 0; i < g_fileCount; i++) {
        if (g_files[i].content) {
            free(g_files[i].content);
            g_files[i].content = NULL;
        }
    }
    g_fileCount = 0;
}

void AddFile(const wchar_t* name, const char* data, size_t size) {
    if (g_fileCount >= MAX_FILES) return;
    EMLFileEntry* entry = &g_files[g_fileCount];
    memset(entry, 0, sizeof(EMLFileEntry));
    
    wcscpy(entry->name, name);
    if (data && size > 0) {
        entry->content = (char*)malloc(size);
        if (entry->content) {
            memcpy(entry->content, data, size);
            entry->size = size;
        }
    }
    
    entry->attributes = FILE_ATTRIBUTE_NORMAL;
    SYSTEMTIME st;
    GetSystemTime(&st);
    SystemTimeToFileTime(&st, &entry->creationTime);
    entry->lastAccessTime = entry->creationTime;
    entry->lastWriteTime = entry->creationTime;
    g_fileCount++;
}

// ==================== Парсер MIME ====================

void ParseMimePart(const char* partStart, size_t partLen) {
    if (!partStart || partLen == 0) return;
    
    size_t headerLen = partLen;
    const char* bodyStart = partStart;
    
    if (partLen >= 4) {
        for (size_t i = 0; i <= partLen - 4; i++) {
            if (partStart[i] == '\r' && partStart[i+1] == '\n' && partStart[i+2] == '\r' && partStart[i+3] == '\n') {
                headerLen = i; bodyStart = partStart + i + 4; break;
            }
        }
    }
    
    char* headers = (char*)malloc(headerLen + 1);
    if (!headers) return;
    memcpy(headers, partStart, headerLen);
    headers[headerLen] = 0;
    
    char ct[1024] = {0}; // Увеличено с 256 до 1024
    GetHeader(headers, headerLen, "Content-Type:", ct, sizeof(ct));
    char cd[2048] = {0}; // Увеличено с 256 до 2048 для длинных имен файлов
    GetHeader(headers, headerLen, "Content-Disposition:", cd, sizeof(cd));
    char cte[128] = {0};
    GetHeader(headers, headerLen, "Content-Transfer-Encoding:", cte, sizeof(cte));
    
    const char* limit = partStart + partLen;
    size_t bodyLen = limit - bodyStart;
    
    if (stristr(ct, "multipart/")) {
        char boundary[256] = {0};
        ExtractParam(ct, "boundary", boundary, sizeof(boundary));
        if (boundary[0]) {
            char bLine[512];
            sprintf(bLine, "--%s", boundary);
            
            const char* current = FindNextBoundary(bodyStart, bLine, limit);
            while (current && current < limit) {
                const char* part_start = current + strlen(bLine);
                
                if (strncmp(part_start, "--", 2) == 0) {
                    break; 
                }
                
                if (*part_start == '\r') part_start++;
                if (*part_start == '\n') part_start++;
                
                const char* next_b = FindNextBoundary(part_start, bLine, limit);
                if (!next_b) next_b = limit;
                
                ParseMimePart(part_start, next_b - part_start);
                current = next_b;
            }
        }
    } else {
        wchar_t wname[256] = {0};
        bool hasFilename = false;
        
        if (cd[0]) {
            if (ExtractRFC2231(cd, "filename", wname, 256)) hasFilename = true;
        }
        if (!hasFilename && ct[0]) {
            if (ExtractRFC2231(ct, "name", wname, 256)) hasFilename = true;
        }
        
        if (!hasFilename) {
            char filename[512] = {0};
            ExtractParam(cd, "filename", filename, sizeof(filename));
            if (!filename[0]) ExtractParam(ct, "name", filename, sizeof(filename));
            
            if (filename[0]) {
                DecodeMimeFilename(filename, wname, 256);
                hasFilename = true;
            }
        }
        
        if (!hasFilename) {
            char genName[64];
            if (stristr(ct, "text/html")) strcpy(genName, "message.html");
            else if (stristr(ct, "text/plain")) strcpy(genName, "message.txt");
            else {
                const char* ext = "bin";
                if (stristr(ct, "image/png")) ext = "png";
                else if (stristr(ct, "image/jpeg")) ext = "jpg";
                else if (stristr(ct, "image/gif")) ext = "gif";
                else if (stristr(ct, "application/pdf")) ext = "pdf";
                sprintf(genName, "attachment_%d.%s", g_fileCount, ext);
            }
            MultiByteToWideChar(CP_ACP, 0, genName, -1, wname, 256);
        }
        
        char* decodedBody = NULL;
        size_t decodedLen = 0;
        
        if (stristr(cte, "base64")) {
            decodedLen = Base64Decode(bodyStart, bodyLen, &decodedBody);
        } else if (stristr(cte, "quoted-printable")) {
            decodedLen = QPDecode(bodyStart, bodyLen, &decodedBody);
        } else {
            decodedLen = bodyLen;
            decodedBody = (char*)malloc(decodedLen + 1);
            if (decodedBody) memcpy(decodedBody, bodyStart, decodedLen);
        }
        
        if (decodedBody && decodedLen > 0) {
            AddFile(wname, decodedBody, decodedLen);
        }
        if (decodedBody) free(decodedBody);
    }
    free(headers);
}

bool ParseEML(const wchar_t* filename) {
    HANDLE hFile = CreateFileW(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    
    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == 0 || fileSize == INVALID_FILE_SIZE) { CloseHandle(hFile); return false; }
    
    char* content = (char*)malloc(fileSize + 1);
    if (!content) { CloseHandle(hFile); return false; }
    
    DWORD read = 0;
    ReadFile(hFile, content, fileSize, &read, NULL);
    CloseHandle(hFile);
    content[fileSize] = 0;
    
    size_t headerLen = fileSize;
    const char* bodyStart = content;
    if (fileSize >= 4) {
        for (size_t i = 0; i <= fileSize - 4; i++) {
            if (content[i] == '\r' && content[i+1] == '\n' && content[i+2] == '\r' && content[i+3] == '\n') {
                headerLen = i; bodyStart = content + i + 4; break;
            }
        }
    }
    
    AddFile(L"headers.txt", content, headerLen);
    
    char ct[1024] = {0}; // Увеличено
    GetHeader(content, headerLen, "Content-Type:", ct, sizeof(ct));
    
    if (stristr(ct, "multipart/")) {
        char boundary[256] = {0};
        ExtractParam(ct, "boundary", boundary, sizeof(boundary));
        if (boundary[0]) {
            char bLine[512];
            sprintf(bLine, "--%s", boundary);
            
            const char* limit = content + fileSize;
            const char* current = FindNextBoundary(bodyStart, bLine, limit);
            
            while (current && current < limit) {
                const char* part_start = current + strlen(bLine);
                
                if (strncmp(part_start, "--", 2) == 0) break; 
                
                if (*part_start == '\r') part_start++;
                if (*part_start == '\n') part_start++;
                
                const char* next_b = FindNextBoundary(part_start, bLine, limit);
                if (!next_b) next_b = limit;
                
                ParseMimePart(part_start, next_b - part_start);
                current = next_b;
            }
        }
    } else {
        char cte[128] = {0};
        GetHeader(content, headerLen, "Content-Transfer-Encoding:", cte, sizeof(cte));
        
        char* decodedBody = NULL;
        size_t decodedLen = 0;
        if (stristr(cte, "base64")) decodedLen = Base64Decode(bodyStart, fileSize - (bodyStart - content), &decodedBody);
        else if (stristr(cte, "quoted-printable")) decodedLen = QPDecode(bodyStart, fileSize - (bodyStart - content), &decodedBody);
        else {
            decodedLen = fileSize - (bodyStart - content);
            decodedBody = (char*)malloc(decodedLen + 1);
            if (decodedBody) memcpy(decodedBody, bodyStart, decodedLen);
        }
        
        if (stristr(ct, "text/html")) AddFile(L"message.html", decodedBody, decodedLen);
        else AddFile(L"message.txt", decodedBody, decodedLen);
        
        if (decodedBody) free(decodedBody);
    }
    
    free(content);
    return g_fileCount > 0;
}

// ==================== FAR API ====================

void WINAPI SetStartupInfoW(const struct PluginStartupInfo *Info) {
    g_psi = *Info;
    g_fsf = Info->FSF;
}

HANDLE WINAPI OpenW(const struct OpenInfo *Info) {
    wchar_t fileName[MAX_PATH] = {0};
    
    if (Info->OpenFrom == OPEN_COMMANDLINE) {
        const OpenCommandLineInfo* cmdInfo = (const OpenCommandLineInfo*)Info->Data;
        const wchar_t* cmd = cmdInfo ? cmdInfo->CommandLine : NULL;
        
        if (cmd && *cmd) {
            wchar_t tmp[MAX_PATH] = {0};
            
            if (*cmd == L'"') {
                cmd++;
                const wchar_t* end = wcschr(cmd, L'"');
                if (end) {
                    wcsncpy(tmp, cmd, end - cmd);
                    tmp[end - cmd] = 0;
                } else {
                    wcscpy(tmp, cmd);
                }
            } else {
                wcscpy(tmp, cmd);
            }
            
            wchar_t* s = tmp;
            while (*s == L' ') s++;
            if (s != tmp) memmove(tmp, s, (wcslen(s) + 1) * sizeof(wchar_t));
            
            if (tmp[0]) {
                wcscpy(fileName, tmp);
            } else {
                int result = g_psi.InputBox(NULL, NULL, L"Open EML Archive", L"Enter path to EML file:", L"", L"", fileName, MAX_PATH, L"", 0);
                if (!result || !fileName[0]) return INVALID_HANDLE_VALUE;
            }
        } else {
            int result = g_psi.InputBox(NULL, NULL, L"Open EML Archive", L"Enter path to EML file:", L"", L"", fileName, MAX_PATH, L"", 0);
            if (!result || !fileName[0]) return INVALID_HANDLE_VALUE;
        }
    } else if (Info->OpenFrom == OPEN_PLUGINSMENU) {
        int result = g_psi.InputBox(NULL, NULL, L"Open EML Archive", L"Enter path to EML file:", L"", L"", fileName, MAX_PATH, L"", 0);
        if (!result || !fileName[0]) return INVALID_HANDLE_VALUE;
    } else {
        return INVALID_HANDLE_VALUE;
    }
    
    wcscpy(g_currentEML, fileName);
    ClearFiles();
    
    if (!ParseEML(g_currentEML)) {
        const wchar_t* msg[] = { L"Failed to parse EML file" };
        g_psi.Message(NULL, NULL, FMSG_WARNING, NULL, msg, 1, 0);
        return INVALID_HANDLE_VALUE;
    }
    
    g_bOpened = true;
    return (HANDLE)1;
}

void WINAPI ClosePanelW(const struct ClosePanelInfo *Info) {
    g_bOpened = false;
    ClearFiles();
}

intptr_t WINAPI SetDirectoryW(const struct SetDirectoryInfo *Info) {
    if (Info->Dir && (wcscmp(Info->Dir, L"..") == 0 || wcscmp(Info->Dir, L"\\") == 0 || wcscmp(Info->Dir, L"/") == 0)) {
        g_psi.PanelControl(PANEL_ACTIVE, (FILE_CONTROL_COMMANDS)FCTL_CLOSEPLUGIN, 0, NULL);
        return 1;
    }
    return 0;
}

intptr_t WINAPI GetFindDataW(struct GetFindDataInfo *Info) {
    if (!g_bOpened || g_fileCount == 0) {
        Info->ItemsNumber = 0; Info->PanelItem = NULL; return 0;
    }
    
    Info->ItemsNumber = g_fileCount;
    Info->PanelItem = (PluginPanelItem*)malloc(sizeof(PluginPanelItem) * g_fileCount);
    if (!Info->PanelItem) return 0;
    
    for (int i = 0; i < g_fileCount; i++) {
        memset(&Info->PanelItem[i], 0, sizeof(PluginPanelItem));
        size_t nameLen = wcslen(g_files[i].name) + 1;
        wchar_t* nameCopy = (wchar_t*)malloc(nameLen * sizeof(wchar_t));
        if (nameCopy) wcscpy(nameCopy, g_files[i].name);
        
        Info->PanelItem[i].FileName = nameCopy;
        Info->PanelItem[i].FileAttributes = g_files[i].attributes;
        Info->PanelItem[i].FileSize = g_files[i].size;
        Info->PanelItem[i].CreationTime = g_files[i].creationTime;
        Info->PanelItem[i].LastAccessTime = g_files[i].lastAccessTime;
        Info->PanelItem[i].LastWriteTime = g_files[i].lastWriteTime;
        Info->PanelItem[i].UserData.Data = (void*)(uintptr_t)(i + 1);
    }
    return 1;
}

void WINAPI FreeFindDataW(const struct FreeFindDataInfo *Info) {
    if (Info->PanelItem) {
        for (size_t i = 0; i < Info->ItemsNumber; i++) {
            if (Info->PanelItem[i].FileName) free((void*)Info->PanelItem[i].FileName);
        }
        free((void*)Info->PanelItem);
    }
}

void WINAPI GetOpenPanelInfoW(struct OpenPanelInfo *Info) {
    static wchar_t title[256];
    swprintf(title, 256, L"EML: %s", g_currentEML);
    Info->StructSize = sizeof(OpenPanelInfo);
    Info->PanelTitle = title;
    Info->CurDir = g_currentEML;
    Info->HostFile = g_currentEML;
    Info->Flags = OPIF_SHOWNAMESONLY | OPIF_ADDDOTS;
}

intptr_t WINAPI GetFilesW(struct GetFilesInfo *Info) {
    if (!g_bOpened || Info->ItemsNumber == 0) return 0;
    
    bool silent = (Info->OpMode & OPM_SILENT) != 0;
    
    wchar_t destDir[MAX_PATH * 2] = {0};
    
    if (Info->DestPath && Info->DestPath[0]) {
        wcscpy_s(destDir, MAX_PATH * 2, Info->DestPath);
    } else {
        GetTempPathW(MAX_PATH, destDir);
    }
    
    size_t len = wcslen(destDir);
    if (len > 0 && destDir[len-1] != L'\\' && destDir[len-1] != L'/') {
        wcscat_s(destDir, MAX_PATH * 2, L"\\");
    }
    
    bool overwriteAll = false;
    
    for (size_t i = 0; i < Info->ItemsNumber; i++) {
        int fileIndex = (int)(uintptr_t)Info->PanelItem[i].UserData.Data - 1;
        if (fileIndex < 0 || fileIndex >= g_fileCount) continue;
        
        wchar_t destFile[MAX_PATH * 2] = {0};
        wcscpy_s(destFile, MAX_PATH * 2, destDir);
        wcscat_s(destFile, MAX_PATH * 2, g_files[fileIndex].name);
            
        wchar_t dir[MAX_PATH * 2];
        wcscpy_s(dir, destFile);
        wchar_t* slash = wcsrchr(dir, L'\\');
        if (slash) {
            *slash = 0;
            CreateDirectoryW(dir, NULL); 
        }
        
        DWORD attrs = GetFileAttributesW(destFile);
        if (attrs != INVALID_FILE_ATTRIBUTES && !silent) {
            if (!overwriteAll) {
                const wchar_t* msg[] = { L"File already exists", destFile, L"Overwrite?", L"Yes", L"All", L"Skip", L"Cancel" };
                int res = g_psi.Message(NULL, NULL, FMSG_WARNING, NULL, msg, 7, 4);
                if (res == 1) overwriteAll = true;
                else if (res == 2) continue; 
                else if (res == 3) return 0; 
            }
        }
        
        HANDLE hFile = CreateFileW(destFile, GENERIC_WRITE, 0, NULL, overwriteAll ? CREATE_ALWAYS : OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            if (silent) return 0;
            
            DWORD err = GetLastError();
            LPWSTR errMsg = NULL;
            FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, err, 0, (LPWSTR)&errMsg, 0, NULL);
            
            const wchar_t* msg[] = { L"Write error (no access?)", destFile, errMsg ? errMsg : L"Unknown error", L"Skip", L"Cancel" };
            int res = g_psi.Message(NULL, NULL, FMSG_WARNING, NULL, msg, 3, 2);
            if (errMsg) LocalFree(errMsg);
            
            if (res == 1) return 0;
            continue; 
        }
        
        DWORD written = 0;
        if (g_files[fileIndex].content && g_files[fileIndex].size > 0) {
            SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
            SetEndOfFile(hFile);
            WriteFile(hFile, g_files[fileIndex].content, (DWORD)g_files[fileIndex].size, &written, NULL);
        }
        CloseHandle(hFile);
    }
    return 1; 
}

void WINAPI GetPluginInfoW(struct PluginInfo *Info) {
    Info->StructSize = sizeof(PluginInfo);
    Info->Flags = PF_VIEWER | PF_EDITOR;
    static const wchar_t* menuStrings[] = { L"&Open EML archive" };
    static const UUID menuGuids[] = { {0x12345678, 0x1234, 0x1234, {0x12,0x34,0x56,0x78,0x9A,0xBC,0xDE,0xF0}} };
    Info->PluginMenu.Strings = menuStrings;
    Info->PluginMenu.Count = 1;
    Info->PluginMenu.Guids = menuGuids;
    Info->CommandPrefix = L"eml_plugin"; 
}

void WINAPI GetGlobalInfoW(struct GlobalInfo *Info) {
    Info->StructSize = sizeof(GlobalInfo);
    Info->MinFarVersion = MAKEFARVERSION(3, 0, 0, 0, VS_RELEASE);
    Info->Version = MAKEFARVERSION(1, 0, 0, 0, VS_RELEASE);
    static UUID guid = {0x12345678, 0x1234, 0x1234, {0x12,0x34,0x56,0x78,0x9A,0xBC,0xDE,0xF0}};
    Info->Guid = guid;
    Info->Title = PLUGIN_NAME;
    Info->Description = PLUGIN_DESCRIPTION;
    Info->Author = L"V171";
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_hInstance = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}