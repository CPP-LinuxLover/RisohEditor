#include "stdafx.h"
#include "CRcCompareControler.h"
#include <shlwapi.h>
#include <set>
#include <functional>

#pragma comment(lib, "shlwapi.lib")

// ============================================================================
// 静态辅助函数 — RC 文本级行内替换
// ============================================================================
namespace
{

/// 替换行内第一个 "..." 引号区段的内容，保留行的其余部分不变
CStringW ReplaceFirstQuotedString(const CStringW& line, const CStringW& newText)
{
    int firstQuote = line.Find(L'"');
    if (firstQuote < 0) return line;

    // 跳过引号内容（处理 "" 转义）
    int pos = firstQuote + 1;
    while (pos < line.GetLength())
    {
        if (line[pos] == L'"')
        {
            if (pos + 1 < line.GetLength() && line[pos + 1] == L'"')
            { pos += 2; continue; }   // "" 转义
            break;
        }
        pos++;
    }
    if (pos >= line.GetLength()) return line;   // 未找到闭合引号

    CStringW escaped = newText;
    escaped.Replace(L"\\", L"\\\\");   // 反斜杠（必须最先处理）
    escaped.Replace(L"\n", L"\\n");    // 换行
    escaped.Replace(L"\r", L"\\r");    // 回车
    escaped.Replace(L"\t", L"\\t");    // 制表符
    escaped.Replace(L"\"", L"\"\"");   // 引号（RC 约定）

    return line.Left(firstQuote + 1) + escaped + line.Mid(pos);
}

/// 将宏名/数字字符串解析为 UINT；失败返回 (UINT)-2
UINT ResolveTokenHelper(const CStringW& token, const rceditor::SResourceHeader& header)
{
    if (token.IsEmpty()) return (UINT)-2;

    auto it = header.nameToId.find(token);
    if (it != header.nameToId.end())
        return it->second;

    // Windows 标准控件 ID（定义在 <winuser.h>，不在 resource.h 中）
    static const struct { LPCWSTR name; UINT id; } kStdIds[] = {
        { L"IDOK",       1 },
        { L"IDCANCEL",   2 },
        { L"IDABORT",    3 },
        { L"IDRETRY",    4 },
        { L"IDIGNORE",   5 },
        { L"IDYES",      6 },
        { L"IDNO",       7 },
        { L"IDCLOSE",    8 },
        { L"IDHELP",     9 },
        { L"IDC_STATIC", (UINT)-1 },
    };
    for (const auto& e : kStdIds)
    {
        if (token.CompareNoCase(e.name) == 0)
            return e.id;
    }

    wchar_t* end = nullptr;
    long val = wcstol(token.GetString(), &end, 0);
    if (end && end != token.GetString() && *end == L'\0')
        return (UINT)val;

    return (UINT)-2;
}

/// 从控件行解析 IDC；对有引号文本的行跳过首个 "..." 区段后取 IDC 令牌
int ParseControlIDCFromLine(const CStringW& trimmedLine, const rceditor::SResourceHeader& header)
{
    int firstQuote = trimmedLine.Find(L'"');
    int searchFrom = 0;

    if (firstQuote >= 0)
    {
        // 有引号文本的控件行: PUSHBUTTON "text", IDC, ...
        int pos = firstQuote + 1;
        while (pos < trimmedLine.GetLength())
        {
            if (trimmedLine[pos] == L'"')
            {
                if (pos + 1 < trimmedLine.GetLength() && trimmedLine[pos + 1] == L'"')
                { pos += 2; continue; }
                break;
            }
            pos++;
        }
        searchFrom = pos + 1;
    }
    else
    {
        // 无引号文本的控件行: EDITTEXT IDC, ...
        int i = 0;
        while (i < trimmedLine.GetLength() && trimmedLine[i] != L' ' && trimmedLine[i] != L'\t')
            i++;
        searchFrom = i;
    }

    // 跳过逗号和空白
    int i = searchFrom;
    while (i < trimmedLine.GetLength() &&
           (trimmedLine[i] == L',' || trimmedLine[i] == L' ' || trimmedLine[i] == L'\t'))
        i++;

    int tokenStart = i;
    while (i < trimmedLine.GetLength() &&
           trimmedLine[i] != L',' && trimmedLine[i] != L' ' && trimmedLine[i] != L'\t')
        i++;

    if (tokenStart >= i) return -2;
    CStringW idcToken = trimmedLine.Mid(tokenStart, i - tokenStart);

    UINT resolved = ResolveTokenHelper(idcToken, header);
    return (int)resolved;
}

/// Dialog 文本 + 尺寸查找表
struct SDialogTextLookup
{
    std::map<UINT, CStringW> idcToText;       // IDC → 文本
    std::vector<CStringW>    staticTexts;     // IDC=-1/0xFFFF/0 的非GroupBox控件文本（按序）
    std::vector<CStringW>    groupBoxTexts;   // GroupBox(IDC=-1/0xFFFF/0)文本（按序）
    CStringW caption;                         // 标题

