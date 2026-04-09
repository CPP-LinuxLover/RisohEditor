#pragma once

/*!
 * \file RcFileMerger.h
 * \brief RC 文本级合并/注入引擎
 *
 * 将主语言的资源差异注入到次语言的 SRcFileContent 中，
 * 并在 SResourceHeader 中插入缺失的 #define 宏定义。
 * 注入位置参考主语言中的相对位置（前驱控件定位）。
 */

#include <afxwin.h>
#include <vector>
#include <map>
#include <memory>
#include "RcCore.h"
#include "RcFileParser.h"

namespace rceditor {

// ============================================================================
// 合并结果
// ============================================================================

/*!
 * \brief 单个注入操作的记录
 */
struct SInjectionRecord
{
    enum EInjectionType
    {
        Control,        /*!< 控件注入到 DIALOG 块 */
        StringEntry,    /*!< 字符串条目注入到 STRINGTABLE */
        MenuItem,       /*!< 菜单项注入到 MENU 块 */
        DefineEntry,    /*!< #define 宏注入到 resource.h */
        WholeBlock,     /*!< 整个资源块注入（次语言缺失整个 Dialog/Menu 等） */
    };

    EInjectionType type;
    UINT resourceId;         /*!< 资源 ID */
    UINT controlId;          /*!< 控件 ID (如适用) */
    int insertedAtLine;      /*!< 在次语言文件中的插入行号 */
    CStringW insertedText;   /*!< 插入的文本内容 */
    CStringW description;    /*!< 描述信息 */

    SInjectionRecord()
        : type(Control), resourceId(0), controlId(0), insertedAtLine(-1) {}
};

/*!
 * \brief 合并结果
 */
struct SMergeResult
{
    int totalInjectedControls;    /*!< 注入的控件数 */
    int totalInjectedStrings;     /*!< 注入的字符串条目数 */
    int totalInjectedMenuItems;   /*!< 注入的菜单项数 */
    int totalInjectedDefines;     /*!< 注入的 #define 数 */
    int totalInjectedBlocks;      /*!< 注入的完整资源块数 */

    std::vector<SInjectionRecord> injections; /*!< 所有注入记录 */
    std::vector<SConflictInfo> conflicts;     /*!< 检测到的冲突 */
    std::vector<CStringW> warnings;           /*!< 警告信息 */

    SMergeResult()
        : totalInjectedControls(0)
        , totalInjectedStrings(0)
        , totalInjectedMenuItems(0)
        , totalInjectedDefines(0)
        , totalInjectedBlocks(0)
    {}

    int TotalInjected() const
    {
        return totalInjectedControls + totalInjectedStrings +
               totalInjectedMenuItems + totalInjectedBlocks;
    }
};

// ============================================================================
// 合并器类
// ============================================================================

/*!
 * \brief RC 文本级合并器
 */
class CRcFileMerger
{
public:
    CRcFileMerger();

    /*!
     * \brief 执行完整的 RC 文件合并
     * \param primaryRc 主语言 RC 文件内容
     * \param primaryHeader 主语言 resource.h
     * \param primaryRes 主语言的结构化资源信息
     * \param secondaryRc [in/out] 次语言 RC 文件内容（将被原地修改）
     * \param secondaryHeader [in/out] 次语言 resource.h（将被原地修改）
     * \param secondaryRes [in/out] 次语言的结构化资源信息（将被更新）
     * \return 合并结果
     */
    SMergeResult MergeAll(
        const SRcFileContent& primaryRc,
        const SResourceHeader& primaryHeader,
        const SSingleResourceInfo& primaryRes,
        SRcFileContent& secondaryRc,
        SResourceHeader& secondaryHeader,
        SSingleResourceInfo& secondaryRes);

    /*!
     * \brief 仅执行冲突检测（不修改任何数据）
     */
    std::vector<SConflictInfo> DetectConflicts(
        const SResourceHeader& primaryHeader,
        const SResourceHeader& secondaryHeader,
        const SSingleResourceInfo& primaryRes,
        const SSingleResourceInfo& secondaryRes);

private:
    // ---- Dialog 合并 ----
    int MergeDialogs(
        const SRcFileContent& primaryRc,
        const SResourceHeader& primaryHeader,
        const SSingleResourceInfo& primaryRes,
        SRcFileContent& secondaryRc,
        SResourceHeader& secondaryHeader,
        SSingleResourceInfo& secondaryRes,
        SMergeResult& result);

