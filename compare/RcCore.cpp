#include "stdafx.h"
#include "RcCore.h"

#include <sstream>
#include <algorithm>
#include <cmath>
#include <set>


namespace rceditor
{
    // 在 RcCore.h 中声明的符号的定义
    bool gbIsLevelAdmin = false;               //!< 当前用户是否为等级管理器管理员
    unsigned short gbCurNeedShowLan = 1;       //!< 等级管理器使用的当前界面语言索引
	std::shared_ptr<SCtrlInfo> DeepCloneCtrlInfo(const std::shared_ptr<SCtrlInfo>& src)
	{
		if (!src)
			return nullptr;
		return std::make_shared<SCtrlInfo>(*src);
	}

// 全局命名空间中不存在 `gbCurNeedShowLan` 的独立符号。代码应直接使用
// `rceditor::gbCurNeedShowLan`（或包含 LevelLocalize.h，它会将旧的未限定用法
// 映射到命名空间中的符号）。

	std::shared_ptr<SDialogInfo> DeepCloneDialogInfo(const std::shared_ptr<SDialogInfo>& src)
	{
		if (!src)
			return nullptr;

		auto dst = std::make_shared<SDialogInfo>(*src);

		// 解除可选对话框字体的别名引用，确保深拷贝独立
		if (src->pFontInfo)
			dst->pFontInfo = std::make_shared<SFontInfo>(*src->pFontInfo);
		else
			dst->pFontInfo = nullptr;

        // 解除控件的别名引用，确保每个控件都有独立拷贝。
		dst->vecCtrlInfo.clear();
		dst->vecCtrlInfo.reserve(src->vecCtrlInfo.size());
		for (const auto& c : src->vecCtrlInfo)
			dst->vecCtrlInfo.push_back(DeepCloneCtrlInfo(c));

		// 保持 vecAddedCtrlInfo 与 vecCtrlInfo 的别名关系：
		// 若 added 控件来源于 vecCtrlInfo，则指向已克隆的 vecCtrlInfo 项。
		dst->vecAddedCtrlInfo.clear();
		dst->vecAddedCtrlInfo.reserve(src->vecAddedCtrlInfo.size());
		for (const auto& addedCtrl : src->vecAddedCtrlInfo)
		{
			if (!addedCtrl)
			{
				dst->vecAddedCtrlInfo.push_back(nullptr);
				continue;
			}

			int idxInAll = -1;
			for (int i = 0; i < (int)src->vecCtrlInfo.size(); ++i)
			{
				if (src->vecCtrlInfo[i] == addedCtrl)
				{
					idxInAll = i;
					break;
				}
			}

			if (idxInAll >= 0 && idxInAll < (int)dst->vecCtrlInfo.size())
				dst->vecAddedCtrlInfo.push_back(dst->vecCtrlInfo[idxInAll]);
			else
				dst->vecAddedCtrlInfo.push_back(DeepCloneCtrlInfo(addedCtrl));
		}

	return dst;
	}

	std::shared_ptr<SMenuInfo> DeepCloneMenuInfo(const std::shared_ptr<SMenuInfo>& src)
	{
		if (!src)
			return nullptr;

		auto dst = std::make_shared<SMenuInfo>(*src);
		dst->vecSubMenu.clear();
		dst->vecSubMenu.reserve(src->vecSubMenu.size());
		for (const auto& sm : src->vecSubMenu)
			dst->vecSubMenu.push_back(DeepCloneMenuInfo(sm));
		
		// 深拷贝新增菜单项列表
		dst->vecAddedMenuItems.clear();
		dst->vecAddedMenuItems.reserve(src->vecAddedMenuItems.size());
		for (const auto& am : src->vecAddedMenuItems)
			dst->vecAddedMenuItems.push_back(DeepCloneMenuInfo(am));
		return dst;
	}

	SSingleResourceInfo DeepCloneResourceInfo(const SSingleResourceInfo& src)
	{
		SSingleResourceInfo dst;
		dst.langID = src.langID;

		for (const auto& kv : src.mapLangAndDialogInfo)
			dst.mapLangAndDialogInfo[kv.first] = DeepCloneDialogInfo(kv.second);

    for (const auto& kv : src.mapLangAndStringTableInfo)
    {
        if (!kv.second)
            continue;
        auto p = std::make_shared<SStringTableInfo>(*kv.second);
        // 复制已新增字符串的列表
        p->vecAddedStrings = kv.second->vecAddedStrings;
        dst.mapLangAndStringTableInfo[kv.first] = p;
    }

		dst.pVersionInfo = src.pVersionInfo ? std::make_shared<SVersionInfo>(*src.pVersionInfo) : nullptr;

		for (const auto& kv : src.mapLangAndMenuInfo)
			dst.mapLangAndMenuInfo[kv.first] = DeepCloneMenuInfo(kv.second);

		for (const auto& kv : src.mapLangAndToolbarInfo)
		{
			if (!kv.second)
				continue;
			dst.mapLangAndToolbarInfo[kv.first] = std::make_shared<SToolbarInfo>(*kv.second);
		}

		return dst;
	}

	// =========================================================================
	// 控件棋盘（Board-Tile）— 拓扑映射构建与匹配
	// =========================================================================

	// IsStaticIDC 已移至 RcCore.h

	SDialogBoard BuildDialogBoard(const std::shared_ptr<SDialogInfo>& pDlg)
	{
		SDialogBoard board;
		if (!pDlg) return board;

		const auto& ctrls = pDlg->vecCtrlInfo;
		board.topLevel.groupIDC = 0;
		board.topLevel.groupRect = pDlg->rectDlg;

		// Step 1: 识别所有 GroupBox，收集其 IDC 和矩形
		// 无名 GroupBox（IDC 为 STATIC 类）使用合成键 0x80000000|K 避免哈希冲突
		struct GBInfo { int ctrlIdx; UINT gbKey; CRect rect; int area; };
		std::vector<GBInfo> gbList;
		int namelessGBOrdinal = 0;
		for (int i = 0; i < (int)ctrls.size(); ++i)
		{
			const auto& c = ctrls[i];
			if (!c) continue;
			if (IsGroupBoxCtrl(c))
			{
				UINT gbKey = c->nIDC;
				if (IsStaticIDC(gbKey))
					gbKey = 0x80000000u | (UINT)(namelessGBOrdinal++);

				int area = c->ctrlRect.Width() * c->ctrlRect.Height();
				gbList.push_back({ i, gbKey, c->ctrlRect, area });

				STileGroup grp;
				grp.groupIDC = gbKey;
				grp.groupRect = c->ctrlRect;
				int gbIdx = (int)board.groupBoxes.size();
				board.groupBoxes.push_back(grp);
				board.groupIDCToIndex[gbKey] = gbIdx;
			}
		}

		// Step 2: 将每个控件分配到所属 GroupBox（几何包含，最小面积取最内层）
		for (int i = 0; i < (int)ctrls.size(); ++i)
		{
			const auto& c = ctrls[i];
			if (!c) continue;

			SBoardTile tile;
			tile.globalIndex = i;
			tile.isGroupBox = IsGroupBoxCtrl(c);
			tile.isAnchor = !IsStaticIDC(c->nIDC) && !tile.isGroupBox;

			// GroupBox 控件自身归入顶层组（它们是"边界"而非"内容"）
			if (tile.isGroupBox)
			{
				tile.localStaticOrdinal = -1;
				board.topLevel.tiles.push_back(tile);
				continue;
			}

			// 找包含此控件的最内层 GroupBox
			CPoint topLeft(c->ctrlRect.left, c->ctrlRect.top);
			int bestGB = -1;
			int bestArea = INT_MAX;
			for (int g = 0; g < (int)gbList.size(); ++g)
			{
				const CRect& gbR = gbList[g].rect;
				// 控件 top-left 在 GroupBox 内部（且非 GroupBox 自身）
				if (gbList[g].ctrlIdx == i) continue;
				if (topLeft.x >= gbR.left && topLeft.x < gbR.right &&
				    topLeft.y >= gbR.top && topLeft.y < gbR.bottom)
				{
					if (gbList[g].area < bestArea)
					{
						bestArea = gbList[g].area;
						bestGB = g;
					}
				}
			}

			if (bestGB >= 0)
			{
				// 归入 GroupBox 组
				auto& grp = board.groupBoxes[bestGB];
				if (IsStaticIDC(c->nIDC))
					tile.localStaticOrdinal = grp.staticCount++;
				board.groupBoxes[bestGB].tiles.push_back(tile);
			}
			else
			{
				// 归入顶层组
				if (IsStaticIDC(c->nIDC))
					tile.localStaticOrdinal = board.topLevel.staticCount++;
				board.topLevel.tiles.push_back(tile);
			}
		}

		// Step 3: 为每个组构建拓扑网格（Y 聚类 → X 排序 → topoRow / topoCol）
		BuildTopoGrid(board.topLevel, ctrls);
		for (auto& grp : board.groupBoxes)
			BuildTopoGrid(grp, ctrls);

		return board;
	}

	// =========================================================================
	// 拓扑网格构建：Y 聚类 → X 排序 → 分配 topoRow / topoCol
	// =========================================================================

	void BuildTopoGrid(STileGroup& grp,
		const std::vector<std::shared_ptr<SCtrlInfo>>& ctrls)
	{
		grp.topoRows.clear();
		if (grp.tiles.empty()) return;

		// 收集 (tileIndex, Y, X)，按 Y 排序后聚类
		struct TileSortEntry { int tileIdx; int y; int x; };
		std::vector<TileSortEntry> entries;
		entries.reserve(grp.tiles.size());
		for (int ti = 0; ti < (int)grp.tiles.size(); ++ti)
		{
			const auto& tile = grp.tiles[ti];
			if (tile.globalIndex < 0 || tile.globalIndex >= (int)ctrls.size()) continue;
			const auto& c = ctrls[tile.globalIndex];
			if (!c) continue;
			entries.push_back({ ti, c->ctrlRect.top, c->ctrlRect.left });
		}

		// 按 Y 排序
		std::sort(entries.begin(), entries.end(),
			[](const TileSortEntry& a, const TileSortEntry& b) {
				return a.y < b.y || (a.y == b.y && a.x < b.x);
			});

		// Y 聚类：相邻 ≤ kRowClusterToleranceDLU → 同一行
		for (int i = 0; i < (int)entries.size(); )
		{
			STopoRowInfo row;
			row.representativeY = entries[i].y;
			int j = i;
			while (j < (int)entries.size() &&
				   abs(entries[j].y - row.representativeY) <= kRowClusterToleranceDLU)
			{
				row.tileIndices.push_back(entries[j].tileIdx);
				++j;
			}

			// 行内按 X 排序
			std::sort(row.tileIndices.begin(), row.tileIndices.end(),
				[&](int a, int b) {
					const auto& ca = ctrls[grp.tiles[a].globalIndex];
					const auto& cb = ctrls[grp.tiles[b].globalIndex];
					return ca->ctrlRect.left < cb->ctrlRect.left;
				});

			int rowIdx = (int)grp.topoRows.size();

			// 为行内每个 tile 分配 topoRow；STATIC 分配连续 topoCol
			int staticCol = 0;
			for (int idx : row.tileIndices)
			{
				auto& tile = grp.tiles[idx];
				tile.topoRow = rowIdx;
				if (tile.localStaticOrdinal >= 0)
					tile.topoCol = staticCol++;
				else
					tile.topoCol = -1;
			}

			grp.topoRows.push_back(std::move(row));
			i = j;
		}
	}
	// =========================================================================
	// 拓扑行对齐 + 基于拓扑网格的 STATIC 匹配
	// =========================================================================