    // 尺寸数据（次语言二进制，DLU 坐标）——用于修复 AlignSecondary 后 RC 中的主语言尺寸
    CRect dialogRect;                         // 对话框矩形（left=x, top=y, right=x+w, bottom=y+h）
    std::map<UINT, CRect> idcToRect;          // 命名控件 IDC → 矩形
    std::vector<CRect>    staticRects;        // 无名 STATIC 矩形（按序）
    std::vector<CRect>    groupBoxRects;      // GroupBox 矩形（按序）
};

/// 从二进制 Dialog 资源构建文本查找表
/// 主语言二进制用于确定控件顺序（与 RC 行顺序一致），
/// 次语言二进制提供翻译文本。STATIC 控件用 ctrlRect 几何匹配 + 锚点回退。
SDialogTextLookup BuildDialogTextLookup(
    UINT dlgID,
    const rceditor::SSingleResourceInfo& primaryBinaryRes,
    const rceditor::SSingleResourceInfo& secondaryBinaryRes)
{
    using namespace rceditor;
    SDialogTextLookup lookup;
    auto itSec = secondaryBinaryRes.mapLangAndDialogInfo.find(dlgID);
    if (itSec == secondaryBinaryRes.mapLangAndDialogInfo.end() || !itSec->second)
        return lookup;

    const auto& pSecDlg = itSec->second;
    lookup.caption = pSecDlg->csCaption;
    lookup.dialogRect = pSecDlg->rectDlg;

    // 次语言二进制中 IDC 有效的控件 → idcToText + idcToRect
    for (const auto& ctrl : pSecDlg->vecCtrlInfo)
    {
        if (!ctrl) continue;
        UINT idc = ctrl->nIDC;
        if (idc != (UINT)-1 && idc != 0xFFFF && idc != 0)
        {
            lookup.idcToText[idc] = ctrl->csText;
            lookup.idcToRect[idc] = ctrl->ctrlRect;
        }
    }

    // --- STATIC 控件：按主语言顺序，用 ctrlRect 匹配次语言文本 ---
    // 收集次语言的 STATIC 控件（排除 GroupBox，GroupBox 单独处理）
    struct StaticEntry { CRect rect; CStringW text; bool used; };
    std::vector<StaticEntry> secStatics;
    std::vector<StaticEntry> secGroupBoxes;  // GroupBox(IDC=-1) 按顺序收集
    for (const auto& ctrl : pSecDlg->vecCtrlInfo)
    {
        if (!ctrl) continue;
        UINT idc = ctrl->nIDC;
        if (idc == (UINT)-1 || idc == 0xFFFF || idc == 0)
        {
            if (rceditor::IsGroupBoxCtrl(ctrl))
                secGroupBoxes.push_back({ ctrl->ctrlRect, ctrl->csText, false });
            else
                secStatics.push_back({ ctrl->ctrlRect, ctrl->csText, false });
        }
    }

    // 按主语言 STATIC 控件顺序（== RC 行顺序）遍历
    auto itPri = primaryBinaryRes.mapLangAndDialogInfo.find(dlgID);
    if (itPri != primaryBinaryRes.mapLangAndDialogInfo.end() && itPri->second)
    {
        const auto& pPriDlg = itPri->second;

        // 棋盘拓扑匹配预计算：建立主语言 STATIC → 次语言 secStatics 列表索引的映射
        std::map<int, int> tileMatchMap; // priCtrlIndex → secStatics list index
        {
            // 建立次语言 vecCtrlInfo 索引 → secStatics 列表索引的映射（排除 GroupBox）
            std::map<int, int> secCtrlToListIdx;
            {
                int si = 0;
                for (int i = 0; i < (int)pSecDlg->vecCtrlInfo.size(); ++i)
                {
                    const auto& c2 = pSecDlg->vecCtrlInfo[i];
                    if (!c2) continue;
                    UINT idc2 = c2->nIDC;
                    if (idc2 == (UINT)-1 || idc2 == 0xFFFF || idc2 == 0)
                    {
                        if (!rceditor::IsGroupBoxCtrl(c2))
                        {
                            secCtrlToListIdx[i] = si;
                            ++si;
                        }
                    }
                }
            }

            // 构建两语言的棋盘模型
            SDialogBoard priBoard = BuildDialogBoard(pPriDlg);
            SDialogBoard secBoard = BuildDialogBoard(pSecDlg);

            // 在匹配的 TileGroup 内，按拓扑行对齐 + topoCol 配对 + 平坦序号回退
            auto mapGroup = [&](const STileGroup& pg, const STileGroup& sg) {
                auto rowMap = AlignTopoRows(pg, sg, pPriDlg->vecCtrlInfo, pSecDlg->vecCtrlInfo);

                std::set<int> priMapped, secMapped; // 已拓扑配对的 globalIndex

                // Phase 1: 拓扑行配对
                for (const auto& rp : rowMap)
                {
                    int priRow = rp.first;
                    int secRow = rp.second;
                    if (priRow < 0 || priRow >= (int)pg.topoRows.size()) continue;
                    if (secRow < 0 || secRow >= (int)sg.topoRows.size()) continue;

                    std::vector<const SBoardTile*> priST, secST;
                    for (int ti : pg.topoRows[priRow].tileIndices)
                    {
                        const auto& tile = pg.tiles[ti];
                        if (tile.localStaticOrdinal >= 0) priST.push_back(&tile);
                    }
                    for (int ti : sg.topoRows[secRow].tileIndices)
                    {
                        const auto& tile = sg.tiles[ti];
                        if (tile.localStaticOrdinal >= 0) secST.push_back(&tile);
                    }

                    int pairs = (int)(std::min)(priST.size(), secST.size());
                    for (int k = 0; k < pairs; ++k)
                    {
                        auto itMap = secCtrlToListIdx.find(secST[k]->globalIndex);
                        if (itMap != secCtrlToListIdx.end())
                        {
                            tileMatchMap[priST[k]->globalIndex] = itMap->second;
                            priMapped.insert(priST[k]->globalIndex);
                            secMapped.insert(secST[k]->globalIndex);
                        }
                    }
                }

                // Phase 2: 平坦序号回退 — 未匹配的 STATIC 按组内顺序 1:1
                std::vector<const SBoardTile*> priRemain, secRemain;
                for (const auto& t : pg.tiles)
                    if (t.localStaticOrdinal >= 0 && !priMapped.count(t.globalIndex))
                        priRemain.push_back(&t);
                for (const auto& t : sg.tiles)
                    if (t.localStaticOrdinal >= 0 && !secMapped.count(t.globalIndex))
                        secRemain.push_back(&t);

                int fallbackPairs = (int)(std::min)(priRemain.size(), secRemain.size());
                for (int k = 0; k < fallbackPairs; ++k)
                {
                    auto itMap = secCtrlToListIdx.find(secRemain[k]->globalIndex);
                    if (itMap != secCtrlToListIdx.end())
                        tileMatchMap[priRemain[k]->globalIndex] = itMap->second;
                }
            };

            // 匹配各 GroupBox 组
            for (int gi = 0; gi < (int)priBoard.groupBoxes.size(); ++gi)
            {
                const auto& priGrp = priBoard.groupBoxes[gi];
                auto itSec = secBoard.groupIDCToIndex.find(priGrp.groupIDC);
                if (itSec == secBoard.groupIDCToIndex.end()) continue;
                mapGroup(priGrp, secBoard.groupBoxes[itSec->second]);
            }

            // 匹配顶层组
            mapGroup(priBoard.topLevel, secBoard.topLevel);
        }

        // 遍历主语言 STATIC 控件（排除 GroupBox），匹配次语言文本
        int priStaticIdx = 0;
        for (int ci = 0; ci < (int)pPriDlg->vecCtrlInfo.size(); ++ci)
        {
            const auto& ctrl = pPriDlg->vecCtrlInfo[ci];
            if (!ctrl) continue;
            UINT idc = ctrl->nIDC;
            if (idc != (UINT)-1 && idc != 0xFFFF && idc != 0) continue;
            // GroupBox 单独处理，不参与 STATIC 匹配
            if (rceditor::IsGroupBoxCtrl(ctrl)) continue;

            // Level 1: 精确匹配 ctrlRect
            bool matched = false;
            for (auto& se : secStatics)
            {
                if (!se.used && se.rect == ctrl->ctrlRect)
                {
                    lookup.staticTexts.push_back(se.text);
                    lookup.staticRects.push_back(se.rect);
                    se.used = true;
                    matched = true;
                    break;
                }
            }
            if (matched) { ++priStaticIdx; continue; }

            // Level 2: 棋盘拓扑匹配
            {
                auto itTile = tileMatchMap.find(ci);
                if (itTile != tileMatchMap.end())
                {
                    int secIdx = itTile->second;
                    if (secIdx >= 0 && secIdx < (int)secStatics.size() && !secStatics[secIdx].used)
                    {
                        lookup.staticTexts.push_back(secStatics[secIdx].text);
                        lookup.staticRects.push_back(secStatics[secIdx].rect);
                        secStatics[secIdx].used = true;
                        matched = true;
                    }
                }
            }
            if (matched) { ++priStaticIdx; continue; }

            // Level 3: 回退：最近距离匹配（带距离上限，防止跨区域误匹配）
            {
                constexpr double kMaxFallbackDistSq = 30.0 * 30.0; // 30 DLU 上限
                int bestIdx = -1;
                double bestDist = 1e30;
                for (int j = 0; j < (int)secStatics.size(); ++j)
                {
                    if (secStatics[j].used) continue;
                    double dx = (double)(secStatics[j].rect.left - ctrl->ctrlRect.left);
                    double dy = (double)(secStatics[j].rect.top  - ctrl->ctrlRect.top);
                    double d  = dx * dx + dy * dy;
                    if (d < bestDist && d <= kMaxFallbackDistSq) { bestDist = d; bestIdx = j; }
                }
                if (bestIdx >= 0)
                {
                    lookup.staticTexts.push_back(secStatics[bestIdx].text);
                    lookup.staticRects.push_back(secStatics[bestIdx].rect);
                    secStatics[bestIdx].used = true;
                }
                else
                {
                    // 次语言无对应 STATIC → 用主语言文本/尺寸占位
                    lookup.staticTexts.push_back(ctrl->csText);
                    lookup.staticRects.push_back(ctrl->ctrlRect);
                }
            }
            ++priStaticIdx;
        }

        // --- GroupBox(IDC=-1) 单独按序匹配 ---
        {
            int priGBIdx = 0;
            for (int ci = 0; ci < (int)pPriDlg->vecCtrlInfo.size(); ++ci)
            {
                const auto& ctrl = pPriDlg->vecCtrlInfo[ci];
                if (!ctrl) continue;
                UINT idc = ctrl->nIDC;
                if (idc != (UINT)-1 && idc != 0xFFFF && idc != 0) continue;
                if (!rceditor::IsGroupBoxCtrl(ctrl)) continue;

                // Level 1: 精确匹配 ctrlRect
                bool matched = false;
                for (auto& se : secGroupBoxes)
                {
                    if (!se.used && se.rect == ctrl->ctrlRect)
                    {
                        lookup.groupBoxTexts.push_back(se.text);
                        lookup.groupBoxRects.push_back(se.rect);
                        se.used = true;
                        matched = true;
                        break;
                    }
                }
                if (matched) { ++priGBIdx; continue; }

                // Level 2: 按序号匹配
                if (priGBIdx < (int)secGroupBoxes.size() && !secGroupBoxes[priGBIdx].used)
                {
                    lookup.groupBoxTexts.push_back(secGroupBoxes[priGBIdx].text);
                    lookup.groupBoxRects.push_back(secGroupBoxes[priGBIdx].rect);
                    secGroupBoxes[priGBIdx].used = true;
                }
                else
                {
                    // 无对应次语言 GroupBox → 用主语言文本/尺寸占位
                    lookup.groupBoxTexts.push_back(ctrl->csText);
                    lookup.groupBoxRects.push_back(ctrl->ctrlRect);
                }
                ++priGBIdx;
            }
        }
    }
    else
    {
        // 无主语言二进制 → 回退到次语言顺序
        for (const auto& se : secStatics)
        {
            lookup.staticTexts.push_back(se.text);
            lookup.staticRects.push_back(se.rect);
        }
        for (const auto& se : secGroupBoxes)
        {
            lookup.groupBoxTexts.push_back(se.text);
            lookup.groupBoxRects.push_back(se.rect);
        }
    }

    return lookup;
}

// ============================================================================
// 控件行尺寸替换辅助函数
// ============================================================================

/// 将 RC 行按逗号分割（尊重双引号括起的字符串），返回各字段在原始行中的 (起始位置, 长度)
static std::vector<std::pair<int,int>> SplitLineByComma(const CStringW& line)
{
    std::vector<std::pair<int,int>> fields;
    bool inQuote = false;
    int fieldStart = 0;
    for (int i = 0; i <= line.GetLength(); ++i)
    {
        if (i < line.GetLength())
        {
            if (line[i] == L'"')
            {
                if (inQuote && i + 1 < line.GetLength() && line[i+1] == L'"')
                    { ++i; continue; }
                inQuote = !inQuote;
                continue;
            }
            if (line[i] == L',' && !inQuote)
            {
                fields.push_back({ fieldStart, i - fieldStart });
                fieldStart = i + 1;
                continue;
            }
        }
        else
        {
            fields.push_back({ fieldStart, i - fieldStart });
        }
    }
    return fields;
}

/// 替换控件行中的 x, y, w, h 四个尺寸字段
/// 根据控件行格式自动定位：
///   Typed(有文本): KEYWORD "text", IDC, x, y, w, h [, style [, exstyle]]  → xField=2
///   Typed(无文本): KEYWORD IDC, x, y, w, h [, style [, exstyle]]          → xField=1
///   CONTROL:       CONTROL "text", IDC, "class", style, x, y, w, h [, ex] → xField=4
static bool ReplaceControlDimensions(CStringW& lineText, const CRect& newRect)
{
    auto fields = SplitLineByComma(lineText);
    if (fields.size() < 5) return false;   // 至少需要 5 个字段

    CStringW trimmed = lineText; trimmed.TrimLeft();
    CStringW upper = trimmed; upper.MakeUpper();

    // 判断是否为 CONTROL 通用格式
    bool isControlGeneric = false;
    {
        CStringW kw = upper;
        int sp = kw.FindOneOf(L" \t");
        if (sp > 0) kw = kw.Left(sp);
        isControlGeneric = (kw == L"CONTROL");
    }

    // 判断第一个字段是否包含引号（有文本的 typed 控件）
    bool firstFieldHasQuote = false;
    if (!fields.empty())
    {
        CStringW f0 = lineText.Mid(fields[0].first, fields[0].second);
        firstFieldHasQuote = (f0.Find(L'"') >= 0);
    }

    int xFieldIdx;
    if (isControlGeneric)
        xFieldIdx = 4;     // CONTROL "text", IDC, "class", style, x, y, w, h
    else if (firstFieldHasQuote)
        xFieldIdx = 2;     // KEYWORD "text", IDC, x, y, w, h
    else
        xFieldIdx = 1;     // KEYWORD IDC, x, y, w, h

    if (xFieldIdx + 3 >= (int)fields.size())
        return false;

    int dims[4] = {
        newRect.left, newRect.top, newRect.Width(), newRect.Height()
    };

    // 重建行：保留非尺寸字段原文，替换尺寸字段（保留前导空格）
    CStringW result;
    for (int fi = 0; fi < (int)fields.size(); ++fi)
    {
        if (fi > 0) result += L',';

        if (fi >= xFieldIdx && fi <= xFieldIdx + 3)
        {
            CStringW origField = lineText.Mid(fields[fi].first, fields[fi].second);
            // 保留原始字段的前导空白
            CStringW leading;
            for (int ci = 0; ci < origField.GetLength(); ++ci)
            {
                if (origField[ci] == L' ' || origField[ci] == L'\t')
                    leading += origField[ci];
                else break;
            }
            CStringW valStr;
            valStr.Format(L"%d", dims[fi - xFieldIdx]);
            result += leading + valStr;
        }
        else
        {
            result += lineText.Mid(fields[fi].first, fields[fi].second);
        }
    }
    lineText = result;
    return true;
}

/// 替换 Dialog 头行（IDD_xxx DIALOGEX x, y, w, h）中的尺寸
static bool ReplaceDialogHeaderDimensions(CStringW& lineText, const CRect& newRect)
{
    CStringW upper = lineText;
    upper.MakeUpper();

    // 找到 DIALOGEX 或 DIALOG 关键词的位置
    int kwPos = -1, kwLen = 0;
    int posEx = upper.Find(L"DIALOGEX");
    int posD  = upper.Find(L"DIALOG");

    if (posEx >= 0)      { kwPos = posEx; kwLen = 8; }
    else if (posD >= 0)  { kwPos = posD;  kwLen = 6; }
    else return false;

    // 保留关键词之前及关键词本身的内容，替换之后的尺寸
    CStringW prefix = lineText.Left(kwPos + kwLen);
    CStringW newDims;
    newDims.Format(L" %d, %d, %d, %d",
        newRect.left, newRect.top, newRect.Width(), newRect.Height());
    lineText = prefix + newDims;
    return true;
}

/// 遍历 Dialog 块行，对 CAPTION 和控件行做引号文本替换 + 尺寸替换
/// 属性区（ResourceHeader ~ BEGIN）：仅替换 CAPTION，跳过 FONT/STYLE/EXSTYLE 等
/// 控件区（BEGIN ~ END）：按 IDC 或 STATIC 顺序匹配替换
void ReplaceDialogBlockTexts(
    std::vector<rceditor::SRcLine>& lines,
    int startIdx, int endIdx,
    const SDialogTextLookup& lookup,
    const rceditor::SResourceHeader& header,
    bool replaceDimensions = true)
{
    int staticCounter = 0;
    int groupBoxCounter = 0;
    bool insideBlock = false;   // 仅在 BEGIN/{ 之后才处理控件行
    bool headerDimDone = false; // 是否已替换 Dialog 头行尺寸

    for (int li = startIdx; li <= endIdx && li < (int)lines.size(); ++li)
    {
        auto& line = lines[li];
        CStringW trimmed = line.text;
        trimmed.TrimLeft();
        trimmed.TrimRight();
        if (trimmed.IsEmpty()) continue;

        CStringW upper = trimmed;
        upper.MakeUpper();

        // ---- 属性区（ResourceHeader ~ BEGIN 之间）----
        if (!insideBlock)
        {
            // Dialog 头行尺寸替换（IDD_xxx DIALOGEX x, y, w, h）
            if (replaceDimensions && !headerDimDone && !lookup.dialogRect.IsRectNull())
            {
                // 检查行是否包含 DIALOGEX 或 DIALOG 关键词（按 token 匹配）
                bool isDlgHeader = (upper.Find(L"DIALOGEX") >= 0 || upper.Find(L"DIALOG") >= 0);
                if (isDlgHeader)
                {
                    ReplaceDialogHeaderDimensions(line.text, lookup.dialogRect);
                    headerDimDone = true;
                }
            }

            // CAPTION 是唯一需要替换的属性行
            if (upper.Left(7) == L"CAPTION")
            {
                if (!lookup.caption.IsEmpty())
                    line.text = ReplaceFirstQuotedString(line.text, lookup.caption);
            }
            // 检测 BEGIN / { → 进入控件区
            if (upper == L"BEGIN" || upper == L"{")
                insideBlock = true;
            continue;   // 跳过所有属性行（FONT, STYLE, EXSTYLE, CLASS 等）
        }

        // END / } → 停止
        if (upper == L"END" || upper == L"}")
            break;

        // ---- 控件区（BEGIN ~ END 之间）----
        if (line.type != rceditor::ERcLineType::Content)
            continue;

        int idc = ParseControlIDCFromLine(trimmed, header);

        CStringW newText;
        bool foundText = false;
        CRect newRect;
        bool foundRect = false;

        bool isStaticId = (idc == -1 || idc == (int)(UINT)0xFFFF || idc == 0);

        // 判断当前行是否为 GROUPBOX
        bool isGroupBoxLine = false;
        {
            CStringW kw = upper;
            int spacePos = kw.FindOneOf(L" \t");
            if (spacePos > 0) kw = kw.Left(spacePos);
            if (kw == L"GROUPBOX")
                isGroupBoxLine = true;
        }

        if (idc == -2)
        {
            // 无法解析 IDC → 跳过替换，保留原文
            continue;
        }
        else if (!isStaticId)
        {
            auto it = lookup.idcToText.find((UINT)idc);
            if (it != lookup.idcToText.end())
            { newText = it->second; foundText = true; }
            auto itR = lookup.idcToRect.find((UINT)idc);
            if (itR != lookup.idcToRect.end())
            { newRect = itR->second; foundRect = true; }
        }
        else if (isGroupBoxLine)
        {
            // GroupBox(IDC=-1)：使用单独的 groupBoxTexts/groupBoxRects 序列
            if (groupBoxCounter < (int)lookup.groupBoxTexts.size())
            { newText = lookup.groupBoxTexts[groupBoxCounter]; foundText = true; }
            if (groupBoxCounter < (int)lookup.groupBoxRects.size())
            { newRect = lookup.groupBoxRects[groupBoxCounter]; foundRect = true; }
            groupBoxCounter++;
        }
        else
        {
            // 其它静态控件 (IDC=-1/0xFFFF/0)：按出现顺序匹配
            if (staticCounter < (int)lookup.staticTexts.size())
            { newText = lookup.staticTexts[staticCounter]; foundText = true; }
            if (staticCounter < (int)lookup.staticRects.size())
            { newRect = lookup.staticRects[staticCounter]; foundRect = true; }
            staticCounter++;
        }

        // 替换引号文本
        if (foundText && line.text.Find(L'"') >= 0)
            line.text = ReplaceFirstQuotedString(line.text, newText);

        // 替换尺寸
        if (replaceDimensions && foundRect)
            ReplaceControlDimensions(line.text, newRect);
    }
}

/// 遍历 STRINGTABLE 块行，按 ID 做引号文本替换
void ReplaceStringTableBlockTexts(
    std::vector<rceditor::SRcLine>& lines,
    int startIdx, int endIdx,
    const std::map<UINT, CStringW>& stringMap,
    const rceditor::SResourceHeader& header)
{
    for (int li = startIdx; li <= endIdx && li < (int)lines.size(); ++li)
    {
        auto& line = lines[li];
        CStringW trimmed = line.text;
        trimmed.TrimLeft();
        trimmed.TrimRight();
        if (trimmed.IsEmpty()) continue;
        if (line.type == rceditor::ERcLineType::Comment) continue;
        if (line.text.Find(L'"') < 0) continue;

        // 第一个非空白 token 即为 ID
        int i = 0;
        while (i < trimmed.GetLength() &&
               trimmed[i] != L' ' && trimmed[i] != L'\t' && trimmed[i] != L',')
            i++;
        CStringW idToken = trimmed.Left(i);

        UINT strId = ResolveTokenHelper(idToken, header);
        if (strId == 0 || strId == (UINT)-2) continue;

        auto itStr = stringMap.find(strId);
        if (itStr != stringMap.end())
            line.text = ReplaceFirstQuotedString(line.text, itStr->second);
    }
}

/// Menu 文本查找表
struct SMenuTextLookup
{
    std::map<UINT, CStringW> menuIdToText;  // 菜单项 ID → 文本
    std::vector<CStringW>    popupTexts;    // POPUP 文本（深度优先序）
};

/// 深度优先收集菜单文本到查找表
void CollectMenuTextsRecursive(
    const std::shared_ptr<rceditor::SMenuInfo>& menu,
    SMenuTextLookup& lookup)
{
    if (!menu) return;

    if (menu->bIsPopup)
        lookup.popupTexts.push_back(menu->csText);
    else if (menu->nID != 0)
        lookup.menuIdToText[menu->nID] = menu->csText;

    for (const auto& sub : menu->vecSubMenu)
        CollectMenuTextsRecursive(sub, lookup);
}

/// 遍历 Menu 块行，POPUP 按序替换，MENUITEM 按 ID 替换
void ReplaceMenuBlockTexts(
    std::vector<rceditor::SRcLine>& lines,
    int startIdx, int endIdx,
    const SMenuTextLookup& lookup,
    const rceditor::SResourceHeader& header)
{
    int popupIndex = 0;

    for (int li = startIdx; li <= endIdx && li < (int)lines.size(); ++li)
    {
        auto& line = lines[li];
        if (line.text.Find(L'"') < 0) continue;

        CStringW trimmed = line.text;
        trimmed.TrimLeft();
        CStringW upper = trimmed;
        upper.MakeUpper();

        if (upper.Left(5) == L"POPUP")
        {
            if (popupIndex < (int)lookup.popupTexts.size())
                line.text = ReplaceFirstQuotedString(line.text, lookup.popupTexts[popupIndex]);
            popupIndex++;
        }
        else if (upper.Left(8) == L"MENUITEM" && upper.Find(L"SEPARATOR") < 0)
        {
            // MENUITEM "text", ID [, ...]
            int firstQuote = trimmed.Find(L'"');
            if (firstQuote < 0) continue;
            int pos = firstQuote + 1;
            while (pos < trimmed.GetLength())
            {
                if (trimmed[pos] == L'"')
                {
                    if (pos + 1 < trimmed.GetLength() && trimmed[pos + 1] == L'"')
                    { pos += 2; continue; }
                    break;
                }
                pos++;
            }
            // 跳过引号后的逗号和空白，取 ID token
            int ci = pos + 1;
            while (ci < trimmed.GetLength() &&
                   (trimmed[ci] == L',' || trimmed[ci] == L' ' || trimmed[ci] == L'\t'))
                ci++;
            int tokenStart = ci;
            while (ci < trimmed.GetLength() &&
                   trimmed[ci] != L',' && trimmed[ci] != L' ' && trimmed[ci] != L'\t')
                ci++;
            if (tokenStart < ci)
            {
                CStringW idToken = trimmed.Mid(tokenStart, ci - tokenStart);
                UINT mId = ResolveTokenHelper(idToken, header);
                if (mId != 0 && mId != (UINT)-2)
                {
                    auto it = lookup.menuIdToText.find(mId);
                    if (it != lookup.menuIdToText.end())
                        line.text = ReplaceFirstQuotedString(line.text, it->second);
                }
            }
        }
    }
}

// ============================================================================
// LANGID → RC 语言指令映射
// ============================================================================

/// LANGID 到 RC 语言指令的映射信息
struct SLangDirectiveInfo
{
    LPCWSTR afxTargSuffix;   // AFX_TARG_xxx 中的 xxx
    LPCWSTR langMacro;       // LANG_xxx
    LPCWSTR sublangMacro;    // SUBLANG_xxx
    UINT    codePage;        // ANSI 代码页
};

/// 根据 LANGID 获取 RC 语言指令信息
/// 优先使用静态表查找，代码页使用 Windows API 动态获取后回退静态值
SLangDirectiveInfo GetLangDirectiveInfo(LANGID langID)
{
    // 静态查找表：(PRIMARYLANGID, SUBLANGID) → 指令信息
    // 覆盖常见语言；未列出的使用通用英语回退
    struct SLangTableEntry
    {
        WORD    primaryLang;
        WORD    subLang;
        LPCWSTR afxTarg;
        LPCWSTR langMacro;
        LPCWSTR sublangMacro;
        UINT    codePage;
    };

    static const SLangTableEntry s_langTable[] =
    {
        { LANG_CHINESE,     SUBLANG_CHINESE_SIMPLIFIED,     L"CHS", L"LANG_CHINESE",    L"SUBLANG_CHINESE_SIMPLIFIED",      936  },
        { LANG_CHINESE,     SUBLANG_CHINESE_TRADITIONAL,    L"CHT", L"LANG_CHINESE",    L"SUBLANG_CHINESE_TRADITIONAL",     950  },
        { LANG_CHINESE,     SUBLANG_CHINESE_HONGKONG,       L"CHT", L"LANG_CHINESE",    L"SUBLANG_CHINESE_HONGKONG",        950  },
        { LANG_CHINESE,     SUBLANG_CHINESE_SINGAPORE,      L"CHS", L"LANG_CHINESE",    L"SUBLANG_CHINESE_SINGAPORE",       936  },
        { LANG_CHINESE,     SUBLANG_CHINESE_MACAU,          L"CHT", L"LANG_CHINESE",    L"SUBLANG_CHINESE_MACAU",           950  },
        { LANG_JAPANESE,    SUBLANG_DEFAULT,                L"JPN", L"LANG_JAPANESE",   L"SUBLANG_DEFAULT",                 932  },
        { LANG_KOREAN,      SUBLANG_DEFAULT,                L"KOR", L"LANG_KOREAN",     L"SUBLANG_DEFAULT",                 949  },
        { LANG_KOREAN,      SUBLANG_KOREAN,                 L"KOR", L"LANG_KOREAN",     L"SUBLANG_KOREAN",                  949  },
        { LANG_ENGLISH,     SUBLANG_ENGLISH_US,             L"ENU", L"LANG_ENGLISH",    L"SUBLANG_ENGLISH_US",              1252 },
        { LANG_ENGLISH,     SUBLANG_ENGLISH_UK,             L"ENG", L"LANG_ENGLISH",    L"SUBLANG_ENGLISH_UK",              1252 },
        { LANG_ENGLISH,     SUBLANG_DEFAULT,                L"ENU", L"LANG_ENGLISH",    L"SUBLANG_DEFAULT",                 1252 },
        { LANG_GERMAN,      SUBLANG_DEFAULT,                L"DEU", L"LANG_GERMAN",     L"SUBLANG_DEFAULT",                 1252 },
        { LANG_GERMAN,      SUBLANG_GERMAN,                 L"DEU", L"LANG_GERMAN",     L"SUBLANG_GERMAN",                  1252 },
        { LANG_FRENCH,      SUBLANG_DEFAULT,                L"FRA", L"LANG_FRENCH",     L"SUBLANG_DEFAULT",                 1252 },
        { LANG_FRENCH,      SUBLANG_FRENCH,                 L"FRA", L"LANG_FRENCH",     L"SUBLANG_FRENCH",                  1252 },
        { LANG_SPANISH,     SUBLANG_DEFAULT,                L"ESP", L"LANG_SPANISH",    L"SUBLANG_DEFAULT",                 1252 },
        { LANG_SPANISH,     SUBLANG_SPANISH_MODERN,         L"ESP", L"LANG_SPANISH",    L"SUBLANG_SPANISH_MODERN",          1252 },
        { LANG_ITALIAN,     SUBLANG_DEFAULT,                L"ITA", L"LANG_ITALIAN",    L"SUBLANG_DEFAULT",                 1252 },
        { LANG_ITALIAN,     SUBLANG_ITALIAN,                L"ITA", L"LANG_ITALIAN",    L"SUBLANG_ITALIAN",                 1252 },
        { LANG_PORTUGUESE,  SUBLANG_DEFAULT,                L"PTB", L"LANG_PORTUGUESE", L"SUBLANG_DEFAULT",                 1252 },
        { LANG_PORTUGUESE,  SUBLANG_PORTUGUESE_BRAZILIAN,   L"PTB", L"LANG_PORTUGUESE", L"SUBLANG_PORTUGUESE_BRAZILIAN",    1252 },
        { LANG_RUSSIAN,     SUBLANG_DEFAULT,                L"RUS", L"LANG_RUSSIAN",    L"SUBLANG_DEFAULT",                 1251 },
        { LANG_POLISH,      SUBLANG_DEFAULT,                L"PLK", L"LANG_POLISH",     L"SUBLANG_DEFAULT",                 1250 },
        { LANG_CZECH,       SUBLANG_DEFAULT,                L"CSY", L"LANG_CZECH",      L"SUBLANG_DEFAULT",                 1250 },
        { LANG_HUNGARIAN,   SUBLANG_DEFAULT,                L"HUN", L"LANG_HUNGARIAN",  L"SUBLANG_DEFAULT",                 1250 },
        { LANG_TURKISH,     SUBLANG_DEFAULT,                L"TRK", L"LANG_TURKISH",    L"SUBLANG_DEFAULT",                 1254 },
        { LANG_THAI,        SUBLANG_DEFAULT,                L"THA", L"LANG_THAI",       L"SUBLANG_DEFAULT",                 874  },
        { LANG_ARABIC,      SUBLANG_DEFAULT,                L"ARA", L"LANG_ARABIC",     L"SUBLANG_DEFAULT",                 1256 },
        { LANG_HEBREW,      SUBLANG_DEFAULT,                L"HEB", L"LANG_HEBREW",     L"SUBLANG_DEFAULT",                 1255 },
        { LANG_VIETNAMESE,  SUBLANG_DEFAULT,                L"VIT", L"LANG_VIETNAMESE", L"SUBLANG_DEFAULT",                 1258 },
        { LANG_DUTCH,       SUBLANG_DEFAULT,                L"NLD", L"LANG_DUTCH",      L"SUBLANG_DEFAULT",                 1252 },
        { LANG_SWEDISH,     SUBLANG_DEFAULT,                L"SVE", L"LANG_SWEDISH",    L"SUBLANG_DEFAULT",                 1252 },
        { LANG_DANISH,      SUBLANG_DEFAULT,                L"DAN", L"LANG_DANISH",     L"SUBLANG_DEFAULT",                 1252 },
        { LANG_NORWEGIAN,   SUBLANG_DEFAULT,                L"NOR", L"LANG_NORWEGIAN",  L"SUBLANG_DEFAULT",                 1252 },
        { LANG_FINNISH,     SUBLANG_DEFAULT,                L"FIN", L"LANG_FINNISH",    L"SUBLANG_DEFAULT",                 1252 },
        { LANG_GREEK,       SUBLANG_DEFAULT,                L"ELL", L"LANG_GREEK",      L"SUBLANG_DEFAULT",                 1253 },
        { LANG_ROMANIAN,    SUBLANG_DEFAULT,                L"ROM", L"LANG_ROMANIAN",   L"SUBLANG_DEFAULT",                 1250 },
        { LANG_UKRAINIAN,   SUBLANG_DEFAULT,                L"UKR", L"LANG_UKRAINIAN",  L"SUBLANG_DEFAULT",                 1251 },
    };

    WORD priLang = PRIMARYLANGID(langID);
    WORD subLang = SUBLANGID(langID);

    // 精确匹配
    for (const auto& entry : s_langTable)
    {
        if (entry.primaryLang == priLang && entry.subLang == subLang)
        {
            SLangDirectiveInfo info;
            info.afxTargSuffix = entry.afxTarg;
            info.langMacro = entry.langMacro;
            info.sublangMacro = entry.sublangMacro;
            info.codePage = entry.codePage;

            // 使用 Windows API 动态获取代码页（更准确）
            LCID lcid = MAKELCID(langID, SORT_DEFAULT);
            wchar_t cpBuf[16] = {};
            if (GetLocaleInfoW(lcid, LOCALE_IDEFAULTANSICODEPAGE, cpBuf, _countof(cpBuf)) > 0)
            {
                UINT dynamicCp = (UINT)_wtoi(cpBuf);
                if (dynamicCp > 0)
                    info.codePage = dynamicCp;
            }
            return info;
        }
    }

    // 仅匹配 primaryLang（忽略子语言）
    for (const auto& entry : s_langTable)
    {
        if (entry.primaryLang == priLang)
        {
            SLangDirectiveInfo info;
            info.afxTargSuffix = entry.afxTarg;
            info.langMacro = entry.langMacro;
            info.sublangMacro = (subLang == SUBLANG_DEFAULT) ? L"SUBLANG_DEFAULT" : entry.sublangMacro;
            info.codePage = entry.codePage;

            LCID lcid = MAKELCID(langID, SORT_DEFAULT);
            wchar_t cpBuf[16] = {};
            if (GetLocaleInfoW(lcid, LOCALE_IDEFAULTANSICODEPAGE, cpBuf, _countof(cpBuf)) > 0)
            {
                UINT dynamicCp = (UINT)_wtoi(cpBuf);
                if (dynamicCp > 0)
                    info.codePage = dynamicCp;
            }
            return info;
        }
    }

    // 完全未知语言 — 回退到英语
    SLangDirectiveInfo fallback;
    fallback.afxTargSuffix = L"ENU";
    fallback.langMacro = L"LANG_ENGLISH";
    fallback.sublangMacro = L"SUBLANG_DEFAULT";
    fallback.codePage = 1252;

    LCID lcid = MAKELCID(langID, SORT_DEFAULT);
    wchar_t cpBuf[16] = {};
    if (GetLocaleInfoW(lcid, LOCALE_IDEFAULTANSICODEPAGE, cpBuf, _countof(cpBuf)) > 0)
    {
        UINT dynamicCp = (UINT)_wtoi(cpBuf);
        if (dynamicCp > 0)
            fallback.codePage = dynamicCp;
    }
    return fallback;
}

/// 替换 RC 文件行中的语言相关指令，使其匹配目标 LANGID
/// 处理三类行:
///   1. #if !defined(AFX_RESOURCE_DLL) || defined(AFX_TARG_xxx)
///   2. LANGUAGE LANG_xxx, SUBLANG_xxx
///   3. #pragma code_page(nnn)
void ReplaceLanguageDirectives(std::vector<rceditor::SRcLine>& lines, LANGID targetLangID)
{
    SLangDirectiveInfo info = GetLangDirectiveInfo(targetLangID);

    CStringW newAfxTarg;
    newAfxTarg.Format(L"AFX_TARG_%s", info.afxTargSuffix);

    CStringW newLanguageLine;
    newLanguageLine.Format(L"LANGUAGE %s, %s", info.langMacro, info.sublangMacro);

    CStringW newCodePage;
    newCodePage.Format(L"%u", info.codePage);

    for (auto& line : lines)
    {
        CStringW trimmed = line.text;
        trimmed.TrimLeft();
        trimmed.TrimRight();
        CStringW upper = trimmed;
        upper.MakeUpper();

        // --- 1. #if !defined(AFX_RESOURCE_DLL) || defined(AFX_TARG_xxx) ---
        if (upper.Find(L"AFX_TARG_") >= 0 && upper.Find(L"DEFINED") >= 0)
        {
            // 找到 AFX_TARG_xxx 并替换 xxx 部分
            int pos = line.text.Find(L"AFX_TARG_");
            if (pos >= 0)
            {
                int suffixStart = pos + 9; // len("AFX_TARG_")
                int suffixEnd = suffixStart;
                while (suffixEnd < line.text.GetLength() &&
                       (iswalpha(line.text[suffixEnd]) || iswdigit(line.text[suffixEnd]) || line.text[suffixEnd] == L'_'))
                    suffixEnd++;
                line.text = line.text.Left(pos) + newAfxTarg + line.text.Mid(suffixEnd);
            }
            continue;
        }

        // --- 2. LANGUAGE LANG_xxx, SUBLANG_xxx ---
        if (upper.Left(8) == L"LANGUAGE" && upper.GetLength() > 8 &&
            (upper[8] == L' ' || upper[8] == L'\t'))
        {
            // 保留原始行的前导空白
            int leadingSpaces = 0;
            while (leadingSpaces < line.text.GetLength() &&
                   (line.text[leadingSpaces] == L' ' || line.text[leadingSpaces] == L'\t'))
                leadingSpaces++;
            line.text = line.text.Left(leadingSpaces) + newLanguageLine;
            continue;
        }

        // --- 3. #pragma code_page(nnn) ---
        if (upper.Find(L"CODE_PAGE") >= 0 && upper.Find(L"#PRAGMA") >= 0)
        {
            int parenOpen = line.text.Find(L'(');
            int parenClose = line.text.Find(L')', parenOpen + 1);
            if (parenOpen >= 0 && parenClose > parenOpen)
            {
                line.text = line.text.Left(parenOpen + 1) + newCodePage + line.text.Mid(parenClose);
            }
            continue;
        }
    }
}

/// 检查某个块是否为 GUIDELINES DESIGNINFO 内部的伪块
/// DESIGNINFO 条目格式: "IDD_X, DIALOG"（名称后紧跟逗号）
/// 真正的资源头行格式: "IDD_X DIALOGEX 0, 0, w, h"（名称后紧跟空白）
bool IsDesignInfoEntry(const std::vector<rceditor::SRcLine>& lines, const rceditor::SRcBlock& block)
{
    if (block.resourceName.IsEmpty()) return false;
    if (block.headerLineIndex < 0 || block.headerLineIndex >= (int)lines.size())
        return false;

    CStringW trimmed = lines[block.headerLineIndex].text;
    trimmed.TrimLeft();
    trimmed.TrimRight();

    int nameLen = block.resourceName.GetLength();
    if (nameLen >= trimmed.GetLength()) return false;

    // 名称之后的第一个字符: 空白 → 真正的资源头; 逗号 → DESIGNINFO 伪条目
    wchar_t ch = trimmed[nameLen];
    return (ch == L',');
}

// ============================================================================
// DESIGNINFO 修复机制
// GUIDELINES DESIGNINFO 段落内部的每个 IDD_XXX, DIALOG / BEGIN / ... 条目
// 都需要匹配的 END。合并管线或原始 RC 可能导致这些内部 END 缺失。
// 此函数在管线结束后扫描并修复 BEGIN/END 平衡。
// ============================================================================

// ============================================================================
// 共用辅助：Token 级资源头行检测
// ============================================================================

/*!
 * \brief 判断行是否为资源头行（如 "IDD_XXX DIALOGEX ..." 或 "STRINGTABLE"）。
 *
 * 使用 token 精确匹配：提取行的前两个非引号 token，检查是否与已知资源类型
 * 关键词完全相同。避免 `Find()` 子串匹配导致引号内文本（如 "Dialog Settings"）
 * 触发误判（此为 premature END 的根本原因）。
 *
 * \param lineText 行文本
 * \param keywords nullptr 结尾的关键词数组
 * \return true = 行首的 token 结构符合资源头行模式
 */
static bool IsResourceHeaderByToken(const CStringW& lineText, const wchar_t* const keywords[])
{
    CStringW t = lineText; t.TrimLeft(); t.TrimRight();
    if (t.IsEmpty()) return false;
    if (t.GetAt(0) == L'#') return false;
    if (t.Left(2) == L"//" || t.Left(2) == L"/*") return false;

    CStringW upper = t; upper.MakeUpper();

    // 提取第 1 个 token（到空白截止）
    int pos = 0;
    while (pos < upper.GetLength() && upper[pos] != L' ' && upper[pos] != L'\t')
        ++pos;
    CStringW tok1 = upper.Left(pos);

    // 第 1 个 token 匹配关键词（STRINGTABLE、DESIGNINFO 等无名称前缀的资源）？
    for (int k = 0; keywords[k]; ++k)
    {
        if (tok1 == keywords[k])
            return true;
    }

    // 第 1 个 token 必须是标识符（字母/下划线/数字开头），否则不可能是资源头
    if (tok1.IsEmpty()) return false;
    wchar_t ch0 = tok1[0];
    if (!(iswalpha(ch0) || ch0 == L'_' || iswdigit(ch0)))
        return false;

    // 跳过空白，提取第 2 个 token
    while (pos < upper.GetLength() && (upper[pos] == L' ' || upper[pos] == L'\t'))
        ++pos;
    if (pos >= upper.GetLength()) return false;

    // 第 2 个 token 以引号开头 → 不是资源类型关键词（是控件文本）
    if (upper[pos] == L'"') return false;

    int ts = pos;
    while (pos < upper.GetLength() && upper[pos] != L' ' && upper[pos] != L'\t' && upper[pos] != L',')
        ++pos;
    CStringW tok2 = upper.Mid(ts, pos - ts);

    // 第 2 个 token 匹配关键词？
    for (int k = 0; keywords[k]; ++k)
    {
        if (tok2 == keywords[k])
            return true;
    }
    return false;
}

/// 修复 DESIGNINFO 段落的 BEGIN/END 平衡
/// 原理：遍历 DESIGNINFO 的内容，跟踪"挂起的 BEGIN"栈。
/// 当遇到新的 "IDD_XXX, DIALOG" 伪条目行时，说明上一个条目结束了——
/// 如果此时有未关闭的 BEGIN，插入对应数量的 END。
/// 段落结束时（遇到外层 END 或 #endif），同样补齐所有挂起的 BEGIN。
void RepairDesignInfoSections(std::vector<rceditor::SRcLine>& lines)
{
    for (int i = 0; i < (int)lines.size(); ++i)
    {
        CStringW trimmed = lines[i].text;
        trimmed.TrimLeft(); trimmed.TrimRight();
        CStringW upper = trimmed; upper.MakeUpper();

        // 匹配 "GUIDELINES DESIGNINFO" 或独立 "DESIGNINFO"
        // 使用 token 精确匹配，避免引号内文本触发误判
        {
            static const wchar_t* diKeywords[] = { L"DESIGNINFO", nullptr };
            if (!IsResourceHeaderByToken(lines[i].text, diKeywords))
                continue;
        }

        // 找后续的外层 BEGIN
        int outerBegin = i + 1;
        while (outerBegin < (int)lines.size())
        {
            CStringW t = lines[outerBegin].text; t.TrimLeft(); t.TrimRight();
            CStringW u = t; u.MakeUpper();
            if (t.IsEmpty() || u.Left(2) == L"//" || u.Left(2) == L"/*")
            { ++outerBegin; continue; }
            break;
        }
        if (outerBegin >= (int)lines.size()) continue;
        {
            CStringW bLine = lines[outerBegin].text;
            bLine.TrimLeft(); bLine.TrimRight(); bLine.MakeUpper();
            if (bLine != L"BEGIN" && bLine != L"{") continue;
        }

        // 扫描 DESIGNINFO 内部，修复每个条目的 BEGIN/END 平衡
        // innerDepth 跟踪当前条目内尚未匹配的 BEGIN 数量
        int innerDepth = 0;
        int outerDepth = 1; // 外层 BEGIN 已计入
        int li = outerBegin + 1;

        // 检测 DESIGNINFO 伪条目头行的 lambda
        // 格式: "IDD_X, DIALOG" "IDD_X, DIALOG" 等 (名称后紧跟逗号)
        auto isDesignInfoEntryLine = [](const CStringW& lineText) -> bool
        {
            CStringW t = lineText; t.TrimLeft(); t.TrimRight();
            if (t.IsEmpty()) return false;
            CStringW u = t; u.MakeUpper();
            // 必须含有 DIALOG 关键词
            if (u.Find(L"DIALOG") < 0) return false;
            // 查找逗号：名称(标识符)后紧跟逗号
            int pos = 0;
            // 跳过标识符字符
            while (pos < t.GetLength() && (iswalnum(t[pos]) || t[pos] == L'_'))
                ++pos;
            // 跳过空白
            while (pos < t.GetLength() && (t[pos] == L' ' || t[pos] == L'\t'))
                ++pos;
            return (pos < t.GetLength() && t[pos] == L',');
        };

        // 推断缩进（取外层 BEGIN 行的缩进 + 额外 4 空格）
        CStringW outerBeginFull = lines[outerBegin].text;
        CStringW indentBase;
        for (int ci = 0; ci < outerBeginFull.GetLength(); ++ci)
        {
            wchar_t ch = outerBeginFull[ci];
            if (ch == L' ' || ch == L'\t')
                indentBase += ch;
            else
                break;
        }
        CStringW endIndent = indentBase + L"    "; // 内部条目 END 的缩进

        while (li < (int)lines.size())
        {
            CStringW t = lines[li].text; t.TrimLeft(); t.TrimRight();
            CStringW u = t; u.MakeUpper();

            // 到达 #endif 等预处理指令 → 段落在这里断裂
            if (u.GetLength() > 0 && u.GetAt(0) == L'#')
            {
                // 补齐所有挂起的内部 END
                int insertPos = li;
                for (int d = 0; d < innerDepth; ++d)
                {
                    rceditor::SRcLine endLine;
                    endLine.text = endIndent + L"END";
                    endLine.lineNumber = insertPos + 1;
                    endLine.blockIndex = -1;
                    endLine.type = rceditor::ERcLineType::Comment;
                    lines.insert(lines.begin() + insertPos, endLine);
                    ++insertPos;
                    ++li;
                }
                // 还要补外层 END
                if (outerDepth > 0)
                {
                    rceditor::SRcLine endLine;
                    endLine.text = indentBase + L"END";
                    endLine.lineNumber = insertPos + 1;
                    endLine.blockIndex = -1;
                    endLine.type = rceditor::ERcLineType::Comment;
                    lines.insert(lines.begin() + insertPos, endLine);
                    ++li;
                }
                break;
            }

            // 外层 END（outerDepth 减到 0 时退出）
            if (u == L"END" || u == L"}")
            {
                if (innerDepth > 0)
                {
                    // 这是一个内部 END
                    --innerDepth;
                }
                else
                {
                    // 这是外层 END → 整个 DESIGNINFO 段落结束
                    --outerDepth;
                    break;
                }
                ++li;
                continue;
            }

            if (u == L"BEGIN" || u == L"{")
            {
                ++innerDepth;
                ++li;
                continue;
            }

            // 检查是否为新的伪条目头行 (IDD_X, DIALOG)
            if (isDesignInfoEntryLine(lines[li].text))
            {
                // 上一个条目的内部 BEGIN 有没有被关闭？
                // 如果 innerDepth > 0，需要在此行之前插入 END
                int insertPos = li;
                while (innerDepth > 0)
                {
                    rceditor::SRcLine endLine;
                    endLine.text = endIndent + L"END";
                    endLine.lineNumber = insertPos + 1;
                    endLine.blockIndex = -1;
                    endLine.type = rceditor::ERcLineType::Comment;
                    lines.insert(lines.begin() + insertPos, endLine);
                    ++insertPos;
                    ++li;
                    --innerDepth;
                }
            }

            ++li;
        }

        // 如果循环结束时仍有挂起的 BEGIN，补齐 END（极端情况）
        if (outerDepth > 0 && li >= (int)lines.size())
        {
            for (int d = 0; d < innerDepth; ++d)
            {
                rceditor::SRcLine endLine;
                endLine.text = endIndent + L"END";
                endLine.lineNumber = (int)lines.size() + 1;
                endLine.blockIndex = -1;
                endLine.type = rceditor::ERcLineType::Comment;
                lines.push_back(endLine);
            }
            {
                rceditor::SRcLine endLine;
                endLine.text = indentBase + L"END";
                endLine.lineNumber = (int)lines.size() + 1;
                endLine.blockIndex = -1;
                endLine.type = rceditor::ERcLineType::Comment;
                lines.push_back(endLine);
            }
        }

        // 跳过已处理的部分，避免重复扫描
        // (i 会在 for 循环的 ++i 后前进)
    }

    // 重编行号
    for (int i = 0; i < (int)lines.size(); ++i)
        lines[i].lineNumber = i + 1;
}

/// 通用 BEGIN/END 平衡修复：扫描整个文件，为缺失 END 的 BEGIN 插入 END。
/// 适用于所有资源块类型（Dialog, DLGINIT, VERSIONINFO, StringFileInfo 等）。
/// 原理：用一个栈跟踪 BEGIN 的行号和缩进。遇到新的资源头行（如 IDD_XXX DIALOGEX
/// 或 IDD_XXX DLGINIT）或文件结尾时，如果栈不为空则补齐 END。
/// 注意：此函数应在 DESIGNINFO 修复之后调用，因为 DESIGNINFO 需要特殊处理。
void RepairAllBeginEndBalance(std::vector<rceditor::SRcLine>& lines)
{
    // 扫一遍文件，逐行检查 BEGIN/END 平衡
    // 用深度计数：每遇到 BEGIN +1，每遇到 END -1
    // 如果遇到新的顶层资源头行（depth 应该为 0），但 depth > 0，
    // 说明前一个块缺少 END → 在资源头行之前插入 END

    int depth = 0;
    CStringW lastIndent = L""; // 最外层 BEGIN 的缩进
    bool inDesignInfo = false; // DESIGNINFO 段落跳过（已由专门的修复处理）

    for (int i = 0; i < (int)lines.size(); ++i)
    {
        CStringW t = lines[i].text; t.TrimLeft(); t.TrimRight();
        if (t.IsEmpty()) continue;
        CStringW upper = t; upper.MakeUpper();

        // 跳过 DESIGNINFO 段落（已被 RepairDesignInfoSections 处理）
        // 使用 token 匹配避免控件文本中的 "DESIGNINFO" 子串触发误判
        {
            static const wchar_t* designKeywords[] = { L"DESIGNINFO", nullptr };
            if (!inDesignInfo && IsResourceHeaderByToken(lines[i].text, designKeywords))
            {
                // 先补齐之前丢失的 END
                while (depth > 0)
                {
                    rceditor::SRcLine endLine;
                    endLine.text = L"END";
                    endLine.lineNumber = i + 1;
                    endLine.blockIndex = -1;
                    endLine.type = rceditor::ERcLineType::Comment;
                    lines.insert(lines.begin() + i, endLine);
                    ++i;
                    --depth;
                }
                inDesignInfo = true;
                continue;
            }
        }
        if (inDesignInfo)
        {
            // DESIGNINFO 段落内部什么都不做，直到段落结束
            // 段落结束标志：#endif（DESIGNINFO 通常在 #ifdef APSTUDIO_INVOKED 内）
            if (upper.Left(6) == L"#ENDIF")
                inDesignInfo = false;
            continue;
        }

        // 预处理指令：不影响 depth
        if (upper.GetAt(0) == L'#') continue;
        // 注释行
        if (upper.Left(2) == L"//" || upper.Left(2) == L"/*") continue;

        if (upper == L"BEGIN" || upper == L"{")
        {
            if (depth == 0)
            {
                // 记录外层 BEGIN 的缩进
                CStringW full = lines[i].text;
                lastIndent.Empty();
                for (int ci = 0; ci < full.GetLength(); ++ci)
                {
                    if (full[ci] == L' ' || full[ci] == L'\t')
                        lastIndent += full[ci];
                    else break;
                }
            }
            ++depth;
            continue;
        }

        if (upper == L"END" || upper == L"}")
        {
            if (depth > 0) --depth;
            continue;
        }

        // 检查是否为顶层资源头行（depth 应该为 0）
        // 使用 token 精确匹配，避免引号内文本（如 "Dialog Settings"）
        // 触发误判导致 premature END 插入
        if (depth > 0)
        {
            static const wchar_t* resKeywords[] = {
                L"DIALOGEX", L"DIALOG", L"DLGINIT", L"VERSIONINFO",
                L"TEXTINCLUDE", L"STRINGTABLE", L"MENUEX", L"MENU",
                L"ACCELERATORS", L"TOOLBAR", L"AFX_DIALOG_LAYOUT",
                nullptr
            };
            bool isResourceHeader = IsResourceHeaderByToken(lines[i].text, resKeywords);

            if (isResourceHeader)
            {
                // 当前有未关闭的 BEGIN → 在此行之前插入缺失的 END
                while (depth > 0)
                {
                    rceditor::SRcLine endLine;
                    endLine.text = L"END";
                    endLine.lineNumber = i + 1;
                    endLine.blockIndex = -1;
                    endLine.type = rceditor::ERcLineType::Comment;
                    lines.insert(lines.begin() + i, endLine);
                    ++i;
                    --depth;
                }
            }
        }
    }

    // 文件末尾：如果还有未关闭的 BEGIN，补齐
    while (depth > 0)
    {
        rceditor::SRcLine endLine;
        endLine.text = L"END";
        endLine.lineNumber = (int)lines.size() + 1;
        endLine.blockIndex = -1;
        endLine.type = rceditor::ERcLineType::Comment;
        lines.push_back(endLine);
        --depth;
    }

    // 重编行号
    for (int i = 0; i < (int)lines.size(); ++i)
        lines[i].lineNumber = i + 1;
}

/// 修复 DIALOG/MENU 块中的 premature END：
/// 当一个块内存在多个 depth-0 END 时，说明有提前关闭的 END 把块截断了。
/// 此函数找到每个块的全部 depth-0 END，只保留最后一个，删除提前的 END，
/// 同时去重因错误合并产生的重复控件行。
/// 这是管线最终阶段的安全网——在所有其他修复（Align、Validate、
/// RepairDesignInfo、RepairAllBeginEndBalance）之后运行。
void RemovePrematureEndsInDialogBlocks(std::vector<rceditor::SRcLine>& lines)
{
    // 辅助：判断一行是否为资源头行（IDD_XXX DIALOGEX / IDR_XXX MENU 等）
    // 使用 token 精确匹配，避免引号内 "DIALOG" / "MENU" 触发误判
    auto isDialogOrMenuHeader = [](const CStringW& lineText) -> bool
    {
        static const wchar_t* dlgMenuKeywords[] = {
            L"DIALOGEX", L"DIALOG", L"MENUEX", L"MENU", nullptr
        };
        if (!IsResourceHeaderByToken(lineText, dlgMenuKeywords))
            return false;
        // 排除 DESIGNINFO 伪条目（"IDD_X, DIALOG"）：标识符后紧跟逗号
        CStringW t = lineText; t.TrimLeft(); t.TrimRight();
        int pos = 0;
        while (pos < t.GetLength() && (iswalnum(t[pos]) || t[pos] == L'_'))
            ++pos;
        if (pos > 0 && pos < t.GetLength() && t[pos] == L',')
            return false;
        return true;
    };

    // 辅助：判断一行是否为任何资源头行（用于界定块范围）
    // 使用 token 精确匹配，避免引号内文本触发误判
    auto isAnyResourceHeader = [](const CStringW& lineText) -> bool
    {
        static const wchar_t* keywords[] = {
            L"DIALOGEX", L"DIALOG", L"MENUEX", L"MENU", L"STRINGTABLE",
            L"ACCELERATORS", L"TOOLBAR", L"VERSIONINFO", L"DLGINIT",
            L"TEXTINCLUDE", L"AFX_DIALOG_LAYOUT", L"DESIGNINFO", nullptr
        };
        return IsResourceHeaderByToken(lineText, keywords);
    };

    int i = 0;
    while (i < (int)lines.size())
    {
        // 找 DIALOG/MENU 头行
        if (!isDialogOrMenuHeader(lines[i].text))
        {
            ++i;
            continue;
        }

        int headerIdx = i;

        // 找后面的 BEGIN
        int beginIdx = -1;
        for (int j = headerIdx + 1; j < (int)lines.size(); ++j)
        {
            CStringW t = lines[j].text; t.TrimLeft(); t.TrimRight(); t.MakeUpper();
            if (t == L"BEGIN" || t == L"{") { beginIdx = j; break; }
            // 如果遇到另一个资源头或预处理指令，说明没有 BEGIN
            if (t.GetLength() > 0 && t.GetAt(0) == L'#') break;
            if (isAnyResourceHeader(lines[j].text)) break;
        }
        if (beginIdx < 0) { ++i; continue; }

        // 找该块的安全边界（下一个资源头行或预处理指令或文件末尾）
        int boundary = (int)lines.size();
        for (int j = beginIdx + 1; j < (int)lines.size(); ++j)
        {
            CStringW t = lines[j].text; t.TrimLeft(); t.TrimRight();
            if (t.IsEmpty()) continue;
            CStringW u = t; u.MakeUpper();
            if (u.GetAt(0) == L'#') { boundary = j; break; }
            if (isAnyResourceHeader(lines[j].text)) { boundary = j; break; }
        }

        // 在 [beginIdx, boundary) 范围内做 depth 扫描，找所有 depth-0 END
        std::vector<int> depth0Ends;
        int d = 1; // BEGIN 之后深度为 1
        for (int j = beginIdx + 1; j < boundary; ++j)
        {
            CStringW t = lines[j].text; t.TrimLeft(); t.TrimRight(); t.MakeUpper();
            if (t == L"BEGIN" || t == L"{") ++d;
            else if (t == L"END" || t == L"}")
            {
                --d;
                if (d == 0)
                {
                    depth0Ends.push_back(j);
                    d = 1; // 重置以继续扫描后续内容
                }
            }
        }

        // 只有一个 depth-0 END（正常情况），跳过
        if (depth0Ends.size() <= 1)
        {
            i = (depth0Ends.empty() ? beginIdx : depth0Ends.back()) + 1;
            continue;
        }

        // 有多个 depth-0 END → 需要修复
        // 策略：重建块体 = header...BEGIN + 去重内容 + 最后一个 END
        // 保留 header 到 BEGIN（包含 BEGIN）的所有行不变
        std::vector<rceditor::SRcLine> headerPart(
            lines.begin() + headerIdx, lines.begin() + beginIdx + 1);
        std::vector<rceditor::SRcLine> body;
        std::set<CStringW> seenLines;

        int segStart = beginIdx + 1;
        for (int ei = 0; ei < (int)depth0Ends.size(); ++ei)
        {
            int segEnd = depth0Ends[ei]; // depth-0 END 行（不包含）
            for (int j = segStart; j < segEnd; ++j)
            {
                CStringW trimmed = lines[j].text;
                trimmed.TrimLeft(); trimmed.TrimRight();
                if (trimmed.IsEmpty()) continue;
                CStringW upper = trimmed; upper.MakeUpper();
                // 嵌套 BEGIN/END（菜单 POPUP）始终保留
                if (upper == L"BEGIN" || upper == L"{" ||
                    upper == L"END" || upper == L"}")
                {
                    body.push_back(lines[j]);
                    continue;
                }
                // 控件行：去重
                if (seenLines.insert(trimmed).second)
                    body.push_back(lines[j]);
            }
            segStart = segEnd + 1; // 跳过 depth-0 END 自身
        }

        // 用最后一个 depth-0 END 行作为结尾
        rceditor::SRcLine endLine = lines[depth0Ends.back()];

        // 计算替换范围：从 headerIdx 到最后一个 depth-0 END
        int replaceEnd = depth0Ends.back();

        // 构建新内容
        std::vector<rceditor::SRcLine> newBlock;
        newBlock.insert(newBlock.end(), headerPart.begin(), headerPart.end());
        newBlock.insert(newBlock.end(), body.begin(), body.end());
        newBlock.push_back(endLine);

        // 替换
        lines.erase(lines.begin() + headerIdx, lines.begin() + replaceEnd + 1);
        lines.insert(lines.begin() + headerIdx, newBlock.begin(), newBlock.end());

        // 继续从替换后的位置扫描
        i = headerIdx + (int)newBlock.size();
    }

    // 重编行号
    for (int j = 0; j < (int)lines.size(); ++j)
        lines[j].lineNumber = j + 1;
}

} // anonymous namespace

