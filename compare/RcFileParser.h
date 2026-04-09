#pragma once

/*!
 * \file RcFileParser.h
 * \brief RC 文本文件解析器 & resource.h 解析器
 *
 * 将 .rc 源文件和 resource.h 头文件完整加载到内存中，
 * 保留行结构、编码元数据、注释和预处理指令。
 *
 * 解析范围：
 * - DIALOG / DIALOGEX (含控件)
 * - STRINGTABLE
 * - MENU / MENUEX (递归)
 * - ACCELERATORS
 * - TOOLBAR
 * - VERSIONINFO
 */

#include <afxwin.h>
#include <vector>
#include <map>
#include <string>
#include <memory>
#include "RcCore.h"

namespace rceditor {

// ============================================================================
// 枚举定义
// ============================================================================

/*!
 * \brief 文件编码类型
 */
enum class EFileEncoding
{
    Unknown,         /*!< 未知 */
    ANSI,            /*!< ANSI (当前代码页) */
    UTF8_NoBOM,      /*!< UTF-8 无 BOM */
    UTF8_BOM,        /*!< UTF-8 with BOM (EF BB BF) */
    UTF16LE_BOM,     /*!< UTF-16 LE with BOM (FF FE) */
    UTF16BE_BOM,     /*!< UTF-16 BE with BOM (FE FF) */
};

/*!
 * \brief RC 行类型
 */
enum class ERcLineType
{
    Blank,           /*!< 空行 */
    Comment,         /*!< 注释行 (// 或 C 风格) */
    Preprocessor,    /*!< 预处理指令 (#include, #if, #ifdef 等) */
    ResourceHeader,  /*!< 资源头行 (如 "IDD_DIALOG1 DIALOGEX ...") */
    BlockBegin,      /*!< BEGIN 或 { */
    BlockEnd,        /*!< END 或 } */
    Content,         /*!< 资源块内容行 (控件、菜单项、字符串等) */
    Other,           /*!< 其他 */
};

/*!
 * \brief RC 资源块类型
 */
enum class ERcBlockType
{
    Unknown,
    Dialog,          /*!< DIALOG / DIALOGEX */
    StringTable,     /*!< STRINGTABLE */
    Menu,            /*!< MENU / MENUEX */
    Accelerators,    /*!< ACCELERATORS */
    Toolbar,         /*!< TOOLBAR */
    VersionInfo,     /*!< VERSIONINFO */
    Other,           /*!< 未识别的资源类型 */
};

// ============================================================================
// 数据结构
// ============================================================================

/*!
 * \brief RC 文件中的一行
 */
struct SRcLine
{
    CStringW text;           /*!< 行文本（不含行结束符） */
    int lineNumber;          /*!< 原始行号 (1-based) */
    int blockIndex;          /*!< 所属资源块的索引 (-1 表示顶层) */
    ERcLineType type;        /*!< 行类型 */

    SRcLine() : lineNumber(0), blockIndex(-1), type(ERcLineType::Other) {}
};

/*!
 * \brief RC 资源块
 */
struct SRcBlock
{
    ERcBlockType type;           /*!< 资源块类型 */
    UINT resourceID;             /*!< 资源 ID 数值 (如 IDD_DIALOG1 = 102) */
    CStringW resourceName;       /*!< 资源 ID 宏名 (如 "IDD_DIALOG1") */
    CStringW resourceTypeKeyword;/*!< 关键词 (如 "DIALOGEX", "MENU") */
    int headerLineIndex;         /*!< 资源声明行在 lines 中的索引 */
    int beginLineIndex;          /*!< BEGIN/{ 行的索引 */
    int endLineIndex;            /*!< END/} 行的索引 */
    int blockIndex;              /*!< 本块在 blocks 列表中的索引 */

    // Dialog 专用属性行索引 (STYLE, EXSTYLE, CAPTION, FONT, CLASS, MENU)
    std::vector<int> attributeLineIndices;

    SRcBlock()
        : type(ERcBlockType::Unknown)
        , resourceID(0)
        , headerLineIndex(-1)
        , beginLineIndex(-1)
        , endLineIndex(-1)
        , blockIndex(-1)
    {}
};

/*!
 * \brief 缩进风格
 */
struct SIndentStyle
{
    bool useTabs;       /*!< 使用 Tab 还是空格 */
    int indentWidth;    /*!< 缩进宽度（空格数或 1 表示 Tab） */