	std::map<int, int> AlignTopoRows(
		const STileGroup& priGrp,
		const STileGroup& secGrp,
		const std::vector<std::shared_ptr<SCtrlInfo>>& priCtrls,
		const std::vector<std::shared_ptr<SCtrlInfo>>& secCtrls)
	{
		std::map<int, int> rowMap; // priRowIdx → secRowIdx

		// Phase A: Anchor 对齐 — 主行中 anchor IDC 与次行中 anchor IDC 的交集
		// 收集每行的 anchor IDC 集合
		auto collectRowAnchors = [](const STileGroup& grp, const std::vector<std::shared_ptr<SCtrlInfo>>& ctrls)
		{
			// rowIdx → set<IDC>
			std::vector<std::set<UINT>> result(grp.topoRows.size());
			for (int r = 0; r < (int)grp.topoRows.size(); ++r)
			{
				for (int ti : grp.topoRows[r].tileIndices)
				{
					const auto& tile = grp.tiles[ti];
					if (tile.isAnchor && tile.globalIndex >= 0 &&
						tile.globalIndex < (int)ctrls.size() && ctrls[tile.globalIndex])
					{
						result[r].insert(ctrls[tile.globalIndex]->nIDC);
					}
				}
			}
			return result;
		};

		auto priAnchors = collectRowAnchors(priGrp, priCtrls);
		auto secAnchors = collectRowAnchors(secGrp, secCtrls);

		std::set<int> priUsed, secUsed;
		for (int pr = 0; pr < (int)priAnchors.size(); ++pr)
		{
			if (priAnchors[pr].empty()) continue;
			for (int sr = 0; sr < (int)secAnchors.size(); ++sr)
			{
				if (secUsed.count(sr)) continue;
				// 检查是否有共同 anchor IDC
				bool hasCommon = false;
				for (UINT idc : priAnchors[pr])
				{
					if (secAnchors[sr].count(idc)) { hasCommon = true; break; }
				}
				if (hasCommon)
				{
					rowMap[pr] = sr;
					priUsed.insert(pr);
					secUsed.insert(sr);
					break;
				}
			}
		}

		// Phase B: 序号回退 — 未匹配行按出现顺序 1:1
		std::vector<int> priRemain, secRemain;
		for (int r = 0; r < (int)priGrp.topoRows.size(); ++r)
			if (!priUsed.count(r)) priRemain.push_back(r);
		for (int r = 0; r < (int)secGrp.topoRows.size(); ++r)
			if (!secUsed.count(r)) secRemain.push_back(r);

		int pairCount = (int)(std::min)(priRemain.size(), secRemain.size());
		for (int k = 0; k < pairCount; ++k)
			rowMap[priRemain[k]] = secRemain[k];

		return rowMap;
	}

	int MatchGroupByTopology(
		const STileGroup& priGrp,
		const STileGroup& secGrp,
		const std::vector<std::shared_ptr<SCtrlInfo>>& priCtrls,
		const std::vector<std::shared_ptr<SCtrlInfo>>& secCtrls,
		std::map<int, bool>& outMatched,
		std::set<int>* outSecConsumed)
	{
		auto rowMap = AlignTopoRows(priGrp, secGrp, priCtrls, secCtrls);

		int matched = 0;

		// 跟踪次语言中哪些 STATIC 已被拓扑配对消费
		std::set<int> secStaticGlobalUsed;

		// Phase 1: 拓扑行配对 — 对齐行内按 topoCol 1:1
		for (const auto& rp : rowMap)
		{
			int priRow = rp.first;
			int secRow = rp.second;
			if (priRow < 0 || priRow >= (int)priGrp.topoRows.size()) continue;
			if (secRow < 0 || secRow >= (int)secGrp.topoRows.size()) continue;

			// 收集该行中的 STATIC tile（已按 topoCol 排列）
			std::vector<const SBoardTile*> priST, secST;
			for (int ti : priGrp.topoRows[priRow].tileIndices)
			{
				const auto& tile = priGrp.tiles[ti];
				if (tile.localStaticOrdinal >= 0) priST.push_back(&tile);
			}
			for (int ti : secGrp.topoRows[secRow].tileIndices)
			{
				const auto& tile = secGrp.tiles[ti];
				if (tile.localStaticOrdinal >= 0) secST.push_back(&tile);
			}

			// 按 topoCol 1:1 配对
			int pairs = (int)(std::min)(priST.size(), secST.size());
			for (int k = 0; k < pairs; ++k)
			{
				outMatched[priST[k]->globalIndex] = true;
				secStaticGlobalUsed.insert(secST[k]->globalIndex);
				if (outSecConsumed) outSecConsumed->insert(secST[k]->globalIndex);
				++matched;
			}
		}

		// Phase 2: 平坦序号回退 — 拓扑未匹配的主语言 STATIC ↔ 次语言未消费的 STATIC
		// 按组内原始出现顺序（localStaticOrdinal）收集剩余项
		std::vector<const SBoardTile*> priRemain, secRemain;
		for (const auto& t : priGrp.tiles)
		{
			if (t.localStaticOrdinal >= 0 &&
				outMatched.find(t.globalIndex) == outMatched.end())
				priRemain.push_back(&t);
		}
		for (const auto& t : secGrp.tiles)
		{
			if (t.localStaticOrdinal >= 0 &&
				secStaticGlobalUsed.find(t.globalIndex) == secStaticGlobalUsed.end())
				secRemain.push_back(&t);
		}

		int fallbackPairs = (int)(std::min)(priRemain.size(), secRemain.size());
		for (int k = 0; k < fallbackPairs; ++k)
		{
			outMatched[priRemain[k]->globalIndex] = true;
			if (outSecConsumed) outSecConsumed->insert(secRemain[k]->globalIndex);
			++matched;
		}

		return matched;
	}

	int BoardTileStaticMatch(
		const std::shared_ptr<SDialogInfo>& primaryDlg,
		const std::shared_ptr<SDialogInfo>& secondaryDlg,
		std::map<int, bool>& outMatched)
	{
		if (!primaryDlg || !secondaryDlg)
			return 0;

		SDialogBoard priBoard = BuildDialogBoard(primaryDlg);
		SDialogBoard secBoard = BuildDialogBoard(secondaryDlg);

		int matchCount = 0;
		std::set<int> secConsumed; // 全局跟踪已消费的次语言 STATIC globalIndex

		// 匹配各 GroupBox 组（按 IDC 对应——含无名 GroupBox 的合成键）
		for (int gi = 0; gi < (int)priBoard.groupBoxes.size(); ++gi)
		{
			const auto& priGrp = priBoard.groupBoxes[gi];
			auto itSec = secBoard.groupIDCToIndex.find(priGrp.groupIDC);
			if (itSec == secBoard.groupIDCToIndex.end())
				continue; // 次语言中缺失此 GroupBox → 其 STATIC 留给对话框级回退

			matchCount += MatchGroupByTopology(
				priGrp, secBoard.groupBoxes[itSec->second],
				primaryDlg->vecCtrlInfo, secondaryDlg->vecCtrlInfo, outMatched, &secConsumed);
		}

		// 匹配顶层组
		matchCount += MatchGroupByTopology(
			priBoard.topLevel, secBoard.topLevel,
			primaryDlg->vecCtrlInfo, secondaryDlg->vecCtrlInfo, outMatched, &secConsumed);

		// =================================================================
		// 对话框级安全网：坐标近邻匹配 + 平坦序号回退
		// 收集所有仍未匹配的主语言 STATIC（排除 GroupBox）
		// → 与次语言未消费的 STATIC 按坐标距离配对
		// → 剩余部分退化为按序号 1:1 配对
		// 解决：GroupBox 缺失、跨组迁移、合成键错位 等导致的遗漏
		// =================================================================
		{
			// 收集主语言未匹配的非 GroupBox STATIC
			std::vector<int> priUnmatched;
			for (int ci = 0; ci < (int)primaryDlg->vecCtrlInfo.size(); ++ci)
			{
				const auto& c = primaryDlg->vecCtrlInfo[ci];
				if (!c || !IsStaticIDC(c->nIDC)) continue;
				if (IsGroupBoxCtrl(c)) continue;
				if (outMatched.find(ci) == outMatched.end())
					priUnmatched.push_back(ci);
			}

			// 收集次语言未消费的非 GroupBox STATIC（精确追踪）
			std::vector<int> secUnmatched;
			for (int ci = 0; ci < (int)secondaryDlg->vecCtrlInfo.size(); ++ci)
			{
				const auto& c = secondaryDlg->vecCtrlInfo[ci];
				if (!c || !IsStaticIDC(c->nIDC)) continue;
				if (IsGroupBoxCtrl(c)) continue;
				if (secConsumed.find(ci) == secConsumed.end())
					secUnmatched.push_back(ci);
			}

			// Phase A: 坐标近邻配对（≤30 DLU 平方距离）
			constexpr int kSafetyNetTolDLU2 = 30 * 30;
			std::set<int> secUsedInNet; // secUnmatched 中的局部索引
			for (size_t pi = 0; pi < priUnmatched.size(); ++pi)
			{
				if (outMatched.find(priUnmatched[pi]) != outMatched.end()) continue;
				const auto& pc = primaryDlg->vecCtrlInfo[priUnmatched[pi]];
				int bestLocal = -1;
				int bestDist = INT_MAX;
				for (int si = 0; si < (int)secUnmatched.size(); ++si)
				{
					if (secUsedInNet.count(si)) continue;
					const auto& sc = secondaryDlg->vecCtrlInfo[secUnmatched[si]];
					int dx = pc->ctrlRect.left - sc->ctrlRect.left;
					int dy = pc->ctrlRect.top  - sc->ctrlRect.top;
					int dist = dx * dx + dy * dy;
					if (dist < bestDist) { bestDist = dist; bestLocal = si; }
				}
				if (bestLocal >= 0 && bestDist <= kSafetyNetTolDLU2)
				{
					outMatched[priUnmatched[pi]] = true;
					secUsedInNet.insert(bestLocal);
					++matchCount;
				}
			}

			// Phase B: 平坦序号回退 — 坐标匹配后的剩余项按出现顺序 1:1
			std::vector<int> priStill, secStill;
			for (int pi : priUnmatched)
				if (outMatched.find(pi) == outMatched.end())
					priStill.push_back(pi);
			for (int si = 0; si < (int)secUnmatched.size(); ++si)
				if (!secUsedInNet.count(si))
					secStill.push_back(si);

			int finalPairs = (int)(std::min)(priStill.size(), secStill.size());
			for (int k = 0; k < finalPairs; ++k)
			{
				outMatched[priStill[k]] = true;
				++matchCount;
			}
		}

		return matchCount;
	}

#pragma region CControlAutoLayout

    bool CControlAutoLayout::IsOverlapping(const CRect& rect1, const CRect& rect2, int margin)
    {
        // 扩展rect2以包含边距
        CRect expandedRect2 = rect2;
        expandedRect2.InflateRect(margin, margin);
        
        // 检查两个矩形是否相交
        CRect intersection;
        return intersection.IntersectRect(&rect1, &expandedRect2) != 0;
    }

    bool CControlAutoLayout::CollidesWithAny(
        const CRect& rect,
        const std::vector<std::shared_ptr<SCtrlInfo>>& existingCtrls,
        int margin)
    {
        for (const auto& ctrl : existingCtrls)
        {
            if (!ctrl)
                continue;
            if (IsOverlapping(rect, ctrl->ctrlRect, margin))
                return true;
        }
        return false;
    }