CRcCompareControler::CRcCompareControler()
    : m_primaryLangID(1033)     // 默认英语
    , m_secondaryLangID(2052)   // 默认简体中文
    , m_currentTaskIndex(-1)
{
}

CRcCompareControler::~CRcCompareControler()
{
    ClearFiles();
}

void CRcCompareControler::SetPrimaryLanguage(LANGID langID)
{
    m_primaryLangID = langID;
}

void CRcCompareControler::SetSecondaryLanguage(LANGID langID)
{
    m_secondaryLangID = langID;
}

bool CRcCompareControler::AddPrimaryFile(const CString& strFilePath)
{
    if (!PathFileExists(strFilePath))
        return false;

    // 检查是否已加载
    if (m_mapPrimaryEditors.find(strFilePath) != m_mapPrimaryEditors.end())
        return true;

    auto pEditor = std::make_shared<rceditor::PEFileEditor>(strFilePath);
    auto errCode = pEditor->ReadPEInfo();
    if (errCode != rceditor::PEFileEditor::ERR_SUCCESS)
        return false;

    m_mapPrimaryEditors[strFilePath] = pEditor;
    return true;
}

bool CRcCompareControler::AddSecondaryFile(const CString& strFilePath)
{
    if (!PathFileExists(strFilePath))
        return false;

    // 检查是否已加载
    if (m_mapSecondaryEditors.find(strFilePath) != m_mapSecondaryEditors.end())
        return true;

    auto pEditor = std::make_shared<rceditor::PEFileEditor>(strFilePath);
    auto errCode = pEditor->ReadPEInfo();
    if (errCode != rceditor::PEFileEditor::ERR_SUCCESS)
        return false;

    m_mapSecondaryEditors[strFilePath] = pEditor;
    return true;
}

