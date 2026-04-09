#include "stdafx.h"
#include "RcFileWriter.h"
#include <fstream>
#include <sstream>

namespace rceditor {

namespace
{
    struct SStyleFlagEntry
    {
        DWORD mask;
        const wchar_t* name;
    };

    static const SStyleFlagEntry g_styleFlags[] =
    {
        { WS_CHILD, L"WS_CHILD" },
        { WS_VISIBLE, L"WS_VISIBLE" },
        { WS_DISABLED, L"WS_DISABLED" },
        { WS_TABSTOP, L"WS_TABSTOP" },
        { WS_GROUP, L"WS_GROUP" },
        { WS_BORDER, L"WS_BORDER" },
        { WS_DLGFRAME, L"WS_DLGFRAME" },
        { WS_VSCROLL, L"WS_VSCROLL" },
        { WS_HSCROLL, L"WS_HSCROLL" },
        { WS_CLIPSIBLINGS, L"WS_CLIPSIBLINGS" },
        { WS_CLIPCHILDREN, L"WS_CLIPCHILDREN" },
        { WS_SYSMENU, L"WS_SYSMENU" },
        { WS_THICKFRAME, L"WS_THICKFRAME" },
        { WS_MINIMIZEBOX, L"WS_MINIMIZEBOX" },
        { WS_MAXIMIZEBOX, L"WS_MAXIMIZEBOX" },
        { WS_CAPTION, L"WS_CAPTION" },
    };