	bool CControlAutoLayout::TryMoveInDirection(
		CRect& rect,
		const CRect& dlgRect,
		const std::vector<std::shared_ptr<SCtrlInfo>>& existingCtrls,
		int direction,
		int maxAttempts)
	{
		const int margin = 2;
		CRect testRect = rect;

		for (int i = 0; i < maxAttempts; ++i)
		{
			// 找到当前位置碰撞的控件，并跳过它（而不是每次只移动固定像素）
			bool collided = false;
			for (const auto& ctrl : existingCtrls)
			{
				if (!ctrl) continue;
				if (!IsOverlapping(testRect, ctrl->ctrlRect, margin))
					continue;

				// 根据方向跳过碰撞控件
				collided = true;
				switch (direction)
				{
				case 0: // 右: 跳到碰撞控件的右边
					testRect.OffsetRect(ctrl->ctrlRect.right + margin + 1 - testRect.left, 0);
					break;
				case 1: // 下: 跳到碰撞控件的下边
					testRect.OffsetRect(0, ctrl->ctrlRect.bottom + margin + 1 - testRect.top);
					break;
				case 2: // 左: 跳到碰撞控件的左边
					testRect.OffsetRect(ctrl->ctrlRect.left - margin - testRect.Width() - testRect.left, 0);
					break;
				case 3: // 上: 跳到碰撞控件的上边
					testRect.OffsetRect(0, ctrl->ctrlRect.top - margin - testRect.Height() - testRect.top);
					break;
				}
				break; // 处理完第一个碰撞后重新检测
			}

			if (!collided)
			{
				// 没有碰撞，检查边界后返回成功
				if (testRect.left >= 0 && testRect.top >= 0 &&
					testRect.right <= dlgRect.Width() && testRect.bottom <= dlgRect.Height())
				{
					rect = testRect;
					return true;
				}
				return false;
			}

			// 检查是否超出对话框边界
			if (testRect.left < 0 || testRect.top < 0 ||
				testRect.right > dlgRect.Width() || testRect.bottom > dlgRect.Height())
			{
				return false;
			}
		}

		return false;
	}

    CRect CControlAutoLayout::CalculateNewPosition(
        const CRect& srcCtrlRect,
        const CRect& srcDlgRect,
        const CRect& dstDlgRect,
        const std::vector<std::shared_ptr<SCtrlInfo>>& existingCtrls)
    {
        // 计算源控件在源对话框中的相对位置（比例）
        double relativeLeft = 0.0, relativeTop = 0.0;
        
        if (srcDlgRect.Width() > 0)
            relativeLeft = static_cast<double>(srcCtrlRect.left) / srcDlgRect.Width();
        if (srcDlgRect.Height() > 0)
            relativeTop = static_cast<double>(srcCtrlRect.top) / srcDlgRect.Height();
        
        // 根据相对位置计算在目标对话框中的新位置
        int newLeft = static_cast<int>(relativeLeft * dstDlgRect.Width());
        int newTop = static_cast<int>(relativeTop * dstDlgRect.Height());
        
        // 保持控件原始大小
        CRect newRect(newLeft, newTop, 
                      newLeft + srcCtrlRect.Width(), 
                      newTop + srcCtrlRect.Height());
        
        // 确保不超出目标对话框边界
        if (newRect.right > dstDlgRect.Width())
        {
            int offset = newRect.right - dstDlgRect.Width();
            newRect.OffsetRect(-offset, 0);
        }
        if (newRect.bottom > dstDlgRect.Height())
        {
            int offset = newRect.bottom - dstDlgRect.Height();
            newRect.OffsetRect(0, -offset);
        }
        if (newRect.left < 0)
            newRect.OffsetRect(-newRect.left, 0);
        if (newRect.top < 0)
            newRect.OffsetRect(0, -newRect.top);
        
        return newRect;
    }

	CRect CControlAutoLayout::FindNonOverlappingPosition(
		const CRect& ctrlRect,
		const CRect& dlgRect,
		const std::vector<std::shared_ptr<SCtrlInfo>>& existingCtrls)
	{
		const int margin = 2;
		CRect result = ctrlRect;

		// 如果当前位置没有碰撞，直接返回
		if (!CollidesWithAny(result, existingCtrls, margin))
			return result;

		// 阶段1：尝试四个方向智能跳跃（跳过碰撞控件而非逐像素移动）
		const int directions[] = { 0, 1, 2, 3 };
		for (int dir : directions)
		{
			CRect testRect = ctrlRect;
			if (TryMoveInDirection(testRect, dlgRect, existingCtrls, dir))
			{
				return testRect;
			}
		}

		// 阶段2：候选坐标法 —— 只在已有控件边缘处尝试放置
		// 收集候选 X 和 Y 坐标（控件边缘 + margin 处）
		const int ctrlW = ctrlRect.Width();
		const int ctrlH = ctrlRect.Height();
		const int dlgW = dlgRect.Width();
		const int dlgH = dlgRect.Height();

		std::vector<int> candidateX;
		std::vector<int> candidateY;
		candidateX.reserve(existingCtrls.size() * 2 + 2);
		candidateY.reserve(existingCtrls.size() * 2 + 2);

		// 添加对话框边缘候选
		candidateX.push_back(0);
		candidateY.push_back(0);

		for (const auto& ctrl : existingCtrls)
		{
			if (!ctrl) continue;
			const CRect& r = ctrl->ctrlRect;
			// 控件右侧 + margin
			int xRight = r.right + margin + 1;
			if (xRight + ctrlW <= dlgW)
				candidateX.push_back(xRight);
			// 控件左侧 - 控件宽度 - margin
			int xLeft = r.left - margin - ctrlW;
			if (xLeft >= 0)
				candidateX.push_back(xLeft);
			// 控件下方 + margin
			int yBottom = r.bottom + margin + 1;
			if (yBottom + ctrlH <= dlgH)
				candidateY.push_back(yBottom);
			// 控件上方 - 控件高度 - margin
			int yTop = r.top - margin - ctrlH;
			if (yTop >= 0)
				candidateY.push_back(yTop);
		}

		// 按距离原始位置排序候选点，优先选择最近的位置
		int origX = ctrlRect.left;
		int origY = ctrlRect.top;

		std::sort(candidateX.begin(), candidateX.end(),
			[origX](int a, int b) { return abs(a - origX) < abs(b - origX); });
		std::sort(candidateY.begin(), candidateY.end(),
			[origY](int a, int b) { return abs(a - origY) < abs(b - origY); });

		// 保持最近距离记录
		int bestDist = INT_MAX;
		CRect bestRect = ctrlRect;
		bool found = false;

		for (int x : candidateX)
		{
			for (int y : candidateY)
			{
				CRect testRect(x, y, x + ctrlW, y + ctrlH);

				// 边界检查
				if (testRect.left < 0 || testRect.top < 0 ||
					testRect.right > dlgW || testRect.bottom > dlgH)
				{
					continue;
				}

				// 碰撞检查
				if (!CollidesWithAny(testRect, existingCtrls, margin))
				{
					int dist = abs(x - origX) + abs(y - origY);
					if (dist < bestDist)
					{
						bestDist = dist;
						bestRect = testRect;
						found = true;
						// 如果距离为0或很小，直接返回
						if (dist == 0)
							return bestRect;
					}
				}
			}
			// 剪枝：如果当前X已经比已知最优距离更远，后续X只会更远
			if (found && abs(candidateX.back() - origX) > bestDist)
				break;
		}

		if (found)
			return bestRect;

		// 阶段3：行扫描法 —— 在对话框内逐行扫描寻找空隙
		// 使用较大步长避免过多迭代
		const int scanStep = (std::max)(ctrlH / 2, 4);
		const int xScanStep = (std::max)(ctrlW / 2, 4);

		for (int y = 0; y + ctrlH <= dlgH; y += scanStep)
		{
			for (int x = 0; x + ctrlW <= dlgW; x += xScanStep)
			{
				CRect testRect(x, y, x + ctrlW, y + ctrlH);
				if (!CollidesWithAny(testRect, existingCtrls, margin))
				{
					return testRect;
				}
			}
		}

		// 如果实在找不到合适位置，尝试放在对话框底部
		result.SetRect(4, dlgH - ctrlH - 4,
					   4 + ctrlW, dlgH - 4);

		// 如果底部也有碰撞，就保持原始位置（让用户手动调整）
		if (CollidesWithAny(result, existingCtrls, margin))
		{
			result = ctrlRect;
		}

		return result;
	}

    CControlAutoLayout::SValidationResult CControlAutoLayout::ValidateControlPosition(
        const CRect& ctrlRect,
        const CRect& dlgRect,
        const std::vector<std::shared_ptr<SCtrlInfo>>& existingCtrls,
        int excludeCtrlIndex,
        bool computeSuggestion)
    {
        SValidationResult result;

        // 1. 边界检查
        if (ctrlRect.left < 0 || ctrlRect.top < 0 ||
            ctrlRect.right > dlgRect.Width() || ctrlRect.bottom > dlgRect.Height())
        {
            result.bOutOfBounds = true;
            CStringW msg;
            msg.Format(L"Control rect (%d,%d,%d,%d) exceeds dialog bounds (%d x %d)",
                       ctrlRect.left, ctrlRect.top, ctrlRect.right, ctrlRect.bottom,
                       dlgRect.Width(), dlgRect.Height());
            result.description = msg;
        }

        // 2. 重叠检查（排除自身）
        for (int i = 0; i < (int)existingCtrls.size(); ++i)
        {
            if (i == excludeCtrlIndex)
                continue;
            const auto& ctrl = existingCtrls[i];
            if (!ctrl)
                continue;
            if (IsOverlapping(ctrlRect, ctrl->ctrlRect, 0))
            {
                result.bCollision = true;
                if (!result.description.IsEmpty())
                    result.description += L"\n";
                CStringW msg;
                msg.Format(L"Overlaps with control IDC=%u at (%d,%d,%d,%d)",
                           ctrl->nIDC,
                           ctrl->ctrlRect.left, ctrl->ctrlRect.top,
                           ctrl->ctrlRect.right, ctrl->ctrlRect.bottom);
                result.description += msg;
                break; // 只报告第一个碰撞
            }
        }

        // 3. 如果有问题且需要计算建议位置
        if (!result.IsValid() && computeSuggestion)
        {
            // 构建排除自身的控件列表
            std::vector<std::shared_ptr<SCtrlInfo>> filteredCtrls;
            for (int i = 0; i < (int)existingCtrls.size(); ++i)
            {
                if (i != excludeCtrlIndex && existingCtrls[i])
                    filteredCtrls.push_back(existingCtrls[i]);
            }

            // 先尝试夹紧到边界
            CRect clampedRect = ctrlRect;
            if (clampedRect.left < 0)
                clampedRect.OffsetRect(-clampedRect.left, 0);
            if (clampedRect.top < 0)
                clampedRect.OffsetRect(0, -clampedRect.top);
            if (clampedRect.right > dlgRect.Width())
                clampedRect.OffsetRect(dlgRect.Width() - clampedRect.right, 0);
            if (clampedRect.bottom > dlgRect.Height())
                clampedRect.OffsetRect(0, dlgRect.Height() - clampedRect.bottom);

            result.suggestedRect = FindNonOverlappingPosition(clampedRect, dlgRect, filteredCtrls);
        }

        return result;
    }

#pragma endregion CControlAutoLayout

#pragma region SFontInfo
	//! 字体信息
	size_t SFontInfo::AddFontInfoToResource(std::vector<BYTE>& resource, size_t offset, bool isEx) const
	{
		// 写入字体大小
		if (offset + sizeof(WORD) > resource.size()) {
			resource.resize(offset + sizeof(WORD));
		}
		*reinterpret_cast<WORD*>(resource.data() + offset) = this->wPointSize;
		offset += sizeof(WORD);

		if (isEx)
		{
			// 写入字体权重
			if (offset + sizeof(WORD) > resource.size()) {
				resource.resize(offset + sizeof(WORD));
			}
			*reinterpret_cast<WORD*>(resource.data() + offset) = this->wWeight;
			offset += sizeof(WORD);

			// 写入斜体标志
			if (offset + sizeof(BYTE) > resource.size()) {
				resource.resize(offset + sizeof(BYTE));
			}
			*reinterpret_cast<BYTE*>(resource.data() + offset) = this->bItalic;
			offset += sizeof(BYTE);

			// 写入字符集
			if (offset + sizeof(BYTE) > resource.size()) {
				resource.resize(offset + sizeof(BYTE));
			}
			*reinterpret_cast<BYTE*>(resource.data() + offset) = this->bCharSet;
			offset += sizeof(BYTE);
		}

		// 写入字体名称
		return AddStringToResource(resource, offset, this->csFaceName);
	}
#pragma endregion SFontInfo

#pragma region SCtrlInfo

