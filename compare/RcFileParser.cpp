#include "stdafx.h"
#include "RcFileParser.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <algorithm>
#include <cctype>

namespace rceditor {

// ============================================================================
// SResourceHeader
// ============================================================================

UINT SResourceHeader::GetNextAvailableId(const CStringW& prefix) const
{
    UINT maxForPrefix = 0;
    for (const auto& def : defines)
    {
        if (def.prefix.CompareNoCase(prefix) == 0)
        {
            if (def.value > maxForPrefix)
                maxForPrefix = def.value;
        }
    }
    // 如果前缀区间内没有定义，从常规起始值开始
    if (maxForPrefix == 0)
    {
        if (prefix.CompareNoCase(L"IDD_") == 0) return 101;
        if (prefix.CompareNoCase(L"IDC_") == 0) return 1000;
        if (prefix.CompareNoCase(L"IDR_") == 0) return 128;
        if (prefix.CompareNoCase(L"IDS_") == 0) return 1;
        if (prefix.CompareNoCase(L"IDM_") == 0) return 32768;
        return 1000;
    }
    return maxForPrefix + 1;
}

void SResourceHeader::UpdateApsNextValues()
{
    // 扫描所有已定义的 ID，按前缀分组计算最大值
    UINT maxResource = 0;  // IDD_, IDR_, IDP_ 等资源级 ID
    UINT maxCommand = 0;   // ID_, IDM_ 等命令级 ID
    UINT maxControl = 0;   // IDC_ 控件级 ID
    UINT maxSymed = 0;     // 全局最大 ID

    for (const auto& def : defines)
    {
        // 跳过 _APS_NEXT_* 自身
        if (def.name.Find(L"_APS_NEXT_") == 0 || def.name.Find(L"_APS_") == 0)
            continue;

        if (def.value > maxSymed)
            maxSymed = def.value;

        CStringW upperPrefix = def.prefix;
        upperPrefix.MakeUpper();

        if (upperPrefix == L"IDD_" || upperPrefix == L"IDR_" || upperPrefix == L"IDP_" ||
            upperPrefix == L"IDB_" || upperPrefix == L"IDS_")
        {
            if (def.value > maxResource)
                maxResource = def.value;
        }
        else if (upperPrefix == L"ID_" || upperPrefix == L"IDM_")
        {
            if (def.value > maxCommand)
                maxCommand = def.value;
        }
        else if (upperPrefix == L"IDC_")
        {
            if (def.value > maxControl)
                maxControl = def.value;
        }
        else
        {
            // 未分类的 ID 归入资源类
            if (def.value > maxResource)
                maxResource = def.value;
        }
    }

    // 计算下一个可用值
    struct ApsEntry {
        const wchar_t* name;
        UINT nextValue;
    };
    ApsEntry apsEntries[] = {
        { L"_APS_NEXT_RESOURCE_VALUE", maxResource > 0 ? maxResource + 1 : 101 },
        { L"_APS_NEXT_COMMAND_VALUE",  maxCommand > 0 ? maxCommand + 1 : 32771 },
        { L"_APS_NEXT_CONTROL_VALUE",  maxControl > 0 ? maxControl + 1 : 1000 },
        { L"_APS_NEXT_SYMED_VALUE",    maxSymed > 0 ? maxSymed + 1 : 101 },
    };

    for (const auto& aps : apsEntries)
    {
        CStringW apsName(aps.name);

        // 更新 nameToId / idToName 映射
        auto itName = nameToId.find(apsName);
        if (itName != nameToId.end())
        {
            // 从旧值映射中移除
            auto itOldId = idToName.find(itName->second);
            if (itOldId != idToName.end() && itOldId->second == apsName)
                idToName.erase(itOldId);

            itName->second = aps.nextValue;
        }
        else
        {
            nameToId[apsName] = aps.nextValue;
        }
        // 注意：_APS_NEXT_* 数值通常很大，可能与其他 ID 碰撞，
        // 因此不写入 idToName（避免覆盖正常 ID）。

        // 更新 defines 列表和 rawLines
        bool found = false;
        for (auto& def : defines)
        {
            if (def.name == apsName)
            {
                def.value = aps.nextValue;
                if (def.lineIndex >= 0 && def.lineIndex < (int)rawLines.size())
                {
                    CStringW newLine;
                    newLine.Format(L"#define %s                %u", (LPCWSTR)apsName, aps.nextValue);
                    rawLines[def.lineIndex].text = newLine;
                }
                found = true;
                break;
            }
        }

        // 如果原文件中不存在此 _APS_NEXT_* 宏，在文件末尾的 #endif 前插入
        if (!found)
        {
            CStringW newLine;
            newLine.Format(L"#define %s                %u", (LPCWSTR)apsName, aps.nextValue);

            SDefineEntry newDef;
            newDef.name = apsName;
            newDef.value = aps.nextValue;
            newDef.prefix = L"_APS_";

            // 查找最后一个 #endif 行
            int insertAt = (int)rawLines.size();
            for (int i = (int)rawLines.size() - 1; i >= 0; --i)
            {
                CStringW trimmed = rawLines[i].text;
                trimmed.Trim();
                if (trimmed.Find(L"#endif") == 0)
                {
                    insertAt = i;
                    break;
                }
            }

            SRcLine newRcLine;
            newRcLine.text = newLine;
            newRcLine.lineNumber = insertAt + 1;
            newRcLine.blockIndex = -1;
            newRcLine.type = ERcLineType::Preprocessor;

            rawLines.insert(rawLines.begin() + insertAt, newRcLine);
            newDef.lineIndex = insertAt;
            defines.push_back(newDef);

            // 调整后续 define 的 lineIndex
            for (auto& d : defines)
            {
                if (&d != &defines.back() && d.lineIndex >= insertAt)
                    d.lineIndex++;
            }

            nameToId[apsName] = aps.nextValue;
        }
    }
}

CStringW SResourceHeader::ValidateNewDefine(const CStringW& macroName, UINT macroValue) const
{
    // 检查宏名冲突
    auto itName = nameToId.find(macroName);
    if (itName != nameToId.end())
    {
        if (itName->second != macroValue)
        {
            CStringW msg;
            msg.Format(L"Macro name conflict: %s already defined as %u (new value: %u)",
                       (LPCWSTR)macroName, itName->second, macroValue);
            return msg;
        }
    }

    // 检查数值冲突（同值不同名）
    auto itVal = idToName.find(macroValue);
    if (itVal != idToName.end())
    {
        if (itVal->second.CompareNoCase(macroName) != 0)
        {
            CStringW msg;
            msg.Format(L"ID value conflict: value %u already used by %s (new macro: %s)",
                       macroValue, (LPCWSTR)itVal->second, (LPCWSTR)macroName);
            return msg;
        }
    }

    return CStringW();
}

// ============================================================================
// CRcFileParser 构造
// ============================================================================

CRcFileParser::CRcFileParser()
{
}

// ============================================================================
// 文件编码检测
// ============================================================================

EFileEncoding CRcFileParser::DetectFileEncoding(const CStringW& filePath)
{
    HANDLE hFile = CreateFileW(filePath.GetString(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return EFileEncoding::Unknown;

    BYTE bom[3] = { 0 };
    DWORD bytesRead = 0;
    ReadFile(hFile, bom, 3, &bytesRead, nullptr);
    CloseHandle(hFile);

    if (bytesRead >= 3 && bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF)
        return EFileEncoding::UTF8_BOM;
    if (bytesRead >= 2 && bom[0] == 0xFF && bom[1] == 0xFE)
        return EFileEncoding::UTF16LE_BOM;
    if (bytesRead >= 2 && bom[0] == 0xFE && bom[1] == 0xFF)
        return EFileEncoding::UTF16BE_BOM;

    // 尝试判断 UTF-8 vs ANSI：读取更多字节检测 UTF-8 特征
    hFile = CreateFileW(filePath.GetString(), GENERIC_READ, FILE_SHARE_READ,
                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return EFileEncoding::ANSI;

    LARGE_INTEGER fileSize;
    GetFileSizeEx(hFile, &fileSize);
    DWORD readSize = (DWORD)min((LONGLONG)8192, fileSize.QuadPart);
    std::vector<BYTE> buf(readSize);
    ReadFile(hFile, buf.data(), readSize, &bytesRead, nullptr);
    CloseHandle(hFile);

    // 检查是否为有效 UTF-8
    bool hasHighBytes = false;
    bool validUtf8 = true;
    for (DWORD i = 0; i < bytesRead && validUtf8; )
    {
        BYTE b = buf[i];
        if (b <= 0x7F) { ++i; continue; }
        hasHighBytes = true;
        int contBytes = 0;
        if ((b & 0xE0) == 0xC0) contBytes = 1;
        else if ((b & 0xF0) == 0xE0) contBytes = 2;
        else if ((b & 0xF8) == 0xF0) contBytes = 3;
        else { validUtf8 = false; break; }
        if (i + contBytes >= bytesRead) break;
        for (int j = 1; j <= contBytes; ++j)
        {
            if ((buf[i + j] & 0xC0) != 0x80) { validUtf8 = false; break; }
        }
        i += 1 + contBytes;
    }

    if (hasHighBytes && validUtf8)
        return EFileEncoding::UTF8_NoBOM;
    return EFileEncoding::ANSI;
}

// ============================================================================
// 读取文件到行数组
// ============================================================================

bool CRcFileParser::ReadFileToLines(const CStringW& filePath, SRcFileContent& outContent)
{
    outContent = SRcFileContent();
    outContent.filePath = filePath;
    outContent.encoding = DetectFileEncoding(filePath);

    // 读取原始字节
    HANDLE hFile = CreateFileW(filePath.GetString(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        AddError(L"Cannot open file: " + filePath);
        return false;
    }

    LARGE_INTEGER fileSize;
    GetFileSizeEx(hFile, &fileSize);
    std::vector<BYTE> rawData((size_t)fileSize.QuadPart);
    DWORD bytesRead = 0;
    ReadFile(hFile, rawData.data(), (DWORD)fileSize.QuadPart, &bytesRead, nullptr);
    CloseHandle(hFile);

    // 转换为宽字符串
    CStringW fullText;
    switch (outContent.encoding)
    {
    case EFileEncoding::UTF16LE_BOM:
    {
        size_t offset = 2; // skip BOM
        size_t charCount = (bytesRead - offset) / sizeof(wchar_t);
        const wchar_t* start = reinterpret_cast<const wchar_t*>(rawData.data() + offset);
        fullText = CStringW(start, (int)charCount);
        break;
    }
    case EFileEncoding::UTF16BE_BOM:
    {
        size_t offset = 2;
        size_t charCount = (bytesRead - offset) / sizeof(wchar_t);
        std::vector<wchar_t> swapped(charCount);
        const BYTE* p = rawData.data() + offset;
        for (size_t i = 0; i < charCount; ++i)
        {
            swapped[i] = (wchar_t)((p[i * 2] << 8) | p[i * 2 + 1]);
        }
        fullText = CStringW(swapped.data(), (int)charCount);
        break;
    }
    case EFileEncoding::UTF8_BOM:
    {
        size_t offset = 3; // skip BOM
        int wlen = MultiByteToWideChar(CP_UTF8, 0,
                                       (const char*)(rawData.data() + offset),
                                       (int)(bytesRead - offset), nullptr, 0);
        if (wlen > 0)
        {
            wchar_t* buf = fullText.GetBuffer(wlen);
            MultiByteToWideChar(CP_UTF8, 0,
                                (const char*)(rawData.data() + offset),
                                (int)(bytesRead - offset), buf, wlen);
            fullText.ReleaseBuffer(wlen);
        }
        break;
    }
    case EFileEncoding::UTF8_NoBOM:
    {
        int wlen = MultiByteToWideChar(CP_UTF8, 0,
                                       (const char*)rawData.data(),
                                       (int)bytesRead, nullptr, 0);
        if (wlen > 0)
        {
            wchar_t* buf = fullText.GetBuffer(wlen);
            MultiByteToWideChar(CP_UTF8, 0,
                                (const char*)rawData.data(),
                                (int)bytesRead, buf, wlen);
            fullText.ReleaseBuffer(wlen);
        }
        break;
    }
    case EFileEncoding::ANSI:
    default:
    {
        int wlen = MultiByteToWideChar(CP_ACP, 0,
                                       (const char*)rawData.data(),
                                       (int)bytesRead, nullptr, 0);
        if (wlen > 0)
        {
            wchar_t* buf = fullText.GetBuffer(wlen);
            MultiByteToWideChar(CP_ACP, 0,
                                (const char*)rawData.data(),
                                (int)bytesRead, buf, wlen);
            fullText.ReleaseBuffer(wlen);
        }
        break;
    }
    }

    // 检测行结束符
    if (fullText.Find(L"\r\n") >= 0)
        outContent.lineEnding = L"\r\n";
    else
        outContent.lineEnding = L"\n";

    // 按行分割
    int lineNum = 1;
    int pos = 0;
    int len = fullText.GetLength();
    while (pos <= len)
    {
        int crlfPos = fullText.Find(L"\r\n", pos);
        int lfPos = fullText.Find(L'\n', pos);

        int endPos;
        int nextPos;

        if (crlfPos >= 0 && (crlfPos <= lfPos || lfPos < 0))
        {
            endPos = crlfPos;
            nextPos = crlfPos + 2;
        }
        else if (lfPos >= 0)
        {
            endPos = lfPos;
            nextPos = lfPos + 1;
        }
        else
        {
            // 最后一行（无结束符）
            endPos = len;
            nextPos = len + 1;
        }

        SRcLine line;
        line.text = fullText.Mid(pos, endPos - pos);
        line.lineNumber = lineNum++;
        line.blockIndex = -1;
        line.type = ERcLineType::Other;
        outContent.lines.push_back(std::move(line));

        pos = nextPos;
    }

    // 检测缩进风格
    outContent.indentStyle = DetectIndentStyle(outContent);

    return true;
}

// ============================================================================
// resource.h 解析
// ============================================================================

bool CRcFileParser::ReadResourceHeader(const CStringW& filePath,
                                        SRcFileContent& outContent,
                                        SResourceHeader& outHeader)
{
    if (!ReadFileToLines(filePath, outContent))
        return false;

    outHeader = SResourceHeader();
    outHeader.filePath = filePath;
    outHeader.encoding = outContent.encoding;
    outHeader.lineEnding = outContent.lineEnding;
    outHeader.rawLines = outContent.lines;
    outHeader.maxUsedId = 0;

    // 解析 #define 行
    for (int i = 0; i < (int)outContent.lines.size(); ++i)
    {
        const CStringW& line = outContent.lines[i].text;
        CStringW trimmed = TrimLine(line);

        if (trimmed.Left(7).CompareNoCase(L"#define") != 0)
            continue;

        // 跳过 "#define"
        CStringW rest = TrimLine(trimmed.Mid(7));
        if (rest.IsEmpty())
            continue;

        // 提取宏名
        int nameEnd = 0;
        while (nameEnd < rest.GetLength() && !iswspace(rest[nameEnd]))
            ++nameEnd;
        CStringW name = rest.Left(nameEnd);
        CStringW valueStr = TrimLine(rest.Mid(nameEnd));

        if (name.IsEmpty() || valueStr.IsEmpty())
            continue;

        // 尝试解析数值（十进制或十六进制）
        UINT value = 0;
        bool parsed = false;

        if (valueStr.Left(2) == L"0x" || valueStr.Left(2) == L"0X")
        {
            wchar_t* endPtr = nullptr;
            unsigned long v = wcstoul(valueStr.GetString(), &endPtr, 16);
            if (endPtr != valueStr.GetString())
            {
                value = (UINT)v;
                parsed = true;
            }
        }
        else
        {
            wchar_t* endPtr = nullptr;
            unsigned long v = wcstoul(valueStr.GetString(), &endPtr, 10);
            if (endPtr != valueStr.GetString() && (*endPtr == L'\0' || iswspace(*endPtr)))
            {
                value = (UINT)v;
                parsed = true;
            }
        }

        if (!parsed)
            continue;

        SDefineEntry entry;
        entry.name = name;
        entry.value = value;
        entry.lineIndex = i;
        entry.prefix = DetectIdPrefix(name);

        outHeader.defines.push_back(entry);
        outHeader.nameToId[name] = value;
        // 对于同一数值可能有多个宏名，取最后一个
        outHeader.idToName[value] = name;

        if (value > outHeader.maxUsedId)
            outHeader.maxUsedId = value;
    }

    return true;
}

bool CRcFileParser::ParseResourceHeader(const CStringW& filePath, SResourceHeader& outHeader)
{
    SRcFileContent content;
    return ReadResourceHeader(filePath, content, outHeader);
}

// ============================================================================
// 行类型识别与资源块标注
// ============================================================================

CStringW CRcFileParser::TrimLine(const CStringW& line) const
{
    CStringW s = line;
    s.TrimLeft();
    s.TrimRight();
    return s;
}

ERcLineType CRcFileParser::ClassifyLine(const CStringW& line) const
{
    CStringW trimmed = TrimLine(line);
    if (trimmed.IsEmpty())
        return ERcLineType::Blank;
    if (trimmed.Left(2) == L"//")
        return ERcLineType::Comment;
    if (trimmed.Left(2) == L"/*")
        return ERcLineType::Comment;
    if (trimmed.GetAt(0) == L'#')
        return ERcLineType::Preprocessor;

    return ERcLineType::Other;
}

bool CRcFileParser::IsBlockBegin(const CStringW& line) const
{
    CStringW t = TrimLine(line);
    return (t.CompareNoCase(L"BEGIN") == 0 || t == L"{");
}

bool CRcFileParser::IsBlockEnd(const CStringW& line) const
{
    CStringW t = TrimLine(line);
    return (t.CompareNoCase(L"END") == 0 || t == L"}");
}

// RC 资源类型关键词列表
static const CStringW s_resourceKeywords[] = {
    L"DIALOGEX", L"DIALOG", L"STRINGTABLE", L"MENUEX", L"MENU",
    L"ACCELERATORS", L"TOOLBAR", L"VERSIONINFO"
};

static ERcBlockType KeywordToBlockType(const CStringW& kw)
{
    CStringW upper = kw;
    upper.MakeUpper();
    if (upper == L"DIALOG" || upper == L"DIALOGEX")     return ERcBlockType::Dialog;
    if (upper == L"STRINGTABLE")                        return ERcBlockType::StringTable;
    if (upper == L"MENU" || upper == L"MENUEX")         return ERcBlockType::Menu;
    if (upper == L"ACCELERATORS")                       return ERcBlockType::Accelerators;
    if (upper == L"TOOLBAR")                            return ERcBlockType::Toolbar;
    if (upper == L"VERSIONINFO")                        return ERcBlockType::VersionInfo;
    return ERcBlockType::Other;
}

bool CRcFileParser::IsResourceHeaderLine(const CStringW& line, CStringW& outName, CStringW& outTypeKeyword) const
{
    CStringW trimmed = TrimLine(line);
    if (trimmed.IsEmpty()) return false;

    // 格式: <ID> <KEYWORD> [params...]
    // 或 STRINGTABLE (无 ID 前缀)
    std::vector<CStringW> tokens;
    Tokenize(trimmed, tokens);
    if (tokens.empty()) return false;

    // STRINGTABLE 特殊处理（无资源ID）
    if (tokens[0].CompareNoCase(L"STRINGTABLE") == 0)
    {
        outName = L"";
        outTypeKeyword = L"STRINGTABLE";
        return true;
    }

    // 需要至少两个 token: ID KEYWORD
    if (tokens.size() < 2) return false;

    // 第一个 token 应该是标识符（字母/下划线开头，或纯数字）
    wchar_t firstChar = tokens[0].GetAt(0);
    if (!iswalpha(firstChar) && firstChar != L'_' && !iswdigit(firstChar))
        return false;

    // 第二个 token 检查是否为资源类型关键词
    for (const auto& kw : s_resourceKeywords)
    {
        if (tokens[1].CompareNoCase(kw) == 0)
        {
            // 排除 GUIDELINES DESIGNINFO 中的伪条目
            // DESIGNINFO 条目格式: "IDD_X, DIALOG"（名称后紧跟逗号）
            // 真正的资源头格式:   "IDD_X DIALOGEX 0, 0, w, h"（名称后紧跟空白）
            int nameLen = tokens[0].GetLength();
            if (nameLen < trimmed.GetLength())
            {
                int pos = nameLen;
                while (pos < trimmed.GetLength() && (trimmed[pos] == L' ' || trimmed[pos] == L'\t'))
                    pos++;
                if (pos < trimmed.GetLength() && trimmed[pos] == L',')
                    return false;  // DESIGNINFO 伪条目，不是真正的资源头
            }

            outName = tokens[0];
            outTypeKeyword = tokens[1];
            return true;
        }
    }

    return false;
}

bool CRcFileParser::IdentifyBlocks(SRcFileContent& content)
{
    bool inBlockComment = false;

    for (int i = 0; i < (int)content.lines.size(); ++i)
    {
        SRcLine& line = content.lines[i];
        CStringW trimmed = TrimLine(line.text);

        // 处理 C 风格多行注释
        if (inBlockComment)
        {
            line.type = ERcLineType::Comment;
            if (trimmed.Find(L"*/") >= 0)
                inBlockComment = false;
            continue;
        }
        if (trimmed.Left(2) == L"/*")
        {
            line.type = ERcLineType::Comment;
            if (trimmed.Find(L"*/") < 0)
                inBlockComment = true;
            continue;
        }

        line.type = ClassifyLine(line.text);
    }

    // ---- 预扫描：标记 GUIDELINES DESIGNINFO 的行范围，后续跳过 ----
    // GUIDELINES DESIGNINFO (或 DESIGNINFO 单独出现) 的 BEGIN/END 内部含有
    // "IDD_X, DIALOG" 伪条目。虽然 IsResourceHeaderLine 已排除逗号格式，
    // 此处额外将整个 DESIGNINFO 范围标记为 Comment 以避免干扰。
    {
        for (int i = 0; i < (int)content.lines.size(); ++i)
        {
            CStringW trimmed = TrimLine(content.lines[i].text);
            CStringW upper = trimmed;
            upper.MakeUpper();
            // 匹配 "GUIDELINES DESIGNINFO" 或单独 "DESIGNINFO" 行
            if (upper.Find(L"DESIGNINFO") < 0)
                continue;
            // 查找后续 BEGIN
            int j = i + 1;
            while (j < (int)content.lines.size())
            {
                CStringW t = TrimLine(content.lines[j].text);
                if (t.IsEmpty() || content.lines[j].type == ERcLineType::Comment ||
                    content.lines[j].type == ERcLineType::Preprocessor)
                { ++j; continue; }
                break;
            }
            if (j < (int)content.lines.size() && IsBlockBegin(content.lines[j].text))
            {
                // 标记从 DESIGNINFO 行到其匹配 END 的所有行为 Comment（跳过）
                content.lines[i].type = ERcLineType::Comment;
                int depth = 1;
                int k = j + 1;
                content.lines[j].type = ERcLineType::Comment;
                while (k < (int)content.lines.size() && depth > 0)
                {
                    CStringW tk = TrimLine(content.lines[k].text);
                    if (IsBlockBegin(tk)) ++depth;
                    else if (IsBlockEnd(tk)) --depth;
                    content.lines[k].type = ERcLineType::Comment;
                    ++k;
                }
                i = (k > 0) ? k - 1 : i; // 跳过已处理的范围
            }
        }
    }

    // 识别资源块
    int blockIdx = 0;
    for (int i = 0; i < (int)content.lines.size(); ++i)
    {
        SRcLine& line = content.lines[i];
        if (line.type == ERcLineType::Comment ||
            line.type == ERcLineType::Preprocessor ||
            line.type == ERcLineType::Blank)
            continue;

        CStringW name, typeKeyword;
        if (!IsResourceHeaderLine(line.text, name, typeKeyword))
            continue;

        line.type = ERcLineType::ResourceHeader;

        SRcBlock block;
        block.type = KeywordToBlockType(typeKeyword);
        block.resourceName = name;
        block.resourceTypeKeyword = typeKeyword;
        block.headerLineIndex = i;
        block.blockIndex = blockIdx;

        // DIALOGEX 的属性行 (STYLE, EXSTYLE, CAPTION, FONT, CLASS, MENU) 在 BEGIN 之前
        // 寻找 BEGIN/{ 行
        int j = i + 1;

        // 收集 Dialog 属性行（STYLE, EXSTYLE, CAPTION, FONT, CLASS, MENU 在 BEGIN 之前）
        while (j < (int)content.lines.size())
        {
            CStringW trimJ = TrimLine(content.lines[j].text);
            if (trimJ.IsEmpty() || content.lines[j].type == ERcLineType::Blank)
            {
                content.lines[j].type = ERcLineType::Blank;
                ++j;
                continue;
            }
            if (content.lines[j].type == ERcLineType::Comment)
            {
                ++j;
                continue;
            }
            if (IsBlockBegin(trimJ))
                break;

            // Dialog 属性行
            CStringW upper = trimJ;
            upper.MakeUpper();
            bool isAttr = (upper.Left(5) == L"STYLE" ||
                          upper.Left(7) == L"EXSTYLE" ||
                          upper.Left(7) == L"CAPTION" ||
                          upper.Left(4) == L"FONT" ||
                          upper.Left(5) == L"CLASS" ||
                          upper.Left(4) == L"MENU");
            if (isAttr || (block.type == ERcBlockType::Dialog))
            {
                content.lines[j].type = ERcLineType::Content;
                block.attributeLineIndices.push_back(j);
            }
            ++j;
        }

        // 找到 BEGIN/{
        if (j < (int)content.lines.size() && IsBlockBegin(content.lines[j].text))
        {
            block.beginLineIndex = j;
            content.lines[j].type = ERcLineType::BlockBegin;
            content.lines[j].blockIndex = blockIdx;
        }

        // 寻找匹配的 END/}（处理嵌套）
        int depth = 1;
        int k = j + 1;
        while (k < (int)content.lines.size() && depth > 0)
        {
            CStringW trimK = TrimLine(content.lines[k].text);
            if (IsBlockBegin(trimK))
            {
                ++depth;
                content.lines[k].type = ERcLineType::BlockBegin;
            }
            else if (IsBlockEnd(trimK))
            {
                --depth;
                if (depth == 0)
                {
                    block.endLineIndex = k;
                    content.lines[k].type = ERcLineType::BlockEnd;
                    content.lines[k].blockIndex = blockIdx;
                }
                else
                {
                    content.lines[k].type = ERcLineType::BlockEnd;
                }
            }
            else if (content.lines[k].type != ERcLineType::Comment &&
                     content.lines[k].type != ERcLineType::Preprocessor &&
                     !trimK.IsEmpty())
            {
                content.lines[k].type = ERcLineType::Content;
            }
            content.lines[k].blockIndex = blockIdx;
            ++k;
        }

        // 标记属性行和header行的blockIndex
        content.lines[i].blockIndex = blockIdx;
        for (int attrIdx : block.attributeLineIndices)
            content.lines[attrIdx].blockIndex = blockIdx;

        content.blocks.push_back(std::move(block));
        ++blockIdx;

        // 跳过已处理的行
        i = (k > 0) ? k - 1 : i;
    }

    // 建立索引
    for (int b = 0; b < (int)content.blocks.size(); ++b)
    {
        content.blocks[b].blockIndex = b;
        if (!content.blocks[b].resourceName.IsEmpty())
        {
            content.nameToBlockIndex[content.blocks[b].resourceName] = b;
        }
    }

    return true;
}

// ============================================================================
// 辅助函数
// ============================================================================

void CRcFileParser::Tokenize(const CStringW& line, std::vector<CStringW>& tokens) const
{
    tokens.clear();
    int len = line.GetLength();
    int i = 0;

    while (i < len)
    {
        // 跳过空白
        while (i < len && iswspace(line[i])) ++i;
        if (i >= len) break;

        // 处理带引号的字符串
        if (line[i] == L'"')
        {
            int start = i;
            ++i;
            while (i < len)
            {
                if (line[i] == L'"' && (i + 1 >= len || line[i + 1] != L'"'))
                {
                    ++i;
                    break;
                }
                if (line[i] == L'"' && i + 1 < len && line[i + 1] == L'"')
                    i += 2; // 转义的引号
                else
                    ++i;
            }
            tokens.push_back(line.Mid(start, i - start));
            continue;
        }

        // 处理逗号作为分隔符
        if (line[i] == L',')
        {
            ++i;
            continue;
        }

        // 处理 | 运算符
        if (line[i] == L'|')
        {
            tokens.push_back(L"|");
            ++i;
            continue;
        }

        // 常规 token
        int start = i;
        while (i < len && !iswspace(line[i]) && line[i] != L',' && line[i] != L'|')
        {
            if (line[i] == L'"') break;
            ++i;
        }
        if (i > start)
            tokens.push_back(line.Mid(start, i - start));
    }
}

UINT CRcFileParser::ResolveIdentifier(const CStringW& token, const SResourceHeader& header) const
{
    // 先尝试作为宏名查找
    auto it = header.nameToId.find(token);
    if (it != header.nameToId.end())
        return it->second;

    // 尝试作为数字解析
    CStringW t = token;
    t.Trim();
    if (t.IsEmpty()) return 0;

    wchar_t* endPtr = nullptr;
    if (t.Left(2) == L"0x" || t.Left(2) == L"0X")
    {
        unsigned long v = wcstoul(t.GetString(), &endPtr, 16);
        if (endPtr != t.GetString()) return (UINT)v;
    }
    else
    {
        // 处理可能的负数（如 IDC_STATIC = -1）
        long v = wcstol(t.GetString(), &endPtr, 10);
        if (endPtr != t.GetString()) return (UINT)v;
    }

    return 0;
}

CStringW CRcFileParser::ExtractQuotedString(const CStringW& text, int& pos) const
{
    if (pos >= text.GetLength() || text[pos] != L'"')
        return L"";

    ++pos; // skip opening quote
    CStringW result;
    while (pos < text.GetLength())
    {
        if (text[pos] == L'"')
        {
            if (pos + 1 < text.GetLength() && text[pos + 1] == L'"')
            {
                result += L'"';
                pos += 2;
            }
            else
            {
                ++pos; // skip closing quote
                break;
            }
        }
        else
        {
            result += text[pos];
            ++pos;
        }
    }
    return result;
}

SIndentStyle CRcFileParser::DetectIndentStyle(const SRcFileContent& content) const
{
    SIndentStyle style;
    int tabCount = 0;
    int spaceCount = 0;
    int totalSpaces = 0;
    int spaceLines = 0;

    for (const auto& line : content.lines)
    {
        if (line.text.IsEmpty()) continue;
        wchar_t first = line.text.GetAt(0);
        if (first == L'\t')
            ++tabCount;
        else if (first == L' ')
        {
            ++spaceCount;
            int spaces = 0;
            for (int i = 0; i < line.text.GetLength() && line.text[i] == L' '; ++i)
                ++spaces;
            if (spaces > 0)
            {
                totalSpaces += spaces;
                ++spaceLines;
            }
        }
    }

    style.useTabs = (tabCount >= spaceCount);
    if (!style.useTabs && spaceLines > 0)
    {
        int avg = totalSpaces / spaceLines;
        style.indentWidth = (avg <= 2) ? 2 : (avg <= 4) ? 4 : 8;
    }
    else
    {
        style.indentWidth = 1; // tab
    }

    return style;
}

CStringW CRcFileParser::DetectIdPrefix(const CStringW& name) const
{
    int underscorePos = name.Find(L'_');
    if (underscorePos > 0 && underscorePos < name.GetLength() - 1)
        return name.Left(underscorePos + 1);
    return L"";
}

// ============================================================================
// 语义层解析
// ============================================================================

bool CRcFileParser::ParseResourceBlocks(const SRcFileContent& content,
                                         const SResourceHeader& header,
                                         SSingleResourceInfo& outResInfo)
{
    outResInfo = SSingleResourceInfo();

    UINT syntheticIdCounter = 0; // 用于为未解析的宏名分配合成ID，从0xFFF0递减

    for (const auto& block : content.blocks)
    {
        // 解析资源ID
        UINT resId = ResolveIdentifier(block.resourceName, header);

        switch (block.type)
        {
        case ERcBlockType::Dialog:
        {
            auto pDlg = std::make_shared<SDialogInfo>();
            if (ParseDialogBlock(content, block, header, pDlg))
            {
                pDlg->csResourceName = block.resourceName;
                // 当宏未解析(resId==0)且map中已存在key 0时，分配合成ID避免覆盖
                if (resId == 0 && outResInfo.mapLangAndDialogInfo.count(0) > 0)
                {
                    if (syntheticIdCounter == 0)
                        syntheticIdCounter = 0xFFF0;
                    resId = syntheticIdCounter--;
                }
                pDlg->nIDD = resId;
                outResInfo.mapLangAndDialogInfo[resId] = pDlg;
            }
            break;
        }
        case ERcBlockType::StringTable:
        {
            ParseStringTableBlock(content, block, header, outResInfo.mapLangAndStringTableInfo);
            break;
        }
        case ERcBlockType::Menu:
        {
            auto pMenu = std::make_shared<SMenuInfo>();
            if (ParseMenuBlock(content, block, header, pMenu))
            {
                pMenu->nID = resId;
                outResInfo.mapLangAndMenuInfo[resId] = pMenu;
            }
            break;
        }
        case ERcBlockType::Toolbar:
        {
            auto pToolbar = std::make_shared<SToolbarInfo>();
            if (ParseToolbarBlock(content, block, header, pToolbar))
            {
                pToolbar->nToolbarID = resId;
                outResInfo.mapLangAndToolbarInfo[resId] = pToolbar;
            }
            break;
        }
        case ERcBlockType::VersionInfo:
        {
            auto pVersion = std::make_shared<SVersionInfo>();
            if (ParseVersionInfoBlock(content, block, header, pVersion))
            {
                pVersion->nVersionID = resId;
                outResInfo.pVersionInfo = pVersion;
            }
            break;
        }
        case ERcBlockType::Accelerators:
        {
            // 加速器表解析（暂存结构信息，不生成专用结构）
            ParseAcceleratorsBlock(content, block, header);
            break;
        }
        default:
            break;
        }
    }

    return true;
}

// ============================================================================
// Dialog 块解析
// ============================================================================

bool CRcFileParser::ParseDialogBlock(const SRcFileContent& content,
                                      const SRcBlock& block,
                                      const SResourceHeader& header,
                                      std::shared_ptr<SDialogInfo>& outDialog)
{
    if (!outDialog) outDialog = std::make_shared<SDialogInfo>();

    // 解析 header 行: ID DIALOGEX x, y, cx, cy
    const CStringW& headerLine = content.lines[block.headerLineIndex].text;
    std::vector<CStringW> tokens;
    Tokenize(headerLine, tokens);

    // tokens[0] = ID, tokens[1] = DIALOGEX/DIALOG, tokens[2..5] = x,y,cx,cy
    if (tokens.size() >= 6)
    {
        outDialog->rectDlg.left = _wtoi(tokens[2].GetString());
        outDialog->rectDlg.top = _wtoi(tokens[3].GetString());
        outDialog->rectDlg.right = outDialog->rectDlg.left + _wtoi(tokens[4].GetString());
        outDialog->rectDlg.bottom = outDialog->rectDlg.top + _wtoi(tokens[5].GetString());
    }

    CStringW typeKw = block.resourceTypeKeyword;
    typeKw.MakeUpper();
    outDialog->bIsDlgTemplateEx = (typeKw == L"DIALOGEX");

    // 解析属性行 (STYLE, EXSTYLE, CAPTION, FONT, CLASS, MENU)
    for (int attrIdx : block.attributeLineIndices)
    {
        CStringW attrLine = TrimLine(content.lines[attrIdx].text);
        CStringW upper = attrLine;
        upper.MakeUpper();

        if (upper.Left(7) == L"CAPTION")
        {
            int pos = attrLine.Find(L'"');
            if (pos >= 0)
                outDialog->csCaption = ExtractQuotedString(attrLine, pos);
        }
        else if (upper.Left(4) == L"FONT")
        {
            // FONT pointSize, "faceName"[, weight[, italic[, charset]]]
            CStringW fontLine = TrimLine(attrLine.Mid(4));
            std::vector<CStringW> fontTokens;
            Tokenize(fontLine, fontTokens);
            if (!fontTokens.empty())
            {
                outDialog->pFontInfo = std::make_shared<SFontInfo>();
                outDialog->pFontInfo->wPointSize = (WORD)_wtoi(fontTokens[0].GetString());
                if (fontTokens.size() > 1 && fontTokens[1].GetAt(0) == L'"')
                {
                    int p = 0;
                    outDialog->pFontInfo->csFaceName = ExtractQuotedString(fontTokens[1], p);
                }
                if (fontTokens.size() > 2)
                    outDialog->pFontInfo->wWeight = (WORD)_wtoi(fontTokens[2].GetString());
                if (fontTokens.size() > 3)
                    outDialog->pFontInfo->bItalic = (BYTE)_wtoi(fontTokens[3].GetString());
                if (fontTokens.size() > 4)
                    outDialog->pFontInfo->bCharSet = (BYTE)_wtoi(fontTokens[4].GetString());
            }
        }
        else if (upper.Left(7) == L"EXSTYLE")
        {
            // EXSTYLE value | value | ...
            CStringW styleLine = TrimLine(attrLine.Mid(7));
            // 合并 | 分隔的值
            DWORD style = 0;
            std::vector<CStringW> styleTokens;
            Tokenize(styleLine, styleTokens);
            for (const auto& st : styleTokens)
            {
                if (st == L"|") continue;
                style |= ResolveIdentifier(st, header);
            }
            outDialog->dwExStyle = style;
        }
        else if (upper.Left(5) == L"STYLE")
        {
            CStringW styleLine = TrimLine(attrLine.Mid(5));
            DWORD style = 0;
            std::vector<CStringW> styleTokens;
            Tokenize(styleLine, styleTokens);
            for (const auto& st : styleTokens)
            {
                if (st == L"|" || st == L"NOT") continue;
                style |= ResolveIdentifier(st, header);
            }
            outDialog->dwStyle = style;
        }
        else if (upper.Left(5) == L"CLASS")
        {
            CStringW classLine = TrimLine(attrLine.Mid(5));
            int pos = classLine.Find(L'"');
            if (pos >= 0)
                outDialog->csClassName = ExtractQuotedString(classLine, pos);
            else
                outDialog->csClassName = classLine;
        }
        else if (upper.Left(4) == L"MENU")
        {
            CStringW menuLine = TrimLine(attrLine.Mid(4));
            outDialog->csMenuName = menuLine;
        }
    }

    // 解析控件行 (在 BEGIN...END 内)
    if (block.beginLineIndex >= 0 && block.endLineIndex >= 0)
    {
        for (int i = block.beginLineIndex + 1; i < block.endLineIndex; ++i)
        {
            const CStringW& ctrlLine = content.lines[i].text;
            CStringW trimmed = TrimLine(ctrlLine);
            if (trimmed.IsEmpty()) continue;
            if (content.lines[i].type == ERcLineType::Comment) continue;

            auto pCtrl = std::make_shared<SCtrlInfo>();
            if (ParseControlLine(trimmed, header, pCtrl))
            {
                outDialog->vecCtrlInfo.push_back(pCtrl);
            }
        }
    }

    return true;
}

// ============================================================================
// 控件行解析
// ============================================================================

bool CRcFileParser::ParseControlLine(const CStringW& line,
                                      const SResourceHeader& header,
                                      std::shared_ptr<SCtrlInfo>& outCtrl)
{
    if (!outCtrl) outCtrl = std::make_shared<SCtrlInfo>();

    std::vector<CStringW> tokens;
    Tokenize(line, tokens);
    if (tokens.empty()) return false;

    CStringW keyword = tokens[0];
    keyword.MakeUpper();

    // 通用 CONTROL 语法:
    // CONTROL "text", id, "className", style, x, y, cx, cy [, exStyle]
    if (keyword == L"CONTROL")
    {
        if (tokens.size() < 8) return false;
        int idx = 1;

        // text
        if (tokens[idx].GetAt(0) == L'"')
        {
            int pos = 0;
            outCtrl->csText = ExtractQuotedString(tokens[idx], pos);
        }
        else
        {
            outCtrl->csText = tokens[idx];
        }
        ++idx;

        // id
        outCtrl->nIDC = ResolveIdentifier(tokens[idx], header);
        ++idx;

        // className
        if (tokens[idx].GetAt(0) == L'"')
        {
            int pos = 0;
            outCtrl->csClassName = ExtractQuotedString(tokens[idx], pos);
        }
        else
        {
            outCtrl->csClassName = tokens[idx];
        }
        ++idx;

        // style (可能含 | 组合)
        DWORD style = 0;
        while (idx < (int)tokens.size())
        {
            CStringW t = tokens[idx];
            if (t == L"|") { ++idx; continue; }
            // 检查是否为坐标数字（连续 4 个数字作为 x,y,cx,cy）
            bool isNum = true;
            for (int c = 0; c < t.GetLength(); ++c)
            {
                if (!iswdigit(t[c]) && t[c] != L'-') { isNum = false; break; }
            }
            if (isNum)
                break;
            style |= ResolveIdentifier(t, header);
            ++idx;
        }
        outCtrl->dwStyle = style | WS_CHILD | WS_VISIBLE;

        // x, y, cx, cy
        if (idx + 3 < (int)tokens.size())
        {
            outCtrl->ctrlRect.left = _wtoi(tokens[idx].GetString());
            outCtrl->ctrlRect.top = _wtoi(tokens[idx + 1].GetString());
            int cx = _wtoi(tokens[idx + 2].GetString());
            int cy = _wtoi(tokens[idx + 3].GetString());
            outCtrl->ctrlRect.right = outCtrl->ctrlRect.left + cx;
            outCtrl->ctrlRect.bottom = outCtrl->ctrlRect.top + cy;
            idx += 4;
        }

        // optional exStyle
        if (idx < (int)tokens.size())
        {
            DWORD exStyle = 0;
            while (idx < (int)tokens.size())
            {
                if (tokens[idx] == L"|") { ++idx; continue; }
                exStyle |= ResolveIdentifier(tokens[idx], header);
                ++idx;
            }
            outCtrl->dwExStyle = exStyle;
        }
        return true;
    }

    // 简写控件语法:
    // PUSHBUTTON "text", id, x, y, cx, cy [, style [, exStyle]]
    // DEFPUSHBUTTON, EDITTEXT, LTEXT, RTEXT, CTEXT, GROUPBOX,
    // COMBOBOX, LISTBOX, SCROLLBAR, ICON, CHECKBOX, RADIOBUTTON, AUTO3STATE 等

    struct ShorthandDef {
        const wchar_t* keyword;
        const wchar_t* className;
        bool hasText;  // 是否第一个参数是文本
    };

    static const ShorthandDef shorthandDefs[] = {
        { L"PUSHBUTTON",     L"BUTTON",    true  },
        { L"DEFPUSHBUTTON",  L"BUTTON",    true  },
        { L"CHECKBOX",       L"BUTTON",    true  },
        { L"AUTOCHECKBOX",   L"BUTTON",    true  },
        { L"RADIOBUTTON",    L"BUTTON",    true  },
        { L"AUTORADIOBUTTON",L"BUTTON",    true  },
        { L"STATE3",         L"BUTTON",    true  },
        { L"AUTO3STATE",     L"BUTTON",    true  },
        { L"GROUPBOX",       L"BUTTON",    true  },
        { L"LTEXT",          L"STATIC",    true  },
        { L"RTEXT",          L"STATIC",    true  },
        { L"CTEXT",          L"STATIC",    true  },
        { L"ICON",           L"STATIC",    true  },
        { L"EDITTEXT",       L"EDIT",      false },
        { L"COMBOBOX",       L"COMBOBOX",  false },
        { L"LISTBOX",        L"LISTBOX",   false },
        { L"SCROLLBAR",      L"SCROLLBAR", false },
    };

    for (const auto& def : shorthandDefs)
    {
        if (keyword != def.keyword) continue;

        outCtrl->csClassName = def.className;
        int idx = 1;

        if (def.hasText)
        {
            // text
            if (idx < (int)tokens.size())
            {
                if (tokens[idx].GetAt(0) == L'"')
                {
                    int pos = 0;
                    outCtrl->csText = ExtractQuotedString(tokens[idx], pos);
                }
                else
                {
                    outCtrl->csText = tokens[idx];
                }
                ++idx;
            }
        }

        // id
        if (idx < (int)tokens.size())
        {
            outCtrl->nIDC = ResolveIdentifier(tokens[idx], header);
            ++idx;
        }

        // x, y, cx, cy
        if (idx + 3 < (int)tokens.size())
        {
            outCtrl->ctrlRect.left = _wtoi(tokens[idx].GetString());
            outCtrl->ctrlRect.top = _wtoi(tokens[idx + 1].GetString());
            int cx = _wtoi(tokens[idx + 2].GetString());
            int cy = _wtoi(tokens[idx + 3].GetString());
            outCtrl->ctrlRect.right = outCtrl->ctrlRect.left + cx;
            outCtrl->ctrlRect.bottom = outCtrl->ctrlRect.top + cy;
            idx += 4;
        }

        // 可选的 style
        if (idx < (int)tokens.size())
        {
            DWORD style = 0;
            while (idx < (int)tokens.size())
            {
                if (tokens[idx] == L"|") { ++idx; continue; }
                // 检查是否进入了 exStyle 区域（用逗号分隔）
                // 简化处理：全部作为 style 或
                style |= ResolveIdentifier(tokens[idx], header);
                ++idx;
            }
            outCtrl->dwStyle = style | WS_CHILD | WS_VISIBLE;
        }
        else
        {
            outCtrl->dwStyle = WS_CHILD | WS_VISIBLE;
        }

        return true;
    }

    return false;
}

// ============================================================================
// StringTable 解析
// ============================================================================

bool CRcFileParser::ParseStringTableBlock(const SRcFileContent& content,
                                           const SRcBlock& block,
                                           const SResourceHeader& header,
                                           std::map<UINT, std::shared_ptr<SStringTableInfo>>& outStringTables)
{
    if (block.beginLineIndex < 0 || block.endLineIndex < 0)
        return false;

    for (int i = block.beginLineIndex + 1; i < block.endLineIndex; ++i)
    {
        CStringW trimmed = TrimLine(content.lines[i].text);
        if (trimmed.IsEmpty()) continue;
        if (content.lines[i].type == ERcLineType::Comment) continue;

        // 格式: ID "text" 或 ID, "text"
        std::vector<CStringW> tokens;
        Tokenize(trimmed, tokens);
        if (tokens.size() < 2) continue;

        UINT strId = ResolveIdentifier(tokens[0], header);
        CStringW text;
        // 从第二个token提取引号字符串
        int pos = trimmed.Find(L'"');
        if (pos >= 0)
            text = ExtractQuotedString(trimmed, pos);

        // 确定字符串表块ID（每块16个字符串）
        UINT blockId = (strId / 16) + 1;

        auto it = outStringTables.find(blockId);
        if (it == outStringTables.end())
        {
            auto pST = std::make_shared<SStringTableInfo>();
            pST->nResourceStringID = blockId;
            outStringTables[blockId] = pST;
            it = outStringTables.find(blockId);
        }
        it->second->mapIDAndString[strId] = text;
    }

    return true;
}

// ============================================================================
// Menu 解析
// ============================================================================

static bool ParseMenuItems(const std::vector<SRcLine>& lines,
                           int startIdx, int endIdx,
                           const SResourceHeader& header,
                           const CRcFileParser* parser,
                           std::shared_ptr<SMenuInfo>& parentMenu);

bool CRcFileParser::ParseMenuBlock(const SRcFileContent& content,
                                    const SRcBlock& block,
                                    const SResourceHeader& header,
                                    std::shared_ptr<SMenuInfo>& outMenu)
{
    if (!outMenu) outMenu = std::make_shared<SMenuInfo>();
    outMenu->bIsPopup = true; // 顶层菜单即 popup

    if (block.beginLineIndex < 0 || block.endLineIndex < 0)
        return false;

    ParseMenuItems(content.lines, block.beginLineIndex + 1, block.endLineIndex,
                   header, this, outMenu);

    return true;
}

static bool ParseMenuItems(const std::vector<SRcLine>& lines,
                           int startIdx, int endIdx,
                           const SResourceHeader& header,
                           const CRcFileParser* parser,
                           std::shared_ptr<SMenuInfo>& parentMenu)
{
    for (int i = startIdx; i < endIdx; ++i)
    {
        CStringW trimmed = lines[i].text;
        trimmed.TrimLeft();
        trimmed.TrimRight();
        if (trimmed.IsEmpty()) continue;
        if (lines[i].type == ERcLineType::Comment) continue;

        CStringW upper = trimmed;
        upper.MakeUpper();

        if (upper.Left(5) == L"POPUP")
        {
            auto pSub = std::make_shared<SMenuInfo>();
            pSub->bIsPopup = true;
            // 提取文本
            int pos = trimmed.Find(L'"');
            if (pos >= 0)
            {
                ++pos;
                CStringW text;
                while (pos < trimmed.GetLength() && trimmed[pos] != L'"')
                {
                    text += trimmed[pos];
                    ++pos;
                }
                pSub->csText = text;
            }

            // 找到对应的 BEGIN...END
            int j = i + 1;
            while (j < endIdx)
            {
                CStringW t = lines[j].text;
                t.TrimLeft(); t.TrimRight();
                t.MakeUpper();
                if (t == L"BEGIN" || t == L"{")
                    break;
                ++j;
            }

            int beginIdx = j;
            int depth = 1;
            int k = beginIdx + 1;
            while (k < endIdx && depth > 0)
            {
                CStringW t = lines[k].text;
                t.TrimLeft(); t.TrimRight();
                t.MakeUpper();
                if (t == L"BEGIN" || t == L"{") ++depth;
                else if (t == L"END" || t == L"}") --depth;
                if (depth > 0) ++k;
            }
            int endSubIdx = k;

            // 递归解析子菜单
            ParseMenuItems(lines, beginIdx + 1, endSubIdx, header, parser, pSub);

            parentMenu->vecSubMenu.push_back(pSub);
            i = endSubIdx; // 跳过已处理区域
        }
        else if (upper.Left(9) == L"SEPARATOR")
        {
            auto pSep = std::make_shared<SMenuInfo>();
            pSep->nType = MFT_SEPARATOR;
            pSep->csText = L"";
            pSep->nID = 0;
            parentMenu->vecSubMenu.push_back(pSep);
        }
        else if (upper.Left(8) == L"MENUITEM")
        {
            CStringW rest = trimmed.Mid(8);
            rest.TrimLeft();
            // MENUITEM SEPARATOR 特殊情况
            CStringW restUpper = rest;
            restUpper.MakeUpper();
            if (restUpper.Left(9) == L"SEPARATOR")
            {
                auto pSep = std::make_shared<SMenuInfo>();
                pSep->nType = MFT_SEPARATOR;
                parentMenu->vecSubMenu.push_back(pSep);
                continue;
            }

            auto pItem = std::make_shared<SMenuInfo>();
            // 提取文本
            int pos = rest.Find(L'"');
            if (pos >= 0)
            {
                ++pos;
                CStringW text;
                while (pos < rest.GetLength() && rest[pos] != L'"')
                {
                    text += rest[pos];
                    ++pos;
                }
                pItem->csText = text;
                ++pos; // skip closing quote

                // 跳过逗号和空白
                while (pos < rest.GetLength() && (rest[pos] == L',' || iswspace(rest[pos])))
                    ++pos;

                // ID
                CStringW idStr;
                while (pos < rest.GetLength() && !iswspace(rest[pos]) && rest[pos] != L',')
                {
                    idStr += rest[pos];
                    ++pos;
                }
                if (!idStr.IsEmpty())
                {
                    auto it = header.nameToId.find(idStr);
                    if (it != header.nameToId.end())
                        pItem->nID = it->second;
                    else
                        pItem->nID = (UINT)_wtoi(idStr.GetString());
                }
            }
            parentMenu->vecSubMenu.push_back(pItem);
        }
        // 忽略 BEGIN/END 行（已在块解析阶段处理）
    }
    return true;
}

// ============================================================================
// Toolbar 解析
// ============================================================================

bool CRcFileParser::ParseToolbarBlock(const SRcFileContent& content,
                                       const SRcBlock& block,
                                       const SResourceHeader& header,
                                       std::shared_ptr<SToolbarInfo>& outToolbar)
{
    if (!outToolbar) outToolbar = std::make_shared<SToolbarInfo>();

    // Header: ID TOOLBAR width, height
    const CStringW& hdrLine = content.lines[block.headerLineIndex].text;
    std::vector<CStringW> tokens;
    Tokenize(hdrLine, tokens);
    // tokens: [ID, TOOLBAR, width, height]
    if (tokens.size() >= 4)
    {
        outToolbar->wWidth = (uint16_t)_wtoi(tokens[2].GetString());
        outToolbar->wHeight = (uint16_t)_wtoi(tokens[3].GetString());
    }

    if (block.beginLineIndex < 0 || block.endLineIndex < 0)
        return false;

    // 解析 BUTTON ID 行
    for (int i = block.beginLineIndex + 1; i < block.endLineIndex; ++i)
    {
        CStringW trimmed = TrimLine(content.lines[i].text);
        if (trimmed.IsEmpty()) continue;
        if (content.lines[i].type == ERcLineType::Comment) continue;

        CStringW upper = trimmed;
        upper.MakeUpper();

        if (upper.Left(6) == L"BUTTON")
        {
            CStringW rest = TrimLine(trimmed.Mid(6));
            UINT btnId = ResolveIdentifier(rest, header);
            outToolbar->vecItemIDs.push_back((uint16_t)btnId);
        }
        else if (upper == L"SEPARATOR")
        {
            outToolbar->vecItemIDs.push_back(0); // separator = ID 0
        }
    }

    return true;
}

// ============================================================================
// VersionInfo 解析
// ============================================================================

bool CRcFileParser::ParseVersionInfoBlock(const SRcFileContent& content,
                                           const SRcBlock& block,
                                           const SResourceHeader& header,
                                           std::shared_ptr<SVersionInfo>& outVersionInfo)
{
    if (!outVersionInfo) outVersionInfo = std::make_shared<SVersionInfo>();

    // VERSIONINFO 属性行
    for (int attrIdx : block.attributeLineIndices)
    {
        CStringW attrLine = TrimLine(content.lines[attrIdx].text);
        CStringW upper = attrLine;
        upper.MakeUpper();

        if (upper.Left(11) == L"FILEVERSION")
        {
            CStringW rest = TrimLine(attrLine.Mid(11));
            std::vector<CStringW> parts;
            Tokenize(rest, parts);
            if (parts.size() >= 4)
            {
                outVersionInfo->fileInfo.dwFileVersionMS =
                    MAKELONG(_wtoi(parts[1].GetString()), _wtoi(parts[0].GetString()));
                outVersionInfo->fileInfo.dwFileVersionLS =
                    MAKELONG(_wtoi(parts[3].GetString()), _wtoi(parts[2].GetString()));
            }
        }
        else if (upper.Left(14) == L"PRODUCTVERSION")
        {
            CStringW rest = TrimLine(attrLine.Mid(14));
            std::vector<CStringW> parts;
            Tokenize(rest, parts);
            if (parts.size() >= 4)
            {
                outVersionInfo->fileInfo.dwProductVersionMS =
                    MAKELONG(_wtoi(parts[1].GetString()), _wtoi(parts[0].GetString()));
                outVersionInfo->fileInfo.dwProductVersionLS =
                    MAKELONG(_wtoi(parts[3].GetString()), _wtoi(parts[2].GetString()));
            }
        }
    }

    // 在 BEGIN...END 块中解析 BLOCK "StringFileInfo" 等
    if (block.beginLineIndex >= 0 && block.endLineIndex >= 0)
    {
        for (int i = block.beginLineIndex + 1; i < block.endLineIndex; ++i)
        {
            CStringW trimmed = TrimLine(content.lines[i].text);
            if (trimmed.IsEmpty()) continue;

            CStringW upper = trimmed;
            upper.MakeUpper();

            // VALUE "key", "value"
            if (upper.Left(5) == L"VALUE")
            {
                CStringW rest = TrimLine(trimmed.Mid(5));
                int pos = 0;
                // 跳到第一个引号
                while (pos < rest.GetLength() && rest[pos] != L'"') ++pos;
                if (pos < rest.GetLength())
                {
                    CStringW key = ExtractQuotedString(rest, pos);
                    // 跳过逗号和空白
                    while (pos < rest.GetLength() && (rest[pos] == L',' || iswspace(rest[pos]))) ++pos;
                    if (pos < rest.GetLength() && rest[pos] == L'"')
                    {
                        CStringW val = ExtractQuotedString(rest, pos);
                        if (key.CompareNoCase(L"Translation") != 0)
                        {
                            outVersionInfo->mapStringInfo[key] = val;
                        }
                    }
                }
            }
        }
    }

    outVersionInfo->fileInfo.dwSignature = 0xFEEF04BD;
    outVersionInfo->fileInfo.dwStrucVersion = 0x00010000;

    return true;
}

// ============================================================================
// Accelerators 解析（基础存储，无专用结构）
// ============================================================================

bool CRcFileParser::ParseAcceleratorsBlock(const SRcFileContent& content,
                                            const SRcBlock& block,
                                            const SResourceHeader& header)
{
    // 当前不生成专用结构，仅确保块被正确识别
    // 加速器数据保留在 SRcFileContent 的行结构中，导出时原样输出
    return true;
}

// ============================================================================
// 完整解析流程
// ============================================================================

bool CRcFileParser::ParseAll(const CStringW& rcPath,
                              const CStringW& headerPath,
                              SRcFileContent& outRcContent,
                              SResourceHeader& outHeader,
                              SSingleResourceInfo& outResInfo)
{
    m_errors.clear();
    m_warnings.clear();

    // 1. 解析 resource.h
    if (!headerPath.IsEmpty())
    {
        if (!ParseResourceHeader(headerPath, outHeader))
        {
            AddWarning(L"Failed to parse resource.h: " + headerPath);
            // 不阻断，继续解析 RC（没有 resource.h 只是无法解析宏名）
        }
    }

    // 2. 读取 RC 文件
    if (!ReadFileToLines(rcPath, outRcContent))
        return false;

    // 3. 识别资源块
    if (!IdentifyBlocks(outRcContent))
        return false;

    // 4. 解析资源ID（将宏名映射到数值）
    for (auto& block : outRcContent.blocks)
    {
        if (!block.resourceName.IsEmpty())
        {
            block.resourceID = ResolveIdentifier(block.resourceName, outHeader);
            if (block.resourceID > 0)
            {
                outRcContent.idToBlockIndex[block.resourceID] = block.blockIndex;
            }
        }
    }

    // 5. 语义解析
    if (!ParseResourceBlocks(outRcContent, outHeader, outResInfo))
        return false;

    return true;
}

} // namespace rceditor