    SIndentStyle() : useTabs(false), indentWidth(4) {}
};

/*!
 * \brief RC 文件完整内容
 */
struct SRcFileContent
{
    CStringW filePath;                   /*!< 文件路径 */
    EFileEncoding encoding;              /*!< 原始编码 */
    CStringW lineEnding;                 /*!< 行结束符 ("\r\n" / "\n") */
    SIndentStyle indentStyle;            /*!< 缩进风格 */

    std::vector<SRcLine> lines;          /*!< 按行存储 */
    std::vector<SRcBlock> blocks;        /*!< 资源块列表 */

    // 索引辅助
    std::map<UINT, int> idToBlockIndex;  /*!< 资源ID → 块索引 (同类型内查找) */
    std::map<CStringW, int> nameToBlockIndex; /*!< 资源宏名 → 块索引 */

    SRcFileContent() : encoding(EFileEncoding::Unknown) {}
};

// ============================================================================
// resource.h 解析结构
// ============================================================================

/*!
 * \brief resource.h 中的一个 #define 条目
 */
struct SDefineEntry
{
    CStringW name;       /*!< 宏名称 (如 "IDC_BUTTON1") */
    UINT value;          /*!< 数值 (如 1001) */
    int lineIndex;       /*!< 在 rawLines 中的行号索引 */
    CStringW prefix;     /*!< ID 前缀 ("IDC_", "IDD_", "IDR_" 等) */

    SDefineEntry() : value(0), lineIndex(-1) {}
};

/*!
 * \brief resource.h 文件完整内容
 */
struct SResourceHeader
{
    CStringW filePath;                       /*!< 文件路径 */
    EFileEncoding encoding;                  /*!< 原始编码 */
    CStringW lineEnding;                     /*!< 行结束符 */
    std::vector<SRcLine> rawLines;           /*!< 完整行存储 */
    std::vector<SDefineEntry> defines;       /*!< 所有 #define 条目 */

    std::map<CStringW, UINT> nameToId;       /*!< 宏名 → 数值 */
    std::map<UINT, CStringW> idToName;       /*!< 数值 → 宏名 */

    UINT maxUsedId;                          /*!< 当前最大 ID 值 */

    SResourceHeader() : encoding(EFileEncoding::Unknown), maxUsedId(0) {}

    /*!
     * \brief 获取指定前缀的下一个可用 ID
     */
    UINT GetNextAvailableId(const CStringW& prefix = L"IDC_") const;

    /*!
     * \brief 检查指定宏名是否已存在
     */
    bool HasDefine(const CStringW& name) const { return nameToId.find(name) != nameToId.end(); }

    /*!
     * \brief 检查指定数值是否已被使用
     */
    bool HasValue(UINT value) const { return idToName.find(value) != idToName.end(); }

    /*!
     * \brief 更新 _APS_NEXT_* 系列宏的值
     * \details 扫描所有已定义的 ID，计算各前缀的最大值+1，
     *          更新 _APS_NEXT_RESOURCE_VALUE, _APS_NEXT_COMMAND_VALUE,
     *          _APS_NEXT_CONTROL_VALUE, _APS_NEXT_SYMED_VALUE
     *          同时更新 rawLines 中对应行的文本
     */
    void UpdateApsNextValues();

    /*!
     * \brief 验证控件 ID 是否与 Dialog 父窗口范围内已有 ID 冲突
     * \param macroName 待检查的宏名
     * \param macroValue 待检查的宏值
     * \return 冲突描述，无冲突返回空字符串
     */
    CStringW ValidateNewDefine(const CStringW& macroName, UINT macroValue) const;
};

// ============================================================================
// 解析器类
// ============================================================================

/*!
 * \brief RC 文件解析器
 */
class CRcFileParser
{
public:
    CRcFileParser();

    // ---- 文件层 ----

    /*!
     * \brief 检测文件编码（BOM检测）
     * \param filePath 文件路径
     * \return 检测到的编码类型
     */
    static EFileEncoding DetectFileEncoding(const CStringW& filePath);

    /*!
     * \brief 读取文件到行数组
     * \param filePath 文件路径
     * \param outContent 输出的文件内容结构
     * \return 成功返回 true
     */
    bool ReadFileToLines(const CStringW& filePath, SRcFileContent& outContent);

    /*!
     * \brief 读取 resource.h 文件
     * \param filePath 文件路径
     * \param outContent 输出的文件内容结构（用于行存储）
     * \param outHeader 输出的头文件解析结果
     * \return 成功返回 true
     */
    bool ReadResourceHeader(const CStringW& filePath,
                            SRcFileContent& outContent,
                            SResourceHeader& outHeader);