void CRcCompareControler::ClearFiles()
{
    m_mapPrimaryEditors.clear();
    m_mapSecondaryEditors.clear();
    m_vecAddedItems.clear();
}

int CRcCompareControler::DoCompare()
{
    m_vecAddedItems.clear();
    int nTotalAdded = 0;

    // 遍历所有主语言文件
    for (const auto& primaryPair : m_mapPrimaryEditors)
    {
        const CString& strPrimaryPath = primaryPair.first;
        auto& pPrimaryEditor = primaryPair.second;

        // 查找匹配的次语言文件
        CString strSecondaryPath = FindMatchingSecondaryFile(strPrimaryPath);
        if (strSecondaryPath.IsEmpty())
            continue;

        auto itSecondary = m_mapSecondaryEditors.find(strSecondaryPath);
        if (itSecondary == m_mapSecondaryEditors.end())
            continue;

        auto& pSecondaryEditor = itSecondary->second;

        // 获取资源信息的深拷贝
        rceditor::SSingleResourceInfo primaryRes = 
            rceditor::DeepCloneResourceInfo(pPrimaryEditor->GetResourceInfo());
        rceditor::SSingleResourceInfo secondaryRes = 
            rceditor::DeepCloneResourceInfo(pSecondaryEditor->GetResourceInfo());

        // 使用 operator+= 将主语言资源合并到次语言资源
        // 这会自动找出次语言中缺失的项，并添加到 vecAddedCtrlInfo/vecAddedStrings 中
        secondaryRes += primaryRes;

        // 从合并后的资源中提取新增项信息
        nTotalAdded += CollectAddedItemsFromResource(strSecondaryPath, secondaryRes);

        // 更新次语言编辑器的资源信息
        pSecondaryEditor->SetResourceInfo(secondaryRes);
    }

    // 触发比较完成回调
    if (m_pfnOnCompareComplete)
    {
        m_pfnOnCompareComplete(nTotalAdded);
    }

    return nTotalAdded;
}

int CRcCompareControler::CompareDialogs(const CString& strFilePath,
                                         const std::shared_ptr<rceditor::SSingleResourceInfo>& pPrimary,
                                         const std::shared_ptr<rceditor::SSingleResourceInfo>& pSecondary)
{
    // 此函数已废弃，使用 CollectAddedItemsFromResource 替代
    return 0;
}

void CRcCompareControler::RebuildAddedItemsFromModel()
{
    // 保留已有修改状态的映射 (dialogID, ctrlIndex) -> bModified/csSecondaryText
    struct ModifiedState { bool bModified; CStringW csSecondaryText; };
    std::map<std::pair<UINT, int>, ModifiedState> ctrlStates;
    std::map<UINT, ModifiedState> strStates;
    for (const auto& item : m_vecAddedItems)
    {
        if (item.eType == EAddedItemType::Control)
            ctrlStates[{item.nDialogID, item.nCtrlIndex}] = { item.bModified, item.csSecondaryText };
        else if (item.eType == EAddedItemType::String)
            strStates[item.nStringID] = { item.bModified, item.csSecondaryText };
    }

    m_vecAddedItems.clear();

    // 从所有已加载任务的次语言资源中重新收集
    for (const auto& data : m_vecTaskData)
    {
        if (!data.bLoaded)
            continue;
        CString filePath = data.config.secondaryDllPath;
        CollectAddedItemsFromResource(filePath, data.secondaryBinaryRes);
    }

    // 恢复之前保存的修改状态
    for (auto& item : m_vecAddedItems)
    {
        if (item.eType == EAddedItemType::Control)
        {
            auto it = ctrlStates.find({item.nDialogID, item.nCtrlIndex});
            if (it != ctrlStates.end())
            {
                item.bModified = it->second.bModified;
                item.csSecondaryText = it->second.csSecondaryText;
            }
        }
        else if (item.eType == EAddedItemType::String)
        {
            auto it = strStates.find(item.nStringID);
            if (it != strStates.end())
            {
                item.bModified = it->second.bModified;
                item.csSecondaryText = it->second.csSecondaryText;
            }
        }
    }
}

int CRcCompareControler::CollectAddedItemsFromResource(const CString& strFilePath,
                                                        const rceditor::SSingleResourceInfo& res)
{
    int nAdded = 0;

    // 收集对话框中的新增控件
    for (const auto& dlgPair : res.mapLangAndDialogInfo)
    {
        UINT nDlgID = dlgPair.first;
        const auto& pDlg = dlgPair.second;
        if (!pDlg)
            continue;

        // 遍历新增控件列表
        for (size_t i = 0; i < pDlg->vecAddedCtrlInfo.size(); ++i)
        {
            const auto& pCtrl = pDlg->vecAddedCtrlInfo[i];
            if (!pCtrl)
                continue;

            SAddedItemInfo addedItem;
            addedItem.eType = EAddedItemType::Control;
            addedItem.nDialogID = nDlgID;
            addedItem.nCtrlID = pCtrl->nIDC;
            addedItem.nCtrlIndex = (int)i;
            addedItem.csPrimaryText = pCtrl->csText;
            addedItem.csSecondaryText = pCtrl->csText; // 初始为主语言文本
            addedItem.bModified = false;  // 所有控件初始都未修改，需要用户手动操作
            addedItem.csFilePath = strFilePath;

            m_vecAddedItems.push_back(addedItem);

            if (m_pfnOnAddedItemFound)
            {
                m_pfnOnAddedItemFound(addedItem);
            }

            ++nAdded;
        }
    }

    // 收集字符串表中的新增字符串
    for (const auto& stPair : res.mapLangAndStringTableInfo)
    {
        const auto& pST = stPair.second;
        if (!pST)
            continue;

        for (const auto& addedStr : pST->vecAddedStrings)
        {
            SAddedItemInfo addedItem;
            addedItem.eType = EAddedItemType::String;
            addedItem.nStringID = addedStr.nStringID;
            addedItem.csPrimaryText = addedStr.csText;
            addedItem.csSecondaryText = addedStr.csText;
            addedItem.bModified = addedStr.bModified;
            addedItem.csFilePath = strFilePath;

            m_vecAddedItems.push_back(addedItem);

            if (m_pfnOnAddedItemFound)
            {
                m_pfnOnAddedItemFound(addedItem);
            }

            ++nAdded;
        }
    }

    // 收集菜单中的新增菜单项
    for (const auto& menuPair : res.mapLangAndMenuInfo)
    {
        UINT nMenuID = menuPair.first;
        const auto& pMenu = menuPair.second;
        if (!pMenu)
            continue;

        // 递归收集新增菜单项
        nAdded += CollectAddedMenuItems(strFilePath, nMenuID, pMenu);
    }

    return nAdded;
}

int CRcCompareControler::CollectAddedMenuItems(const CString& strFilePath,
                                                UINT nMenuID,
                                                const std::shared_ptr<rceditor::SMenuInfo>& pMenu)
{
    int nAdded = 0;
    if (!pMenu)
        return nAdded;

    // 收集当前菜单的新增菜单项
    for (size_t i = 0; i < pMenu->vecAddedMenuItems.size(); ++i)
    {
        const auto& pMenuItem = pMenu->vecAddedMenuItems[i];
        if (!pMenuItem)
            continue;

        SAddedItemInfo addedItem;
        addedItem.eType = EAddedItemType::Menu;
        addedItem.nMenuID = pMenuItem->nID;
        addedItem.nMenuItemIndex = (int)i;
        addedItem.csPrimaryText = pMenuItem->csText;
        addedItem.csSecondaryText = pMenuItem->csText;
        addedItem.bModified = false;
        addedItem.csFilePath = strFilePath;

        m_vecAddedItems.push_back(addedItem);

        if (m_pfnOnAddedItemFound)
        {
            m_pfnOnAddedItemFound(addedItem);
        }

        ++nAdded;
    }

    // 递归处理子菜单
    for (const auto& pSubMenu : pMenu->vecSubMenu)
    {
        nAdded += CollectAddedMenuItems(strFilePath, nMenuID, pSubMenu);
    }

    return nAdded;
}

bool CRcCompareControler::UpdateMenuItemText(const std::shared_ptr<rceditor::SMenuInfo>& pMenu,
                                              UINT nMenuID,
                                              const CStringW& csText)
{
    if (!pMenu)
        return false;

    // 检查当前菜单是否是要更新的菜单项
    if (pMenu->nID == nMenuID && pMenu->m_blIsAdded)
    {
        pMenu->csText = csText;
        return true;
    }

    // 在新增菜单项列表中查找
    for (auto& pMenuItem : pMenu->vecAddedMenuItems)
    {
        if (pMenuItem && pMenuItem->nID == nMenuID)
        {
            pMenuItem->csText = csText;
            return true;
        }
    }

    // 递归搜索子菜单
    for (auto& pSubMenu : pMenu->vecSubMenu)
    {
        if (UpdateMenuItemText(pSubMenu, nMenuID, csText))
            return true;
    }

    return false;
}

int CRcCompareControler::CompareDialogControls(const CString& strFilePath,
                                                UINT nDlgID,
                                                const std::shared_ptr<rceditor::SDialogInfo>& pPrimaryDlg,
                                                const std::shared_ptr<rceditor::SDialogInfo>& pSecondaryDlg)
{
    // 此函数已废弃，使用 operator+= 和 CollectAddedItemsFromResource 替代
    return 0;
}

int CRcCompareControler::CompareStringTables(const CString& strFilePath,
                                              const std::shared_ptr<rceditor::SSingleResourceInfo>& pPrimary,
                                              const std::shared_ptr<rceditor::SSingleResourceInfo>& pSecondary)
{
    // 此函数已废弃，使用 operator+= 和 CollectAddedItemsFromResource 替代
    return 0;
}

int CRcCompareControler::CompareMenus(const CString& strFilePath,
                                       const std::shared_ptr<rceditor::SSingleResourceInfo>& pPrimary,
                                       const std::shared_ptr<rceditor::SSingleResourceInfo>& pSecondary)
{
    // 此函数已废弃，使用 operator+= 和 CollectAddedItemsFromResource 替代
    return 0;
}

int CRcCompareControler::CompareMenuItems(const CString& strFilePath,
                                           const std::shared_ptr<rceditor::SMenuInfo>& pPrimaryMenu,
                                           const std::shared_ptr<rceditor::SMenuInfo>& pSecondaryMenu)
{
    // 此函数已废弃
    return 0;
}

int CRcCompareControler::GetUnmodifiedCount() const
{
    int nCount = 0;
    for (const auto& item : m_vecAddedItems)
    {
        if (!item.bModified)
            ++nCount;
    }
    return nCount;
}

bool CRcCompareControler::IsAllModified() const
{
    for (const auto& item : m_vecAddedItems)
    {
        if (!item.bModified)
            return false;
    }
    return true;
}

void CRcCompareControler::SetItemModified(int nIndex, bool bModified)
{
    if (nIndex < 0 || nIndex >= (int)m_vecAddedItems.size())
        return;

    m_vecAddedItems[nIndex].bModified = bModified;
}

