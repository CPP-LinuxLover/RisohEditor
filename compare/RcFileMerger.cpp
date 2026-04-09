#include "stdafx.h"
#include "RcFileMerger.h"
#include "RcFileParser.h"
#include <algorithm>
#include <cmath>

namespace rceditor {

CRcFileMerger::CRcFileMerger()
{
}

// ============================================================================
// 完整合并
// ============================================================================

SMergeResult CRcFileMerger::MergeAll(
    const SRcFileContent& primaryRc,
    const SResourceHeader& primaryHeader,
    const SSingleResourceInfo& primaryRes,
    SRcFileContent& secondaryRc,
    SResourceHeader& secondaryHeader,
    SSingleResourceInfo& secondaryRes)
{
    SMergeResult result;

    // 1. 冲突检测
    DetectMacroConflicts(primaryHeader, secondaryHeader, result.conflicts);
    DetectControlIdConflicts(primaryRes, secondaryRes, result.conflicts);

    // 2. 合并 resource.h 宏定义
    result.totalInjectedDefines = MergeDefines(primaryHeader, secondaryHeader, result);

    // 3. 合并 Dialog 控件
    result.totalInjectedControls = MergeDialogs(
        primaryRc, primaryHeader, primaryRes,
        secondaryRc, secondaryHeader, secondaryRes, result);

    // 4. 合并 StringTable
    result.totalInjectedStrings = MergeStringTables(
        primaryRc, primaryHeader, primaryRes,
        secondaryRc, secondaryHeader, secondaryRes, result);

    // 5. 合并 Menu
    result.totalInjectedMenuItems = MergeMenus(
        primaryRc, primaryHeader, primaryRes,
        secondaryRc, secondaryHeader, secondaryRes, result);

    return result;
}

// ============================================================================
// 冲突检测 (只读)
// ============================================================================

std::vector<SConflictInfo> CRcFileMerger::DetectConflicts(
    const SResourceHeader& primaryHeader,
    const SResourceHeader& secondaryHeader,
    const SSingleResourceInfo& primaryRes,
    const SSingleResourceInfo& secondaryRes)
{
    std::vector<SConflictInfo> conflicts;
    DetectMacroConflicts(primaryHeader, secondaryHeader, conflicts);
    DetectControlIdConflicts(primaryRes, secondaryRes, conflicts);
    return conflicts;
}

void CRcFileMerger::DetectMacroConflicts(
    const SResourceHeader& primaryHeader,
    const SResourceHeader& secondaryHeader,
    std::vector<SConflictInfo>& conflicts)
{
    for (const auto& pDef : primaryHeader.defines)
    {
        // 同名不同值
        auto it = secondaryHeader.nameToId.find(pDef.name);
        if (it != secondaryHeader.nameToId.end() && it->second != pDef.value)
        {
            SConflictInfo c;
            c.type = EConflictType::MacroNameConflict;
            c.primaryName = pDef.name;
            c.primaryValue = pDef.value;
            c.secondaryName = pDef.name;
            c.secondaryValue = it->second;
            c.description.Format(L"Macro '%s': primary=%u, secondary=%u",
                                 (LPCWSTR)pDef.name, pDef.value, it->second);
            conflicts.push_back(c);
        }

        // 同值不同名（在次语言中值已被占用但名字不同）
        auto itVal = secondaryHeader.idToName.find(pDef.value);
        if (itVal != secondaryHeader.idToName.end() &&
            itVal->second.CompareNoCase(pDef.name) != 0)
        {
            // 只报告主语言中不存在于次语言的宏
            if (secondaryHeader.nameToId.find(pDef.name) == secondaryHeader.nameToId.end())
            {
                SConflictInfo c;
                c.type = EConflictType::IdValueConflict;
                c.primaryName = pDef.name;
                c.primaryValue = pDef.value;
                c.secondaryName = itVal->second;
                c.secondaryValue = pDef.value;
                c.description.Format(L"Value %u: primary='%s', secondary='%s'",
                                     pDef.value, (LPCWSTR)pDef.name, (LPCWSTR)itVal->second);
                conflicts.push_back(c);
            }
        }
    }
}

void CRcFileMerger::DetectControlIdConflicts(
    const SSingleResourceInfo& primaryRes,
    const SSingleResourceInfo& secondaryRes,
    std::vector<SConflictInfo>& conflicts)
{
    for (const auto& [dlgId, pPrimaryDlg] : primaryRes.mapLangAndDialogInfo)
    {
        auto itSec = secondaryRes.mapLangAndDialogInfo.find(dlgId);
        if (itSec == secondaryRes.mapLangAndDialogInfo.end())
            continue;

        const auto& pSecDlg = itSec->second;

        // 收集次语言已有的 IDC 集合
        std::map<UINT, bool> secIdcSet;
        for (const auto& ctrl : pSecDlg->vecCtrlInfo)
        {
            if (ctrl->nIDC != (UINT)-1)
                secIdcSet[ctrl->nIDC] = true;
        }

        // 检查主语言中不在次语言中的控件（这些才需要注入）
        // 但如果它的 IDC 已经被次语言的另一个控件占用 → 冲突
        for (const auto& ctrl : pPrimaryDlg->vecCtrlInfo)
        {
            if (ctrl->nIDC == (UINT)-1) continue;

            // 看次语言中是否有相同 IDC
            bool existsInSec = false;
            for (const auto& secCtrl : pSecDlg->vecCtrlInfo)
            {
                if (secCtrl->nIDC == ctrl->nIDC)
                {
                    existsInSec = true;
                    break;
                }
            }

            // 如果次语言没有这个控件，但 IDC 值已被某个不同控件使用
            // 此处简化：如果次语言中不存在该控件 且 IDC 在 secIdcSet 中
            // → 不是冲突（因为相同 IDC 意味着是同一个控件）
            // 实际冲突情况在 resource.h 宏层面已捕获
        }
    }
}

// ============================================================================
// Dialog 控件合并
// ============================================================================

int CRcFileMerger::MergeDialogs(
    const SRcFileContent& primaryRc,
    const SResourceHeader& primaryHeader,
    const SSingleResourceInfo& primaryRes,
    SRcFileContent& secondaryRc,
    SResourceHeader& secondaryHeader,
    SSingleResourceInfo& secondaryRes,
    SMergeResult& result)
{
    int injectedCount = 0;

    for (const auto& [dlgId, pPrimaryDlg] : primaryRes.mapLangAndDialogInfo)
    {
        auto itSec = secondaryRes.mapLangAndDialogInfo.find(dlgId);

        // 当按 ID 找不到时，尝试按 csResourceName 回退匹配
        // 安全性：仅当唯一匹配时才允许回退，防止跨窗口误合并
        if (itSec == secondaryRes.mapLangAndDialogInfo.end() &&
            pPrimaryDlg && !pPrimaryDlg->csResourceName.IsEmpty())
        {
            int matchCount = 0;
            decltype(secondaryRes.mapLangAndDialogInfo)::iterator uniqueMatch;
            for (auto it2 = secondaryRes.mapLangAndDialogInfo.begin();
                 it2 != secondaryRes.mapLangAndDialogInfo.end(); ++it2)
            {
                if (it2->second && !it2->second->csResourceName.IsEmpty() &&
                    it2->second->csResourceName.CompareNoCase(pPrimaryDlg->csResourceName) == 0)
                {
                    uniqueMatch = it2;
                    ++matchCount;
                    if (matchCount > 1) break; // 歧义：多个窗口同名，放弃回退
                }
            }
            if (matchCount == 1)
                itSec = uniqueMatch;
        }

        if (itSec == secondaryRes.mapLangAndDialogInfo.end())
        {
            // 次语言完全缺失此 Dialog — 从主语言 RC 中复制整块注入
            CStringW priDlgName = LookupIdName(dlgId, primaryHeader);
            // 若按 ID 找不到块，用 csResourceName 回退
            if (priDlgName.IsEmpty() || primaryRc.nameToBlockIndex.find(priDlgName) == primaryRc.nameToBlockIndex.end())
            {
                if (pPrimaryDlg && !pPrimaryDlg->csResourceName.IsEmpty())
                    priDlgName = pPrimaryDlg->csResourceName;
            }
            auto itPriBlock = primaryRc.nameToBlockIndex.find(priDlgName);
            if (itPriBlock != primaryRc.nameToBlockIndex.end())
            {
                const SRcBlock& priBlock = primaryRc.blocks[itPriBlock->second];
                if (priBlock.headerLineIndex >= 0 && priBlock.endLineIndex >= 0)
                {
                    // 在次语言 RC 末尾插入空行分隔，然后逐行复制主语言 Dialog 块
                    int insertAfter = (int)secondaryRc.lines.size() - 1;

                    // 添加空行分隔
                    InsertLine(secondaryRc, insertAfter, L"", ERcLineType::Blank, -1);
                    ++insertAfter;

                    // 复制从 headerLineIndex 到 endLineIndex 的所有行
                    for (int li = priBlock.headerLineIndex; li <= priBlock.endLineIndex; ++li)
                    {
                        const SRcLine& srcLine = primaryRc.lines[li];
                        InsertLine(secondaryRc, insertAfter, srcLine.text, srcLine.type, -1);
                        ++insertAfter;
                    }

                    // 更新次语言的 blocks 索引（为新插入的块建立索引）
                    SRcBlock newBlock;
                    newBlock.type = ERcBlockType::Dialog;
                    newBlock.resourceID = dlgId;
                    newBlock.resourceName = priDlgName;
                    newBlock.resourceTypeKeyword = priBlock.resourceTypeKeyword;
                    // 计算新块的行索引：插入起点 = 原末尾 + 1（空行）+ 1（第一行）
                    int blockStartLine = (int)secondaryRc.lines.size() - (priBlock.endLineIndex - priBlock.headerLineIndex + 1);
                    newBlock.headerLineIndex = blockStartLine;
                    newBlock.beginLineIndex = blockStartLine + (priBlock.beginLineIndex - priBlock.headerLineIndex);
                    newBlock.endLineIndex = blockStartLine + (priBlock.endLineIndex - priBlock.headerLineIndex);
                    newBlock.blockIndex = (int)secondaryRc.blocks.size();
                    secondaryRc.blocks.push_back(newBlock);
                    secondaryRc.nameToBlockIndex[priDlgName] = newBlock.blockIndex;
                    secondaryRc.idToBlockIndex[dlgId] = newBlock.blockIndex;

                    // 更新内存模型：深拷贝主语言的 Dialog 信息到次语言
                    auto pClonedDlg = std::make_shared<SDialogInfo>(*pPrimaryDlg);
                    secondaryRes.mapLangAndDialogInfo[dlgId] = pClonedDlg;
                }
            }

            SInjectionRecord rec;
            rec.type = SInjectionRecord::WholeBlock;
            rec.resourceId = dlgId;
            rec.description.Format(L"Entire Dialog %u injected from primary", dlgId);
            result.injections.push_back(rec);
            ++result.totalInjectedBlocks;
            continue;
        }

        const auto& pSecDlg = itSec->second;

        // ---- 混合匹配：快速路径 + 锚点分析 ----
        std::vector<int> missingIndices;
        FindMissingControlsHybrid(pPrimaryDlg, pSecDlg, missingIndices);

        // 为 GroupBox 偏移位置修正准备棋盘模型（缓存，避免重复构建）
        SDialogBoard priBoard;
        bool boardBuilt = false;

        // ================================================================
        // GroupBox 重叠修正：当注入的 GroupBox 与次语言已有控件重叠时，
        // 记录偏移量（不修改主语言原始数据），在注入副本时应用
        // ================================================================
        std::map<int, int> gbOffsetDY; // ctrlIndex → 偏移 DY
        if (!missingIndices.empty())
        {
            priBoard = BuildDialogBoard(pPrimaryDlg);
            boardBuilt = true;

            std::set<int> missingSet(missingIndices.begin(), missingIndices.end());

            for (const auto& grp : priBoard.groupBoxes)
            {
                // 找此 GroupBox 对应的控件索引
                int gbCtrlIdx = -1;
                for (int ci = 0; ci < (int)pPrimaryDlg->vecCtrlInfo.size(); ++ci)
                {
                    const auto& c = pPrimaryDlg->vecCtrlInfo[ci];
                    if (c && IsGroupBoxCtrl(c) && c->ctrlRect == grp.groupRect)
                    {
                        if (missingSet.count(ci)) { gbCtrlIdx = ci; break; }
                    }
                }
                if (gbCtrlIdx < 0) continue;

                // 收集此 GroupBox + 其待注入子控件索引
                std::set<int> groupMissingSet;
                groupMissingSet.insert(gbCtrlIdx);
                for (const auto& tile : grp.tiles)
                    if (missingSet.count(tile.globalIndex))
                        groupMissingSet.insert(tile.globalIndex);

                // 检查 GroupBox 区域是否与次语言已有控件重叠
                CRect gbRect = grp.groupRect;
                bool overlaps = false;
                int maxExistingBottom = 0;
                for (const auto& secCtrl : pSecDlg->vecCtrlInfo)
                {
                    if (!secCtrl) continue;
                    CRect inter;
                    if (inter.IntersectRect(&gbRect, &secCtrl->ctrlRect))
                        overlaps = true;
                    if (secCtrl->ctrlRect.bottom > maxExistingBottom)
                        maxExistingBottom = secCtrl->ctrlRect.bottom;
                }

                if (overlaps && maxExistingBottom > gbRect.top)
                {
                    int dy = maxExistingBottom + 5 - gbRect.top;
                    for (int ci : groupMissingSet)
                        gbOffsetDY[ci] = dy;
                }
            }
        }

        for (int ci : missingIndices)
        {
            const auto& pCtrl = pPrimaryDlg->vecCtrlInfo[ci];

            // 需要注入此控件
            // 找前驱 IDC
            UINT predIDC = FindPredecessorIDC(pPrimaryDlg, ci);

            // 创建要注入的控件副本
            auto pNewCtrl = std::make_shared<SCtrlInfo>(*pCtrl);
            pNewCtrl->m_blIsAdded = true;
            pNewCtrl->m_blNeedsLayoutAdjust = true;

            // GroupBox 重叠偏移：对副本应用偏移（不修改主语言原始数据）
            {
                auto itOff = gbOffsetDY.find(ci);
                if (itOff != gbOffsetDY.end())
                    pNewCtrl->ctrlRect.OffsetRect(0, itOff->second);
            }

            // 对 STATIC 控件使用 GroupBox 偏移修正注入位置
            // GroupBox 自身保持原始坐标（不做比例缩放，避免渲染错位）
            if (IsStaticIDC(pCtrl->nIDC) && !IsGroupBoxCtrl(pCtrl))
            {
                if (!boardBuilt)
                {
                    priBoard = BuildDialogBoard(pPrimaryDlg);
                    boardBuilt = true;
                }
                CRect correctedRect = ComputeInjectionPosition(
                    pCtrl, ci, pPrimaryDlg, pSecDlg, priBoard);
                pNewCtrl->ctrlRect = correctedRect;
            }

            // 找次语言 RC 中对应的 Dialog 块
            CStringW dlgName = LookupIdName(dlgId, secondaryHeader);
            auto itBlock = secondaryRc.nameToBlockIndex.find(dlgName);
            // 若按 ID 名称找不到，用次语言对话框的 csResourceName 回退
            if (itBlock == secondaryRc.nameToBlockIndex.end() &&
                itSec != secondaryRes.mapLangAndDialogInfo.end() &&
                itSec->second && !itSec->second->csResourceName.IsEmpty())
            {
                itBlock = secondaryRc.nameToBlockIndex.find(itSec->second->csResourceName);
            }
            if (itBlock == secondaryRc.nameToBlockIndex.end())
            {
                // 尝试通过 ID 查找
                auto itId = secondaryRc.idToBlockIndex.find(dlgId);
                if (itId == secondaryRc.idToBlockIndex.end())
                    continue;
                itBlock = secondaryRc.nameToBlockIndex.end();
                // 直接用 ID 索引
                const SRcBlock& secBlock = secondaryRc.blocks[itId->second];

                // 找主语言对应的块
                CStringW priDlgName = LookupIdName(dlgId, primaryHeader);
                auto itPriBlock = primaryRc.nameToBlockIndex.find(priDlgName);
                if (itPriBlock == primaryRc.nameToBlockIndex.end())
                    continue;
                const SRcBlock& priBlock = primaryRc.blocks[itPriBlock->second];

                InjectControlIntoBlock(priBlock, primaryRc, pNewCtrl, predIDC,
                                       secBlock, secondaryRc, primaryHeader, secondaryHeader, result);
            }
            else
            {
                const SRcBlock& secBlock = secondaryRc.blocks[itBlock->second];

                CStringW priDlgName = LookupIdName(dlgId, primaryHeader);
                auto itPriBlock = primaryRc.nameToBlockIndex.find(priDlgName);
                const SRcBlock& priBlock = (itPriBlock != primaryRc.nameToBlockIndex.end())
                    ? primaryRc.blocks[itPriBlock->second]
                    : secBlock; // fallback

                InjectControlIntoBlock(priBlock, primaryRc, pNewCtrl, predIDC,
                                       secBlock, secondaryRc, primaryHeader, secondaryHeader, result);
            }

            // 更新内存中的 SDialogInfo
            pSecDlg->vecCtrlInfo.push_back(pNewCtrl);
            pSecDlg->vecAddedCtrlInfo.push_back(pNewCtrl);

            ++injectedCount;
        }
    }

    return injectedCount;
}

// ============================================================================
// 混合匹配：快速路径 + 锚点分析
// ============================================================================

void CRcFileMerger::FindMissingControlsHybrid(
    const std::shared_ptr<SDialogInfo>& pPrimaryDlg,
    const std::shared_ptr<SDialogInfo>& pSecDlg,
    std::vector<int>& outMissingIndices)
{
    outMissingIndices.clear();
    if (!pPrimaryDlg || !pSecDlg) return;

    // 非 STATIC 控件：IDC 直接匹配（含命名 GroupBox）
    std::map<int, bool> idcMatched;
    for (int ci = 0; ci < (int)pPrimaryDlg->vecCtrlInfo.size(); ++ci)
    {
        const auto& pCtrl = pPrimaryDlg->vecCtrlInfo[ci];
        if (!pCtrl) continue;
        bool isStatic = IsStaticIDC(pCtrl->nIDC);
        if (isStatic) continue;

        for (const auto& secCtrl : pSecDlg->vecCtrlInfo)
        {
            if (secCtrl && secCtrl->nIDC == pCtrl->nIDC)
            {
                idcMatched[ci] = true;
                break;
            }
        }
    }

    // 无名 GroupBox 序号匹配（IDC 为 STATIC 类的 GroupBox 按出现顺序配对）
    std::map<int, bool> gbMatched;
    {
        std::vector<int> priNamelessGB, secNamelessGB;
        for (int ci = 0; ci < (int)pPrimaryDlg->vecCtrlInfo.size(); ++ci)
        {
            const auto& c = pPrimaryDlg->vecCtrlInfo[ci];
            if (c && IsGroupBoxCtrl(c) && IsStaticIDC(c->nIDC))
                priNamelessGB.push_back(ci);
        }
        for (int ci = 0; ci < (int)pSecDlg->vecCtrlInfo.size(); ++ci)
        {
            const auto& c = pSecDlg->vecCtrlInfo[ci];
            if (c && IsGroupBoxCtrl(c) && IsStaticIDC(c->nIDC))
                secNamelessGB.push_back(ci);
        }
        int gbPairs = (int)(std::min)(priNamelessGB.size(), secNamelessGB.size());
        for (int k = 0; k < gbPairs; ++k)
            gbMatched[priNamelessGB[k]] = true;
    }

    // 全量拓扑网格分析：对所有 STATIC 控件进行拓扑槽位匹配
    std::map<int, bool> tileMatched;
    BoardTileStaticMatch(pPrimaryDlg, pSecDlg, tileMatched);

    // 汇总缺失控件：命名控件用 IDC，无名 GroupBox 用序号，STATIC 用拓扑
    for (int ci = 0; ci < (int)pPrimaryDlg->vecCtrlInfo.size(); ++ci)
    {
        const auto& pCtrl = pPrimaryDlg->vecCtrlInfo[ci];
        if (!pCtrl) continue;

        bool isGB = IsGroupBoxCtrl(pCtrl);
        bool isStatic = IsStaticIDC(pCtrl->nIDC);

        if (isGB && isStatic)
        {
            // 无名 GroupBox → 序号匹配
            if (gbMatched.find(ci) == gbMatched.end())
                outMissingIndices.push_back(ci);
        }
        else if (!isStatic)
        {
            // 非 STATIC（含命名 GroupBox） → IDC 匹配
            if (idcMatched.find(ci) == idcMatched.end())
                outMissingIndices.push_back(ci);
        }
        else
        {
            // 普通 STATIC（非 GroupBox） → 拓扑匹配
            if (tileMatched.find(ci) == tileMatched.end())
                outMissingIndices.push_back(ci);
        }
    }

    // ====================================================================
    // 伴随规则：STATIC 只有在附近存在真正缺失的非 STATIC 控件时才注入
    // "附近"定义：Y 坐标差 ≤ 15 DLU 或位于同一 GroupBox 内
    // 孤立 STATIC（附近无缺失非 STATIC）视为已存在于次语言中
    // ====================================================================
    {
        // 收集缺失非 STATIC 的坐标及所属 GroupBox
        struct NonStaticInfo { CPoint pos; int gbOwner; }; // gbOwner: priBoard GroupBox 索引, -1 = 顶层
        std::vector<NonStaticInfo> missingNonStatics;
        std::set<int> missingNonStaticIndices;

        SDialogBoard priBoard = BuildDialogBoard(pPrimaryDlg);

        // 辅助: 查找控件所属 GroupBox 索引
        auto findGroupOwner = [&](int ctrlIdx) -> int {
            for (int gi = 0; gi < (int)priBoard.groupBoxes.size(); ++gi)
                for (const auto& tile : priBoard.groupBoxes[gi].tiles)
                    if (tile.globalIndex == ctrlIdx) return gi;
            return -1;
        };

        // 构建 GroupBox 组索引 → 控件索引 的映射（按 vecCtrlInfo 出现顺序）
        std::vector<int> gbCtrlIndices;
        for (int ci = 0; ci < (int)pPrimaryDlg->vecCtrlInfo.size(); ++ci)
        {
            const auto& c = pPrimaryDlg->vecCtrlInfo[ci];
            if (c && IsGroupBoxCtrl(c))
                gbCtrlIndices.push_back(ci);
        }

        // 标记哪些 GroupBox 组自身也在缺失列表中
        std::set<int> missingIdxSet(outMissingIndices.begin(), outMissingIndices.end());
        std::set<int> missingGBGroups;
        for (int gi = 0; gi < (int)gbCtrlIndices.size(); ++gi)
        {
            if (missingIdxSet.count(gbCtrlIndices[gi]))
                missingGBGroups.insert(gi);
        }

        for (int ci : outMissingIndices)
        {
            const auto& c = pPrimaryDlg->vecCtrlInfo[ci];
            if (!c) continue;
            // 只有需要 STATIC 标签的控件（Edit、ComboBox 等自身无文本描述能力的）
            // 才算伴随源；Button/CheckBox/RadioButton 等自带文本，不触发 STATIC 伴随
            if (NeedsStaticLabel(c))
            {
                missingNonStaticIndices.insert(ci);
                missingNonStatics.push_back({
                    CPoint(c->ctrlRect.left, c->ctrlRect.top),
                    findGroupOwner(ci)
                });
            }
        }

        // 收集缺失的普通 STATIC 坐标（用于 STATIC 互伴随：多个标题 STATIC 互为伴随）
        struct StaticPosInfo { CPoint pos; int gbOwner; int selfIdx; };
        std::vector<StaticPosInfo> missingStaticPositions;
        for (int ci : outMissingIndices)
        {
            const auto& c = pPrimaryDlg->vecCtrlInfo[ci];
            if (!c) continue;
            if (IsStaticIDC(c->nIDC) && !IsGroupBoxCtrl(c))
                missingStaticPositions.push_back({ CPoint(c->ctrlRect.left, c->ctrlRect.top), findGroupOwner(ci), ci });
        }

        // 过滤: 只保留非 STATIC + 有伴随的 STATIC
        constexpr int kCompanionToleranceDLU = 15;
        std::vector<int> filtered;
        for (int ci : outMissingIndices)
        {
            const auto& c = pPrimaryDlg->vecCtrlInfo[ci];
            if (!c) continue;

            // 非 STATIC 和 GroupBox 直接通过
            if (!IsStaticIDC(c->nIDC) || IsGroupBoxCtrl(c))
            {
                filtered.push_back(ci);
                continue;
            }

            // STATIC: 检查附近是否有缺失的非 STATIC
            int myGB = findGroupOwner(ci);
            CPoint myPos(c->ctrlRect.left, c->ctrlRect.top);
            bool hasCompanion = false;

            // 若所属 GroupBox 自身也在缺失列表中，整组为新增内容，直接保留
            if (myGB >= 0 && missingGBGroups.count(myGB))
            {
                hasCompanion = true;
            }

            if (!hasCompanion)
            {
                for (const auto& ns : missingNonStatics)
                {
                    // 同一 GroupBox 内直接视为伴随
                    if (myGB >= 0 && myGB == ns.gbOwner)
                    {
                        hasCompanion = true;
                        break;
                    }
                    // Y 方向近邻
                    if (abs(myPos.y - ns.pos.y) <= kCompanionToleranceDLU)
                    {
                        hasCompanion = true;
                        break;
                    }
                }
            }

            // STATIC 互伴随：多个缺失 STATIC 聚在一起（栏目标题行）视为互相伴随
            if (!hasCompanion)
            {
                for (const auto& sp : missingStaticPositions)
                {
                    if (sp.selfIdx == ci) continue; // 跳过自身
                    if (myGB >= 0 && myGB == sp.gbOwner) { hasCompanion = true; break; }
                    if (abs(myPos.y - sp.pos.y) <= kCompanionToleranceDLU) { hasCompanion = true; break; }
                }
            }

            if (hasCompanion)
                filtered.push_back(ci);
        }
        outMissingIndices = filtered;
    }
}

CRect CRcFileMerger::ComputeInjectionPosition(
    const std::shared_ptr<SCtrlInfo>& ctrl,
    int ctrlIndex,
    const std::shared_ptr<SDialogInfo>& primaryDlg,
    const std::shared_ptr<SDialogInfo>& secondaryDlg,
    const SDialogBoard& priBoard)
{
    // 默认：原始位置
    CRect result = ctrl->ctrlRect;

    // 找控件所属的 GroupBox
    UINT ownerGroupIDC = 0;
    CRect priGroupRect;
    bool foundInGroup = false;

    for (const auto& grp : priBoard.groupBoxes)
    {
        for (const auto& tile : grp.tiles)
        {
            if (tile.globalIndex == ctrlIndex)
            {
                ownerGroupIDC = grp.groupIDC;
                priGroupRect = grp.groupRect;
                foundInGroup = true;
                break;
            }
        }
        if (foundInGroup) break;
    }

    if (foundInGroup && ownerGroupIDC != 0)
    {
        if (ownerGroupIDC & 0x80000000u)
        {
            // 无名 GroupBox 合成键 — 按序号在次语言中查找对应 GroupBox
            int targetOrdinal = (int)(ownerGroupIDC & 0x7FFFFFFFu);
            int namelessOrd = 0;
            for (const auto& secCtrl : secondaryDlg->vecCtrlInfo)
            {
                if (secCtrl && IsGroupBoxCtrl(secCtrl) && IsStaticIDC(secCtrl->nIDC))
                {
                    if (namelessOrd == targetOrdinal)
                    {
                        int dx = secCtrl->ctrlRect.left - priGroupRect.left;
                        int dy = secCtrl->ctrlRect.top  - priGroupRect.top;
                        int newLeft = ctrl->ctrlRect.left + dx;
                        int newTop  = ctrl->ctrlRect.top  + dy;
                        result = CRect(newLeft, newTop,
                                       newLeft + ctrl->ctrlRect.Width(),
                                       newTop  + ctrl->ctrlRect.Height());
                        return result;
                    }
                    ++namelessOrd;
                }
            }
        }
        else
        {
            // 命名 GroupBox — 按 IDC 在次语言中匹配
            for (const auto& secCtrl : secondaryDlg->vecCtrlInfo)
            {
                if (secCtrl && secCtrl->nIDC == ownerGroupIDC)
                {
                    int dx = secCtrl->ctrlRect.left - priGroupRect.left;
                    int dy = secCtrl->ctrlRect.top  - priGroupRect.top;
                    int newLeft = ctrl->ctrlRect.left + dx;
                    int newTop  = ctrl->ctrlRect.top  + dy;
                    result = CRect(newLeft, newTop,
                                   newLeft + ctrl->ctrlRect.Width(),
                                   newTop  + ctrl->ctrlRect.Height());
                    return result;
                }
            }
        }
    }

    // 顶层控件或 GroupBox 缺失 — 使用对话框比例缩放
    if (primaryDlg && secondaryDlg &&
        primaryDlg->rectDlg.Width() > 0 && primaryDlg->rectDlg.Height() > 0)
    {
        double scaleX = (double)secondaryDlg->rectDlg.Width() / primaryDlg->rectDlg.Width();
        double scaleY = (double)secondaryDlg->rectDlg.Height() / primaryDlg->rectDlg.Height();
        int newLeft = (int)(ctrl->ctrlRect.left * scaleX);
        int newTop  = (int)(ctrl->ctrlRect.top  * scaleY);
        result = CRect(newLeft, newTop,
                       newLeft + ctrl->ctrlRect.Width(),
                       newTop  + ctrl->ctrlRect.Height());
    }

    return result;
}

bool CRcFileMerger::InjectControlIntoBlock(
    const SRcBlock& primaryBlock,
    const SRcFileContent& primaryRc,
    const std::shared_ptr<SCtrlInfo>& ctrl,
    UINT predecessorIDC,
    const SRcBlock& secondaryBlock,
    SRcFileContent& secondaryRc,
    const SResourceHeader& primaryHeader,
    const SResourceHeader& secondaryHeader,
    SMergeResult& result)
{
    int insertAfter = -1;

    // 策略1: 使用 FindControlLineByIDC（结构化匹配）查找前驱控件位置
    if (predecessorIDC != (UINT)-1 && predecessorIDC != 0)
    {
        insertAfter = FindControlLineByIDC(secondaryRc, secondaryBlock, predecessorIDC, secondaryHeader);

        // 边界验证：确保找到的位置在 BEGIN/END 块内部
        if (insertAfter >= 0 &&
            (insertAfter <= secondaryBlock.beginLineIndex || insertAfter >= secondaryBlock.endLineIndex))
        {
            // 找到的位置不在块内部，回退到策略2
            CStringW warnMsg;
            warnMsg.Format(L"FindControlLineByIDC returned line %d outside block [%d, %d] for IDC %u, falling back",
                           insertAfter, secondaryBlock.beginLineIndex, secondaryBlock.endLineIndex, predecessorIDC);
            result.warnings.push_back(warnMsg);
            insertAfter = -1;
        }
    }

    // 策略2: 追加到 END 之前
    if (insertAfter < 0 && secondaryBlock.endLineIndex > 0)
    {
        insertAfter = secondaryBlock.endLineIndex - 1;
    }

    if (insertAfter < 0)
        return false;

    // 生成控件文本行
    // 查找主语言中该控件的原始文本行（保持格式）
    CStringW ctrlLine;
    bool foundOrigLine = false;

    if (ctrl->nIDC != (UINT)-1 && primaryBlock.beginLineIndex >= 0 && primaryBlock.endLineIndex >= 0)
    {
        // 使用结构化匹配在主语言块中查找控件原始行
        int priLine = FindControlLineByIDC(primaryRc, primaryBlock, ctrl->nIDC, primaryHeader);
        if (priLine >= 0)
        {
            ctrlLine = primaryRc.lines[priLine].text;
            foundOrigLine = true;
        }
    }

    if (!foundOrigLine)
    {
        // 生成新行，使用实际的 header 以获得正确的宏名
        ctrlLine = GenerateControlLine(ctrl, primaryHeader, secondaryRc.indentStyle);
    }

    InsertLine(secondaryRc, insertAfter, ctrlLine, ERcLineType::Content, secondaryBlock.blockIndex);

    SInjectionRecord rec;
    rec.type = SInjectionRecord::Control;
    rec.resourceId = 0;
    rec.controlId = ctrl->nIDC;
    rec.insertedAtLine = insertAfter + 1;
    rec.insertedText = ctrlLine;
    result.injections.push_back(rec);

    return true;
}

// ============================================================================
// StringTable 合并
// ============================================================================

int CRcFileMerger::MergeStringTables(
    const SRcFileContent& primaryRc,
    const SResourceHeader& primaryHeader,
    const SSingleResourceInfo& primaryRes,
    SRcFileContent& secondaryRc,
    SResourceHeader& secondaryHeader,
    SSingleResourceInfo& secondaryRes,
    SMergeResult& result)
{
    int injectedCount = 0;

    for (const auto& [blockId, pPrimaryST] : primaryRes.mapLangAndStringTableInfo)
    {
        auto itSec = secondaryRes.mapLangAndStringTableInfo.find(blockId);

        for (const auto& [strId, strText] : pPrimaryST->mapIDAndString)
        {
            if (itSec != secondaryRes.mapLangAndStringTableInfo.end())
            {
                // 次语言有对应块 — 检查该字符串是否缺失
                auto& secST = itSec->second;
                if (secST->mapIDAndString.find(strId) != secST->mapIDAndString.end())
                    continue;

                // 注入到内存模型
                secST->mapIDAndString[strId] = strText;
                SStringTableInfo::SAddedString added(strId, strText);
                secST->vecAddedStrings.push_back(added);
            }
            else
            {
                // 次语言完全缺失此 STRINGTABLE 块 — 创建新块
                auto pNewST = std::make_shared<SStringTableInfo>();
                pNewST->nResourceStringID = blockId;
                pNewST->mapIDAndString[strId] = strText;
                SStringTableInfo::SAddedString added(strId, strText);
                pNewST->vecAddedStrings.push_back(added);
                secondaryRes.mapLangAndStringTableInfo[blockId] = pNewST;
                itSec = secondaryRes.mapLangAndStringTableInfo.find(blockId);
            }

            SInjectionRecord rec;
            rec.type = SInjectionRecord::StringEntry;
            rec.resourceId = strId;
            rec.insertedText.Format(L"%u \"%s\"", strId, (LPCWSTR)strText);
            result.injections.push_back(rec);
            ++injectedCount;
        }
    }

    return injectedCount;
}

// ============================================================================
// Menu 合并
// ============================================================================

static int MergeMenuItemsRecursive(
    const std::shared_ptr<SMenuInfo>& primaryMenu,
    std::shared_ptr<SMenuInfo>& secondaryMenu)
{
    int count = 0;

    for (const auto& pPriItem : primaryMenu->vecSubMenu)
    {
        // 在次语言中查找对应项
        bool found = false;
        for (auto& pSecItem : secondaryMenu->vecSubMenu)
        {
            if (pPriItem->nID == pSecItem->nID && pPriItem->bIsPopup == pSecItem->bIsPopup)
            {
                // 如果是 popup，递归合并子菜单
                if (pPriItem->bIsPopup)
                {
                    count += MergeMenuItemsRecursive(pPriItem, pSecItem);
                }
                found = true;
                break;
            }
        }

        if (!found)
        {
            // 注入菜单项
            auto pNew = std::make_shared<SMenuInfo>(*pPriItem);
            pNew->m_blIsAdded = true;
            secondaryMenu->vecSubMenu.push_back(pNew);
            secondaryMenu->vecAddedMenuItems.push_back(pNew);
            ++count;
        }
    }

    return count;
}

int CRcFileMerger::MergeMenus(
    const SRcFileContent& primaryRc,
    const SResourceHeader& primaryHeader,
    const SSingleResourceInfo& primaryRes,
    SRcFileContent& secondaryRc,
    SResourceHeader& secondaryHeader,
    SSingleResourceInfo& secondaryRes,
    SMergeResult& result)
{
    int injectedCount = 0;

    for (const auto& [menuId, pPrimaryMenu] : primaryRes.mapLangAndMenuInfo)
    {
        auto itSec = secondaryRes.mapLangAndMenuInfo.find(menuId);
        if (itSec == secondaryRes.mapLangAndMenuInfo.end())
        {
            // 整块克隆
            auto pClone = DeepCloneMenuInfo(pPrimaryMenu);
            secondaryRes.mapLangAndMenuInfo[menuId] = pClone;

            SInjectionRecord rec;
            rec.type = SInjectionRecord::WholeBlock;
            rec.resourceId = menuId;
            rec.description.Format(L"Entire Menu %u missing in secondary", menuId);
            result.injections.push_back(rec);
            ++result.totalInjectedBlocks;
            continue;
        }

        injectedCount += MergeMenuItemsRecursive(pPrimaryMenu, itSec->second);
    }

    return injectedCount;
}

// ============================================================================
// resource.h 宏合并
// ============================================================================

int CRcFileMerger::MergeDefines(
    const SResourceHeader& primaryHeader,
    SResourceHeader& secondaryHeader,
    SMergeResult& result)
{
    int injectedCount = 0;

    for (const auto& pDef : primaryHeader.defines)
    {
        if (secondaryHeader.nameToId.find(pDef.name) != secondaryHeader.nameToId.end())
            continue;

        // 检查值是否冲突
        UINT value = pDef.value;
        if (secondaryHeader.idToName.find(value) != secondaryHeader.idToName.end())
        {
            // 值已被占用 → 分配新值
            CStringW prefix = pDef.prefix.IsEmpty() ? L"IDC_" : pDef.prefix;
            value = secondaryHeader.GetNextAvailableId(prefix);
            CStringW warnMsg;
            warnMsg.Format(L"Reassigned %s from %u to %u (value conflict)",
                           (LPCWSTR)pDef.name, pDef.value, value);
            result.warnings.push_back(warnMsg);
        }

        // 找插入位置
        int insertPos = FindDefineInsertionPoint(secondaryHeader, pDef.prefix, value);
        InsertDefineLine(secondaryHeader, insertPos, pDef.name, value);

        SInjectionRecord rec;
        rec.type = SInjectionRecord::DefineEntry;
        rec.resourceId = value;
        rec.insertedText.Format(L"#define %s %u", (LPCWSTR)pDef.name, value);
        rec.insertedAtLine = insertPos + 1;
        result.injections.push_back(rec);
        ++injectedCount;
    }

    return injectedCount;
}

// ============================================================================
// 辅助函数
// ============================================================================

UINT CRcFileMerger::FindPredecessorIDC(
    const std::shared_ptr<SDialogInfo>& primaryDlg, int ctrlIndex) const
{
    if (ctrlIndex <= 0)
        return (UINT)-1;

    // 从当前位置向前找第一个有效 IDC 的控件
    for (int i = ctrlIndex - 1; i >= 0; --i)
    {
        UINT idc = primaryDlg->vecCtrlInfo[i]->nIDC;
        if (idc != (UINT)-1 && idc != 0)
            return idc;
    }
    return (UINT)-1;
}

int CRcFileMerger::FindControlLineByIDC(
    const SRcFileContent& rc,
    const SRcBlock& block,
    UINT idcValue,
    const SResourceHeader& header) const
{
    if (block.beginLineIndex < 0 || block.endLineIndex < 0)
        return -1;

    // 查找 IDC 的宏名
    CStringW idcName;
    auto itName = header.idToName.find(idcValue);
    if (itName != header.idToName.end())
        idcName = itName->second;

    CStringW idcNumStr;
    idcNumStr.Format(L"%u", idcValue);

    // 辅助 lambda：判断字符是否为标识符字符（字母/数字/下划线）
    auto isIdentChar = [](wchar_t ch) -> bool {
        return iswalnum(ch) || ch == L'_';
    };

    // 辅助 lambda：在行文本中做单词边界匹配（非子串匹配）
    // 确保 token 前后不是标识符字符，避免 "100" 匹配 "1001" 或坐标中的数字
    auto findWholeToken = [&isIdentChar](const CStringW& lineText, const CStringW& token) -> bool {
        if (token.IsEmpty()) return false;
        int tokenLen = token.GetLength();
        int startPos = 0;
        while (true)
        {
            int pos = lineText.Find(token, startPos);
            if (pos < 0) return false;

            // 检查左边界：pos == 0 或前一个字符不是标识符字符
            bool leftOk = (pos == 0) || !isIdentChar(lineText[pos - 1]);
            // 检查右边界：token 末尾是行尾 或后一个字符不是标识符字符
            int afterPos = pos + tokenLen;
            bool rightOk = (afterPos >= lineText.GetLength()) || !isIdentChar(lineText[afterPos]);

            if (leftOk && rightOk)
                return true;

            // 继续搜索下一个出现位置
            startPos = pos + 1;
        }
    };

    // 辅助 lambda：判断 trimmed 行是否以 RC 控件关键词开头
    // （排除坐标行、注释行等误匹配）
    auto isControlLine = [](const CStringW& lineText) -> bool {
        CStringW t = lineText; t.TrimLeft();
        if (t.IsEmpty()) return false;
        CStringW upper = t; upper.MakeUpper();
        // RC 控件语句关键词列表
        static const wchar_t* keywords[] = {
            L"LTEXT", L"RTEXT", L"CTEXT", L"PUSHBUTTON", L"DEFPUSHBUTTON",
            L"GROUPBOX", L"CHECKBOX", L"AUTOCHECKBOX", L"RADIOBUTTON",
            L"AUTORADIOBUTTON", L"COMBOBOX", L"EDITTEXT", L"LISTBOX",
            L"SCROLLBAR", L"CONTROL", L"ICON", nullptr
        };
        for (int k = 0; keywords[k]; ++k)
        {
            int kwLen = (int)wcslen(keywords[k]);
            if (upper.GetLength() >= kwLen &&
                upper.Left(kwLen) == keywords[k])
            {
                // 关键词后必须是空白、制表符或行尾
                if (upper.GetLength() == kwLen)
                    return true;
                wchar_t next = upper[kwLen];
                if (next == L' ' || next == L'\t')
                    return true;
            }
        }
        return false;
    };

    for (int i = block.beginLineIndex + 1; i < block.endLineIndex; ++i)
    {
        const CStringW& lineText = rc.lines[i].text;

        // 必须是控件定义行（以控件关键词开头），排除坐标、注释等误匹配
        if (!isControlLine(lineText))
            continue;

        // 使用单词边界匹配：先匹配宏名（更精确），再匹配数值
        if (!idcName.IsEmpty() && findWholeToken(lineText, idcName))
            return i;
        if (findWholeToken(lineText, idcNumStr))
            return i;
    }

    return -1;
}

CStringW CRcFileMerger::GenerateControlLine(
    const std::shared_ptr<SCtrlInfo>& ctrl,
    const SResourceHeader& header,
    const SIndentStyle& style) const
{
    CStringW indent = style.useTabs ? L"\t" : CStringW(L' ', style.indentWidth);

    // 获取 IDC 名称
    CStringW idcName;
    auto it = header.idToName.find(ctrl->nIDC);
    if (it != header.idToName.end())
        idcName = it->second;
    else if (ctrl->nIDC == (UINT)-1)
        idcName = L"IDC_STATIC";
    else
        idcName.Format(L"%u", ctrl->nIDC);

    int cx = ctrl->ctrlRect.Width();
    int cy = ctrl->ctrlRect.Height();

    CStringW className = ctrl->csClassName;
    className.MakeUpper();

    // 简写语法优先
    if (className == L"STATIC")
    {
        CStringW line;
        line.Format(L"%sLTEXT           \"%s\",%s,%d,%d,%d,%d",
                    (LPCWSTR)indent,
                    (LPCWSTR)ctrl->csText, (LPCWSTR)idcName,
                    ctrl->ctrlRect.left, ctrl->ctrlRect.top, cx, cy);
        return line;
    }
    if (className == L"BUTTON")
    {
        CStringW line;
        line.Format(L"%sPUSHBUTTON      \"%s\",%s,%d,%d,%d,%d",
                    (LPCWSTR)indent,
                    (LPCWSTR)ctrl->csText, (LPCWSTR)idcName,
                    ctrl->ctrlRect.left, ctrl->ctrlRect.top, cx, cy);
        return line;
    }
    if (className == L"EDIT")
    {
        CStringW line;
        line.Format(L"%sEDITTEXT        %s,%d,%d,%d,%d",
                    (LPCWSTR)indent,
                    (LPCWSTR)idcName,
                    ctrl->ctrlRect.left, ctrl->ctrlRect.top, cx, cy);
        return line;
    }

    // 通用 CONTROL 语法
    CStringW line;
    line.Format(L"%sCONTROL         \"%s\",%s,\"%s\",0x%08X,%d,%d,%d,%d",
                (LPCWSTR)indent,
                (LPCWSTR)ctrl->csText, (LPCWSTR)idcName, (LPCWSTR)ctrl->csClassName,
                ctrl->dwStyle,
                ctrl->ctrlRect.left, ctrl->ctrlRect.top, cx, cy);
    return line;
}

CStringW CRcFileMerger::GenerateStringEntryLine(
    UINT stringId,
    const CStringW& text,
    const SResourceHeader& header,
    const SIndentStyle& style) const
{
    CStringW indent = style.useTabs ? L"\t" : CStringW(L' ', style.indentWidth);

    CStringW idName;
    auto it = header.idToName.find(stringId);
    if (it != header.idToName.end())
        idName = it->second;
    else
        idName.Format(L"%u", stringId);

    CStringW line;
    line.Format(L"%s%s \"%s\"", (LPCWSTR)indent, (LPCWSTR)idName, (LPCWSTR)text);
    return line;
}

CStringW CRcFileMerger::GenerateDefineLine(const CStringW& name, UINT value) const
{
    CStringW line;
    // 对齐到 tab 列（模仿 Visual Studio 风格）
    int padding = 40 - 8 - name.GetLength(); // #define + name
    if (padding < 1) padding = 1;
    CStringW spaces(L' ', padding);
    line.Format(L"#define %s%s%u", (LPCWSTR)name, (LPCWSTR)spaces, value);
    return line;
}

int CRcFileMerger::FindDefineInsertionPoint(
    const SResourceHeader& header,
    const CStringW& prefix,
    UINT value) const
{
    int lastSamePrefixLine = -1;
    int lastDefLine = -1;

    for (const auto& def : header.defines)
    {
        if (def.lineIndex >= 0)
            lastDefLine = def.lineIndex;

        if (!prefix.IsEmpty() && def.prefix.CompareNoCase(prefix) == 0)
        {
            if (def.value <= value)
                lastSamePrefixLine = def.lineIndex;
        }
    }

    // 优先插入到同前缀区间
    if (lastSamePrefixLine >= 0)
        return lastSamePrefixLine;

    // 否则追加到最后一个 #define 之后
    if (lastDefLine >= 0)
        return lastDefLine;

    // 真的没有 #define → 文件末尾
    return (int)header.rawLines.size() - 1;
}

void CRcFileMerger::InsertLine(SRcFileContent& content, int afterLineIndex,
                                const CStringW& text, ERcLineType lineType, int blockIndex)
{
    SRcLine newLine;
    newLine.text = text;
    newLine.lineNumber = afterLineIndex + 2; // 近似行号
    newLine.blockIndex = blockIndex;
    newLine.type = lineType;

    if (afterLineIndex + 1 <= (int)content.lines.size())
        content.lines.insert(content.lines.begin() + afterLineIndex + 1, newLine);
    else
        content.lines.push_back(newLine);

    // 更新后续行号（近似）
    for (int i = afterLineIndex + 2; i < (int)content.lines.size(); ++i)
    {
        content.lines[i].lineNumber = i + 1;
    }

    // 更新块的 endLineIndex（如果在块内插入）
    for (auto& block : content.blocks)
    {
        if (block.beginLineIndex > afterLineIndex)
            ++block.beginLineIndex;
        if (block.endLineIndex > afterLineIndex)
            ++block.endLineIndex;
        if (block.headerLineIndex > afterLineIndex)
            ++block.headerLineIndex;
        for (auto& ai : block.attributeLineIndices)
        {
            if (ai > afterLineIndex) ++ai;
        }
    }
}

void CRcFileMerger::InsertDefineLine(SResourceHeader& header, int afterLineIndex,
                                      const CStringW& name, UINT value)
{
    CStringW lineText = GenerateDefineLine(name, value);

    SRcLine newLine;
    newLine.text = lineText;
    newLine.lineNumber = afterLineIndex + 2;
    newLine.blockIndex = -1;
    newLine.type = ERcLineType::Preprocessor;

    if (afterLineIndex + 1 <= (int)header.rawLines.size())
        header.rawLines.insert(header.rawLines.begin() + afterLineIndex + 1, newLine);
    else
        header.rawLines.push_back(newLine);

    // 更新 defines 中的 lineIndex
    for (auto& def : header.defines)
    {
        if (def.lineIndex > afterLineIndex)
            ++def.lineIndex;
    }

    // 添加新条目
    SDefineEntry entry;
    entry.name = name;
    entry.value = value;
    entry.lineIndex = afterLineIndex + 1;
    entry.prefix = name;
    int underscorePos = name.Find(L'_');
    if (underscorePos > 0)
        entry.prefix = name.Left(underscorePos + 1);

    header.defines.push_back(entry);
    header.nameToId[name] = value;
    header.idToName[value] = name;
    if (value > header.maxUsedId)
        header.maxUsedId = value;
}

CStringW CRcFileMerger::LookupIdName(UINT id, const SResourceHeader& header) const
{
    auto it = header.idToName.find(id);
    if (it != header.idToName.end())
        return it->second;
    CStringW s;
    s.Format(L"%u", id);
    return s;
}

} // namespace rceditor