    // ---- 词法层 ----

    /*!
     * \brief 分析行类型并识别资源块
     * \param content 已读入的文件内容，将被原地标注行类型和块索引
     * \return 成功返回 true
     */
    bool IdentifyBlocks(SRcFileContent& content);

    // ---- 语义层 ----

    /*!
     * \brief 解析已识别的资源块为对应的资源结构
     * \param content 解析后的文件内容
     * \param header resource.h 解析结果（用于 ID 名到数值的映射）
     * \param outResInfo 输出的资源信息
     * \return 成功返回 true
     */
    bool ParseResourceBlocks(const SRcFileContent& content,
                             const SResourceHeader& header,
                             SSingleResourceInfo& outResInfo);

    // ---- 完整解析流程 ----

    /*!
     * \brief 完整解析 .rc 文件
     * \param rcPath .rc 文件路径
     * \param headerPath resource.h 文件路径
     * \param outRcContent 输出的 RC 文件内容
     * \param outHeader 输出的 resource.h 解析结果
     * \param outResInfo 输出的资源信息
     * \return 成功返回 true
     */
    bool ParseAll(const CStringW& rcPath,
                  const CStringW& headerPath,
                  SRcFileContent& outRcContent,
                  SResourceHeader& outHeader,
                  SSingleResourceInfo& outResInfo);

    // ---- resource.h 解析 ----

    /*!
     * \brief 解析 resource.h
     * \param filePath 文件路径
     * \param outHeader 输出结果
     * \return 成功返回 true
     */
    bool ParseResourceHeader(const CStringW& filePath, SResourceHeader& outHeader);

    // ---- 错误信息 ----
    const std::vector<CStringW>& GetErrors() const { return m_errors; }
    const std::vector<CStringW>& GetWarnings() const { return m_warnings; }

    // ---- 辅助工具（公开供外部使用）----
    UINT ResolveIdentifier(const CStringW& token, const SResourceHeader& header) const;
    void Tokenize(const CStringW& line, std::vector<CStringW>& tokens) const;

private:
    // ---- 行类型识别 ----
    ERcLineType ClassifyLine(const CStringW& line) const;
    bool IsResourceHeaderLine(const CStringW& line, CStringW& outName, CStringW& outTypeKeyword) const;
    bool IsBlockBegin(const CStringW& line) const;
    bool IsBlockEnd(const CStringW& line) const;

    // ---- 资源块解析 ----
    bool ParseDialogBlock(const SRcFileContent& content,
                          const SRcBlock& block,
                          const SResourceHeader& header,
                          std::shared_ptr<SDialogInfo>& outDialog);

    bool ParseStringTableBlock(const SRcFileContent& content,
                               const SRcBlock& block,
                               const SResourceHeader& header,
                               std::map<UINT, std::shared_ptr<SStringTableInfo>>& outStringTables);

    bool ParseMenuBlock(const SRcFileContent& content,
                        const SRcBlock& block,
                        const SResourceHeader& header,
                        std::shared_ptr<SMenuInfo>& outMenu);

    bool ParseAcceleratorsBlock(const SRcFileContent& content,
                                const SRcBlock& block,
                                const SResourceHeader& header);

    bool ParseToolbarBlock(const SRcFileContent& content,
                           const SRcBlock& block,
                           const SResourceHeader& header,
                           std::shared_ptr<SToolbarInfo>& outToolbar);

    bool ParseVersionInfoBlock(const SRcFileContent& content,
                               const SRcBlock& block,
                               const SResourceHeader& header,
                               std::shared_ptr<SVersionInfo>& outVersionInfo);

    // ---- Dialog 控件行解析 ----
    bool ParseControlLine(const CStringW& line,
                          const SResourceHeader& header,
                          std::shared_ptr<SCtrlInfo>& outCtrl);

    // ---- 辅助 ----
    CStringW ExtractQuotedString(const CStringW& text, int& pos) const;
    CStringW TrimLine(const CStringW& line) const;
    SIndentStyle DetectIndentStyle(const SRcFileContent& content) const;
    CStringW DetectIdPrefix(const CStringW& name) const;

    void AddError(const CStringW& msg) { m_errors.push_back(msg); }
    void AddWarning(const CStringW& msg) { m_warnings.push_back(msg); }

    std::vector<CStringW> m_errors;
    std::vector<CStringW> m_warnings;
};

} // namespace rceditor