bool CRcCompareControler::MarkControlModifiedByIndex(UINT nDialogID, int nCtrlIndex, const CString& csFilePath)
{
    for (auto& item : m_vecAddedItems)
    {
        if (item.eType == EAddedItemType::Control &&
            item.nDialogID == nDialogID &&
            item.nCtrlIndex == nCtrlIndex &&
            item.csFilePath.CompareNoCase(csFilePath) == 0)
        {
            item.bModified = true;
            return true;
        }
    }
    return false;
}

bool CRcCompareControler::MarkControlModifiedByID(UINT nDialogID, UINT nCtrlID, const CString& csFilePath)
{
    for (auto& item : m_vecAddedItems)
    {
        if (item.eType == EAddedItemType::Control &&
            item.nDialogID == nDialogID &&
            item.nCtrlID == nCtrlID &&
            item.csFilePath.CompareNoCase(csFilePath) == 0)
        {
            item.bModified = true;
            return true;
        }
    }
    return false;
}

void CRcCompareControler::UpdateItemText(int nIndex, const CStringW& csText)
{
    if (nIndex < 0 || nIndex >= (int)m_vecAddedItems.size())
        return;

    auto& item = m_vecAddedItems[nIndex];
    item.csSecondaryText = csText;

    // 如果文本与主语言不同，标记为已修改
    if (csText != item.csPrimaryText)
    {
        item.bModified = true;
    }

    // 更新对应资源中的文本
    auto itEditor = m_mapSecondaryEditors.find(item.csFilePath);
    if (itEditor == m_mapSecondaryEditors.end())
        return;

    auto& res = itEditor->second->GetResourceInfo();

    switch (item.eType)
    {
    case EAddedItemType::Control:
        {
            auto itDlg = res.mapLangAndDialogInfo.find(item.nDialogID);
            if (itDlg != res.mapLangAndDialogInfo.end())
            {
                auto& vecAdded = itDlg->second->vecAddedCtrlInfo;
                if (item.nCtrlIndex >= 0 && item.nCtrlIndex < (int)vecAdded.size())
                {
                    vecAdded[item.nCtrlIndex]->csText = csText;
                }
            }
        }
        break;

    case EAddedItemType::String:
        {
            // 查找字符串所在的块
            for (auto& blockPair : res.mapLangAndStringTableInfo)
            {
                for (auto& addedStr : blockPair.second->vecAddedStrings)
                {
                    if (addedStr.nStringID == item.nStringID)
                    {
                        addedStr.csText = csText;
                        addedStr.bModified = item.bModified;
                        // 同步更新 mapIDAndString（导出 RC 时读取此表）
                        blockPair.second->mapIDAndString[item.nStringID] = csText;
                        break;
                    }
                }
            }
        }
        break;

    case EAddedItemType::Menu:
    {
        // 递归查找并更新菜单项文本
        for (auto& menuPair : res.mapLangAndMenuInfo)
        {
            if (UpdateMenuItemText(menuPair.second, item.nMenuID, csText))
                break;
        }
    }
    break;

    default:
        break;
    }
}

void CRcCompareControler::UpdateItemProperties(UINT nDialogID, int nCtrlIndex, const CString& csFilePath,
                                                const CRect& rect, DWORD dwStyle, DWORD dwExStyle, bool blMultiLine)
{
    auto itEditor = m_mapSecondaryEditors.find(csFilePath);
    if (itEditor == m_mapSecondaryEditors.end())
        return;

    auto& res = itEditor->second->GetResourceInfo();
    auto itDlg = res.mapLangAndDialogInfo.find(nDialogID);
    if (itDlg == res.mapLangAndDialogInfo.end() || !itDlg->second)
        return;

    auto& vecAdded = itDlg->second->vecAddedCtrlInfo;
    if (nCtrlIndex < 0 || nCtrlIndex >= (int)vecAdded.size() || !vecAdded[nCtrlIndex])
        return;

    vecAdded[nCtrlIndex]->ctrlRect = rect;
    vecAdded[nCtrlIndex]->dwStyle = dwStyle;
    vecAdded[nCtrlIndex]->dwExStyle = dwExStyle;
    vecAdded[nCtrlIndex]->blMultiLineEditCtrl = blMultiLine;
}

void CRcCompareControler::UpdateDialogRect(UINT nDialogID, const CString& csFilePath, const CRect& rect)
{
    auto itEditor = m_mapSecondaryEditors.find(csFilePath);
    if (itEditor == m_mapSecondaryEditors.end())
        return;

    auto& res = itEditor->second->GetResourceInfo();
    auto itDlg = res.mapLangAndDialogInfo.find(nDialogID);
    if (itDlg == res.mapLangAndDialogInfo.end() || !itDlg->second)
        return;

    itDlg->second->rectDlg = rect;
}

const SAddedItemInfo* CRcCompareControler::GetAddedItem(int nIndex) const
{
    if (nIndex < 0 || nIndex >= (int)m_vecAddedItems.size())
        return nullptr;
    return &m_vecAddedItems[nIndex];
}

int CRcCompareControler::GetNextUnmodifiedIndex(int nStartIndex) const
{
    int nStart = (nStartIndex < 0) ? 0 : nStartIndex + 1;
    for (int i = nStart; i < (int)m_vecAddedItems.size(); ++i)
    {
        if (!m_vecAddedItems[i].bModified)
            return i;
    }
    return -1;
}

std::shared_ptr<rceditor::SSingleResourceInfo> CRcCompareControler::GetSecondaryResource(const CString& strFilePath) const
{
    auto it = m_mapSecondaryEditors.find(strFilePath);
    if (it == m_mapSecondaryEditors.end())
        return nullptr;

    return std::make_shared<rceditor::SSingleResourceInfo>(
        rceditor::DeepCloneResourceInfo(it->second->GetResourceInfo()));
}

std::shared_ptr<rceditor::PEFileEditor> CRcCompareControler::GetSecondaryEditor(const CString& strFilePath) const
{
    auto it = m_mapSecondaryEditors.find(strFilePath);
    if (it == m_mapSecondaryEditors.end())
        return nullptr;
    return it->second;
}

void CRcCompareControler::GetSecondaryFilePaths(std::vector<CString>& vecPaths) const
{
    vecPaths.clear();
    for (const auto& pair : m_mapSecondaryEditors)
    {
        vecPaths.push_back(pair.first);
    }
}

bool CRcCompareControler::SaveSecondaryResource(const CString& strFilePath)
{
    if (strFilePath.IsEmpty())
    {
        // 保存所有
        bool bAllSuccess = true;
        for (auto& pair : m_mapSecondaryEditors)
        {
            if (!pair.second->UpDatePEInfo())
                bAllSuccess = false;
        }
        return bAllSuccess;
    }
    else
    {
        auto it = m_mapSecondaryEditors.find(strFilePath);
        if (it == m_mapSecondaryEditors.end())
            return false;
        return it->second->UpDatePEInfo();
    }
}

bool CRcCompareControler::SaveSecondaryResourceAs(const CString& strSourcePath, const CString& strTargetPath)
{
    auto it = m_mapSecondaryEditors.find(strSourcePath);
    if (it == m_mapSecondaryEditors.end())
        return false;
    return it->second->UpDatePEInfoAs(strTargetPath);
}

CStringW CRcCompareControler::ExportAddedItemsToRcText(const CString& strFilePath) const
{
    CStringW csResult;
    csResult += L"// ========================================\r\n";
    csResult += L"// 新增资源项 - RC格式导出\r\n";
    csResult += L"// ========================================\r\n\r\n";

    // 按类型分组输出
    // 1. 对话框控件
    CStringW csControls;
    int nCtrlCount = 0;
    for (const auto& item : m_vecAddedItems)
    {
        if (!strFilePath.IsEmpty() && item.csFilePath != strFilePath)
            continue;

        if (item.eType == EAddedItemType::Control)
        {
            CStringW csLine;
            csLine.Format(L"    // Dialog %u, Control %u\r\n", item.nDialogID, item.nCtrlID);
            csLine += L"    LTEXT           \"";
            csLine += item.csSecondaryText;
            csLine += L"\", ";
            CStringW csID;
            csID.Format(L"%u", item.nCtrlID);
            csLine += csID;
            csLine += L", 0, 0, 50, 10\r\n";
            csControls += csLine;
            ++nCtrlCount;
        }
    }
    if (nCtrlCount > 0)
    {
        csResult += L"// ---- 新增控件 ----\r\n";
        csResult += csControls;
        csResult += L"\r\n";
    }

    // 2. 字符串表
    CStringW csStrings;
    int nStrCount = 0;
    for (const auto& item : m_vecAddedItems)
    {
        if (!strFilePath.IsEmpty() && item.csFilePath != strFilePath)
            continue;

        if (item.eType == EAddedItemType::String)
        {
            CStringW csLine;
            csLine.Format(L"    %u, \"", item.nStringID);
            csLine += item.csSecondaryText;
            csLine += L"\"\r\n";
            csStrings += csLine;
            ++nStrCount;
        }
    }
    if (nStrCount > 0)
    {
        csResult += L"// ---- 新增字符串 ----\r\nSTRINGTABLE\r\nBEGIN\r\n";
        csResult += csStrings;
        csResult += L"END\r\n\r\n";
    }

    // 3. 菜单项
    CStringW csMenus;
    int nMenuCount = 0;
    for (const auto& item : m_vecAddedItems)
    {
        if (!strFilePath.IsEmpty() && item.csFilePath != strFilePath)
            continue;

        if (item.eType == EAddedItemType::Menu)
        {
            CStringW csLine;
            csLine.Format(L"    MENUITEM \"");
            csLine += item.csSecondaryText;
            csLine += L"\", ";
            CStringW csID;
            csID.Format(L"%u", item.nMenuID);
            csLine += csID;
            csLine += L"\r\n";
            csMenus += csLine;
            ++nMenuCount;
        }
    }
    if (nMenuCount > 0)
    {
        csResult += L"// ---- 新增菜单项 ----\r\n";
        csResult += csMenus;
        csResult += L"\r\n";
    }

    return csResult;
}

CStringW CRcCompareControler::ExportFullRcText(const CString& strFilePath) const
{
    auto appendResourceAsRc = [](const CString& filePath,
                                 const rceditor::SSingleResourceInfo& res,
                                 const rceditor::SResourceHeader* pHeader,
                                 CStringW& outText)
    {
        rceditor::CRcFileWriter writer;
        rceditor::SResourceHeader emptyHeader;
        const rceditor::SResourceHeader& header = pHeader ? *pHeader : emptyHeader;
        rceditor::SIndentStyle style;
        style.useTabs = false;
        style.indentWidth = 4;

        if (!outText.IsEmpty())
            outText += L"\r\n";

        CStringW filePathW(filePath);
        outText += L"// ========================================\r\n";
        outText += L"// Full RC Export\r\n";
        outText += L"// File: ";
        outText += filePathW;
        outText += L"\r\n";
        outText += L"// ========================================\r\n\r\n";

        for (const auto& dlgPair : res.mapLangAndDialogInfo)
        {
            CStringW dialogName;
            auto it = header.idToName.find(dlgPair.first);
            if (it != header.idToName.end())
                dialogName = it->second;
            else
                dialogName.Format(L"%u", dlgPair.first);

            auto lines = writer.SerializeDialog(dlgPair.second, dialogName, header, style);
            for (const auto& line : lines)
            {
                outText += line;
                outText += L"\r\n";
            }
            outText += L"\r\n";
        }

        for (const auto& stPair : res.mapLangAndStringTableInfo)
        {
            auto lines = writer.SerializeStringTable(stPair.second, header, style);
            for (const auto& line : lines)
            {
                outText += line;
                outText += L"\r\n";
            }
            outText += L"\r\n";
        }

        for (const auto& menuPair : res.mapLangAndMenuInfo)
        {
            CStringW menuName;
            auto it = header.idToName.find(menuPair.first);
            if (it != header.idToName.end())
                menuName = it->second;
            else
                menuName.Format(L"%u", menuPair.first);

            auto lines = writer.SerializeMenu(menuPair.second, menuName, header, style);
            for (const auto& line : lines)
            {
                outText += line;
                outText += L"\r\n";
            }
            outText += L"\r\n";
        }
    };

    CStringW result;
    std::set<CString> exportedPaths;

    // 项目模式：优先导出任务中的次语言资源（可覆盖未导入 rc/resource.h 场景）
    for (const auto& td : m_vecTaskData)
    {
        if (!td.bLoaded)
            continue;
        if (td.config.secondaryDllPath.IsEmpty())
            continue;

        CString filePath(td.config.secondaryDllPath);
        if (!strFilePath.IsEmpty() && filePath.CompareNoCase(strFilePath) != 0)
            continue;
        if (exportedPaths.find(filePath) != exportedPaths.end())
            continue;

        appendResourceAsRc(filePath, td.secondaryBinaryRes, &td.secondaryHeader, result);
        exportedPaths.insert(filePath);
    }

    // 单文件模式：导出已加载的次语言编辑器资源
    for (const auto& pair : m_mapSecondaryEditors)
    {
        const CString& filePath = pair.first;
        if (!strFilePath.IsEmpty() && filePath.CompareNoCase(strFilePath) != 0)
            continue;
        if (exportedPaths.find(filePath) != exportedPaths.end())
            continue;

        appendResourceAsRc(filePath, pair.second->GetResourceInfo(), nullptr, result);
        exportedPaths.insert(filePath);
    }

    if (result.IsEmpty())
    {
        result = L"// No secondary resources loaded.\r\n";
    }

    return result;
}

CString CRcCompareControler::ExtractFileName(const CString& strFilePath)
{
    int nPos = strFilePath.ReverseFind(_T('\\'));
    if (nPos == -1)
        nPos = strFilePath.ReverseFind(_T('/'));

    if (nPos >= 0)
        return strFilePath.Mid(nPos + 1);

    return strFilePath;
}

CString CRcCompareControler::ExtractFileNameWithoutExt(const CString& strFilePath)
{
    CString strFileName = ExtractFileName(strFilePath);
    int nDotPos = strFileName.ReverseFind(_T('.'));
    if (nDotPos > 0)
        return strFileName.Left(nDotPos);
    return strFileName;
}

CString CRcCompareControler::FindMatchingSecondaryFile(const CString& strPrimaryPath) const
{
    // 获取主语言文件名（不含扩展名）
    CString strPrimaryNameNoExt = ExtractFileNameWithoutExt(strPrimaryPath);
    
    // 查找次语言文件名中包含主语言文件名的文件
    // 例如：主语言 test.dll -> 次语言 JA_test.dll 或 test_JA.dll
    for (const auto& pair : m_mapSecondaryEditors)
    {
        CString strSecondaryName = ExtractFileName(pair.first);
        CString strSecondaryNameNoExt = ExtractFileNameWithoutExt(pair.first);
        
        // 检查次语言文件名是否包含主语言文件名（不区分大小写）
        CString strSecondaryUpper = strSecondaryNameNoExt;
        CString strPrimaryUpper = strPrimaryNameNoExt;
        strSecondaryUpper.MakeUpper();
        strPrimaryUpper.MakeUpper();
        
        if (strSecondaryUpper.Find(strPrimaryUpper) >= 0)
        {
            return pair.first;
        }
    }

    return CString();
}

// ============================================================================
// 批量项目 — 任务加载/合并/导出
// ============================================================================

CRcCompareControler::STaskLoadedData* CRcCompareControler::GetTaskData(int taskIndex)
{
    if (taskIndex < 0 || taskIndex >= (int)m_vecTaskData.size())
        return nullptr;
    return &m_vecTaskData[taskIndex];
}

const CRcCompareControler::STaskLoadedData* CRcCompareControler::GetTaskData(int taskIndex) const
{
    if (taskIndex < 0 || taskIndex >= (int)m_vecTaskData.size())
        return nullptr;
    return &m_vecTaskData[taskIndex];
}

bool CRcCompareControler::LoadTask(int taskIndex,
                                    const std::atomic<bool>& cancelFlag,
                                    rceditor::ProgressCallback progressCb)
{
    if (taskIndex < 0 || taskIndex >= (int)m_project.tasks.size())
        return false;

    // 确保 m_vecTaskData 已分配
    if ((int)m_vecTaskData.size() <= taskIndex)
        m_vecTaskData.resize(taskIndex + 1);

    auto& data = m_vecTaskData[taskIndex];
    data.config = m_project.tasks[taskIndex];
    data.bLoaded = false;
    data.bMerged = false;
    data.bExported = false;
    data.lastError.Empty();

    const auto& cfg = data.config;

    // === 1. 加载主语言 DLL ===
    if (cancelFlag.load()) return false;
    if (progressCb) progressCb(5);

    if (!cfg.primaryDllPath.IsEmpty())
    {
        data.primaryEditor = std::make_shared<rceditor::PEFileEditor>(CString(cfg.primaryDllPath));
        auto err = data.primaryEditor->ReadPEInfo();
        if (err != rceditor::PEFileEditor::ERR_SUCCESS)
        {
            data.lastError.Format(L"Failed to load primary DLL: %s", (LPCWSTR)cfg.primaryDllPath);
            return false;
        }
        data.primaryBinaryRes = rceditor::DeepCloneResourceInfo(data.primaryEditor->GetResourceInfo());
    }
    if (progressCb) progressCb(15);

    // === 2. 加载次语言 DLL ===
    if (cancelFlag.load()) return false;
    if (!cfg.secondaryDllPath.IsEmpty())
    {
        data.secondaryEditor = std::make_shared<rceditor::PEFileEditor>(CString(cfg.secondaryDllPath));
        auto err = data.secondaryEditor->ReadPEInfo();
        if (err != rceditor::PEFileEditor::ERR_SUCCESS)
        {
            data.lastError.Format(L"Failed to load secondary DLL: %s", (LPCWSTR)cfg.secondaryDllPath);
            return false;
        }
        data.secondaryBinaryRes = rceditor::DeepCloneResourceInfo(data.secondaryEditor->GetResourceInfo());
    }
    if (progressCb) progressCb(30);

    // === 3. 解析主语言 RC + resource.h ===
    if (cancelFlag.load()) return false;
    if (!cfg.primaryRcPath.IsEmpty() && !cfg.primaryResourceHPath.IsEmpty())
    {
        rceditor::CRcFileParser parser;
        rceditor::SSingleResourceInfo primaryRcRes;
        if (!parser.ParseAll(cfg.primaryRcPath, cfg.primaryResourceHPath,
                             data.primaryRcContent, data.primaryHeader, primaryRcRes))
        {
            data.lastError.Format(L"Failed to parse primary RC: %s", (LPCWSTR)cfg.primaryRcPath);
            return false;
        }
    }
    if (progressCb) progressCb(50);

    // === 4. 解析次语言 RC + resource.h ===
    if (cancelFlag.load()) return false;
    if (!cfg.secondaryRcPath.IsEmpty() && !cfg.secondaryResourceHPath.IsEmpty())
    {
        rceditor::CRcFileParser parser;
        rceditor::SSingleResourceInfo secondaryRcRes;
        if (!parser.ParseAll(cfg.secondaryRcPath, cfg.secondaryResourceHPath,
                             data.secondaryRcContent, data.secondaryHeader, secondaryRcRes))
        {
            data.lastError.Format(L"Failed to parse secondary RC: %s", (LPCWSTR)cfg.secondaryRcPath);
            return false;
        }
    }
    if (progressCb) progressCb(70);

    data.bLoaded = true;
    return true;
}