	void SCtrlInfo::SetLevel(const int& nLevel, const bool& bEnable)
	{
		if (nLevel < 0 || nLevel >= MAX_LEVEL_USE)
			return;
		////! 设置等级时自动同步低等级
		//for (int i = nLevel; i >= 0; --i)
		//{
		//	if (bEnable)
		//		ullLevelMask |= 1ULL << nLevel;
		//	else
		//		ullLevelMask &= ~(1ULL << nLevel);
		//}

		//return;//! 旧版单一等级设置


		if (bEnable)
			uiLevelMask |= 1ULL << nLevel;
		else
			uiLevelMask &= ~(1ULL << nLevel);

	}

	bool SCtrlInfo::GetLevel(const int& nLevel) const
	{
		if (nLevel < 0 || nLevel >= MAX_LEVEL_USE)
			return false;

		for (int i = nLevel; i >= 0; --i)
		{
			if ((uiLevelMask & (1ULL << i)) != 0)
				return true;
		}

		return false;		//! 高等级将自动用有低等级的权限

	}

	SCtrlInfo::SCtrlInfo(const SCtrlInfo& other)
	{
		dwStyle = other.dwStyle;
		dwExStyle = other.dwExStyle;
		ctrlRect = other.ctrlRect;
		nIDC = other.nIDC;
		csClassName = other.csClassName;
		csText = other.csText;
        oFontInfo = other.oFontInfo; // SFontInfo 为简单结构，默认拷贝即可
		wResourceID = other.wResourceID;
		blIsResourceIDText = other.blIsResourceIDText;
		blMultiLineEditCtrl = other.blMultiLineEditCtrl;
		uiLevelMask = other.uiLevelMask;
		vecExtraData.reserve(other.vecExtraData.size());
		for (const auto& bytePtr : other.vecExtraData)
		{
			if (bytePtr)
			{
				vecExtraData.push_back(std::make_shared<BYTE>(*bytePtr));
			}
			else
			{
				vecExtraData.push_back(nullptr);
			}
		}

		// copy added flag
		m_blIsAdded = other.m_blIsAdded;
	}

	//! 控件信息
	size_t SCtrlInfo::AddControlToResource(std::vector<BYTE>& resource, size_t offset, bool isEx) const
	{
		if (isEx)
		{
			// 填充DLGITEMTEMPLATEEX
			if (offset + sizeof(DLGITEMTEMPLATEEX) > resource.size()) {
				resource.resize(offset + sizeof(DLGITEMTEMPLATEEX));
			}
			DLGITEMTEMPLATEEX* pItemTemplate = reinterpret_cast<DLGITEMTEMPLATEEX*>(resource.data() + offset);
			pItemTemplate->helpID = 0;
			pItemTemplate->dwExtendedStyle = this->dwExStyle;
			pItemTemplate->style = this->dwStyle;
            pItemTemplate->x = static_cast<short>(this->ctrlRect.left);
            pItemTemplate->y = static_cast<short>(this->ctrlRect.top);
            pItemTemplate->cx = static_cast<short>(this->ctrlRect.Width());
            pItemTemplate->cy = static_cast<short>(this->ctrlRect.Height());
			pItemTemplate->id = this->nIDC;

			offset += sizeof(DLGITEMTEMPLATEEX);
		}
		else {
			// 填充DLGITEMTEMPLATE
			if (offset + sizeof(DLGITEMTEMPLATE) > resource.size()) {
				resource.resize(offset + sizeof(DLGITEMTEMPLATE));
			}
			DLGITEMTEMPLATE* pItemTemplate = reinterpret_cast<DLGITEMTEMPLATE*>(resource.data() + offset);
			pItemTemplate->style = this->dwStyle;
			pItemTemplate->dwExtendedStyle = this->dwExStyle;
            pItemTemplate->x = static_cast<short>(this->ctrlRect.left);
            pItemTemplate->y = static_cast<short>(this->ctrlRect.top);
            pItemTemplate->cx = static_cast<short>(this->ctrlRect.Width());
            pItemTemplate->cy = static_cast<short>(this->ctrlRect.Height());
			pItemTemplate->id = this->nIDC;

			offset += sizeof(DLGITEMTEMPLATE);
		}

		// 添加类名
		WORD classId = GetClassIdFromName(this->csClassName);
		if (classId != 0) {
			// 使用预定义类
			if (offset + 2 * sizeof(WORD) > resource.size()) {
				resource.resize(offset + 2 * sizeof(WORD));
			}
			*reinterpret_cast<WORD*>(resource.data() + offset) = 0xFFFF;
			*reinterpret_cast<WORD*>(resource.data() + offset + sizeof(WORD)) = classId;
			offset += 2 * sizeof(WORD);
		}
		else
		{
			// 使用自定义类
			offset = AddStringToResource(resource, offset, this->csClassName);
		}

		// 添加文本或资源ID
		if (this->blIsResourceIDText)
		{
			// 使用资源ID作为标题
			if (offset + 2 * sizeof(WORD) > resource.size())
			{
				resource.resize(offset + 2 * sizeof(WORD));
			}
			*reinterpret_cast<WORD*>(resource.data() + offset) = 0xFFFF;
			*reinterpret_cast<WORD*>(resource.data() + offset + sizeof(WORD)) = this->wResourceID;
			offset += 2 * sizeof(WORD);
		}
		else {
			offset = AddStringToResource(resource, offset, this->csText);
		}

		// 添加额外数据
		if (this->vecExtraData.size() > 0) {
			// 计算额外数据的大小（以字节为单位）
			WORD dataSize = static_cast<WORD>(this->vecExtraData.size());

			// 写入数据大小
			if (offset + sizeof(WORD) > resource.size()) {
				resource.resize(offset + sizeof(WORD));
			}
			*reinterpret_cast<WORD*>(resource.data() + offset) = dataSize;
			offset += sizeof(WORD);

			// 写入数据内容
			if (offset + dataSize > resource.size()) {
				resource.resize(offset + dataSize);
			}

			// 处理额外数据
			for (size_t i = 0; i < this->vecExtraData.size(); i++) {
				if (this->vecExtraData[i]) {
					resource[offset + i] = *(this->vecExtraData[i].get());
				}
			}
			offset += dataSize;
		}
		else
		{
			// 写入0表示没有额外数据
			if (offset + sizeof(WORD) > resource.size())
			{
				resource.resize(offset + sizeof(WORD));
			}
			*reinterpret_cast<WORD*>(resource.data() + offset) = 0;
			offset += sizeof(WORD);
		}

		return offset;
	}
#pragma endregion SCtrlInfo

#pragma region SDialogInfo
	//! 窗口信息
	SConvertedResData SDialogInfo::Convert2ResData() const
	{
		SConvertedResData resData;
		resData.resType = RT_DIALOG;
		resData.nResID = this->nIDD;
		resData.langID = this->langID;
		size_t estimatedSize = CalculateDialogResourceSize();

		auto& resourceData = resData.vecData;
		resourceData.resize(estimatedSize);

		// 判断是否使用DLGTEMPLATEEX
		bool isEx = this->bIsDlgTemplateEx;

		size_t offset = 0;
		if (isEx) {
			// 填充DLGTEMPLATEEX头部
			DLGTEMPLATEEX* pDlgTemplate = reinterpret_cast<DLGTEMPLATEEX*>(resourceData.data());
			pDlgTemplate->dlgVer = 1;
			pDlgTemplate->signature = 0xFFFF;
			pDlgTemplate->helpID = 0;
			pDlgTemplate->dwExtendedStyle = this->dwExStyle;
			pDlgTemplate->style = this->dwStyle;
			pDlgTemplate->cDlgItems = static_cast<WORD>(this->vecCtrlInfo.size());
            pDlgTemplate->x = static_cast<short>(this->rectDlg.left);
            pDlgTemplate->y = static_cast<short>(this->rectDlg.top);
            pDlgTemplate->cx = static_cast<short>(this->rectDlg.Width());
            pDlgTemplate->cy = static_cast<short>(this->rectDlg.Height());

			offset = sizeof(DLGTEMPLATEEX);
		}
		else {
			// 填充DLGTEMPLATE头部
			DLGTEMPLATE* pDlgTemplate = reinterpret_cast<DLGTEMPLATE*>(resourceData.data());
			pDlgTemplate->style = this->dwStyle;
			pDlgTemplate->dwExtendedStyle = this->dwExStyle;
			pDlgTemplate->cdit = static_cast<WORD>(this->vecCtrlInfo.size());
            pDlgTemplate->x = static_cast<short>(this->rectDlg.left);
            pDlgTemplate->y = static_cast<short>(this->rectDlg.top);
            pDlgTemplate->cx = static_cast<short>(this->rectDlg.Width());
            pDlgTemplate->cy = static_cast<short>(this->rectDlg.Height());

			offset = sizeof(DLGTEMPLATE);
		}

		// 添加菜单名
		if (this->blIsMenuResourceIDText)
		{
			// 使用资源ID
			if (offset + 2 * sizeof(WORD) > resourceData.size())
			{
				resourceData.resize(offset + 2 * sizeof(WORD));
			}
			*reinterpret_cast<WORD*>(resourceData.data() + offset) = 0xFFFF;
			*reinterpret_cast<WORD*>(resourceData.data() + offset + sizeof(WORD)) = this->wMenuResourceID;
			offset += 2 * sizeof(WORD);
		}
		else
		{
			offset = AddStringToResource(resourceData, offset, this->csMenuName);
		}

		// 添加类名
		if (this->blIsClassResourceIDText)
		{
			// 使用资源ID
			if (offset + 2 * sizeof(WORD) > resourceData.size())
			{
				resourceData.resize(offset + 2 * sizeof(WORD));
			}
			*reinterpret_cast<WORD*>(resourceData.data() + offset) = 0xFFFF;
			*reinterpret_cast<WORD*>(resourceData.data() + offset + sizeof(WORD)) = this->wClassResourceID;
			offset += 2 * sizeof(WORD);
		}
		else
		{
			offset = AddStringToResource(resourceData, offset, this->csClassName);
		}

		// 添加标题
		offset = AddStringToResource(resourceData, offset, this->csCaption);

		// 添加字体信息
		if (this->dwStyle & DS_SETFONT)
		{
			offset = this->pFontInfo->AddFontInfoToResource(resourceData, offset, isEx);
		}

		// 添加控件信息
		for (const auto& pCtrlInfo : this->vecCtrlInfo)
		{
			offset = AlignOffset(offset, 4); // DWORD对齐
			offset = pCtrlInfo->AddControlToResource(resourceData, offset, isEx);
		}

		// 调整最终大小
		resourceData.resize(offset);

		return resData;
	}