    static const SStyleFlagEntry g_exStyleFlags[] =
    {
        { WS_EX_CLIENTEDGE, L"WS_EX_CLIENTEDGE" },
        { WS_EX_STATICEDGE, L"WS_EX_STATICEDGE" },
        { WS_EX_WINDOWEDGE, L"WS_EX_WINDOWEDGE" },
        { WS_EX_DLGMODALFRAME, L"WS_EX_DLGMODALFRAME" },
        { WS_EX_TOPMOST, L"WS_EX_TOPMOST" },
        { WS_EX_ACCEPTFILES, L"WS_EX_ACCEPTFILES" },
        { WS_EX_TRANSPARENT, L"WS_EX_TRANSPARENT" },
        { WS_EX_TOOLWINDOW, L"WS_EX_TOOLWINDOW" },
        { WS_EX_APPWINDOW, L"WS_EX_APPWINDOW" },
        { WS_EX_CONTEXTHELP, L"WS_EX_CONTEXTHELP" },
        { WS_EX_RIGHT, L"WS_EX_RIGHT" },
        { WS_EX_RTLREADING, L"WS_EX_RTLREADING" },
        { WS_EX_LEFTSCROLLBAR, L"WS_EX_LEFTSCROLLBAR" },
        { WS_EX_CONTROLPARENT, L"WS_EX_CONTROLPARENT" },
        { WS_EX_NOPARENTNOTIFY, L"WS_EX_NOPARENTNOTIFY" },
    };
}

CRcFileWriter::CRcFileWriter()
{
}

// ============================================================================
// 主接口
// ============================================================================

bool CRcFileWriter::WriteRcFile(const SRcFileContent& content, const CStringW& outputPath)
{
    CStringW text = JoinLines(content);
    if (!WriteStringToFile(text, outputPath, content.encoding, content.lineEnding))
        return false;
    return true;
}

bool CRcFileWriter::WriteResourceHeader(const SResourceHeader& header, const CStringW& outputPath)
{
    CStringW text = JoinHeaderLines(header);
    if (!WriteStringToFile(text, outputPath, header.encoding, header.lineEnding))
        return false;
    return true;
}

// ============================================================================
// 行拼接
// ============================================================================

CStringW CRcFileWriter::JoinLines(const SRcFileContent& content) const
{
    CStringW result;
    const CStringW& le = content.lineEnding.IsEmpty() ? CStringW(L"\r\n") : content.lineEnding;

    // 预估容量
    int totalLen = 0;
    for (const auto& line : content.lines)
        totalLen += line.text.GetLength() + le.GetLength();
    result.Preallocate(totalLen + 16);

    for (size_t i = 0; i < content.lines.size(); ++i)
    {
        result += content.lines[i].text;
        if (i + 1 < content.lines.size())
            result += le;
    }
    return result;
}

CStringW CRcFileWriter::JoinHeaderLines(const SResourceHeader& header) const
{
    CStringW result;
    const CStringW& le = header.lineEnding.IsEmpty() ? CStringW(L"\r\n") : header.lineEnding;

    int totalLen = 0;
    for (const auto& line : header.rawLines)
        totalLen += line.text.GetLength() + le.GetLength();
    result.Preallocate(totalLen + 16);

    for (size_t i = 0; i < header.rawLines.size(); ++i)
    {
        result += header.rawLines[i].text;
        if (i + 1 < header.rawLines.size())
            result += le;
    }
    return result;
}

// ============================================================================
// 编码写出
// ============================================================================

bool CRcFileWriter::WriteStringToFile(const CStringW& text, const CStringW& path,
                                       EFileEncoding encoding, const CStringW& lineEnding)
{
    HANDLE hFile = CreateFileW(path, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        m_lastError.Format(L"Cannot create file: %s (error %u)", (LPCWSTR)path, GetLastError());
        return false;
    }

    DWORD dwWritten = 0;
    bool ok = true;

    switch (encoding)
    {
    case EFileEncoding::UTF16LE_BOM:
    {
        // UTF-16 LE BOM: FF FE
        BYTE bom[] = { 0xFF, 0xFE };
        WriteFile(hFile, bom, 2, &dwWritten, NULL);
        // 直接写 UTF-16 LE
        DWORD dataBytes = text.GetLength() * sizeof(WCHAR);
        WriteFile(hFile, text.GetString(), dataBytes, &dwWritten, NULL);
        break;
    }
    case EFileEncoding::UTF16BE_BOM:
    {
        // UTF-16 BE BOM: FE FF
        BYTE bom[] = { 0xFE, 0xFF };
        WriteFile(hFile, bom, 2, &dwWritten, NULL);
        // 需要交换字节序
        int len = text.GetLength();
        std::vector<BYTE> buf(len * 2);
        const WCHAR* src = text.GetString();
        for (int i = 0; i < len; ++i)
        {
            buf[i * 2]     = (BYTE)(src[i] >> 8);
            buf[i * 2 + 1] = (BYTE)(src[i] & 0xFF);
        }
        WriteFile(hFile, buf.data(), (DWORD)buf.size(), &dwWritten, NULL);
        break;
    }
    case EFileEncoding::UTF8_BOM:
    {
        // UTF-8 BOM: EF BB BF
        BYTE bom[] = { 0xEF, 0xBB, 0xBF };
        WriteFile(hFile, bom, 3, &dwWritten, NULL);
        // UTF-16 → UTF-8
        std::string utf8 = CStringConverter::ToUtf8(text);
        WriteFile(hFile, utf8.c_str(), (DWORD)utf8.size(), &dwWritten, NULL);
        break;
    }
    case EFileEncoding::UTF8_NoBOM:
    {
        std::string utf8 = CStringConverter::ToUtf8(text);
        WriteFile(hFile, utf8.c_str(), (DWORD)utf8.size(), &dwWritten, NULL);
        break;
    }
    case EFileEncoding::ANSI:
    default:
    {
        // UTF-16 → ANSI (当前代码页)
        CStringA ansi = CStringConverter::ToAnsi(text);
        WriteFile(hFile, ansi.GetString(), ansi.GetLength(), &dwWritten, NULL);
        break;
    }
    }

    CloseHandle(hFile);
    return ok;
}

// ============================================================================
// Dialog 序列化
// ============================================================================

std::vector<CStringW> CRcFileWriter::SerializeDialog(
    const std::shared_ptr<SDialogInfo>& dlg,
    const CStringW& dialogName,
    const SResourceHeader& header,
    const SIndentStyle& style) const
{
    std::vector<CStringW> lines;
    CStringW ind = MakeIndent(style);

    // 头行
    CStringW keyword = dlg->bIsDlgTemplateEx ? L"DIALOGEX" : L"DIALOG";
    CStringW headerLine;
    headerLine.Format(L"%s %s %d, %d, %d, %d",
                      (LPCWSTR)dialogName, (LPCWSTR)keyword,
                      dlg->rectDlg.left, dlg->rectDlg.top,
                      dlg->rectDlg.Width(), dlg->rectDlg.Height());
    lines.push_back(headerLine);

    // STYLE
    if (dlg->dwStyle != 0)
    {
        CStringW styleLine;
        CStringW styleText = FormatStyleFlags(dlg->dwStyle, false);
        styleLine.Format(L"STYLE %s", (LPCWSTR)styleText);
        lines.push_back(styleLine);
    }

    // EXSTYLE
    if (dlg->dwExStyle != 0)
    {
        CStringW exLine;
        CStringW exStyleText = FormatStyleFlags(dlg->dwExStyle, true);
        exLine.Format(L"EXSTYLE %s", (LPCWSTR)exStyleText);
        lines.push_back(exLine);
    }

    // CAPTION
    if (!dlg->csCaption.IsEmpty())
    {
        CStringW capLine;
        capLine.Format(L"CAPTION \"%s\"", (LPCWSTR)EscapeRcString(dlg->csCaption));
        lines.push_back(capLine);
    }

    // FONT
    if (dlg->pFontInfo)
    {
        CStringW fontLine;
        if (dlg->bIsDlgTemplateEx)
        {
            fontLine.Format(L"FONT %u, \"%s\", %u, %u, 0x%X",
                            dlg->pFontInfo->wPointSize,
                            (LPCWSTR)dlg->pFontInfo->csFaceName,
                            dlg->pFontInfo->wWeight,
                            dlg->pFontInfo->bItalic,
                            dlg->pFontInfo->bCharSet);
        }
        else
        {
            fontLine.Format(L"FONT %u, \"%s\"",
                            dlg->pFontInfo->wPointSize,
                            (LPCWSTR)dlg->pFontInfo->csFaceName);
        }
        lines.push_back(fontLine);
    }

    // CLASS
    if (!dlg->csClassName.IsEmpty())
    {
        CStringW clsLine;
        clsLine.Format(L"CLASS \"%s\"", (LPCWSTR)dlg->csClassName);
        lines.push_back(clsLine);
    }

    // MENU
    if (!dlg->csMenuName.IsEmpty())
    {
        CStringW menuLine;
        menuLine.Format(L"MENU %s", (LPCWSTR)dlg->csMenuName);
        lines.push_back(menuLine);
    }

    lines.push_back(L"BEGIN");

    // 控件
    for (const auto& ctrl : dlg->vecCtrlInfo)
    {
        CStringW idcName = FormatId(ctrl->nIDC, header);

        CStringW className = ctrl->csClassName;
        CStringW classUpper = className;
        classUpper.MakeUpper();

        CStringW ctrlLine;

        // 尝试简写语法
        if (classUpper == L"STATIC" && !ctrl->csText.IsEmpty())
        {
            // LTEXT / RTEXT / CTEXT
            CStringW keyword = L"LTEXT";
            if (ctrl->dwStyle & SS_RIGHT) keyword = L"RTEXT";
            else if (ctrl->dwStyle & SS_CENTER) keyword = L"CTEXT";

            ctrlLine.Format(L"%s%-16s\"%s\",%s,%d,%d,%d,%d",
                            (LPCWSTR)ind, (LPCWSTR)keyword,
                            (LPCWSTR)EscapeRcString(ctrl->csText), (LPCWSTR)idcName,
                            ctrl->ctrlRect.left, ctrl->ctrlRect.top,
                            ctrl->ctrlRect.Width(), ctrl->ctrlRect.Height());
        }
        else if (classUpper == L"BUTTON")
        {
            CStringW keyword = L"PUSHBUTTON";
            if (ctrl->dwStyle & BS_DEFPUSHBUTTON) keyword = L"DEFPUSHBUTTON";
            else if (ctrl->dwStyle & BS_GROUPBOX) keyword = L"GROUPBOX";
            else if (ctrl->dwStyle & BS_AUTOCHECKBOX) keyword = L"AUTOCHECKBOX";
            else if (ctrl->dwStyle & BS_CHECKBOX) keyword = L"CHECKBOX";
            else if (ctrl->dwStyle & BS_AUTORADIOBUTTON) keyword = L"AUTORADIOBUTTON";
            else if (ctrl->dwStyle & BS_RADIOBUTTON) keyword = L"RADIOBUTTON";

            ctrlLine.Format(L"%s%-16s\"%s\",%s,%d,%d,%d,%d",
                            (LPCWSTR)ind, (LPCWSTR)keyword,
                            (LPCWSTR)EscapeRcString(ctrl->csText), (LPCWSTR)idcName,
                            ctrl->ctrlRect.left, ctrl->ctrlRect.top,
                            ctrl->ctrlRect.Width(), ctrl->ctrlRect.Height());
        }
        else if (classUpper == L"EDIT")
        {
            ctrlLine.Format(L"%sEDITTEXT        %s,%d,%d,%d,%d",
                            (LPCWSTR)ind, (LPCWSTR)idcName,
                            ctrl->ctrlRect.left, ctrl->ctrlRect.top,
                            ctrl->ctrlRect.Width(), ctrl->ctrlRect.Height());
        }
        else if (classUpper == L"COMBOBOX")
        {
            ctrlLine.Format(L"%sCOMBOBOX        %s,%d,%d,%d,%d",
                            (LPCWSTR)ind, (LPCWSTR)idcName,
                            ctrl->ctrlRect.left, ctrl->ctrlRect.top,
                            ctrl->ctrlRect.Width(), ctrl->ctrlRect.Height());
        }
        else if (classUpper == L"LISTBOX")
        {
            ctrlLine.Format(L"%sLISTBOX         %s,%d,%d,%d,%d",
                            (LPCWSTR)ind, (LPCWSTR)idcName,
                            ctrl->ctrlRect.left, ctrl->ctrlRect.top,
                            ctrl->ctrlRect.Width(), ctrl->ctrlRect.Height());
        }
        else if (classUpper == L"SCROLLBAR")
        {
            ctrlLine.Format(L"%sSCROLLBAR       %s,%d,%d,%d,%d",
                            (LPCWSTR)ind, (LPCWSTR)idcName,
                            ctrl->ctrlRect.left, ctrl->ctrlRect.top,
                            ctrl->ctrlRect.Width(), ctrl->ctrlRect.Height());
        }
        else
        {
            // 通用 CONTROL 语法
            CStringW ctrlStyleText = FormatStyleFlags(ctrl->dwStyle, false);
            ctrlLine.Format(L"%sCONTROL         \"%s\",%s,\"%s\",%s,%d,%d,%d,%d",
                            (LPCWSTR)ind,
                            (LPCWSTR)EscapeRcString(ctrl->csText), (LPCWSTR)idcName,
                            (LPCWSTR)ctrl->csClassName, (LPCWSTR)ctrlStyleText,
                            ctrl->ctrlRect.left, ctrl->ctrlRect.top,
                            ctrl->ctrlRect.Width(), ctrl->ctrlRect.Height());
        }

        // 追加扩展风格（如果有且非默认）
        if (ctrl->dwExStyle != 0 && dlg->bIsDlgTemplateEx)
        {
            CStringW exPart;
            CStringW ctrlExStyleText = FormatStyleFlags(ctrl->dwExStyle, true);
            exPart.Format(L",%s", (LPCWSTR)ctrlExStyleText);
            ctrlLine += exPart;
        }

        lines.push_back(ctrlLine);
    }

    lines.push_back(L"END");
    return lines;
}

// ============================================================================
// StringTable 序列化
// ============================================================================

std::vector<CStringW> CRcFileWriter::SerializeStringTable(
    const std::shared_ptr<SStringTableInfo>& st,
    const SResourceHeader& header,
    const SIndentStyle& style) const
{
    std::vector<CStringW> lines;
    CStringW ind = MakeIndent(style);

    lines.push_back(L"STRINGTABLE");
    lines.push_back(L"BEGIN");

    for (const auto& [strId, strText] : st->mapIDAndString)
    {
        CStringW idName = FormatId(strId, header);
        CStringW entryLine;
        entryLine.Format(L"%s%s \"%s\"",
                         (LPCWSTR)ind, (LPCWSTR)idName,
                         (LPCWSTR)EscapeRcString(strText));
        lines.push_back(entryLine);
    }

    lines.push_back(L"END");
    return lines;
}

// ============================================================================
// Menu 序列化
// ============================================================================

std::vector<CStringW> CRcFileWriter::SerializeMenu(
    const std::shared_ptr<SMenuInfo>& menu,
    const CStringW& menuName,
    const SResourceHeader& header,
    const SIndentStyle& style) const
{
    std::vector<CStringW> lines;

    CStringW headerLine;
    headerLine.Format(L"%s MENU", (LPCWSTR)menuName);
    lines.push_back(headerLine);
    lines.push_back(L"BEGIN");

    for (const auto& item : menu->vecSubMenu)
    {
        SerializeMenuItems(item, header, style, 1, lines);
    }

    lines.push_back(L"END");
    return lines;
}

void CRcFileWriter::SerializeMenuItems(
    const std::shared_ptr<SMenuInfo>& menuItem,
    const SResourceHeader& header,
    const SIndentStyle& style,
    int depth,
    std::vector<CStringW>& outLines) const
{
    CStringW ind = MakeIndent(style, depth);

    if (menuItem->bIsPopup)
    {
        CStringW popupLine;
        popupLine.Format(L"%sPOPUP \"%s\"",
                         (LPCWSTR)ind,
                         (LPCWSTR)EscapeRcString(menuItem->csText));
        outLines.push_back(popupLine);
        outLines.push_back(ind + L"BEGIN");

        for (const auto& sub : menuItem->vecSubMenu)
        {
            SerializeMenuItems(sub, header, style, depth + 1, outLines);
        }

        outLines.push_back(ind + L"END");
    }
    else if (menuItem->nID == 0 && menuItem->csText.IsEmpty())
    {
        // SEPARATOR
        outLines.push_back(ind + L"MENUITEM SEPARATOR");
    }
    else
    {
        CStringW idName = FormatId(menuItem->nID, header);
        CStringW itemLine;
        itemLine.Format(L"%sMENUITEM \"%s\", %s",
                        (LPCWSTR)ind,
                        (LPCWSTR)EscapeRcString(menuItem->csText),
                        (LPCWSTR)idName);
        outLines.push_back(itemLine);
    }
}

// ============================================================================
// 辅助函数
// ============================================================================

CStringW CRcFileWriter::MakeIndent(const SIndentStyle& style, int depth) const
{
    if (style.useTabs)
    {
        CStringW tabs;
        for (int i = 0; i < depth; ++i)
            tabs += L'\t';
        return tabs;
    }
    else
    {
        int total = style.indentWidth * depth;
        return CStringW(L' ', total);
    }
}

CStringW CRcFileWriter::FormatId(UINT id, const SResourceHeader& header) const
{
    auto it = header.idToName.find(id);
    if (it != header.idToName.end())
        return it->second;

    if (id == (UINT)-1)
        return L"-1";

    CStringW numStr;
    numStr.Format(L"%u", id);
    return numStr;
}

CStringW CRcFileWriter::EscapeRcString(const CStringW& text) const
{
    CStringW result;
    result.Preallocate(text.GetLength() + 16);

    for (int i = 0; i < text.GetLength(); ++i)
    {
        WCHAR ch = text[i];
        switch (ch)
        {
        case L'\\': result += L"\\\\"; break;
        case L'\"': result += L"\"\""; break;
        case L'\n': result += L"\\n"; break;
        case L'\r': result += L"\\r"; break;
        case L'\t': result += L"\\t"; break;
        default:    result += ch; break;
        }
    }
    return result;
}

CStringW CRcFileWriter::FormatStyleFlags(DWORD style, bool exStyle) const
{
    const SStyleFlagEntry* pTable = exStyle ? g_exStyleFlags : g_styleFlags;
    const size_t tableSize = exStyle ? _countof(g_exStyleFlags) : _countof(g_styleFlags);

    if (style == 0)
        return L"0";

    CStringW result;
    DWORD remaining = style;

    for (size_t i = 0; i < tableSize; ++i)
    {
        const auto& e = pTable[i];
        if (e.mask != 0 && (remaining & e.mask) == e.mask)
        {
            if (!result.IsEmpty())
                result += L" | ";
            result += e.name;
            remaining &= ~e.mask;
        }
    }

    if (remaining != 0)
    {
        CStringW hexPart;
        hexPart.Format(L"0x%08X", remaining);
        if (!result.IsEmpty())
            result += L" | ";
        result += hexPart;
    }

    return result.IsEmpty() ? CStringW(L"0") : result;
}

} // namespace rceditor