bool CRcCompareControler::MergeTask(int taskIndex)
{
    if (taskIndex < 0 || taskIndex >= (int)m_vecTaskData.size())
        return false;

    auto& data = m_vecTaskData[taskIndex];
    if (!data.bLoaded)
        return false;

    // === 记录次语言原始块键集（合并前），用于 AlignSecondary 跳过已有块 ===
    data.originalSecondaryBlockKeys.clear();
    for (const auto& dlgPair : data.secondaryBinaryRes.mapLangAndDialogInfo)
        data.originalSecondaryBlockKeys.insert({ (int)rceditor::ERcBlockType::Dialog, dlgPair.first });
    for (const auto& menuPair : data.secondaryBinaryRes.mapLangAndMenuInfo)
        data.originalSecondaryBlockKeys.insert({ (int)rceditor::ERcBlockType::Menu, menuPair.first });

    // === 二进制级合并 (operator+=) ===
    data.secondaryBinaryRes += data.primaryBinaryRes;

    // === resource.h 处理：直接使用主语言的 resource.h ===
    // 因为 resource.h 只包含宏定义，主语言作为基准已包含所有需要的定义
    bool hasPrimaryHeader = !data.primaryHeader.rawLines.empty();
    if (hasPrimaryHeader)
    {
        // 保留次语言 resource.h 的原始编码和路径元信息
        rceditor::EFileEncoding secondaryEncoding = data.secondaryHeader.encoding;
        CStringW secondaryPath = data.secondaryHeader.filePath;
        CStringW secondaryLineEnding = data.secondaryHeader.lineEnding;

        // 直接使用主语言 resource.h 的内容
        data.secondaryHeader = data.primaryHeader;

        // 恢复次语言的文件元信息（保证导出时使用正确的编码和路径）
        if (secondaryEncoding != rceditor::EFileEncoding::Unknown)
            data.secondaryHeader.encoding = secondaryEncoding;
        if (!secondaryPath.IsEmpty())
            data.secondaryHeader.filePath = secondaryPath;
        if (!secondaryLineEnding.IsEmpty())
            data.secondaryHeader.lineEnding = secondaryLineEnding;

        // 更新 _APS_NEXT_* 宏值以反映合并后的 ID 使用情况
        data.secondaryHeader.UpdateApsNextValues();
    }

    // === RC 文本级合并 ===
    bool primaryHasRc  = !data.primaryRcContent.lines.empty();
    bool secondaryHasRc = !data.secondaryRcContent.lines.empty();

    if (primaryHasRc && secondaryHasRc)
    {
        // 双方都有 RC → 先走文本级合并（注入缺失项），再对齐结构
        rceditor::CRcFileMerger merger;
        data.mergeResult = merger.MergeAll(
            data.primaryRcContent, data.primaryHeader, data.primaryBinaryRes,
            data.secondaryRcContent, data.secondaryHeader, data.secondaryBinaryRes);

        // 以主语言 RC 结构为基准，校正次语言中不一致的控件定义
        AlignSecondaryRcWithPrimary(data);

        // 校验并修复 Dialog 块结构（缺失IDD头行等）
        ValidateDialogBlockStructure(data);

        // 修复 DESIGNINFO 段落的 BEGIN/END 平衡（内部条目可能缺少 END）
        RepairDesignInfoSections(data.secondaryRcContent.lines);

        // 通用 BEGIN/END 平衡修复（DLGINIT、VERSIONINFO 等非标准块）
        RepairAllBeginEndBalance(data.secondaryRcContent.lines);

        // 最终安全网：修复 DIALOG/MENU 块中的 premature END 和重复控件行
        RemovePrematureEndsInDialogBlocks(data.secondaryRcContent.lines);
    }
    else if (primaryHasRc && !secondaryHasRc)
    {
        // 次语言缺少 .rc 文件 → 以主语言 RC 为模板自动生成
        GenerateSecondaryRcFromTemplate(data);

        // 校验并修复 Dialog 块结构
        ValidateDialogBlockStructure(data);

        // 修复 DESIGNINFO 段落的 BEGIN/END 平衡
        RepairDesignInfoSections(data.secondaryRcContent.lines);

        // 通用 BEGIN/END 平衡修复
        RepairAllBeginEndBalance(data.secondaryRcContent.lines);

        // 最终安全网：修复 DIALOG/MENU 块中的 premature END 和重复控件行
        RemovePrematureEndsInDialogBlocks(data.secondaryRcContent.lines);
    }

    // === 收集新增项 ===
    CString filePath(data.config.secondaryDllPath);
    CollectAddedItemsFromResource(filePath, data.secondaryBinaryRes);

    // 更新次语言编辑器的资源数据
    if (data.secondaryEditor)
    {
        data.secondaryEditor->SetResourceInfo(data.secondaryBinaryRes);

        // 注册到编辑器映射表，确保 UpdateItemText 能在任务模式下
        // 正确找到编辑器并将用户文本编辑写入二进制资源
        if (!data.config.secondaryDllPath.IsEmpty())
            m_mapSecondaryEditors[CString(data.config.secondaryDllPath)] = data.secondaryEditor;
    }

    data.bMerged = true;
    return true;
}

bool CRcCompareControler::ValidateBeforeExport(int taskIndex, std::vector<CStringW>& outWarnings)
{
    outWarnings.clear();

    if (taskIndex < 0 || taskIndex >= (int)m_vecTaskData.size())
        return false;

    const auto& data = m_vecTaskData[taskIndex];
    if (!data.bMerged)
    {
        outWarnings.push_back(L"Task not yet merged.");
        return false;
    }

    const auto& res = data.secondaryBinaryRes;
    const auto& header = data.secondaryHeader;

    // 1. 检查所有新增控件的边界
    for (const auto& dlgPair : res.mapLangAndDialogInfo)
    {
        const auto& pDlg = dlgPair.second;
        if (!pDlg)
            continue;

        for (const auto& pCtrl : pDlg->vecAddedCtrlInfo)
        {
            if (!pCtrl)
                continue;

            // 查找控件在 vecCtrlInfo 中的索引
            int ctrlIndex = -1;
            for (int i = 0; i < (int)pDlg->vecCtrlInfo.size(); ++i)
            {
                if (pDlg->vecCtrlInfo[i] == pCtrl)
                {
                    ctrlIndex = i;
                    break;
                }
            }

            auto validation = rceditor::CControlAutoLayout::ValidateControlPosition(
                pCtrl->ctrlRect, pDlg->rectDlg, pDlg->vecCtrlInfo, ctrlIndex);

            if (validation.bOutOfBounds)
            {
                CStringW msg;
                msg.Format(L"Dialog %u: Control IDC=%u exceeds dialog bounds",
                           dlgPair.first, pCtrl->nIDC);
                outWarnings.push_back(msg);
            }
        }
    }

    // 2. 检查 resource.h 中是否有宏冲突
    for (const auto& conflict : data.mergeResult.conflicts)
    {
        CStringW msg;
        msg.Format(L"Conflict: %s (primary=%u vs secondary=%u) - %s",
                   (LPCWSTR)conflict.primaryName,
                   conflict.primaryValue,
                   conflict.secondaryValue,
                   (LPCWSTR)conflict.description);
        outWarnings.push_back(msg);
    }

    return true; // 警告不阻止导出
}

bool CRcCompareControler::ExportTask(int taskIndex)
{
    if (taskIndex < 0 || taskIndex >= (int)m_vecTaskData.size())
        return false;

    auto& data = m_vecTaskData[taskIndex];
    if (!data.bMerged)
        return false;

    const auto& cfg = data.config;

    auto getFileNameWithoutExt = [](const CStringW& path) -> CStringW
    {
        int slashPos = path.ReverseFind(L'\\');
        if (slashPos < 0)
            slashPos = path.ReverseFind(L'/');
        CStringW fileName = (slashPos >= 0) ? path.Mid(slashPos + 1) : path;
        int dotPos = fileName.ReverseFind(L'.');
        if (dotPos > 0)
            fileName = fileName.Left(dotPos);
        return fileName;
    };

    auto getExtension = [](const CStringW& path) -> CStringW
    {
        int dotPos = path.ReverseFind(L'.');
        int slashPos = path.ReverseFind(L'\\');
        if (slashPos < 0)
            slashPos = path.ReverseFind(L'/');
        if (dotPos > slashPos)
            return path.Mid(dotPos);
        return L"";
    };

    auto joinPath = [](const CStringW& dir, const CStringW& name) -> CStringW
    {
        if (dir.IsEmpty()) return name;
        if (dir.Right(1) == L"\\" || dir.Right(1) == L"/")
            return dir + name;
        return dir + L"\\" + name;
    };

    CStringW outputDir = cfg.outputDir;
    if (outputDir.IsEmpty() && !cfg.outputDllPath.IsEmpty())
    {
        int slashPos = cfg.outputDllPath.ReverseFind(L'\\');
        if (slashPos < 0)
            slashPos = cfg.outputDllPath.ReverseFind(L'/');
        if (slashPos >= 0)
            outputDir = cfg.outputDllPath.Left(slashPos);
    }

    if (outputDir.IsEmpty())
    {
        CStringW secondaryBase = getFileNameWithoutExt(cfg.secondaryDllPath);
        if (!secondaryBase.IsEmpty())
        {
            int slashPos = cfg.secondaryDllPath.ReverseFind(L'\\');
            if (slashPos < 0)
                slashPos = cfg.secondaryDllPath.ReverseFind(L'/');
            CStringW parentDir = (slashPos >= 0) ? cfg.secondaryDllPath.Left(slashPos) : CStringW();
            outputDir = joinPath(parentDir, secondaryBase + L"_merged");
        }
    }

    if (outputDir.IsEmpty())
    {
        data.lastError = L"Output directory is empty.";
        return false;
    }

    if (!CreateDirectoryW(outputDir, nullptr))
    {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS)
        {
            data.lastError.Format(L"Failed to create output directory: %s", (LPCWSTR)outputDir);
            return false;
        }
    }

    CStringW binaryBaseName = getFileNameWithoutExt(cfg.secondaryDllPath);
    if (binaryBaseName.IsEmpty())
        binaryBaseName = L"output";
    CStringW binaryExt = getExtension(cfg.secondaryDllPath);
    if (binaryExt.IsEmpty())
        binaryExt = L".dll";

    CStringW outputBinaryPath = joinPath(outputDir, binaryBaseName + L"_merged" + binaryExt);

    CStringW outputRcPath;
    if (!cfg.secondaryRcPath.IsEmpty())
    {
        CStringW rcBase = getFileNameWithoutExt(cfg.secondaryRcPath);
        if (rcBase.IsEmpty()) rcBase = binaryBaseName;
        outputRcPath = joinPath(outputDir, rcBase + L"_merged.rc");
    }
    else if (!data.secondaryRcContent.lines.empty())
    {
        // 次语言无 RC 路径但已从模板生成 → 从主语言 RC 或二进制名推导
        CStringW rcBase;
        if (!cfg.primaryRcPath.IsEmpty())
            rcBase = getFileNameWithoutExt(cfg.primaryRcPath);
        if (rcBase.IsEmpty())
            rcBase = binaryBaseName;
        outputRcPath = joinPath(outputDir, rcBase + L"_merged.rc");
    }

    CStringW outputResourceHPath;
    if (!cfg.secondaryResourceHPath.IsEmpty())
    {
        CStringW hBase = getFileNameWithoutExt(cfg.secondaryResourceHPath);
        if (hBase.IsEmpty()) hBase = binaryBaseName;
        outputResourceHPath = joinPath(outputDir, hBase + L"_merged.h");
    }
    else if (!data.secondaryHeader.rawLines.empty())
    {
        // 次语言无 resource.h 路径但已从主语言复制 → 从主语言路径或二进制名推导
        CStringW hBase;
        if (!cfg.primaryResourceHPath.IsEmpty())
            hBase = getFileNameWithoutExt(cfg.primaryResourceHPath);
        if (hBase.IsEmpty())
            hBase = binaryBaseName;
        outputResourceHPath = joinPath(outputDir, hBase + L"_merged.h");
    }

    // RC 未加载时禁止导出 RC，避免写出空/不完整文件
    if (!outputRcPath.IsEmpty() && data.secondaryRcContent.lines.empty())
    {
        data.lastError = L"RC content not loaded. Please configure and load secondary RC/resource.h before exporting RC.";
        return false;
    }

    // === 导出前：确保 _APS_NEXT_* 宏值是最新的 ===
    if (!data.secondaryHeader.rawLines.empty())
    {
        data.secondaryHeader.UpdateApsNextValues();
    }

    // === 导出前：从编辑器同步最新的二进制资源，然后刷新 RC 文本 ===
    // UpdateItemText() 只更新 secondaryEditor 中的资源，
    // 而 data.secondaryBinaryRes / secondaryRcContent 是合并阶段的快照，
    // 不含用户的后续编辑。导出前必须刷新。
    if (data.secondaryEditor)
    {
        // 1. 二进制资源快照 ← 编辑器当前状态
        data.secondaryBinaryRes = rceditor::DeepCloneResourceInfo(
            data.secondaryEditor->GetResourceInfo());

        // 2. RC 文本刷新：将最新的二进制文本反映到 RC 行中
        if (!data.secondaryRcContent.lines.empty())
        {
            auto& secLines  = data.secondaryRcContent.lines;
            const auto& header = data.secondaryHeader;

            // 重新解析块结构（合并/对齐操作可能导致块索引过时）
            {
                data.secondaryRcContent.blocks.clear();
                data.secondaryRcContent.nameToBlockIndex.clear();
                data.secondaryRcContent.idToBlockIndex.clear();
                rceditor::CRcFileParser reparser;
                reparser.IdentifyBlocks(data.secondaryRcContent);
                for (auto& block : data.secondaryRcContent.blocks)
                {
                    if (!block.resourceName.IsEmpty())
                    {
                        auto itId = header.nameToId.find(block.resourceName);
                        if (itId != header.nameToId.end())
                        {
                            block.resourceID = itId->second;
                            data.secondaryRcContent.idToBlockIndex[block.resourceID] = block.blockIndex;
                        }
                    }
                }
            }

            // 构建字符串查找表
            std::map<UINT, CStringW> secondaryStringMap;
            for (const auto& stPair : data.secondaryBinaryRes.mapLangAndStringTableInfo)
            {
                if (!stPair.second) continue;
                for (const auto& entry : stPair.second->mapIDAndString)
                    secondaryStringMap[entry.first] = entry.second;
            }

            // 对每个块做文本替换
            for (const auto& block : data.secondaryRcContent.blocks)
            {
                if (block.type == rceditor::ERcBlockType::Dialog)
                {
                    if (IsDesignInfoEntry(secLines, block))
                        continue;

                    auto blockKey = std::make_pair((int)block.type, block.resourceID);
                    bool isExistingBlock = data.originalSecondaryBlockKeys.count(blockKey) > 0;

                    if (isExistingBlock)
                    {
                        // 合并前已存在的块：不做全量替换，仅对新增控件的行做定点文本+尺寸替换。
                        // 原始次语言控件行保持原样（文本和尺寸不变）。
                        auto itDlg = data.secondaryBinaryRes.mapLangAndDialogInfo.find(block.resourceID);
                        if (itDlg != data.secondaryBinaryRes.mapLangAndDialogInfo.end() && itDlg->second)
                        {
                            // 构建新增控件的 IDC → {text, rect} 映射
                            std::map<UINT, std::pair<CStringW, CRect>> addedCtrlMap;
                            for (const auto& addedCtrl : itDlg->second->vecAddedCtrlInfo)
                            {
                                if (!addedCtrl) continue;
                                addedCtrlMap[addedCtrl->nIDC] = { addedCtrl->csText, addedCtrl->ctrlRect };
                            }
                            // 定点替换：遍历块内控件行，仅替换新增控件的文本+尺寸
                            bool inBlock = false;
                            for (int li = block.headerLineIndex; li <= block.endLineIndex && li < (int)secLines.size(); ++li)
                            {
                                auto& line = secLines[li];
                                CStringW trimmed = line.text; trimmed.TrimLeft(); trimmed.TrimRight();
                                if (trimmed.IsEmpty()) continue;
                                CStringW upper = trimmed; upper.MakeUpper();
                                if (!inBlock)
                                {
                                    if (upper == L"BEGIN" || upper == L"{") inBlock = true;
                                    continue;
                                }
                                if (upper == L"END" || upper == L"}") break;
                                if (line.type != rceditor::ERcLineType::Content) continue;

                                int idc = ParseControlIDCFromLine(trimmed, header);
                                if (idc <= 0) continue; // 跳过 STATIC(IDC=-1) 和解析失败
                                auto itAdded = addedCtrlMap.find((UINT)idc);
                                if (itAdded == addedCtrlMap.end()) continue; // 非新增控件，不替换

                                // 替换文本
                                if (line.text.Find(L'"') >= 0)
                                    line.text = ReplaceFirstQuotedString(line.text, itAdded->second.first);
                                // 替换尺寸
                                ReplaceControlDimensions(line.text, itAdded->second.second);
                            }
                        }
                    }
                    else
                    {
                        // 新块（合并时从主语言复制的）：全量替换文本+尺寸
                        SDialogTextLookup lookup = BuildDialogTextLookup(
                            block.resourceID, data.primaryBinaryRes, data.secondaryBinaryRes);
                        int bStart = block.beginLineIndex;
                        int bEnd   = block.endLineIndex;
                        if (bStart >= 0 && bEnd >= bStart && bEnd < (int)secLines.size())
                            ReplaceDialogBlockTexts(secLines, block.headerLineIndex, bEnd, lookup, header);
                    }
                }
                else if (block.type == rceditor::ERcBlockType::Menu)
                {
                    if (IsDesignInfoEntry(secLines, block))
                        continue;
                    auto itMenu = data.secondaryBinaryRes.mapLangAndMenuInfo.find(block.resourceID);
                    if (itMenu != data.secondaryBinaryRes.mapLangAndMenuInfo.end() && itMenu->second)
                    {
                        SMenuTextLookup menuLookup;
                        CollectMenuTextsRecursive(itMenu->second, menuLookup);
                        int innerStart = block.beginLineIndex + 1;
                        int innerEnd   = block.endLineIndex - 1;
                        if (innerStart > 0 && innerEnd >= innerStart && innerEnd < (int)secLines.size())
                            ReplaceMenuBlockTexts(secLines, innerStart, innerEnd, menuLookup, header);
                    }
                }
                else if (block.type == rceditor::ERcBlockType::StringTable)
                {
                    int stStart = block.beginLineIndex + 1;
                    int stEnd   = block.endLineIndex - 1;
                    if (stStart > 0 && stEnd >= stStart && stEnd < (int)secLines.size())
                        ReplaceStringTableBlockTexts(secLines, stStart, stEnd, secondaryStringMap, header);
                }
            }
        }
    }

    // ========================================
    // 回滚机制：记录已输出的文件路径，失败时清理
    // ========================================
    std::vector<CStringW> exportedFiles;

    auto rollback = [&exportedFiles]()
    {
        for (const auto& filePath : exportedFiles)
        {
            DeleteFileW(filePath.GetString());
        }
        exportedFiles.clear();
    };

    // === 1. 导出 resource.h ===
    if (!outputResourceHPath.IsEmpty())
    {
        rceditor::CRcFileWriter writer;
        if (!writer.WriteResourceHeader(data.secondaryHeader, outputResourceHPath))
        {
            data.lastError.Format(L"Failed to write resource.h: %s", (LPCWSTR)outputResourceHPath);
            rollback();
            return false;
        }
        exportedFiles.push_back(outputResourceHPath);
    }

    // === 2. 导出 .rc 文件 ===
    if (!outputRcPath.IsEmpty())
    {
        // --- 诊断：导出前转储每个 Dialog 块的头部行（DIALOGEX ~ BEGIN）---
        {
            CStringW diagPath = outputRcPath + L"_diag.txt";
            FILE* diagFp = nullptr;
            _wfopen_s(&diagFp, diagPath.GetString(), L"wb");
            if (diagFp)
            {
                auto writeUtf8 = [&](const CStringW& s) {
                    int len = WideCharToMultiByte(CP_UTF8, 0, s.GetString(), s.GetLength(), nullptr, 0, nullptr, nullptr);
                    if (len > 0)
                    {
                        std::vector<char> buf(len);
                        WideCharToMultiByte(CP_UTF8, 0, s.GetString(), s.GetLength(), buf.data(), len, nullptr, nullptr);
                        fwrite(buf.data(), 1, len, diagFp);
                    }
                    fwrite("\r\n", 1, 2, diagFp);
                };
                // BOM
                const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
                fwrite(bom, 1, 3, diagFp);

                writeUtf8(L"=== ExportTask RC Diagnostic ===");
                CStringW totalLines;
                totalLines.Format(L"Total lines in secondaryRcContent: %d", (int)data.secondaryRcContent.lines.size());
                writeUtf8(totalLines);
                CStringW totalBlocks;
                totalBlocks.Format(L"Total blocks: %d", (int)data.secondaryRcContent.blocks.size());
                writeUtf8(totalBlocks);
                writeUtf8(L"");

                for (const auto& block : data.secondaryRcContent.blocks)
                {
                    if (block.type != rceditor::ERcBlockType::Dialog)
                        continue;
                    CStringW info;
                    info.Format(L"--- Dialog: name=%s  id=%u  header=%d  begin=%d  end=%d  attrCount=%d ---",
                        (LPCWSTR)block.resourceName, block.resourceID,
                        block.headerLineIndex, block.beginLineIndex, block.endLineIndex,
                        (int)block.attributeLineIndices.size());
                    writeUtf8(info);

                    auto blockKey = std::make_pair((int)block.type, block.resourceID);
                    bool isExisting = data.originalSecondaryBlockKeys.count(blockKey) > 0;
                    writeUtf8(isExisting ? L"  [EXISTING block]" : L"  [NEW block]");

                    // 输出从 headerLineIndex 到 beginLineIndex+1 的行（属性区域）
                    int dumpEnd = (block.beginLineIndex >= 0) ? block.beginLineIndex + 1 : block.headerLineIndex + 8;
                    if (dumpEnd >= (int)data.secondaryRcContent.lines.size())
                        dumpEnd = (int)data.secondaryRcContent.lines.size() - 1;
                    for (int li = block.headerLineIndex; li <= dumpEnd && li < (int)data.secondaryRcContent.lines.size(); ++li)
                    {
                        CStringW lineInfo;
                        lineInfo.Format(L"  L%04d [type=%d]: %s",
                            li, (int)data.secondaryRcContent.lines[li].type,
                            (LPCWSTR)data.secondaryRcContent.lines[li].text);
                        writeUtf8(lineInfo);
                    }
                    writeUtf8(L"");
                }
                fclose(diagFp);
            }
        }

        rceditor::CRcFileWriter writer;
        if (!writer.WriteRcFile(data.secondaryRcContent, outputRcPath))
        {
            data.lastError.Format(L"Failed to write RC: %s", (LPCWSTR)outputRcPath);
            rollback();
            return false;
        }
        exportedFiles.push_back(outputRcPath);
    }

    // === 3. 导出二进制（DLL/EXE）===
    if (!outputBinaryPath.IsEmpty() && data.secondaryEditor)
    {
        CString srcDll(cfg.secondaryDllPath);
        CString dstDll(outputBinaryPath);

        // 先复制原始二进制到目标路径
        if (!CopyFileW(srcDll, dstDll, FALSE))
        {
            data.lastError.Format(L"Failed to copy binary to: %s", (LPCWSTR)outputBinaryPath);
            rollback();
            return false;
        }
        exportedFiles.push_back(outputBinaryPath);

        // 在副本上更新资源
        auto resDataVec = data.secondaryBinaryRes.Convert2ResData();
        HANDLE hUpdate = BeginUpdateResourceW(dstDll, FALSE);
        if (!hUpdate)
        {
            data.lastError.Format(L"BeginUpdateResource failed: %s", (LPCWSTR)outputBinaryPath);
            rollback();
            return false;
        }

        for (const auto& rd : resDataVec)
        {
            if (!UpdateResourceW(hUpdate, rd.resType,
                                 MAKEINTRESOURCE(rd.nResID),
                                 rd.langID,
                                 (LPVOID)rd.vecData.data(),
                                 (DWORD)rd.vecData.size()))
            {
                EndUpdateResourceW(hUpdate, TRUE); // discard
                data.lastError.Format(L"UpdateResource failed for ID %u", rd.nResID);
                rollback();
                return false;
            }
        }

        if (!EndUpdateResourceW(hUpdate, FALSE))
        {
            data.lastError.Format(L"EndUpdateResource failed: %s", (LPCWSTR)outputBinaryPath);
            rollback();
            return false;
        }
    }

    // === 4. 导出对照表 ===
    {
        CStringW compTable = GenerateComparisonTable(data);
        if (!compTable.IsEmpty())
        {
            CStringW compPath = joinPath(outputDir, binaryBaseName + L"_comparison.xls");
            FILE* fp = nullptr;
            _wfopen_s(&fp, compPath.GetString(), L"wb");
            if (fp)
            {
                // UTF-8 BOM
                const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
                fwrite(bom, 1, 3, fp);
                // 转 UTF-8
                int len = WideCharToMultiByte(CP_UTF8, 0, compTable.GetString(), compTable.GetLength(), nullptr, 0, nullptr, nullptr);
                if (len > 0)
                {
                    std::vector<char> buf(len);
                    WideCharToMultiByte(CP_UTF8, 0, compTable.GetString(), compTable.GetLength(), buf.data(), len, nullptr, nullptr);
                    fwrite(buf.data(), 1, len, fp);
                }
                fclose(fp);
                exportedFiles.push_back(compPath);
            }
        }
    }

    data.bExported = true;
    return true;
}