    /*!
     * \brief 将单个控件注入到次语言 Dialog 块
     * \param primaryBlock 主语言的 Dialog 块
     * \param primaryRc 主语言 RC 内容
     * \param ctrl 要注入的控件
     * \param predecessorIDC 主语言中的前驱控件 IDC（用于定位插入位置）
     * \param secondaryBlock 次语言的 Dialog 块
     * \param secondaryRc [in/out] 次语言 RC 内容
     * \param result [in/out] 合并结果
     * \return 成功返回 true
     */
    bool InjectControlIntoBlock(
        const SRcBlock& primaryBlock,
        const SRcFileContent& primaryRc,
        const std::shared_ptr<SCtrlInfo>& ctrl,
        UINT predecessorIDC,
        const SRcBlock& secondaryBlock,
        SRcFileContent& secondaryRc,
        const SResourceHeader& primaryHeader,
        const SResourceHeader& secondaryHeader,
        SMergeResult& result);

    // ---- StringTable 合并 ----
    int MergeStringTables(
        const SRcFileContent& primaryRc,
        const SResourceHeader& primaryHeader,
        const SSingleResourceInfo& primaryRes,
        SRcFileContent& secondaryRc,
        SResourceHeader& secondaryHeader,
        SSingleResourceInfo& secondaryRes,
        SMergeResult& result);

    // ---- Menu 合并 ----
    int MergeMenus(
        const SRcFileContent& primaryRc,
        const SResourceHeader& primaryHeader,
        const SSingleResourceInfo& primaryRes,
        SRcFileContent& secondaryRc,
        SResourceHeader& secondaryHeader,
        SSingleResourceInfo& secondaryRes,
        SMergeResult& result);

    // ---- resource.h 宏合并 ----
    int MergeDefines(
        const SResourceHeader& primaryHeader,
        SResourceHeader& secondaryHeader,
        SMergeResult& result);

    // ---- 冲突检测 ----
    void DetectMacroConflicts(
        const SResourceHeader& primaryHeader,
        const SResourceHeader& secondaryHeader,
        std::vector<SConflictInfo>& conflicts);

    void DetectControlIdConflicts(
        const SSingleResourceInfo& primaryRes,
        const SSingleResourceInfo& secondaryRes,
        std::vector<SConflictInfo>& conflicts);

    // ---- 辅助函数 ----

    /*!
     * \brief 找到控件在主语言 Dialog 中的前驱控件 IDC
     */
    UINT FindPredecessorIDC(
        const std::shared_ptr<SDialogInfo>& primaryDlg,
        int ctrlIndex) const;

    /*!
     * \brief 在次语言 Dialog 块中找到某个 IDC 的行位置
     */
    int FindControlLineByIDC(
        const SRcFileContent& rc,
        const SRcBlock& block,
        UINT idcValue,
        const SResourceHeader& header) const;

    /*!
     * \brief 使用混合匹配策略判断主语言控件是否在次语言中存在
     * \details 先用快速路径(±2 DLU)统计匹配率，匹配率低于阈值时启用锚点分析。
     * \param pPrimaryDlg 主语言对话框
     * \param pSecDlg 次语言对话框
     * \param[out] outMissingIndices 缺失控件在主语言 vecCtrlInfo 中的索引
     */
    void FindMissingControlsHybrid(
        const std::shared_ptr<SDialogInfo>& pPrimaryDlg,
        const std::shared_ptr<SDialogInfo>& pSecDlg,
        std::vector<int>& outMissingIndices);

    /*!
     * \brief 计算注入控件的目标位置（锚点修正或比例缩放）
     */
    CRect ComputeInjectionPosition(
        const std::shared_ptr<SCtrlInfo>& ctrl,
        int ctrlIndex,
        const std::shared_ptr<SDialogInfo>& primaryDlg,
        const std::shared_ptr<SDialogInfo>& secondaryDlg,
        const SDialogBoard& priBoard);

    /*!
     * \brief 生成控件的 RC 文本行
     */
    CStringW GenerateControlLine(
        const std::shared_ptr<SCtrlInfo>& ctrl,
        const SResourceHeader& header,
        const SIndentStyle& style) const;

    /*!
     * \brief 生成 StringTable 条目行
     */
    CStringW GenerateStringEntryLine(
        UINT stringId,
        const CStringW& text,
        const SResourceHeader& header,
        const SIndentStyle& style) const;

    /*!
     * \brief 生成 #define 行
     */
    CStringW GenerateDefineLine(
        const CStringW& name,
        UINT value) const;

    /*!
     * \brief 在 resource.h 中找到同前缀区间的插入位置
     */
    int FindDefineInsertionPoint(
        const SResourceHeader& header,
        const CStringW& prefix,
        UINT value) const;

    /*!
     * \brief 在 SRcFileContent 中插入一行
     */
    void InsertLine(SRcFileContent& content, int afterLineIndex,
                    const CStringW& text, ERcLineType lineType, int blockIndex);

    /*!
     * \brief 在 SResourceHeader 中插入一行（同步 rawLines 和 defines）
     */
    void InsertDefineLine(SResourceHeader& header, int afterLineIndex,
                          const CStringW& name, UINT value);

    CStringW LookupIdName(UINT id, const SResourceHeader& header) const;
};

} // namespace rceditor