	SDialogInfo& SDialogInfo::operator+=(const SDialogInfo& other)
	{
		// nIDD 相同时才进行操作；当 nIDD 不匹配但 csResourceName 匹配时也允许合并
		// （处理宏未解析导致合成ID不一致的情况）
		if (this->nIDD != other.nIDD)
		{
			if (this->csResourceName.IsEmpty() || other.csResourceName.IsEmpty() ||
				this->csResourceName.CompareNoCase(other.csResourceName) != 0)
			{
				return *this;
			}
		}

		// 构建棋盘拓扑模型，用于 STATIC 控件匹配
		auto pOther = std::make_shared<SDialogInfo>(other);
		auto pThis  = std::make_shared<SDialogInfo>(*this);

		// 全量拓扑网格分析：对所有 STATIC 控件进行拓扑槽位匹配
		std::map<int, bool> tileMatchedInThis;
		BoardTileStaticMatch(pOther, pThis, tileMatchedInThis);

		// GroupBox 序号匹配：无名 GroupBox（IDC 为 STATIC 类）按出现顺序配对
		std::map<int, bool> gbMatchedInOther;
		{
			std::vector<int> otherNamelessGB, thisNamelessGB;
			for (int ci = 0; ci < (int)other.vecCtrlInfo.size(); ++ci)
			{
				const auto& c = other.vecCtrlInfo[ci];
				if (c && IsGroupBoxCtrl(c) && IsStaticIDC(c->nIDC))
					otherNamelessGB.push_back(ci);
			}
			for (int ci = 0; ci < (int)this->vecCtrlInfo.size(); ++ci)
			{
				const auto& c = this->vecCtrlInfo[ci];
				if (c && IsGroupBoxCtrl(c) && IsStaticIDC(c->nIDC))
					thisNamelessGB.push_back(ci);
			}
			int gbPairs = (int)(std::min)(otherNamelessGB.size(), thisNamelessGB.size());
			for (int k = 0; k < gbPairs; ++k)
				gbMatchedInOther[otherNamelessGB[k]] = true;
		}

		// 第一轮：收集所有待添加控件索引及类型
		std::vector<int> toAddIndices;
		for (int ci = 0; ci < (int)other.vecCtrlInfo.size(); ++ci)
		{
			const auto& pCtrlToAdd = other.vecCtrlInfo[ci];
			if (!pCtrlToAdd) continue;

			bool exists = false;
			bool isGB = IsGroupBoxCtrl(pCtrlToAdd);

			if (isGB && IsStaticIDC(pCtrlToAdd->nIDC))
			{
				exists = (gbMatchedInOther.find(ci) != gbMatchedInOther.end());
			}
			else if (!IsStaticIDC(pCtrlToAdd->nIDC))
			{
				auto it = std::find_if(this->vecCtrlInfo.begin(), this->vecCtrlInfo.end(),
					[&](const std::shared_ptr<SCtrlInfo>& p) {
						return p && p->nIDC == pCtrlToAdd->nIDC;
					});
				exists = (it != this->vecCtrlInfo.end());
			}
			else
			{
				exists = (tileMatchedInThis.find(ci) != tileMatchedInThis.end());
			}

			if (!exists)
				toAddIndices.push_back(ci);
		}

		// 伴随规则：STATIC 只有附近存在真正缺失的非 STATIC 时才添加
		{
			// 构建主语言棋盘用于 GroupBox 归属查找
			SDialogBoard otherBoard = BuildDialogBoard(pOther);

			auto findGroupOwner = [&](int ctrlIdx) -> int {
				for (int gi = 0; gi < (int)otherBoard.groupBoxes.size(); ++gi)
					for (const auto& tile : otherBoard.groupBoxes[gi].tiles)
						if (tile.globalIndex == ctrlIdx) return gi;
				return -1;
			};

			// 构建 GroupBox 组索引 → 控件索引 的映射（按 vecCtrlInfo 出现顺序）
			std::vector<int> gbCtrlIndices;
			for (int ci = 0; ci < (int)other.vecCtrlInfo.size(); ++ci)
			{
				const auto& c = other.vecCtrlInfo[ci];
				if (c && IsGroupBoxCtrl(c))
					gbCtrlIndices.push_back(ci);
			}

			// 标记哪些 GroupBox 组自身也在待添加列表中
			std::set<int> addIdxSet(toAddIndices.begin(), toAddIndices.end());
			std::set<int> missingGBGroups;
			for (int gi = 0; gi < (int)gbCtrlIndices.size(); ++gi)
			{
				if (addIdxSet.count(gbCtrlIndices[gi]))
					missingGBGroups.insert(gi);
			}

			// 收集需要 STATIC 标签的缺失控件信息（Edit、ComboBox 等）
			struct NSInfo { CPoint pos; int gbOwner; };
			std::vector<NSInfo> missingNS;
			for (int ci : toAddIndices)
			{
				const auto& c = other.vecCtrlInfo[ci];
				if (!c) continue;
				if (NeedsStaticLabel(c))
					missingNS.push_back({ CPoint(c->ctrlRect.left, c->ctrlRect.top), findGroupOwner(ci) });
			}

			// 收集缺失的普通 STATIC 坐标（用于 STATIC 互伴随：多个标题 STATIC 互为伴随）
			struct StaticPosInfo { CPoint pos; int gbOwner; int selfIdx; };
			std::vector<StaticPosInfo> missingStaticPos;
			for (int ci : toAddIndices)
			{
				const auto& c = other.vecCtrlInfo[ci];
				if (!c) continue;
				if (IsStaticIDC(c->nIDC) && !IsGroupBoxCtrl(c))
					missingStaticPos.push_back({ CPoint(c->ctrlRect.left, c->ctrlRect.top), findGroupOwner(ci), ci });
			}

			constexpr int kCompanionTol = 15;
			std::vector<int> filtered;
			for (int ci : toAddIndices)
			{
				const auto& c = other.vecCtrlInfo[ci];
				if (!c) continue;
				if (!IsStaticIDC(c->nIDC) || IsGroupBoxCtrl(c))
				{
					filtered.push_back(ci);
					continue;
				}
				// STATIC: 检查伴随
				int myGB = findGroupOwner(ci);
				CPoint myPos(c->ctrlRect.left, c->ctrlRect.top);
				bool has = false;
				// 若所属 GroupBox 自身也在待添加列表中，整组为新增内容，直接保留
				if (myGB >= 0 && missingGBGroups.count(myGB))
				{
					has = true;
				}
				if (!has)
				{
					for (const auto& ns : missingNS)
					{
						if (myGB >= 0 && myGB == ns.gbOwner) { has = true; break; }
						if (abs(myPos.y - ns.pos.y) <= kCompanionTol) { has = true; break; }
					}
				}
				// STATIC 互伴随：多个缺失 STATIC 聚在一起（栏目标题行）视为互相伴随
				if (!has)
				{
					for (const auto& sp : missingStaticPos)
					{
						if (sp.selfIdx == ci) continue;
						if (myGB >= 0 && myGB == sp.gbOwner) { has = true; break; }
						if (abs(myPos.y - sp.pos.y) <= kCompanionTol) { has = true; break; }
					}
				}
				// 独立 STATIC 保留策略：有非空文本的 STATIC（标签/说明文字）即使无伴随也应保留，
				// 仅过滤空文本的装饰性 STATIC（如分隔线占位符）
				if (!has && !c->csText.IsEmpty())
				{
					CStringW trimmed = c->csText;
					trimmed.Trim();
					if (!trimmed.IsEmpty())
						has = true;
				}
				if (has) filtered.push_back(ci);
			}
			toAddIndices = filtered;
		}

		// 第二轮：执行添加
		for (int ci : toAddIndices)
		{
			auto newCtrl = std::make_shared<SCtrlInfo>(*other.vecCtrlInfo[ci]);
			newCtrl->m_blIsAdded = true;
			this->vecCtrlInfo.push_back(newCtrl);
			this->vecAddedCtrlInfo.push_back(newCtrl);
		}

		return *this;
	}

	SDialogInfo& SDialogInfo::operator-=(const SDialogInfo& other)
	{
		// 仅当 nIDD 相同时才进行操作
		if (/*this->langID != other.langID || */this->nIDD != other.nIDD)
		{
			return *this;
		}

		for (const auto& pCtrlToRemove : other.vecCtrlInfo)
		{
			if (!pCtrlToRemove)
			{
				continue;
			}

			// 查找并移除控件
			this->vecCtrlInfo.erase(
				std::remove_if(this->vecCtrlInfo.begin(), this->vecCtrlInfo.end(),
					[&](const std::shared_ptr<SCtrlInfo>& p) {
						return p && p->nIDC == pCtrlToRemove->nIDC;
					}),
				this->vecCtrlInfo.end());
		}

		return *this;
	}

	SDialogInfo SDialogInfo::operator+(const SDialogInfo& other) const
	{
		SDialogInfo result = *this;
		result += other;
		return result;
	}

	SDialogInfo SDialogInfo::operator-(const SDialogInfo& other) const
	{
		SDialogInfo result = *this;
		result -= other;
		return result;
	}


	size_t SDialogInfo::CalculateDialogResourceSize() const
	{
		// 基础大小（头部）
		size_t size = this->bIsDlgTemplateEx ? sizeof(DLGTEMPLATEEX) : sizeof(DLGTEMPLATE);

		// 菜单名大小
		if (this->blIsMenuResourceIDText)
		{
			size += 2 * sizeof(WORD); // 0xFFFF + resourceId
		}
		else
		{
			size += this->csMenuName.IsEmpty() ? sizeof(WORD) : (this->csMenuName.GetLength() + 1) * sizeof(WCHAR);
		}

		// 类名大小
		if (this->blIsClassResourceIDText)
		{
			size += 2 * sizeof(WORD); // 0xFFFF + resourceId
		}
		else
		{
			size += this->csClassName.IsEmpty() ? sizeof(WORD) : (this->csClassName.GetLength() + 1) * sizeof(WCHAR);
		}

		// 标题大小
		size += this->csCaption.IsEmpty() ? sizeof(WORD) : (this->csCaption.GetLength() + 1) * sizeof(WCHAR);

		// 字体信息大小
		if (this->dwStyle & DS_SETFONT)
		{
			size += sizeof(WORD); // 字体大小
			if (this->bIsDlgTemplateEx || (this->dwStyle & DS_SHELLFONT))
			{
				size += 2 * sizeof(WORD) + 2 * sizeof(BYTE); // 权重、斜体、字符集
			}
			size += this->pFontInfo->csFaceName.IsEmpty() ? sizeof(WORD) :
				(this->pFontInfo->csFaceName.GetLength() + 1) * sizeof(WCHAR);
		}

		// 对齐到DWORD边界
		size = (size + 3) & ~3;

		// 控件信息大小
		for (const auto& pCtrlInfo : this->vecCtrlInfo)
		{
			// 控件头部
			size_t ctrlSize = this->bIsDlgTemplateEx ? sizeof(DLGITEMTEMPLATEEX) : sizeof(DLGITEMTEMPLATE);

			// 类名
			if (GetClassIdFromName(pCtrlInfo->csClassName) != 0)
			{
				ctrlSize += 2 * sizeof(WORD); // 0xFFFF + classId
			}
			else
			{
				ctrlSize += (pCtrlInfo->csClassName.GetLength() + 1) * sizeof(WCHAR);
			}

			// 文本
			if (pCtrlInfo->blIsResourceIDText)
			{
				ctrlSize += 2 * sizeof(WORD); // 0xFFFF + resourceId
			}
			else {
				ctrlSize += pCtrlInfo->csText.IsEmpty() ? sizeof(WORD) : (pCtrlInfo->csText.GetLength() + 1) * sizeof(WCHAR);
			}

			// 创建数据
			ctrlSize += sizeof(WORD); // 创建数据长度
			ctrlSize += pCtrlInfo->vecExtraData.size(); // 实际创建数据

			// 对齐到DWORD边界
			ctrlSize = (ctrlSize + 3) & ~3;

			size += ctrlSize;
		}

		// 添加一些安全边界
		return size + 1024;
	}

#pragma endregion SDialogInfo

	static CStringW JoinFlags(const std::vector<CStringW>& parts)
	{
		CStringW out;
		for (size_t i = 0; i < parts.size(); ++i)
		{
			if (i != 0)
				out += L"|";
			out += parts[i];
		}
		return out;
	}