// ============================================================================
// 导出后校验：修复 Dialog 块的结构缺陷
// Bug1: 缺失 IDD_xxx DIALOGEX/DIALOG 头行 → 从块元数据和二进制资源重建
// Bug2: 控件定义中间多出 END → 检测并删除多余的 END 行
// ============================================================================

void CRcCompareControler::ValidateDialogBlockStructure(STaskLoadedData& data)
{
    auto& lines  = data.secondaryRcContent.lines;

    // ---- Step 0: 重新解析块结构，获取全新的块元数据 ----
    // 合并/对齐操作可能导致块索引过时；重新解析保证所有索引准确。
    {
        data.secondaryRcContent.blocks.clear();
        data.secondaryRcContent.nameToBlockIndex.clear();
        data.secondaryRcContent.idToBlockIndex.clear();
        rceditor::CRcFileParser reparser;
        reparser.IdentifyBlocks(data.secondaryRcContent);

        // 解析资源ID（IdentifyBlocks 只设置 resourceName，不解析 resourceID）
        for (auto& block : data.secondaryRcContent.blocks)
        {
            if (!block.resourceName.IsEmpty())
            {
                auto itId = data.secondaryHeader.nameToId.find(block.resourceName);
                if (itId != data.secondaryHeader.nameToId.end())
                {
                    block.resourceID = itId->second;
                    data.secondaryRcContent.idToBlockIndex[block.resourceID] = block.blockIndex;
                }
            }
        }
    }

    auto& blocks = data.secondaryRcContent.blocks;

    for (auto& block : blocks)
    {
        if (block.type != rceditor::ERcBlockType::Dialog)
            continue;
        if (block.headerLineIndex < 0 || block.endLineIndex < 0)
            continue;
        if (block.headerLineIndex >= (int)lines.size() || block.endLineIndex >= (int)lines.size())
            continue;
        if (IsDesignInfoEntry(lines, block))
            continue;

        // --- Bug1 检查：头行是否包含 DIALOGEX/DIALOG 关键词 ---
        {
            CStringW headerTrimmed = lines[block.headerLineIndex].text;
            headerTrimmed.TrimLeft(); headerTrimmed.TrimRight();
            CStringW headerUpper = headerTrimmed; headerUpper.MakeUpper();

            static const wchar_t* dialogKeywords[] = { L"DIALOGEX", L"DIALOG", nullptr };
            bool hasDialogKeyword = IsResourceHeaderByToken(
                lines[block.headerLineIndex].text, dialogKeywords);

            if (!hasDialogKeyword)
            {
                // 头行不含 DIALOG 关键词 → 尝试从元数据重建
                CStringW resName = block.resourceName;
                if (resName.IsEmpty())
                {
                    auto it = data.secondaryHeader.idToName.find(block.resourceID);
                    if (it != data.secondaryHeader.idToName.end())
                        resName = it->second;
                    else
                        resName.Format(L"%u", block.resourceID);
                }

                CStringW keyword = block.resourceTypeKeyword;
                if (keyword.IsEmpty())
                    keyword = L"DIALOGEX";

                // 从二进制资源获取对话框尺寸
                CStringW headerLine;
                auto itDlg = data.secondaryBinaryRes.mapLangAndDialogInfo.find(block.resourceID);
                if (itDlg != data.secondaryBinaryRes.mapLangAndDialogInfo.end() && itDlg->second)
                {
                    const auto& pDlg = itDlg->second;
                    headerLine.Format(L"%s %s %d, %d, %d, %d",
                                      (LPCWSTR)resName, (LPCWSTR)keyword,
                                      pDlg->rectDlg.left, pDlg->rectDlg.top,
                                      pDlg->rectDlg.Width(), pDlg->rectDlg.Height());
                }
                else
                {
                    headerLine.Format(L"%s %s 0, 0, 100, 100",
                                      (LPCWSTR)resName, (LPCWSTR)keyword);
                }

                // 在当前头行之前插入重建的头行
                rceditor::SRcLine newLine;
                newLine.text = headerLine;
                newLine.lineNumber = block.headerLineIndex + 1;
                newLine.blockIndex = block.blockIndex;
                newLine.type = rceditor::ERcLineType::ResourceHeader;

                lines.insert(lines.begin() + block.headerLineIndex, newLine);

                // 更新当前块和所有后续块的行索引
                block.beginLineIndex++;
                block.endLineIndex++;
                for (auto& ai : block.attributeLineIndices) ai++;

                for (auto& blk2 : blocks)
                {
                    if (&blk2 == &block) continue;
                    if (blk2.headerLineIndex >= block.headerLineIndex)
                    {
                        blk2.headerLineIndex++;
                        blk2.beginLineIndex++;
                        blk2.endLineIndex++;
                        for (auto& ai : blk2.attributeLineIndices) ai++;
                    }
                }
            }
        }

    }

    // 重编行号
    for (int i = 0; i < (int)lines.size(); ++i)
        lines[i].lineNumber = i + 1;
}

// ============================================================================
// 以主语言 RC 为模板，生成次语言 RC（次语言缺少 .rc 文件时使用）
// 策略：深拷贝主语言 RC，仅替换引号内的文本字符串；完整保留控件类型/样式/位置
// ============================================================================

bool CRcCompareControler::GenerateSecondaryRcFromTemplate(STaskLoadedData& data)
{
    if (data.primaryRcContent.lines.empty())
        return false;

    // --- 第 1 步：深拷贝主语言 RC 作为模板 ---
    data.secondaryRcContent = data.primaryRcContent;
    data.secondaryRcContent.filePath.Empty();

    const rceditor::SResourceHeader& header = data.secondaryHeader;

    // 构建次语言二进制中所有字符串的快速查找表：strId → text
    std::map<UINT, CStringW> secondaryStringMap;
    for (const auto& stPair : data.secondaryBinaryRes.mapLangAndStringTableInfo)
    {
        if (!stPair.second) continue;
        for (const auto& entry : stPair.second->mapIDAndString)
            secondaryStringMap[entry.first] = entry.second;
    }

    auto& lines  = data.secondaryRcContent.lines;
    const auto& blocks = data.secondaryRcContent.blocks;

    // --- 第 2 步：遍历每个资源块，仅替换块内引号文本（不增删行） ---
    // 跳过 GUIDELINES DESIGNINFO 内部的伪块
    for (const auto& block : blocks)
    {
        if (block.headerLineIndex < 0 || block.endLineIndex < 0)
            continue;
        if (IsDesignInfoEntry(lines, block))
            continue;

        switch (block.type)
        {
        case rceditor::ERcBlockType::Dialog:
        {
            SDialogTextLookup lookup = BuildDialogTextLookup(
                block.resourceID, data.primaryBinaryRes, data.secondaryBinaryRes);
            ReplaceDialogBlockTexts(lines,
                block.headerLineIndex, block.endLineIndex,
                lookup, header);
            break;
        }
        case rceditor::ERcBlockType::Menu:
        {
            auto itMenu = data.secondaryBinaryRes.mapLangAndMenuInfo.find(block.resourceID);
            if (itMenu != data.secondaryBinaryRes.mapLangAndMenuInfo.end() && itMenu->second)
            {
                SMenuTextLookup lookup;
                CollectMenuTextsRecursive(itMenu->second, lookup);
                ReplaceMenuBlockTexts(lines,
                    block.beginLineIndex + 1, block.endLineIndex - 1,
                    lookup, header);
            }
            break;
        }
        case rceditor::ERcBlockType::StringTable:
        {
            ReplaceStringTableBlockTexts(lines,
                block.beginLineIndex + 1, block.endLineIndex - 1,
                secondaryStringMap, header);
            break;
        }
        default:
            break;
        }
    }

    // --- 第 3 步：替换语言相关指令（LANGUAGE / code_page / AFX_TARG）---
    // 深拷贝自主语言 RC，因此语言指令仍是主语言的，需替换为次语言匹配的值
    // SSingleResourceInfo::langID 不被 PE 读取流程设置（始终为 0），
    // 因此从次语言二进制资源的 SDialogInfo::langID 中提取实际语言。
    LANGID secLangID = 0;
    if (data.secondaryEditor)
    {
        const auto& origRes = data.secondaryEditor->GetResourceInfo();
        for (const auto& [id, pDlg] : origRes.mapLangAndDialogInfo)
        {
            if (pDlg && pDlg->langID != 0)
            {
                secLangID = pDlg->langID;
                break;
            }
        }
    }
    if (secLangID == 0)
        secLangID = m_secondaryLangID;
    ReplaceLanguageDirectives(lines, secLangID);

    return true;
}

// ============================================================================
// 对齐次语言 RC 的 Dialog/Menu 块结构与主语言一致（双方均有 RC 时使用）
// 策略：用主语言 RC 块的原文替换次语言对应块的行，再仅替换引号文本
// ============================================================================

