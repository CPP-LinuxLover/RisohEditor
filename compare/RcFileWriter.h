#pragma once

/*!
 * \file RcFileWriter.h
 * \brief 完整 .rc 文件 & resource.h 导出器
 *
 * 将内存中的 SRcFileContent / SResourceHeader 序列化为源文件，
 * 严格保留原始编码、行结束符、缩进风格。
 */

#include <afxwin.h>
#include <vector>
#include "RcFileParser.h"
#include "RcCore.h"

namespace rceditor {

/*!
 * \brief RC 文件写入器
 *
 * 功能：
 * 1. 将 SRcFileContent 写回 .rc 源文件
 * 2. 将 SResourceHeader 写回 resource.h
 * 3. 严格保持原始编码 / 行结束符 / 缩进风格
 * 4. 新增的资源块按同类型区域追加
 * 5. 可将未在 RC 文件中存在的内存资源数据序列化为标准 RC 文本
 */
class CRcFileWriter
{
public:
    CRcFileWriter();

    // ---- 主接口 ----

    /*!
     * \brief 将 SRcFileContent 写出为 .rc 文件
     * \param content 内存中的 RC 文件数据
     * \param outputPath 输出路径
     * \return 成功返回 true
     */
    bool WriteRcFile(const SRcFileContent& content, const CStringW& outputPath);

    /*!
     * \brief 将 SResourceHeader 写出为 resource.h
     * \param header 内存中的资源头文件数据
     * \param outputPath 输出路径
     * \return 成功返回 true
     */
    bool WriteResourceHeader(const SResourceHeader& header, const CStringW& outputPath);

    /*!
     * \brief 拼接所有行为单个字符串 (用于预览)
     * \param content 内存中的 RC 文件数据
     * \return 拼接后的文本
     */
    CStringW JoinLines(const SRcFileContent& content) const;

    /*!
     * \brief 拼接 resource.h 所有行为单个字符串 (用于预览)
     * \param header 内存中的头文件数据
     * \return 拼接后的文本
     */
    CStringW JoinHeaderLines(const SResourceHeader& header) const;

    // ---- 资源块序列化 (内存 → RC 文本行) ----

    /*!
     * \brief 将 SDialogInfo 序列化为 Dialog 资源块文本行
     */
    std::vector<CStringW> SerializeDialog(const std::shared_ptr<SDialogInfo>& dlg,
                                          const CStringW& dialogName,
                                          const SResourceHeader& header,
                                          const SIndentStyle& style) const;

    /*!
     * \brief 将 SStringTableInfo 序列化为 StringTable 资源块文本行
     */
    std::vector<CStringW> SerializeStringTable(const std::shared_ptr<SStringTableInfo>& st,
                                               const SResourceHeader& header,
                                               const SIndentStyle& style) const;

    /*!
     * \brief 将 SMenuInfo 序列化为 Menu 资源块文本行
     */
    std::vector<CStringW> SerializeMenu(const std::shared_ptr<SMenuInfo>& menu,
                                        const CStringW& menuName,
                                        const SResourceHeader& header,
                                        const SIndentStyle& style) const;

    // ---- 获取最近错误 ----
    const CStringW& GetLastError() const { return m_lastError; }

    // ---- 格式化辅助（公开供外部使用） ----
    CStringW FormatId(UINT id, const SResourceHeader& header) const;
    CStringW EscapeRcString(const CStringW& text) const;

private:
    // ---- 编码写出 ----
    bool WriteStringToFile(const CStringW& text, const CStringW& path,
                           EFileEncoding encoding, const CStringW& lineEnding);

    // ---- 菜单递归序列化 ----
    void SerializeMenuItems(const std::shared_ptr<SMenuInfo>& menuItem,
                            const SResourceHeader& header,
                            const SIndentStyle& style,
                            int depth,
                            std::vector<CStringW>& outLines) const;

    // ---- 辅助 ----
    CStringW MakeIndent(const SIndentStyle& style, int depth = 1) const;
    CStringW FormatStyleFlags(DWORD style, bool exStyle) const;

    CStringW m_lastError;
};

} // namespace rceditor