	CStringW FormatWindowStyleText(DWORD style)
	{
		struct Flag { DWORD v; const wchar_t* name; };
    // 顺序重要：更具体/分组掩码优先。
		static const Flag kFlags[] = {
			{ WS_CHILD, L"WS_CHILD" },
			{ WS_VISIBLE, L"WS_VISIBLE" },
			{ WS_DISABLED, L"WS_DISABLED" },
			{ WS_TABSTOP, L"WS_TABSTOP" },
			{ WS_GROUP, L"WS_GROUP" },
			{ WS_BORDER, L"WS_BORDER" },
			{ WS_DLGFRAME, L"WS_DLGFRAME" },
			{ WS_THICKFRAME, L"WS_THICKFRAME" },
			{ WS_CAPTION, L"WS_CAPTION" },
			{ WS_SYSMENU, L"WS_SYSMENU" },
			{ WS_MINIMIZEBOX, L"WS_MINIMIZEBOX" },
			{ WS_MAXIMIZEBOX, L"WS_MAXIMIZEBOX" },
			{ WS_CLIPSIBLINGS, L"WS_CLIPSIBLINGS" },
			{ WS_CLIPCHILDREN, L"WS_CLIPCHILDREN" },
			{ WS_HSCROLL, L"WS_HSCROLL" },
			{ WS_VSCROLL, L"WS_VSCROLL" },
		};

		DWORD remain = style;
		std::vector<CStringW> parts;
		parts.reserve(_countof(kFlags) + 1);
		for (const auto& f : kFlags)
		{
			if ((remain & f.v) == f.v)
			{
				parts.push_back(f.name);
				remain &= ~f.v;
			}
		}

		if (parts.empty() && remain == 0)
			return L"0";
		if (remain != 0)
		{
			CStringW tail;
			tail.Format(L"0x%08X", remain);
			parts.push_back(tail);
		}
		return JoinFlags(parts);
	}

	CStringW FormatWindowExStyleText(DWORD exStyle)
	{
		struct Flag { DWORD v; const wchar_t* name; };
		static const Flag kFlags[] = {
			{ WS_EX_CLIENTEDGE, L"WS_EX_CLIENTEDGE" },
			{ WS_EX_STATICEDGE, L"WS_EX_STATICEDGE" },
			{ WS_EX_WINDOWEDGE, L"WS_EX_WINDOWEDGE" },
			{ WS_EX_DLGMODALFRAME, L"WS_EX_DLGMODALFRAME" },
			{ WS_EX_TOOLWINDOW, L"WS_EX_TOOLWINDOW" },
			{ WS_EX_TOPMOST, L"WS_EX_TOPMOST" },
			{ WS_EX_TRANSPARENT, L"WS_EX_TRANSPARENT" },
			{ WS_EX_ACCEPTFILES, L"WS_EX_ACCEPTFILES" },
			{ WS_EX_APPWINDOW, L"WS_EX_APPWINDOW" },
			{ WS_EX_CONTROLPARENT, L"WS_EX_CONTROLPARENT" },
			{ WS_EX_NOPARENTNOTIFY, L"WS_EX_NOPARENTNOTIFY" },
			{ WS_EX_RIGHTSCROLLBAR, L"WS_EX_RIGHTSCROLLBAR" },
			{ WS_EX_LEFTSCROLLBAR, L"WS_EX_LEFTSCROLLBAR" },
			{ WS_EX_LAYOUTRTL, L"WS_EX_LAYOUTRTL" },
			{ WS_EX_RTLREADING, L"WS_EX_RTLREADING" },
			{ WS_EX_LTRREADING, L"WS_EX_LTRREADING" },
			{ WS_EX_RIGHT, L"WS_EX_RIGHT" },
			{ WS_EX_LEFT, L"WS_EX_LEFT" },
			{ WS_EX_NOACTIVATE, L"WS_EX_NOACTIVATE" },
		};

		DWORD remain = exStyle;
		std::vector<CStringW> parts;
		parts.reserve(_countof(kFlags) + 1);
		for (const auto& f : kFlags)
		{
			if ((remain & f.v) == f.v)
			{
				parts.push_back(f.name);
				remain &= ~f.v;
			}
		}

		if (parts.empty() && remain == 0)
			return L"0";
		if (remain != 0)
		{
			CStringW tail;
			tail.Format(L"0x%08X", remain);
			parts.push_back(tail);
		}
		return JoinFlags(parts);
	}

#pragma region SStringTableInfo
	SConvertedResData SStringTableInfo::Convert2ResData(LANGID langID) const
	{
		SConvertedResData resData;
		resData.resType = RT_STRING;
		resData.nResID = this->nResourceStringID;
		resData.langID = langID;

		std::vector<BYTE>& resourceData = resData.vecData;
		resourceData.clear();

		// 字符串表资源块中最多包含16个字符串
		for (int i = 0; i < 16; ++i)
		{
			UINT currentID = (this->nResourceStringID - 1) * 16 + i;
			auto it = this->mapIDAndString.find(currentID);

			if (it != this->mapIDAndString.end() && !it->second.IsEmpty())
			{
				const CStringW& str = it->second;
				WORD length = static_cast<WORD>(str.GetLength());

				// 写入字符串长度
				resourceData.insert(resourceData.end(), reinterpret_cast<const BYTE*>(&length), reinterpret_cast<const BYTE*>(&length) + sizeof(WORD));

				// 写入字符串内容 (Unicode)
				resourceData.insert(resourceData.end(), reinterpret_cast<const BYTE*>(str.GetString()), reinterpret_cast<const BYTE*>(str.GetString()) + length * sizeof(WCHAR));
			}
			else
			{
				// 如果字符串不存在或为空，则写入长度0
				WORD length = 0;
				resourceData.insert(resourceData.end(), reinterpret_cast<const BYTE*>(&length), reinterpret_cast<const BYTE*>(&length) + sizeof(WORD));
			}
		}

		return resData;

	}

	SStringTableInfo& SStringTableInfo::operator+=(const SStringTableInfo& other)
	{
		if (this->nResourceStringID != other.nResourceStringID)
		{
			return *this;
		}

		// 合并字符串
		for (const auto& pair : other.mapIDAndString)
		{
            // 如果当前对象不存在该ID，则插入
			if (this->mapIDAndString.find(pair.first) == this->mapIDAndString.end())
			{
			this->mapIDAndString[pair.first] = pair.second;
			// 记录为新增字符串
			SAddedString addedStr;
			addedStr.nStringID = pair.first;
			addedStr.csText = pair.second;
			addedStr.bModified = false;
			this->vecAddedStrings.push_back(addedStr);
			}
		}

		// 合并缺失ID
		for (const auto& id : other.vecLackStringID)
		{
			if (std::find(this->vecLackStringID.begin(), this->vecLackStringID.end(), id) == this->vecLackStringID.end())
			{
				this->vecLackStringID.push_back(id);
			}
		}
		std::sort(this->vecLackStringID.begin(), this->vecLackStringID.end());

		return *this;
	}

	SStringTableInfo& SStringTableInfo::operator-=(const SStringTableInfo& other)
	{
		if (this->nResourceStringID != other.nResourceStringID)
		{
			return *this;
		}

		// 移除字符串
		for (const auto& pair : other.mapIDAndString)
		{
			this->mapIDAndString.erase(pair.first);
		}

		return *this;
	}

	SStringTableInfo SStringTableInfo::operator+(const SStringTableInfo& other) const
	{
		SStringTableInfo result = *this;
		result += other;
		return result;
	}

	SStringTableInfo SStringTableInfo::operator-(const SStringTableInfo& other) const
	{
		SStringTableInfo result = *this;
		result -= other;
		return result;
	}

#pragma endregion SStringTableInfo

#pragma region SVersionInfo

	size_t SVersionInfo::AddVersionString(std::vector<BYTE>& data, size_t offset, const CStringW& str) const
	{
		size_t stringSize = (str.GetLength() + 1) * sizeof(WCHAR);
		if (data.size() < offset + stringSize)
		{
			data.resize(offset + stringSize);
		}
		memcpy(data.data() + offset, str.GetString(), stringSize);
		offset += stringSize;
		// DWORD 对齐
		return (offset + 3) & ~3;
	}

	size_t SVersionInfo::WriteVersionHeader(std::vector<BYTE>& data, size_t offset, WORD wLength, WORD wValueLength, WORD wType) const
	{
		if (data.size() < offset + 3 * sizeof(WORD))
		{
			data.resize(offset + 3 * sizeof(WORD));
		}
		*reinterpret_cast<WORD*>(data.data() + offset) = wLength;
		*reinterpret_cast<WORD*>(data.data() + offset + sizeof(WORD)) = wValueLength;
		*reinterpret_cast<WORD*>(data.data() + offset + 2 * sizeof(WORD)) = wType;
		return offset + 3 * sizeof(WORD);
	}

	SConvertedResData SVersionInfo::Convert2ResData() const
	{
		SConvertedResData resData;
		resData.resType = RT_VERSION;
		resData.nResID = this->nVersionID;
		resData.langID = this->langID;

		std::vector<BYTE>& data = resData.vecData;
		data.clear();

		// 预分配足够的空间
		data.reserve(4096);

		// === 1. VS_VERSIONINFO 根节点 ===
		size_t rootHeaderOffset = 0;
		size_t offset = this->WriteVersionHeader(data, rootHeaderOffset, 0, sizeof(VS_FIXEDFILEINFO), 0); // wLength 稍后更新

		// 写入 "VS_VERSION_INFO" 键名
		offset = this->AddVersionString(data, offset, L"VS_VERSION_INFO");

		// 写入 VS_FIXEDFILEINFO
		size_t fixedInfoOffset = offset;
		if (data.size() < offset + sizeof(VS_FIXEDFILEINFO))
		{
			data.resize(offset + sizeof(VS_FIXEDFILEINFO));
		}
		memcpy(data.data() + offset, &this->fileInfo, sizeof(VS_FIXEDFILEINFO));
		offset += sizeof(VS_FIXEDFILEINFO);
		offset = (offset + 3) & ~3; // DWORD 对齐

		// === 2. StringFileInfo 节点 ===
		size_t stringFileInfoOffset = offset;
		offset = this->WriteVersionHeader(data, offset, 0, 0, 1); // wType=1 表示文本，wLength 稍后更新

		// 写入 "StringFileInfo" 键名
		offset = this->AddVersionString(data, offset, L"StringFileInfo");

		// === 3. StringTable 子节点（语言+代码页）===
		size_t stringTableOffset = offset;
		offset = this->WriteVersionHeader(data, offset, 0, 0, 1); // wLength 稍后更新

		// 构造语言代码页字符串，例如 "040904B0"（英语-美国，Unicode）
		CStringW langCodePage;
		langCodePage.Format(L"%04X%04X", this->langID, this->wCodePage);
		offset = this->AddVersionString(data, offset, langCodePage);

		// === 4. String 节点（键值对）===
		size_t stringEntriesStart = offset;
		for (const auto& pair : this->mapStringInfo)
		{
			size_t stringEntryOffset = offset;
			const CStringW& key = pair.first;
			const CStringW& value = pair.second;

			// String 节头：wValueLength = 值字符串长度（不含 null）
			WORD valueLength = static_cast<WORD>(value.GetLength() + 1);
			offset = this->WriteVersionHeader(data, offset, 0, valueLength, 1); // wLength 稍后更新

			// 写入键名
			offset = this->AddVersionString(data, offset, key);

			// 写入值
			offset = this->AddVersionString(data, offset, value);

			// 更新当前 String 节的 wLength
			WORD stringLength = static_cast<WORD>(offset - stringEntryOffset);
			*reinterpret_cast<WORD*>(data.data() + stringEntryOffset) = stringLength;
		}

		// 更新 StringTable 的 wLength
		WORD stringTableLength = static_cast<WORD>(offset - stringTableOffset);
		*reinterpret_cast<WORD*>(data.data() + stringTableOffset) = stringTableLength;

		// 更新 StringFileInfo 的 wLength
		WORD stringFileInfoLength = static_cast<WORD>(offset - stringFileInfoOffset);
		*reinterpret_cast<WORD*>(data.data() + stringFileInfoOffset) = stringFileInfoLength;

		// === 5. VarFileInfo 节点（可选）===
		offset = (offset + 3) & ~3; // DWORD 对齐
		size_t varFileInfoOffset = offset;
		offset = this->WriteVersionHeader(data, offset, 0, 0, 1); // wLength 稍后更新

		// 写入 "VarFileInfo" 键名
		offset = this->AddVersionString(data, offset, L"VarFileInfo");

		// === 6. Var 子节点（Translation）===
		size_t varOffset = offset;
		offset = this->WriteVersionHeader(data, offset, 0, sizeof(DWORD), 0); // wValueLength = 4 字节，wType = 0 表示二进制

		// 写入 "Translation" 键名
		offset = this->AddVersionString(data, offset, L"Translation");

		// 写入 Translation 值（LANGID + CodePage）
		if (data.size() < offset + sizeof(DWORD))
		{
		 data.resize(offset + sizeof(DWORD));
		}
		*reinterpret_cast<WORD*>(data.data() + offset) = this->langID;
		*reinterpret_cast<WORD*>(data.data() + offset + sizeof(WORD)) = this->wCodePage;
		offset += sizeof(DWORD);

		// 更新 Var 的 wLength
		WORD varLength = static_cast<WORD>(offset - varOffset);
		*reinterpret_cast<WORD*>(data.data() + varOffset) = varLength;

		// 更新 VarFileInfo 的 wLength
		WORD varFileInfoLength = static_cast<WORD>(offset - varFileInfoOffset);
		*reinterpret_cast<WORD*>(data.data() + varFileInfoOffset) = varFileInfoLength;

		// === 7. 更新根节点 VS_VERSIONINFO 的 wLength ===
		WORD rootLength = static_cast<WORD>(offset);
		*reinterpret_cast<WORD*>(data.data() + rootHeaderOffset) = rootLength;

		// 最终对齐并调整大小
		offset = (offset + 3) & ~3;
		data.resize(offset);

		return resData;
	}