bool CRcCompareControler::AlignSecondaryRcWithPrimary(STaskLoadedData& data)
{
    if (data.primaryRcContent.lines.empty() || data.secondaryRcContent.lines.empty())
        return false;

    const rceditor::SResourceHeader& header = data.secondaryHeader;
    auto& secLines  = data.secondaryRcContent.lines;
    auto& secBlocks = data.secondaryRcContent.blocks;
    const auto& priLines  = data.primaryRcContent.lines;
    const auto& priBlocks = data.primaryRcContent.blocks;

    // 构建主语言块索引：(blockType, resourceID) → priBlocks 下标
    // 跳过 GUIDELINES DESIGNINFO 内部的伪块（它们与真正的 Dialog 块共享相同的 resourceID）
    std::map<std::pair<int, UINT>, int> primaryBlockMap;
    for (int i = 0; i < (int)priBlocks.size(); ++i)
    {
        auto t = priBlocks[i].type;
        if (t == rceditor::ERcBlockType::Dialog || t == rceditor::ERcBlockType::Menu)
        {
            if (IsDesignInfoEntry(priLines, priBlocks[i]))
                continue;
            primaryBlockMap[{(int)t, priBlocks[i].resourceID}] = i;
        }
    }

    // 构建字符串查找表
    std::map<UINT, CStringW> secondaryStringMap;
    for (const auto& stPair : data.secondaryBinaryRes.mapLangAndStringTableInfo)
    {
        if (!stPair.second) continue;
        for (const auto& entry : stPair.second->mapIDAndString)
            secondaryStringMap[entry.first] = entry.second;
    }

    // 按 headerLineIndex 降序收集需要对齐的次语言块索引（同样跳过 DESIGNINFO 伪块）
    // 对同一 (type, resourceID) 只保留第一个（避免 MergeAll 复制导致的重复块被处理两次）
    std::vector<int> alignOrder;
    {
        std::set<std::pair<int, UINT>> seenKeys;
        for (int i = 0; i < (int)secBlocks.size(); ++i)
        {
            auto t = secBlocks[i].type;
            if (t == rceditor::ERcBlockType::Dialog || t == rceditor::ERcBlockType::Menu)
            {
                if (IsDesignInfoEntry(secLines, secBlocks[i]))
                    continue;
                auto key = std::make_pair((int)t, secBlocks[i].resourceID);
                if (primaryBlockMap.count(key) && seenKeys.insert(key).second)
                    alignOrder.push_back(i);
            }
        }
    }
    std::sort(alignOrder.begin(), alignOrder.end(), [&secBlocks](int a, int b) {
        return secBlocks[a].headerLineIndex > secBlocks[b].headerLineIndex;
    });

    // 倒序替换：用主语言块行替换次语言块行，再做文本替换
    // 对次语言合并前已存在的块，跳过对齐（保留原始 RC 行，避免破坏已有文本和尺寸）
    for (int si : alignOrder)
    {
        const auto& secBlock = secBlocks[si];
        auto key = std::make_pair((int)secBlock.type, secBlock.resourceID);

        // 跳过合并前已存在的块：其 RC 行已正确，无需替换为主语言结构
        if (data.originalSecondaryBlockKeys.count(key))
            continue;

        int pi = primaryBlockMap[key];
        const auto& priBlock = priBlocks[pi];

        int priStart = priBlock.headerLineIndex;
        // 动态计算主语言块的真实范围（不信任 priBlock.endLineIndex）。
        // 主语言 RC 可能存在 premature END（Bug 2），导致解析器只识别到
        // 第一个 depth-0 END。改用 "安全边界 + 反向扫描" 策略找到真正的最后一个 END。
        int priEnd = -1;
        {
            int boundary = (int)priLines.size();
            // 约束1：其他主语言块的 headerLineIndex
            for (const auto& blk : priBlocks)
            {
                if (blk.headerLineIndex > priStart && blk.headerLineIndex < boundary)
                    boundary = blk.headerLineIndex;
            }
            // 约束2：预处理指令、DESIGNINFO、DLGINIT 等
            // 使用 token 精确匹配，避免引号内文本触发误判
            {
                static const wchar_t* boundaryKeywords[] = {
                    L"DESIGNINFO", L"DLGINIT", L"VERSIONINFO",
                    L"TEXTINCLUDE", L"AFX_DIALOG_LAYOUT", nullptr
                };
                for (int li = priStart + 1; li < boundary; ++li)
                {
                    CStringW t = priLines[li].text; t.TrimLeft(); t.TrimRight();
                    if (t.IsEmpty()) continue;
                    if (t.GetAt(0) == L'#') { boundary = li; break; }
                    if (IsResourceHeaderByToken(priLines[li].text, boundaryKeywords))
                    { boundary = li; break; }
                }
            }
            // 从边界向前（反向）找最后一个 END
            for (int li = boundary - 1; li > priStart; --li)
            {
                CStringW t = priLines[li].text; t.TrimLeft(); t.TrimRight(); t.MakeUpper();
                if (t == L"END" || t == L"}")
                {
                    priEnd = li;
                    break;
                }
            }
        }
        if (priStart < 0 || priEnd < 0 || priEnd >= (int)priLines.size()) continue;

        // --- 块拷贝前的结构验证 ---
        // 确保主语言块的第一行含有资源头（IDD_xxx DIALOGEX/DIALOG 或 IDR_xxx MENU）
        // 以及最后一行是 END/}。否则跳过此块的对齐，避免生成错误结构。
        {
            CStringW firstTrimmed = priLines[priStart].text;
            firstTrimmed.TrimLeft(); firstTrimmed.TrimRight();
            CStringW lastTrimmed = priLines[priEnd].text;
            lastTrimmed.TrimLeft(); lastTrimmed.TrimRight();
            CStringW lastUpper = lastTrimmed; lastUpper.MakeUpper();

            // 第一行应包含资源类型关键词（使用 token 匹配）
            static const wchar_t* structValidKeywords[] = {
                L"DIALOGEX", L"DIALOG", L"MENUEX", L"MENU", nullptr
            };
            bool hasKeyword = IsResourceHeaderByToken(priLines[priStart].text, structValidKeywords);
            bool hasEnd = (lastUpper == L"END" || lastUpper == L"}");

            if (!hasKeyword || !hasEnd)
                continue; // 块结构异常，跳过对齐
        }

        // 从主语言拷贝块行
        std::vector<rceditor::SRcLine> newLines(
            priLines.begin() + priStart, priLines.begin() + priEnd + 1);

        // 规范化：去除主语言块内多余的 depth-0 END（premature END），
        // 同时去重因前次错误合并残留的重复控件行。
        // 策略：找到第一个 BEGIN 后，按 depth-0 END 分割成多个段落。
        // 只保留每段中的唯一行（后续段中若含已出现过的行则跳过），
        // 最后重组为 header + BEGIN + 去重内容 + END。
        {
            int beginIdx = -1;
            for (int k = 0; k < (int)newLines.size(); ++k)
            {
                CStringW t = newLines[k].text; t.TrimLeft(); t.TrimRight(); t.MakeUpper();
                if (t == L"BEGIN" || t == L"{") { beginIdx = k; break; }
            }
            if (beginIdx >= 0)
            {
                // 第 1 步：找出所有 depth-0 END 位置
                std::vector<int> depth0Ends;
                int d = 1;
                for (int k = beginIdx + 1; k < (int)newLines.size(); ++k)
                {
                    CStringW t = newLines[k].text; t.TrimLeft(); t.TrimRight(); t.MakeUpper();
                    if (t == L"BEGIN" || t == L"{") ++d;
                    else if (t == L"END" || t == L"}")
                    {
                        --d;
                        if (d == 0)
                        {
                            depth0Ends.push_back(k);
                            d = 1; // 重置深度以继续扫描 premature END 后的内容
                        }
                    }
                }

                if (depth0Ends.size() > 1)
                {
                    // 第 2 步：重建块体——收集所有段落中的唯一行
                    // header 部分（priStart 到 BEGIN，含 BEGIN）保留不变
                    std::vector<rceditor::SRcLine> header(newLines.begin(), newLines.begin() + beginIdx + 1);
                    std::vector<rceditor::SRcLine> body;
                    std::set<CStringW> seenLines; // 用于去重的已见行文本集

                    // 遍历每个段落（[segStart, segEnd) = 两个 depth-0 END 之间的内容）
                    int segStart = beginIdx + 1;
                    for (int ei = 0; ei < (int)depth0Ends.size(); ++ei)
                    {
                        int segEnd = depth0Ends[ei]; // depth-0 END 行（排除）
                        for (int k = segStart; k < segEnd; ++k)
                        {
                            CStringW trimmed = newLines[k].text;
                            trimmed.TrimLeft(); trimmed.TrimRight();
                            // 空行跳过（不参与去重，也不保留到 body）
                            if (trimmed.IsEmpty()) continue;
                            // 嵌套 BEGIN/END（菜单 POPUP 结构）始终保留
                            CStringW upper = trimmed; upper.MakeUpper();
                            if (upper == L"BEGIN" || upper == L"{" ||
                                upper == L"END" || upper == L"}")
                            {
                                body.push_back(newLines[k]);
                                continue;
                            }
                            // 控件行：去重
                            if (seenLines.insert(trimmed).second)
                                body.push_back(newLines[k]);
                        }
                        segStart = segEnd + 1; // 跳过 depth-0 END 自身
                    }

                    // 第 3 步：重组 = header(含 BEGIN) + 去重 body + END
                    rceditor::SRcLine endLine = newLines[depth0Ends.back()]; // 使用最后一个 END 行
                    newLines.clear();
                    newLines.insert(newLines.end(), header.begin(), header.end());
                    newLines.insert(newLines.end(), body.begin(), body.end());
                    newLines.push_back(endLine);
                }
            }
        }

        // 在副本上做文本替换
        if (secBlock.type == rceditor::ERcBlockType::Dialog)
        {
            SDialogTextLookup lookup = BuildDialogTextLookup(
                secBlock.resourceID, data.primaryBinaryRes, data.secondaryBinaryRes);
            ReplaceDialogBlockTexts(newLines, 0, (int)newLines.size() - 1, lookup, header);
        }
        else if (secBlock.type == rceditor::ERcBlockType::Menu)
        {
            auto itMenu = data.secondaryBinaryRes.mapLangAndMenuInfo.find(secBlock.resourceID);
            if (itMenu != data.secondaryBinaryRes.mapLangAndMenuInfo.end() && itMenu->second)
            {
                SMenuTextLookup lookup;
                CollectMenuTextsRecursive(itMenu->second, lookup);
                // 找到 BEGIN/END 内部范围
                int innerStart = 0, innerEnd = (int)newLines.size() - 1;
                for (int k = 0; k < (int)newLines.size(); ++k)
                {
                    CStringW t = newLines[k].text; t.TrimLeft(); t.MakeUpper();
                    if (t == L"BEGIN" || t == L"{") { innerStart = k + 1; break; }
                }
                for (int k = (int)newLines.size() - 1; k >= 0; --k)
                {
                    CStringW t = newLines[k].text; t.TrimLeft(); t.MakeUpper();
                    if (t == L"END" || t == L"}") { innerEnd = k - 1; break; }
                }
                ReplaceMenuBlockTexts(newLines, innerStart, innerEnd, lookup, header);
            }
        }

        int secStart = secBlocks[si].headerLineIndex;

        // 查找次语言块的真实全范围（headerLineIndex → 最后一个 END）。
        // 不能用深度计数（extra END 会导致提前停止，留下孤儿行），
        // 改用 "安全边界 + 反向扫描" 策略：
        //   1. 找到安全边界（下一个块的 header、预处理指令、DESIGNINFO 注释）
        //   2. 从边界向前（反向）找最后一个 END → 这是块的真正终止行
        int secEnd = -1;
        {
            // 安全边界：默认为文件末尾
            int boundary = (int)secLines.size();

            // 约束1：其他块的 headerLineIndex（已被前序迭代的 delta 更新）
            for (const auto& blk : secBlocks)
            {
                if (blk.headerLineIndex > secStart && blk.headerLineIndex < boundary)
                    boundary = blk.headerLineIndex;
            }

            // 约束2：扫描从 secStart+1 到当前 boundary 之间的行，
            // 遇到以下任何情况都收窄 boundary（它们标志着当前块的范围终结）：
            //   - 预处理指令 (#ifdef, #endif 等)
            //   - DESIGNINFO / DLGINIT / VERSIONINFO 等非块管理但含 BEGIN/END 的资源头行
            // 使用 token 精确匹配，避免引号内文本触发误判
            {
                static const wchar_t* boundaryKeywords[] = {
                    L"DESIGNINFO", L"DLGINIT", L"VERSIONINFO",
                    L"TEXTINCLUDE", L"AFX_DIALOG_LAYOUT", nullptr
                };
                for (int li = secStart + 1; li < boundary; ++li)
                {
                    CStringW t = secLines[li].text; t.TrimLeft(); t.TrimRight();
                    if (t.IsEmpty()) continue;
                    if (t.GetAt(0) == L'#') { boundary = li; break; }
                    if (IsResourceHeaderByToken(secLines[li].text, boundaryKeywords))
                    { boundary = li; break; }
                }
            }

            // 从边界向前（反向）找最后一个 END
            for (int li = boundary - 1; li > secStart; --li)
            {
                CStringW t = secLines[li].text; t.TrimLeft(); t.TrimRight(); t.MakeUpper();
                if (t == L"END" || t == L"}")
                {
                    secEnd = li;
                    break;
                }
            }

            // 孤儿内容检测：检查 secEnd+1 到 boundary-1 之间是否有
            // 被 MergeAll 错误注入到 END 之后的控件行或多余的 END。
            // 如果发现非空白、非注释的实质内容，扩展 secEnd 以包含这些孤儿行，
            // 确保替换时将它们一并清除。
            if (secEnd >= 0)
            {
                int lastOrphanLine = secEnd;
                for (int li = secEnd + 1; li < boundary; ++li)
                {
                    CStringW t = secLines[li].text; t.TrimLeft(); t.TrimRight();
                    if (t.IsEmpty()) continue; // 空行跳过
                    CStringW upper = t; upper.MakeUpper();
                    if (upper.Left(2) == L"//" || upper.Left(2) == L"/*")
                        continue; // 注释行跳过
                    // 发现非空白、非注释内容 — 这是孤儿行，需要被包含到替换范围
                    lastOrphanLine = li;
                }
                if (lastOrphanLine > secEnd)
                    secEnd = lastOrphanLine;
            }
        }

        // 边界检查：确保索引在有效范围内
        if (secStart < 0 || secEnd < 0 || secStart > secEnd)
            continue;
        if (secStart >= (int)secLines.size() || secEnd >= (int)secLines.size())
            continue;

        int oldCount = secEnd - secStart + 1;
        int newCount = (int)newLines.size();
        int delta    = newCount - oldCount;

        // 擦除旧行，插入新行
        secLines.erase(secLines.begin() + secStart, secLines.begin() + secEnd + 1);
        secLines.insert(secLines.begin() + secStart, newLines.begin(), newLines.end());

        // 更新当前块的 beginLineIndex / endLineIndex（它们在 erase+insert 后已过时）
        secBlocks[si].beginLineIndex = secStart + (priBlock.beginLineIndex - priBlock.headerLineIndex);
        secBlocks[si].endLineIndex   = secStart + newCount - 1;
        // 重算 attributeLineIndices
        secBlocks[si].attributeLineIndices.clear();
        for (int ai = 0; ai < (int)priBlock.attributeLineIndices.size(); ++ai)
        {
            secBlocks[si].attributeLineIndices.push_back(
                secStart + (priBlock.attributeLineIndices[ai] - priBlock.headerLineIndex));
        }

        // 更新后续块的行索引
        if (delta != 0)
        {
            for (auto& blk : secBlocks)
            {
                if (blk.headerLineIndex > secEnd)
                {
                    blk.headerLineIndex += delta;
                    blk.beginLineIndex  += delta;
                    blk.endLineIndex    += delta;
                    for (auto& ai : blk.attributeLineIndices) ai += delta;
                }
            }
        }
    }

    // STRINGTABLE 块：仅做文本替换（不做结构替换）
    for (const auto& block : secBlocks)
    {
        if (block.type == rceditor::ERcBlockType::StringTable)
        {
            ReplaceStringTableBlockTexts(secLines,
                block.beginLineIndex + 1, block.endLineIndex - 1,
                secondaryStringMap, header);
        }
    }

    // 重编行号
    for (int i = 0; i < (int)secLines.size(); ++i)
        secLines[i].lineNumber = i + 1;

    return true;
}

// ============================================================================
// 生成主语言/次语言文本对照表（Tab 分隔，UTF-8 BOM）
// ============================================================================

CStringW CRcCompareControler::GenerateComparisonTable(const STaskLoadedData& data) const
{
    // 生成 Excel 2003 XML Spreadsheet 格式
    CStringW xml;
    xml += L"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n";
    xml += L"<?mso-application progid=\"Excel.Sheet\"?>\r\n";
    xml += L"<Workbook xmlns=\"urn:schemas-microsoft-com:office:spreadsheet\"\r\n";
    xml += L" xmlns:ss=\"urn:schemas-microsoft-com:office:spreadsheet\">\r\n";
    xml += L" <Styles>\r\n";
    xml += L"  <Style ss:ID=\"Header\"><Font ss:Bold=\"1\"/><Interior ss:Color=\"#D9E1F2\" ss:Pattern=\"Solid\"/></Style>\r\n";
    xml += L" </Styles>\r\n";
    xml += L" <Worksheet ss:Name=\"Comparison\">\r\n";
    xml += L"  <Table>\r\n";
    xml += L"   <Column ss:Width=\"60\"/><Column ss:Width=\"80\"/><Column ss:Width=\"80\"/><Column ss:Width=\"200\"/><Column ss:Width=\"200\"/>\r\n";

    // XML 转义辅助 lambda
    auto xmlEscape = [](const CStringW& s) -> CStringW {
        CStringW r = s;
        r.Replace(L"&", L"&amp;");
        r.Replace(L"<", L"&lt;");
        r.Replace(L">", L"&gt;");
        r.Replace(L"\"", L"&quot;");
        r.Replace(L"\r", L"");
        r.Replace(L"\n", L"&#10;");
        return r;
    };

    // 写入一行数据的 lambda
    auto addRow = [&](const CStringW& style,
                      const CStringW& c1, const CStringW& c2,
                      const CStringW& c3, const CStringW& c4,
                      const CStringW& c5)
    {
        if (style.IsEmpty())
            xml += L"   <Row>\r\n";
        else
            xml.AppendFormat(L"   <Row ss:StyleID=\"%s\">\r\n", (LPCWSTR)style);
        xml.AppendFormat(L"    <Cell><Data ss:Type=\"String\">%s</Data></Cell>\r\n", (LPCWSTR)xmlEscape(c1));
        xml.AppendFormat(L"    <Cell><Data ss:Type=\"String\">%s</Data></Cell>\r\n", (LPCWSTR)xmlEscape(c2));
        xml.AppendFormat(L"    <Cell><Data ss:Type=\"String\">%s</Data></Cell>\r\n", (LPCWSTR)xmlEscape(c3));
        xml.AppendFormat(L"    <Cell><Data ss:Type=\"String\">%s</Data></Cell>\r\n", (LPCWSTR)xmlEscape(c4));
        xml.AppendFormat(L"    <Cell><Data ss:Type=\"String\">%s</Data></Cell>\r\n", (LPCWSTR)xmlEscape(c5));
        xml += L"   </Row>\r\n";
    };

    // 表头
    addRow(L"Header", L"Type", L"ResourceID", L"ControlID", L"PrimaryText", L"SecondaryText");

    const auto& priRes = data.primaryBinaryRes;
    const auto& secRes = data.secondaryBinaryRes;

    // --- Dialog ---
    for (const auto& pair : priRes.mapLangAndDialogInfo)
    {
        UINT dlgID = pair.first;
        const auto& pPriDlg = pair.second;
        if (!pPriDlg) continue;

        // 次语言 dialog
        std::shared_ptr<rceditor::SDialogInfo> pSecDlg;
        auto itSec = secRes.mapLangAndDialogInfo.find(dlgID);
        if (itSec != secRes.mapLangAndDialogInfo.end())
            pSecDlg = itSec->second;

        // Caption
        CStringW secCaption = pSecDlg ? pSecDlg->csCaption : L"";
        {
            CStringW dlgIdStr;
            dlgIdStr.Format(L"%u", dlgID);
            addRow(L"", L"Dialog", dlgIdStr, L"CAPTION", pPriDlg->csCaption, secCaption);
        }

        // 收集次语言 STATIC 控件
        struct StaticEntry { CRect rect; CStringW text; bool used; };
        std::vector<StaticEntry> secStatics;
        if (pSecDlg)
        {
            for (const auto& ctrl : pSecDlg->vecCtrlInfo)
            {
                if (!ctrl) continue;
                UINT idc = ctrl->nIDC;
                if (idc == (UINT)-1 || idc == 0xFFFF || idc == 0)
                    secStatics.push_back({ ctrl->ctrlRect, ctrl->csText, false });
            }
        }

        // 次语言 IDC → text
        std::map<UINT, CStringW> secIdcMap;
        if (pSecDlg)
        {
            for (const auto& ctrl : pSecDlg->vecCtrlInfo)
            {
                if (!ctrl) continue;
                UINT idc = ctrl->nIDC;
                if (idc != (UINT)-1 && idc != 0xFFFF && idc != 0)
                    secIdcMap[idc] = ctrl->csText;
            }
        }

        // 遍历主语言控件
        for (const auto& ctrl : pPriDlg->vecCtrlInfo)
        {
            if (!ctrl) continue;
            UINT idc = ctrl->nIDC;

            CStringW secText;
            bool isStatic = (idc == (UINT)-1 || idc == 0xFFFF || idc == 0);

            if (isStatic)
            {
                // geometry 匹配
                for (auto& se : secStatics)
                {
                    if (!se.used && se.rect == ctrl->ctrlRect)
                    { secText = se.text; se.used = true; goto found; }
                }
                // 最近距离
                {
                    int bestIdx = -1;
                    double bestDist = 1e30;
                    for (int j = 0; j < (int)secStatics.size(); ++j)
                    {
                        if (secStatics[j].used) continue;
                        double dx = (double)(secStatics[j].rect.left - ctrl->ctrlRect.left);
                        double dy = (double)(secStatics[j].rect.top  - ctrl->ctrlRect.top);
                        double d  = dx * dx + dy * dy;
                        if (d < bestDist) { bestDist = d; bestIdx = j; }
                    }
                    if (bestIdx >= 0)
                    { secText = secStatics[bestIdx].text; secStatics[bestIdx].used = true; }
                }
                found:;
            }
            else
            {
                auto itC = secIdcMap.find(idc);
                if (itC != secIdcMap.end())
                    secText = itC->second;
            }

            CStringW idcStr;
            if (isStatic) idcStr = L"STATIC";
            else          idcStr.Format(L"%u", idc);

            CStringW dlgIdStr;
            dlgIdStr.Format(L"%u", dlgID);
            addRow(L"", L"Dialog", dlgIdStr, idcStr, ctrl->csText, secText);
        }
    }

    // --- StringTable ---
    std::map<UINT, CStringW> priStrings, secStrings;
    for (const auto& stPair : priRes.mapLangAndStringTableInfo)
    {
        if (!stPair.second) continue;
        for (const auto& entry : stPair.second->mapIDAndString)
            priStrings[entry.first] = entry.second;
    }
    for (const auto& stPair : secRes.mapLangAndStringTableInfo)
    {
        if (!stPair.second) continue;
        for (const auto& entry : stPair.second->mapIDAndString)
            secStrings[entry.first] = entry.second;
    }
    for (const auto& entry : priStrings)
    {
        CStringW secText;
        auto itS = secStrings.find(entry.first);
        if (itS != secStrings.end()) secText = itS->second;

        CStringW strIdStr;
        strIdStr.Format(L"%u", entry.first);
        addRow(L"", L"String", strIdStr, strIdStr, entry.second, secText);
    }

    // --- Menu ---
    for (const auto& pair : priRes.mapLangAndMenuInfo)
    {
        UINT menuID = pair.first;
        const auto& pPriMenu = pair.second;
        if (!pPriMenu) continue;

        std::shared_ptr<rceditor::SMenuInfo> pSecMenu;
        auto itSec = secRes.mapLangAndMenuInfo.find(menuID);
        if (itSec != secRes.mapLangAndMenuInfo.end())
            pSecMenu = itSec->second;

        // DFS 同步遍历
        std::function<void(const std::shared_ptr<rceditor::SMenuInfo>&,
                           const std::shared_ptr<rceditor::SMenuInfo>&)> dfs;
        dfs = [&](const std::shared_ptr<rceditor::SMenuInfo>& pri,
                  const std::shared_ptr<rceditor::SMenuInfo>& sec)
        {
            if (!pri) return;
            CStringW secText = sec ? sec->csText : L"";

            CStringW idStr;
            if (pri->bIsPopup) idStr = L"POPUP";
            else               idStr.Format(L"%u", pri->nID);

            CStringW menuIdStr;
            menuIdStr.Format(L"%u", menuID);
            addRow(L"", L"Menu", menuIdStr, idStr, pri->csText, secText);

            int priCount = (int)pri->vecSubMenu.size();
            int secCount = sec ? (int)sec->vecSubMenu.size() : 0;
            int maxCount = (std::max)(priCount, secCount);
            for (int i = 0; i < maxCount; ++i)
            {
                auto subPri = (i < priCount) ? pri->vecSubMenu[i] : nullptr;
                auto subSec = (i < secCount && sec) ? sec->vecSubMenu[i] : nullptr;
                dfs(subPri, subSec);
            }
        };
        dfs(pPriMenu, pSecMenu);
    }

    xml += L"  </Table>\r\n";
    xml += L" </Worksheet>\r\n";
    xml += L"</Workbook>\r\n";
    return xml;
}

void CRcCompareControler::ProcessSingleTask(
    int taskIndex,
    const rceditor::SCompareTaskConfig& task,
    const std::atomic<bool>& cancelFlag,
    rceditor::ProgressCallback progressCb)
{
    // 用于 CCompareWorkerThread::TaskWorkerFunc 回调

    // 1. 加载
    if (cancelFlag.load()) return;
    if (!LoadTask(taskIndex, cancelFlag, [&](int p) {
        if (progressCb) progressCb(p * 40 / 100);
    }))
    {
        return;
    }

    // 2. 合并
    if (cancelFlag.load()) return;
    if (progressCb) progressCb(40);
    MergeTask(taskIndex);

    // 3. (导出由用户确认后手动触发，此处不自动导出)
    if (progressCb) progressCb(100);
}