	SVersionInfo& SVersionInfo::operator+=(const SVersionInfo& other)
	{
		if (this->nVersionID != other.nVersionID /*|| this->langID != other.langID*/)
		{
			return *this;
		}

		// 仅同步产品信息和版本信息
		this->fileInfo.dwProductVersionMS = other.fileInfo.dwProductVersionMS;
		this->fileInfo.dwProductVersionLS = other.fileInfo.dwProductVersionLS;
		this->fileInfo.dwFileVersionMS = other.fileInfo.dwFileVersionMS;
		this->fileInfo.dwFileVersionLS = other.fileInfo.dwFileVersionLS;

		return *this;
	}

	SVersionInfo& SVersionInfo::operator-=(const SVersionInfo& other)
	{
		if (this->nVersionID != other.nVersionID /*|| this->langID != other.langID*/)
		{
			return *this;
		}

		// 仅清除产品信息和版本信息
		this->fileInfo.dwProductVersionMS = other.fileInfo.dwProductVersionMS;
		this->fileInfo.dwProductVersionLS = other.fileInfo.dwProductVersionLS;
		this->fileInfo.dwFileVersionMS = other.fileInfo.dwFileVersionMS;
		this->fileInfo.dwFileVersionLS = other.fileInfo.dwFileVersionLS;

		return *this;
	}

	SVersionInfo SVersionInfo::operator+(const SVersionInfo& other) const
	{
		SVersionInfo result = *this;
		result += other;
		return result;
	}

	SVersionInfo SVersionInfo::operator-(const SVersionInfo& other) const
	{
		SVersionInfo result = *this;
		result -= other;
		return result;
	}
#pragma endregion SVersionInfo


#pragma region SMenuInfo
	SConvertedResData SMenuInfo::Convert2ResData() const
	{
		// 菜单资源的实现也较为复杂，涉及到递归和标准/扩展格式判断
		SConvertedResData resData;
		resData.resType = RT_MENU;
		resData.nResID = this->nID;
		resData.langID = this->lanID;

		auto& data = resData.vecData;
        // 从 MENUEX_TEMPLATE_HEADER 开始构建菜单资源
        data.resize(sizeof(MENUEX_TEMPLATE_HEADER));
		MENUEX_TEMPLATE_HEADER* pHeader = reinterpret_cast<MENUEX_TEMPLATE_HEADER*>(data.data());
		pHeader->wVersion = 1;
        pHeader->wOffset = sizeof(MENUEX_TEMPLATE_HEADER); // 到第一个 MENUEX_TEMPLATE_ITEM 的偏移
		pHeader->dwHelpId = 0;

		size_t currentOffset = sizeof(MENUEX_TEMPLATE_HEADER);

        // 递归添加所有菜单项
		for (const auto& subMenu : this->vecSubMenu)
		{
			if (subMenu)
			{
				currentOffset = AddMenuItemToResource(data, currentOffset, *subMenu);
			}
		}

        // 最终对齐并调整大小
        currentOffset = (currentOffset + 3) & ~3;
        data.resize(currentOffset);

		return resData;
	}

	SMenuInfo& SMenuInfo::operator+=(const SMenuInfo& other)
	{
		if (this->nID != other.nID || this->lanID != other.lanID)
		{
			return *this;
		}

		// 递归合并子菜单
		for (const auto& subMenuToAdd : other.vecSubMenu)
		{
			if (!subMenuToAdd) continue;
			
			// 通过 ID 查找匹配的子菜单项
			auto it = std::find_if(this->vecSubMenu.begin(), this->vecSubMenu.end(),
				[&](const std::shared_ptr<SMenuInfo>& p) {
					return p && p->nID == subMenuToAdd->nID;
				});

			if (it != this->vecSubMenu.end())
			{
				// 如果找到了匹配的子菜单，则递归合并
				(**it) += (*subMenuToAdd);
			}
			else
			{
				// 不存在则添加，并记录为新增
				auto newMenuItem = std::make_shared<SMenuInfo>(*subMenuToAdd);
				newMenuItem->m_blIsAdded = true;
				this->vecSubMenu.push_back(newMenuItem);
				this->vecAddedMenuItems.push_back(newMenuItem);
			}
		}
		return *this;
	}

	SMenuInfo& SMenuInfo::operator-=(const SMenuInfo& other)
	{
		if (this->nID != other.nID || this->lanID != other.lanID)
		{
			return *this;
		}

		// 递归移除子菜单
		for (const auto& subMenuToRemove : other.vecSubMenu)
		{
			if (!subMenuToRemove) continue;
			this->vecSubMenu.erase(
				std::remove_if(this->vecSubMenu.begin(), this->vecSubMenu.end(),
					[&](const std::shared_ptr<SMenuInfo>& p) {
						return p && p->nID == subMenuToRemove->nID && !p->csText.Compare(subMenuToRemove->csText);
					}),
				this->vecSubMenu.end());
		}
		return *this;
	}



	SMenuInfo SMenuInfo::operator+(const SMenuInfo& other) const
	{
		SMenuInfo result = *this;
		result += other;
		return result;
	}

	SMenuInfo SMenuInfo::operator-(const SMenuInfo& other) const
	{
		SMenuInfo result = *this;
		result -= other;
		return result;
	}

	size_t SMenuInfo::AddMenuItemToResource(std::vector<BYTE>& data, size_t offset, const SMenuInfo& menuItem)
	{
        // 对齐到 DWORD 边界
        offset = (offset + 3) & ~3;

        // 为 MENUEX_TEMPLATE_ITEM 调整缓冲区大小
        if (data.size() < offset + sizeof(MENUEX_TEMPLATE_ITEM))
        {
            data.resize(offset + sizeof(MENUEX_TEMPLATE_ITEM));
        }

		MENUEX_TEMPLATE_ITEM* pItem = reinterpret_cast<MENUEX_TEMPLATE_ITEM*>(data.data() + offset);
		pItem->dwType = menuItem.nType;
		pItem->dwState = menuItem.nState;
		pItem->dwHelpId = menuItem.dwHelpID;
		pItem->wID = static_cast<WORD>(menuItem.nID);
		pItem->bResInfo = 0;

		bool isPopup = !menuItem.vecSubMenu.empty() || menuItem.bIsPopup;
		if (isPopup)
		{
			pItem->bResInfo = 0x01; // MF_POPUP flag
		}

        // 调整偏移以开始写入菜单字符串
        size_t textOffset = offset + sizeof(MENUEX_TEMPLATE_ITEM);

        // 写入菜单文本
		if (!menuItem.csText.IsEmpty())
		{
			size_t stringSize = (menuItem.csText.GetLength() + 1) * sizeof(WCHAR);
			if (data.size() < textOffset + stringSize)
			{
				data.resize(textOffset + stringSize);
			}
			memcpy(data.data() + textOffset, menuItem.csText.GetString(), stringSize);
			textOffset += stringSize;
		}
        else
        {
            // 为空字符串写入终止符
            if (data.size() < textOffset + sizeof(WCHAR))
            {
                data.resize(textOffset + sizeof(WCHAR));
            }
            *reinterpret_cast<WCHAR*>(data.data() + textOffset) = L'\0';
            textOffset += sizeof(WCHAR);
        }

        // 如果是弹出菜单，递归添加子项
        if (isPopup && !menuItem.vecSubMenu.empty())
		{
			size_t subMenuOffset = textOffset;
			for (const auto& subMenu : menuItem.vecSubMenu)
			{
				if (subMenu)
				{
					subMenuOffset = AddMenuItemToResource(data, subMenuOffset, *subMenu);
				}
			}
			return subMenuOffset;
		}

		return textOffset;
	}

#pragma endregion SMenuInfo

#pragma region SToolbarInfo


	SConvertedResData SToolbarInfo::Convert2ResData() const
	{
		SConvertedResData resData;
		resData.resType = RT_TOOLBAR;
		resData.nResID = this->nToolbarID;
		resData.langID = this->langID;

		std::vector<BYTE>& data = resData.vecData;
		data.resize(sizeof(TOOLBAR_TEMPLATE) + this->vecItemIDs.size() * sizeof(uint16_t));

		TOOLBAR_TEMPLATE* pHeader = reinterpret_cast<TOOLBAR_TEMPLATE*>(data.data());
		pHeader->wVersion = this->wVersion;
		pHeader->wWidth = this->wWidth;
		pHeader->wHeight = this->wHeight;
		pHeader->wItemCount = static_cast<uint16_t>(this->vecItemIDs.size());

		uint16_t* pItems = reinterpret_cast<uint16_t*>(pHeader + 1);
		for (size_t i = 0; i < this->vecItemIDs.size(); ++i)
		{
			pItems[i] = this->vecItemIDs[i];
		}

		return resData;
	}

	SToolbarInfo& SToolbarInfo::operator+=(const SToolbarInfo& other)
	{
		if (this->nToolbarID != other.nToolbarID /*|| this->langID != other.langID*/)
		{
			return *this;
		}

		// 合并按钮ID，并保持唯一性
		for (uint16_t itemID : other.vecItemIDs)
		{
			if (std::find(this->vecItemIDs.begin(), this->vecItemIDs.end(), itemID) == this->vecItemIDs.end())
			{
				this->vecItemIDs.push_back(itemID);
			}
		}
		return *this;
	}

	SToolbarInfo& SToolbarInfo::operator-=(const SToolbarInfo& other)
	{
		if (this->nToolbarID != other.nToolbarID /*|| this->langID != other.langID*/)
		{
			return *this;
		}

		// 移除按钮ID
		for (uint16_t itemID : other.vecItemIDs)
		{
			this->vecItemIDs.erase(
				std::remove(this->vecItemIDs.begin(), this->vecItemIDs.end(), itemID),
				this->vecItemIDs.end());
		}
		return *this;
	}

	SToolbarInfo SToolbarInfo::operator+(const SToolbarInfo& other) const
	{
		SToolbarInfo result = *this;
		result += other;
		return result;
	}

	SToolbarInfo SToolbarInfo::operator-(const SToolbarInfo& other) const
	{
		SToolbarInfo result = *this;
		result -= other;
		return result;
	}
#pragma endregion SToolbarInfo

#pragma region SSingleResourceInfo

	SSingleResourceInfo& SSingleResourceInfo::operator+=(const SSingleResourceInfo& other)
	{
	    // 合并 Dialog 信息
	    for (auto it = other.mapLangAndDialogInfo.begin(); it != other.mapLangAndDialogInfo.end(); ++it)
	    {
	        // 如果当前对象中不存在该窗口，尝试通过资源名称回退匹配
	        if (mapLangAndDialogInfo.find(it->first) == mapLangAndDialogInfo.end())
	        {
	            // 按 csResourceName 回退查找（处理合成ID / 宏未解析的情况）
	            // 安全性：仅当唯一匹配时才允许回退，防止跨窗口误合并
	            bool mergedByName = false;
	            if (it->second && !it->second->csResourceName.IsEmpty())
	            {
	                int matchCount = 0;
	                decltype(mapLangAndDialogInfo)::iterator uniqueMatch;
	                for (auto kvIt = mapLangAndDialogInfo.begin(); kvIt != mapLangAndDialogInfo.end(); ++kvIt)
	                {
	                    if (kvIt->second && !kvIt->second->csResourceName.IsEmpty() &&
	                        kvIt->second->csResourceName.CompareNoCase(it->second->csResourceName) == 0)
	                    {
	                        uniqueMatch = kvIt;
	                        ++matchCount;
	                        if (matchCount > 1) break; // 歧义：多个窗口同名，放弃回退
	                    }
	                }
	                if (matchCount == 1)
	                {
	                    *uniqueMatch->second += *it->second;
	                    mergedByName = true;
	                }
	            }
	            if (!mergedByName)
	                mapLangAndDialogInfo[it->first] = std::make_shared<SDialogInfo>(*it->second);
	        }
	        else
	        {
	            // 如果存在，则合并控件信息
	            *mapLangAndDialogInfo[it->first] += *it->second;
	        }
	    }

	    // 合并 StringTable 信息
	    for (auto it = other.mapLangAndStringTableInfo.begin(); it != other.mapLangAndStringTableInfo.end(); ++it)
	    {
	        if (mapLangAndStringTableInfo.find(it->first) == mapLangAndStringTableInfo.end())
	        {
	            mapLangAndStringTableInfo[it->first] = std::make_shared<SStringTableInfo>(*it->second);
	        }
	        else
	        {
	            *mapLangAndStringTableInfo[it->first] += *it->second;
	        }
	    }

	    // 合并 Version 信息
	    if (other.pVersionInfo)
	    {
	        if (!pVersionInfo)
	        {
	            pVersionInfo = std::make_shared<SVersionInfo>(*other.pVersionInfo);
	        }
	        else
	        {
	            *pVersionInfo += *other.pVersionInfo;
	        }
	    }

	    // 合并 Menu 信息
	    for (auto it = other.mapLangAndMenuInfo.begin(); it != other.mapLangAndMenuInfo.end(); ++it)
	    {
	        if (mapLangAndMenuInfo.find(it->first) == mapLangAndMenuInfo.end())
	        {
	            mapLangAndMenuInfo[it->first] = std::make_shared<SMenuInfo>(*it->second);
	        }
	        else
	        {
	            *mapLangAndMenuInfo[it->first] += *it->second;
	        }
	    }

	    // 合并 Toolbar 信息
	    for (auto it = other.mapLangAndToolbarInfo.begin(); it != other.mapLangAndToolbarInfo.end(); ++it)
	    {
	        if (mapLangAndToolbarInfo.find(it->first) == mapLangAndToolbarInfo.end())
	        {
	            mapLangAndToolbarInfo[it->first] = std::make_shared<SToolbarInfo>(*it->second);
	        }
	        else
	        {
	            *mapLangAndToolbarInfo[it->first] += *it->second;
	        }
	    }

	    return *this;
	}

	SSingleResourceInfo& SSingleResourceInfo::operator-=(const SSingleResourceInfo& other)
	{
		// 移除 Dialog 信息
		for (auto it = other.mapLangAndDialogInfo.begin(); it != other.mapLangAndDialogInfo.end(); ++it)
		{
			if (mapLangAndDialogInfo.find(it->first) != mapLangAndDialogInfo.end())
			{
				*mapLangAndDialogInfo[it->first] -= *it->second;
				if (mapLangAndDialogInfo[it->first]->vecCtrlInfo.empty())
				{
					mapLangAndDialogInfo.erase(it->first);
				}
			}
		}

		// 移除 StringTable 信息
		for (auto it = other.mapLangAndStringTableInfo.begin(); it != other.mapLangAndStringTableInfo.end(); ++it)
		{
			if (mapLangAndStringTableInfo.find(it->first) != mapLangAndStringTableInfo.end())
			{
				*mapLangAndStringTableInfo[it->first] -= *it->second;
				if (mapLangAndStringTableInfo[it->first]->mapIDAndString.empty())
				{
					mapLangAndStringTableInfo.erase(it->first);
				}
			}
		}

		// 移除 Version 信息
		if (other.pVersionInfo && pVersionInfo)
		{
			*pVersionInfo -= *other.pVersionInfo;
			if (pVersionInfo->mapStringInfo.empty())
			{
				pVersionInfo = nullptr;
			}
		}

		// 移除 Menu 信息
		for (auto it = other.mapLangAndMenuInfo.begin(); it != other.mapLangAndMenuInfo.end(); ++it)
		{
			if (mapLangAndMenuInfo.find(it->first) != mapLangAndMenuInfo.end())
			{
				*mapLangAndMenuInfo[it->first] -= *it->second;
				if (mapLangAndMenuInfo[it->first]->vecSubMenu.empty())
				{
					mapLangAndMenuInfo.erase(it->first);
				}
			}
		}

		// 移除 Toolbar 信息
		for (auto it = other.mapLangAndToolbarInfo.begin(); it != other.mapLangAndToolbarInfo.end(); ++it)
		{
			if (mapLangAndToolbarInfo.find(it->first) != mapLangAndToolbarInfo.end())
			{
				*mapLangAndToolbarInfo[it->first] -= *it->second;
				if (mapLangAndToolbarInfo[it->first]->vecItemIDs.empty())
				{
					mapLangAndToolbarInfo.erase(it->first);
				}
			}
		}

		return *this;
	}

	SSingleResourceInfo SSingleResourceInfo::operator+(const SSingleResourceInfo& other) const
	{
		SSingleResourceInfo result = *this;
		result += other;
		return result;
	}

	SSingleResourceInfo SSingleResourceInfo::operator-(const SSingleResourceInfo& other) const
	{
		SSingleResourceInfo result = *this;
		result -= other;
		return result;
	}

	std::vector<rceditor::SConvertedResData> SSingleResourceInfo::Convert2ResData() const
	{
		std::vector<SConvertedResData> resData;

		// 转换 Dialog 信息
		for (auto it = mapLangAndDialogInfo.begin(); it != mapLangAndDialogInfo.end(); ++it)
		{
			resData.push_back(it->second->Convert2ResData());
		}

		// 转换 StringTable 信息
		for (auto it = mapLangAndStringTableInfo.begin(); it != mapLangAndStringTableInfo.end(); ++it)
		{
			resData.push_back(it->second->Convert2ResData(langID));
		}

		// 转换 Version 信息
		if (pVersionInfo)
		{
			resData.push_back(pVersionInfo->Convert2ResData());
		}

		// 转换 Menu 信息
		for (auto it = mapLangAndMenuInfo.begin(); it != mapLangAndMenuInfo.end(); ++it)
		{
			resData.push_back(it->second->Convert2ResData());
		}

		// 转换 Toolbar 信息
		for (auto it = mapLangAndToolbarInfo.begin(); it != mapLangAndToolbarInfo.end(); ++it)
		{
			resData.push_back(it->second->Convert2ResData());
		}

		return resData;
	}
#pragma endregion SSingleResourceInfo

	WORD GetClassIdFromName(const CStringW& className)
	{
		auto it = MAP_CONTROL_NAME_WORD.find(className);
		if (it != MAP_CONTROL_NAME_WORD.end()) {
			return it->second;
		}
		return 0;
	}

	size_t AddStringToResource(std::vector<BYTE>& resource, size_t offset, const CStringW& str)
	{
		if (str.IsEmpty()) {
			// 添加空字符串标记
			if (offset + sizeof(WORD) > resource.size()) {
				resource.resize(offset + sizeof(WORD));
			}
			*reinterpret_cast<WORD*>(resource.data() + offset) = 0;
			return offset + sizeof(WORD);
		}

		// 计算字符串大小（包括结尾的NULL）
		size_t stringSize = (str.GetLength() + 1) * sizeof(WCHAR);

		// 确保vector有足够空间
		if (offset + stringSize > resource.size()) {
			resource.resize(offset + stringSize);
		}

		// 复制字符串
		memcpy(resource.data() + offset, str.GetString(), stringSize);
		return offset + stringSize;
	}

	CStringW GetClassNameFromRc(WORD wClassId)
	{
		switch (wClassId)
		{
		case 0x80: return L"BUTTON";
		case 0x81: return L"EDIT";
		case 0x82: return L"STATIC";
		case 0x83: return L"LISTBOX";
		case 0x84: return L"SCROLLBAR";
		case 0x85: return L"COMBOBOX";
		case 0x86: return L"MDICLIENT";
		case 0x87: return L"COMBOLBOX";
		case 0x88: return L"DDEMLEEVENT";
			// 0x89-0x8E 保留为旧版控件
		case 0x8F: return L"TOOLBARWINDOW32";
		case 0x90: return L"MSCTLS_STATUSBAR32";
		case 0x91: return L"MSCTLS_PROGRESS32";
		case 0x92: return L"MSCTLS_TRACKBAR32";
		case 0x93: return L"MSCTLS_UPDOWN32";
		case 0x94: return L"MSCTLS_HOTKEY32";
		case 0x95: return L"SYSHEADER32";
		case 0x96: return L"SYSLISTVIEW32";
		case 0x97: return L"SYSTREEVIEW32";
		case 0x98: return L"SYSTABCONTROL32";
		case 0x99: return L"SYSANIMATE32";
		case 0x9A: return L"SYSDATETIMEPICK32";
		case 0x9B: return L"SYSMONTHCAL32";
		case 0x9C: return L"TOOLTIPS_CLASS32";
		case 0x9D: return L"REBARWINDOW32";
		case 0x9E: return L"MSCTLS_TOOLBAR32";
		case 0x9F: return L"SYSLINK";
		case 0xA0: return L"RICHEDIT";
		case 0xA1: return L"RichEdit20W";
		case 0xA2: return L"RichEdit50W";
		case 0xA3: return L"SysIPAddress32";
		case 0xA4: return L"SysPager";
		case 0xA5: return L"NativeFontCtl";
		case 0xA6: return L"DirectUIHWND";
		case 0xA7: return L"Windows.UI.Core.CoreWindow";
		default:
			if (wClassId >= 0xC0 && wClassId <= 0xFFFF)
			{
				wchar_t buffer[64];
				swprintf_s(buffer, L"ATOM_0x%04X", wClassId);
				return buffer;
			}
			return L"UNKNOWN";
		}
	}

}

// 向后兼容的全局别名：一些旧的目标文件在全局命名空间中引用名为
// `gbCurNeedShowLan` 的符号。这里提供一个引用，将其绑定到命名空间内的存储，
// 使两种用法指向相同的值。
unsigned short& gbCurNeedShowLan = rceditor::gbCurNeedShowLan;
