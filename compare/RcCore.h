#pragma once
#include <map>
#include <set>
#include <iostream>
#include <vector>
#include <utility>
#include <memory>
#include <string>
#include <atlconv.h> // for conversion helpers in both ANSI and UNICODE builds
#include <initializer_list>


namespace rceditor
{

    // 全局标志（之前定义在 LevelManagerGlobals.cpp）
    // 已移至此处以集中管理核心级别的全局变量。
    extern bool gbIsLevelAdmin;               //!< 当前用户是否被视为等级管理器管理员
    extern unsigned short gbCurNeedShowLan;   //!< 等级管理器使用的当前界面语言索引


    // 字符集转换辅助：在此封装 CStringW、CStringA 与 std::string（UTF-8）之间的转换。
    // 这样可以在 ANSI 与 Unicode 构建中保持兼容性。
    class CStringConverter
    {
    public:
        // 将宽字符 (UTF-16) 转为窄字符 (ANSI)，使用当前代码页
        static CStringA ToAnsi(const CStringW& w)
        {
            if (w.IsEmpty()) return CStringA("");
#ifdef _MSC_VER
            CW2A conv(w.GetString());
            return CStringA(conv);
#else
            // Fallback: naive conversion
            int len = WideCharToMultiByte(CP_ACP, 0, w.GetString(), -1, nullptr, 0, nullptr, nullptr);
            CStringA out;
            if (len > 0)
            {
                std::string buf(len, '\0');
                WideCharToMultiByte(CP_ACP, 0, w.GetString(), -1, &buf[0], len, nullptr, nullptr);
                out = CStringA(buf.c_str());
            }
            return out;
#endif
        }

        // 将窄字符 (ANSI) 转为宽字符 (UTF-16)，使用当前代码页
        static CStringW ToWide(const CStringA& a)
        {
            if (a.IsEmpty()) return CStringW(L"");
#ifdef _MSC_VER
            CA2W conv(a.GetString());
            return CStringW(conv);
#else
            int len = MultiByteToWideChar(CP_ACP, 0, a.GetString(), -1, nullptr, 0);
            CStringW out;
            if (len > 0)
            {
                std::wstring buf(len, L'\0');
                MultiByteToWideChar(CP_ACP, 0, a.GetString(), -1, &buf[0], len);
                out = CStringW(buf.c_str());
            }
            return out;
#endif
        }

        // 将宽字符转换为 UTF-8 的 std::string
        static std::string ToUtf8(const CStringW& w)
        {
            if (w.IsEmpty()) return std::string();
            int len = WideCharToMultiByte(CP_UTF8, 0, w.GetString(), -1, nullptr, 0, nullptr, nullptr);
            if (len <= 0) return std::string();
            std::string out(len, '\0');
            WideCharToMultiByte(CP_UTF8, 0, w.GetString(), -1, &out[0], len, nullptr, nullptr);
            if (!out.empty() && out.back() == '\0') out.pop_back();
            return out;
        }

        // 将 UTF-8 std::string 转为宽字符
        static CStringW FromUtf8(const std::string& s)
        {
            if (s.empty()) return CStringW(L"");
            int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
            if (len <= 0) return CStringW(L"");
            std::wstring out(len, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &out[0], len);
            if (!out.empty() && out.back() == L'\0') out.pop_back();
            return CStringW(out.c_str());
        }

        // 根据构建类型在 CString 类型之间转换
        static CString ConvertToT(const CStringW& w)
        {
#ifdef UNICODE
            return CString(w);
#else
            return CString(ToAnsi(w));
#endif
        }

        static CString ConvertToT(const CStringA& a)
        {
#ifdef UNICODE
            return CString(ToWide(a));
#else
            return CString(a);
#endif
        }
    };

// Helper macro for embedding UTF-8 string literals into platform TString.
// Usage: ML(u8"文本") -> expands to LPCTSTR that can be passed to Win32/MFC APIs.
#ifndef ML
#if !defined(LEVEL_LOCALIZE)
#define ML(u8literal) (LPCTSTR)rceditor::CrossPlatformStringForLevel(u8literal)
#else
// When LEVEL_LOCALIZE is defined we avoid defining the ML macro to allow
// LevelLocalize.h to provide a function-style ML helper (prevents macro
// substitution conflicts when LevelLocalize declares a function named ML).
#endif
#endif

    // 轻量级辅助用于等级本地化字符串。
    // 存储从 UTF-8 字面量派生的本地编码 std::string。
    // 行为与之前在 LevelLocalize.h 中的 CrossPlatformStringForLevel 实现一致：
    // 当系统区域为 UTF-8 时直接存储字节；否则通过 CStringConverter 将 UTF-8 -> 宽 -> ANSI 进行转换。
    class CrossPlatformStringForLevel
    {
    private:
        // Store as a platform `CString` (TCHAR-aware) so usages that pass
        // the result to Win32/UI APIs always get the correct encoding.
        CString m_cs;

    public:
        explicit CrossPlatformStringForLevel(const char* utf8Str)
        {
            if (!utf8Str)
            {
                m_cs.Empty();
                return;
            }
            // Always convert UTF-8 literal -> wide -> platform TCHAR CString.
            CStringW w = CStringConverter::FromUtf8(std::string(utf8Str));
            m_cs = CStringConverter::ConvertToT(w);
        }

        // Convert UTF-8 literal to platform local encoding as std::string.
        // For Unicode builds this will produce UTF-8 bytes stored in std::string;
        // for ANSI builds it will produce bytes in the current ANSI code page.
        static std::string UTF8ToLocal(const char* utf8Str)
        {
            if (!utf8Str) return std::string();
#ifdef UNICODE
            // In Unicode builds we treat "local" as UTF-8 bytes when stored
            // in std::string to avoid lossy ANSI conversions.
            return std::string(utf8Str);
#else
            // ANSI build: convert UTF-8 -> wide -> ANSI bytes
            CStringW w = CStringConverter::FromUtf8(std::string(utf8Str));
            CStringA a = CStringConverter::ToAnsi(w);
            return std::string(a.GetString());
#endif
        }

        // Convert a platform local C-string to UTF-8 std::string.
        static std::string LocalToUTF8(const char* localStr)
        {
            if (!localStr) return std::string();
#ifdef UNICODE
            // In Unicode builds localStr is expected to already be UTF-8 bytes
            // when passed as narrow char*. Return as-is.
            return std::string(localStr);
#else
            // ANSI -> wide -> UTF-8
            CStringA a(localStr);
            CStringW w = CStringConverter::ToWide(a);
            return CStringConverter::ToUtf8(w);
#endif
        }

        LPCTSTR c_str() const { return m_cs.GetString(); }
        operator LPCTSTR() const { return m_cs.GetString(); }
    };

#pragma region 全局函数与全局常量

	struct CStringHash
	{
		size_t operator()(const CStringW& s) const noexcept
		{
			const wchar_t* p = s.GetString();
			const int len = s.GetLength();
			if (p == nullptr || len <= 0)
				return 0;

			size_t h = sizeof(size_t) == 8 ? 14695981039346656037ull : 2166136261u;
			const size_t prime = sizeof(size_t) == 8 ? 1099511628211ull : 16777619u;
			for (int i = 0; i < len; ++i)
			{
				const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&p[i]);
				for (size_t b = 0; b < sizeof(wchar_t); ++b)
				{
					h ^= static_cast<size_t>(bytes[b]);
					h *= prime;
				}
			}
			return h;
		}

		// ANSI CStringA support
		size_t operator()(const CStringA& s) const noexcept
		{
			const char* p = s.GetString();
			const int len = s.GetLength();
			if (p == nullptr || len <= 0)
				return 0;

			size_t h = sizeof(size_t) == 8 ? 14695981039346656037ull : 2166136261u;
			const size_t prime = sizeof(size_t) == 8 ? 1099511628211ull : 16777619u;
			for (int i = 0; i < len; ++i)
			{
				const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&p[i]);
				for (size_t b = 0; b < sizeof(char); ++b)
				{
					h ^= static_cast<size_t>(bytes[b]);
					h *= prime;
				}
			}
			return h;
		}
	};

} // namespace rceditor

namespace std {
    template<>
    struct hash<CStringW>
    {
        size_t operator()(const CStringW& s) const noexcept
        {
            return rceditor::CStringHash()(s);
        }
    };

    template<>
    struct hash<CStringA>
    {
        size_t operator()(const CStringA& s) const noexcept
        {
            return rceditor::CStringHash()(s);
        }
    };
} // namespace std

namespace rceditor {



	struct CStringEqual
	{
		bool operator()(const CStringW& a, const CStringW& b) const noexcept
		{
			return a == b;
		}

		bool operator()(const CStringA& a, const CStringA& b) const noexcept
		{
			return a == b;
		}
	};


    const std::map<CStringW, WORD> MAP_CONTROL_NAME_WORD =
	{
		// 标准 Windows 控件（大写是Windows API标准）
		{L"BUTTON", 0x80},
		{L"EDIT", 0x81},
		{L"STATIC", 0x82},
		{L"LISTBOX", 0x83},
		{L"SCROLLBAR", 0x84},
		{L"COMBOBOX", 0x85},
		{L"MDICLIENT", 0x86},
		{L"COMBOLBOX", 0x87},
		{L"DDEMLEEVENT", 0x88},

		// 工具栏和通用控件
		{L"TOOLBARWINDOW32", 0x8F},
		{L"MSCTLS_STATUSBAR32", 0x90},
		{L"MSCTLS_PROGRESS32", 0x91},
		{L"MSCTLS_TRACKBAR32", 0x92},
		{L"MSCTLS_UPDOWN32", 0x93},
		{L"MSCTLS_HOTKEY32", 0x94},
		{L"SYSHEADER32", 0x95},
		{L"SYSLISTVIEW32", 0x96},
		{L"SYSTREEVIEW32", 0x97},
		{L"SYSTABCONTROL32", 0x98},
		{L"SYSANIMATE32", 0x99},
		{L"SYSDATETIMEPICK32", 0x9A},
		{L"SYSMONTHCAL32", 0x9B},
		{L"TOOLTIPS_CLASS32", 0x9C},
		{L"REBARWINDOW32", 0x9D},
		{L"MSCTLS_TOOLBAR32", 0x9E},
		{L"SYSLINK", 0x9F},

		// RichEdit 控件
		{L"RICHEDIT", 0xA0},
		{L"RichEdit20W", 0xA1},
		{L"RichEdit50W", 0xA2},

		// 其他控件
		{L"SysIPAddress32", 0xA3},
		{L"SysPager", 0xA4},
		{L"NativeFontCtl", 0xA5},
		{L"DirectUIHWND", 0xA6},
		{L"Windows.UI.Core.CoreWindow", 0xA7},

		// 兼容性 - 常用控件的小写版本
		{L"button", 0x80},
		{L"edit", 0x81},
		{L"static", 0x82},
		{L"listbox", 0x83},
		{L"scrollbar", 0x84},
		{L"combobox", 0x85},

		// 兼容性 - 混合大小写版本（资源编辑器可能使用）
		{L"Button", 0x80},
		{L"Edit", 0x81},
		{L"Static", 0x82},
		{L"ListBox", 0x83},
		{L"ScrollBar", 0x84},
		{L"ComboBox", 0x85}
	};




#pragma endregion 全局函数与全局常量

#pragma region PE信息头
	//! 写入RC的信息格式
	struct SConvertedResData
	{
		LPCTSTR resType;			/*!< 资源类型 */
		UINT nResID;				/*!< 该语言下所有的当前资源类型的资源ID */
		LANGID langID;				/*!< 当前资源类型所属的语言 */
		std::vector<BYTE> vecData; /*!< 资源数据 */

		explicit SConvertedResData()
		{
			resType = RT_DIALOG;
			nResID = 0;
			langID = 0;
			vecData.clear();
		}
	};

	//! 每种语言的资源ID
	struct SPerLanResID
	{
		LANGID langID;	/*!< 当前资源类型所属的语言 */
		std::vector<int> vecResID;	/*!< 该语言下所有的当前资源类型的资源ID */
		explicit SPerLanResID()
		{
			langID = 0;
			vecResID.clear();
		}
	};

#pragma pack(push, 1)
	struct DLGTEMPLATEEX
	{
		WORD dlgVer;
		WORD signature; // 必须是 0xFFFF
		DWORD helpID;
		DWORD dwExtendedStyle;
		DWORD style;
		WORD cDlgItems;
		short x;
		short y;
		short cx;
		short cy;
		// 后面跟着菜单、类名、标题、字体
	};

	struct DLGITEMTEMPLATEEX
	{
		DWORD helpID;
		DWORD dwExtendedStyle;
		DWORD style;
		short x;
		short y;
		short cx;
		short cy;
		DWORD id;
		// 后面跟着类名、标题、extra data
	};

	// 扩展菜单头
	struct MENUEX_TEMPLATE_HEADER
	{
		WORD wVersion;    // = 1
		WORD wOffset;     // = 4
		DWORD dwHelpId;
	};

	// 扩展菜单项
	struct MENUEX_TEMPLATE_ITEM
	{
		DWORD dwType;
		DWORD dwState;
		DWORD dwHelpId;
		WORD  wID;        // 菜单项ID
		WORD  bResInfo;   // Popup标志和子项数量
		// WCHAR szText[]; 后面是菜单文本（以 0 结尾）
	};

	//! toolbar资源头
	struct TOOLBAR_TEMPLATE
	{
		uint16_t wVersion;   /*!< 版本，通常为 1 */
		uint16_t wWidth;     /*!< 按钮宽度 */
		uint16_t wHeight;    /*!< 按钮高度 */
		uint16_t wItemCount; /*!< 按钮数量 */
		// 后面跟着 wItemCount 个 uint16_t 的按钮 ID
	};


#pragma pack(pop)

#pragma endregion PE信息头


#pragma region 中间转储
	//! 字体信息
	struct SFontInfo
	{
		CStringW csFaceName;		//! 字体名称
		WORD wPointSize;			/*!< 字体大小 */
		WORD wWeight;				/*!< 字体粗细 */
		BYTE bItalic;				/*!< 是否斜体 0 ==> 否 1 ==> 是 */
		BYTE bCharSet;				/*!< 字符集 */
	public:
		explicit SFontInfo()
		{
			wPointSize = 0;
			wWeight = 0;
			bItalic = 0;
			bCharSet = 0;
			csFaceName.Empty();
		}


		/*!
		 * \brief 将字体信息添加到资源数据中
		 * \param resource 资源数据缓冲区
		 * \param offset 当前偏移位置
		 * \param isEx 是否为扩展对话框模板
		 * \return 添加字体信息后的新偏移位置
		 */
		size_t AddFontInfoToResource(std::vector<BYTE>& resource, size_t offset, bool isEx) const;
	};


	//! 控件信息
	struct SCtrlInfo
	{
		//! 资源自带的信息
		DWORD dwStyle; /*!< 控件风格 */
		DWORD dwExStyle; /*!< 控件扩展风格 */
		CRect ctrlRect; /*!< 控件位置 */
		UINT nIDC; /*!< 控件ID */
		CStringW csClassName; /*!< 控件类名 */
		CStringW csText; /*!< 控件文本 */
		SFontInfo oFontInfo; /*!< 控件字体信息 */
		WORD wResourceID; /*!< 控件资源ID */
		bool blIsResourceIDText; /*!< 控件资源ID文本标志  true ==> 文本为资源ID false ==> 文本不是资源ID */
		std::vector<std::shared_ptr<BYTE>> vecExtraData; /*!< 控件的额外数据 */
		bool blMultiLineEditCtrl; /*!< 是否为多行编辑控件 true ==> 是 false ==> 否 */


		//! 非资源信息
		constexpr static short MAX_LEVEL_USE = 32; /*!< 最大的使用信息记录数 若需提升为64，需要改变当前json为nlohmann/json.hpp */

		UINT uiLevelMask;		/*!< 当前等级是否可禁用， 用位数表达，比如第0位为1表示 1级可使用 */
		void SetLevel(const int& nLevel, const bool& bEnable);
		bool GetLevel(const int& nLevel) const;
		// 记录新增信息的成员
		bool m_blIsAdded; /*!< 控件是否为新增项 */
		bool m_blNeedsLayoutAdjust; /*!< 新增控件是否需要布局微调 */


	public:
		explicit SCtrlInfo()
		{
			dwStyle = (WS_CHILD | WS_VISIBLE);
			dwExStyle = 0;
			ctrlRect = CRect{ 10,10,10,10 };
			nIDC = 0;
			csClassName = L"STATIC";
			csText = L"";
			oFontInfo = SFontInfo();
			wResourceID = 0;
			blIsResourceIDText = false;
			vecExtraData.clear();
			blMultiLineEditCtrl = false;
			uiLevelMask = 0;
			m_blIsAdded = false;
			m_blNeedsLayoutAdjust = false;
		}
		SCtrlInfo(const SCtrlInfo& other);

		/*!
		 * \brief 将控件信息添加到资源数据中
		 * \param resource 资源数据缓冲区
		 * \param offset 当前偏移位置
		 * \param isEx 是否为扩展对话框模板
		 * \return 添加控件信息后的新偏移位置
		 */
		size_t AddControlToResource(std::vector<BYTE>& resource, size_t offset, bool isEx) const;
	};

	//! 窗口信息
	/*! \brief Static控件坐标匹配容差（DLU单位） */
	constexpr int kStaticCtrlMatchToleranceDLU = 2;

	/*! \brief 判断两个矩形在给定DLU容差内是否匹配 */
	inline bool IsRectWithinTolerance(const CRect& a, const CRect& b, int toleranceDLU)
	{
		return abs(a.left - b.left) <= toleranceDLU &&
		       abs(a.top - b.top) <= toleranceDLU &&
		       abs(a.Width() - b.Width()) <= toleranceDLU &&
		       abs(a.Height() - b.Height()) <= toleranceDLU;
	}

	// =========================================================================
	// 控件棋盘（Board-Tile）拓扑映射 — 数据结构
	// =========================================================================

	/*! 拓扑网格行聚类容差（DLU）— Y 坐标差 ≤ 此值视为同一行 */
	constexpr int kRowClusterToleranceDLU = 5;

	/*! 判断 IDC 是否为无效/STATIC 类 ID */
	inline bool IsStaticIDC(UINT idc)
	{
		return idc == (UINT)-1 || idc == 0xFFFF || idc == 0;
	}

	/*!
	 * \brief 判断控件是否为 GroupBox
	 * \details GroupBox 在 RC 中以 BUTTON 类 + BS_GROUPBOX 样式表示，
	 *          少数场景直接标注为 "GROUPBOX" 类名。
	 */
	inline bool IsGroupBoxCtrl(const std::shared_ptr<SCtrlInfo>& c)
	{
		if (!c) return false;
		CStringW cls = c->csClassName;
		cls.MakeUpper();
		if (cls == L"GROUPBOX") return true;
		if (cls == L"BUTTON" && ((c->dwStyle & 0x0F) == BS_GROUPBOX)) return true;
		return false;
	}

	/*!
	 * \brief 判断控件是否需要外部 STATIC 标签来描述
	 * \details Edit、ComboBox、ListBox 等无法自带描述文本的控件返回 true；
	 *          Button（checkbox/radio）虽自带文本但常配有独立 STATIC 标签，也返回 true。
	 *          GroupBox 不在此函数判断范围内（由外层 IsGroupBoxCtrl 排除）。
	 */
	inline bool NeedsStaticLabel(const std::shared_ptr<SCtrlInfo>& c)
	{
		if (!c) return false;
		CStringW cls = c->csClassName;
		cls.MakeUpper();
		if (cls == L"EDIT") return true;
		if (cls == L"COMBOBOX") return true;
		if (cls == L"LISTBOX") return true;
		if (cls == L"SCROLLBAR") return true;
		if (cls == L"BUTTON") return true;
		if (cls == L"MSCTLS_TRACKBAR32") return true;
		if (cls == L"MSCTLS_PROGRESS32") return true;
		if (cls == L"MSCTLS_UPDOWN32") return true;
		if (cls == L"SYSIPADDRESS32") return true;
		if (cls == L"MSCTLS_HOTKEY32") return true;
		if (cls == L"SYSLISTVIEW32") return true;
		if (cls == L"SYSTREEVIEW32") return true;
		if (cls == L"SYSTABCONTROL32") return true;
		if (cls == L"SYSDATETIMEPICK32") return true;
		if (cls == L"SYSMONTHCAL32") return true;
		if (cls == L"RICHEDIT20W" || cls == L"RICHEDIT") return true;
		return false;
	}

	/*!
	 * \brief 棋盘中的一块瓷砖 — 对应 vecCtrlInfo 中的一个控件
	 */
	struct SBoardTile
	{
		int globalIndex;           /*!< vecCtrlInfo 中的索引 */
		int localStaticOrdinal;    /*!< 在所属 TileGroup 中的第 K 个 STATIC（-1 = 非 STATIC） */
		int topoRow;               /*!< 拓扑网格行索引（-1 = 未分配） */
		int topoCol;               /*!< 拓扑网格列索引：该行中第 K 个 STATIC（-1 = 非 STATIC） */
		bool isAnchor;             /*!< 有唯一 IDC（非 STATIC） */
		bool isGroupBox;           /*!< 是 GroupBox 控件 */

		SBoardTile()
			: globalIndex(-1), localStaticOrdinal(-1)
			, topoRow(-1), topoCol(-1)
			, isAnchor(false), isGroupBox(false) {}
	};

	/*!
	 * \brief 瓷砖组 — 逻辑容器（一个 GroupBox 区域或顶层区域）
	 */
	/*!
	 * \brief 拓扑行信息 — 一行中的所有控件
	 */
	struct STopoRowInfo
	{
		int representativeY;               /*!< 该行的代表 Y 坐标 */
		std::vector<int> tileIndices;       /*!< tiles[] 索引（按 X 排序） */
	};

	struct STileGroup
	{
		UINT groupIDC;             /*!< GroupBox 的 IDC（顶层组 = 0） */
		CRect groupRect;           /*!< GroupBox 矩形（顶层 = 对话框矩形） */
		std::vector<SBoardTile> tiles; /*!< 组内所有控件（RC 顺序） */
		int staticCount;           /*!< 组内 STATIC 控件数量 */
		std::vector<STopoRowInfo> topoRows; /*!< 拓扑网格行（按 Y 聚类后按 X 排序） */

		STileGroup()
			: groupIDC(0), staticCount(0) {}
	};

	/*!
	 * \brief 整个对话框的棋盘模型
	 */
	struct SDialogBoard
	{
		STileGroup topLevel;                        /*!< 不在任何 GroupBox 中的控件 */
		std::vector<STileGroup> groupBoxes;          /*!< 各 GroupBox 的瓷砖组 */
		std::map<UINT, int> groupIDCToIndex;         /*!< GroupBox IDC → groupBoxes[] 索引 */
	};

	struct SDialogInfo
	{
		DWORD dwStyle; /*!< 窗口风格 */
		DWORD dwExStyle; /*!< 窗口扩展风格 */
		CRect rectDlg; /*!< 窗口位置 */
		UINT nIDD; /*!< 窗口ID */
		LANGID langID; /*!< 窗口所属的语言ID */
		CStringW csClassName; /*!< 窗口类名 */
		CStringW csResourceName; /*!< RC文本中的原始资源名称（如"IDD_MYDIALOG"），用于宏未解析时的回退匹配 */
		bool bIsDlgTemplateEx; /*!< 是否为DLGTEMPLATEEX 是 ==> DLGTEMPLATEEX 否 ==> DLGTEMPLATE 默认为false  */
		CStringW csCaption; /*!< 窗口标题 */
		CStringW csMenuName; /*!< 窗口菜单名 */
		WORD wMenuResourceID; /*!< 窗口菜单资源ID */
		bool blIsMenuResourceIDText; /*!< 窗口菜单资源ID文本标志  true ==> 文本为资源ID false ==> 文本不是资源ID */
		WORD wClassResourceID; /*!< 窗口类资源ID */
		bool blIsClassResourceIDText; /*!< 窗口类资源ID文本标志  true ==> 文本为资源ID false ==> 文本不是资源ID */
		std::shared_ptr<SFontInfo> pFontInfo; /*!< 窗口字体信息 */
		std::vector<std::shared_ptr<SCtrlInfo>> vecCtrlInfo; /*!< 窗口控件信息 */




		//! 非PE头信息
		bool m_blIsInLevelManager; /*!< 是否在等级管理器中 true ==> 是 false ==> 否 */
		CStringW csClassPath; /*!< 动态运行时类名路径（LevelManager配置使用，非资源字段） */
		// 记录新增控件信息 (使用vector存储，支持IDC重复且为-1的STATIC控件)
		std::vector<std::shared_ptr<SCtrlInfo>> vecAddedCtrlInfo; /*!< 新增的控件信息 */

	public:
		explicit SDialogInfo()
		{
			dwStyle = 0;
			dwExStyle = 0;
			rectDlg = CRect{ 10,10,10,10 };
			nIDD = 0;
			langID = 1033;
			csClassName.Empty();
			csResourceName.Empty();
			csClassPath.Empty();
			bIsDlgTemplateEx = false;
			csCaption.Empty();
			csMenuName.Empty();
			wMenuResourceID = 0;
			blIsMenuResourceIDText = false;
			wClassResourceID = 0;
			blIsClassResourceIDText = false;
			pFontInfo = nullptr;
			vecCtrlInfo.clear();
			vecAddedCtrlInfo.clear();
			m_blIsInLevelManager = false;
		}

		//! 将窗口信息转为PE信息头
		SConvertedResData Convert2ResData() const;

		SDialogInfo& operator+=(const SDialogInfo& other);
		SDialogInfo& operator-=(const SDialogInfo& other);
		SDialogInfo operator+(const SDialogInfo& other) const;
		SDialogInfo operator-(const SDialogInfo& other) const;


	private:
		//! 计算窗口信息在PE头中的大小
		size_t CalculateDialogResourceSize() const;
	};

	// ---- 棋盘模型函数声明（需要完整的 SDialogInfo 定义）----

	/*!
	 * \brief 构建对话框的棋盘模型
	 * \param pDlg 对话框信息
	 * \return 棋盘模型（包含顶层组和 GroupBox 组）
	 */
	SDialogBoard BuildDialogBoard(const std::shared_ptr<SDialogInfo>& pDlg);

	/*!
	 * \brief 棋盘拓扑 STATIC 控件匹配（用于主/次语言对比）
	 * \param primaryDlg 主语言对话框
	 * \param secondaryDlg 次语言对话框
	 * \param[out] outMatched 输出：主语言索引 → 是否匹配
	 * \return 匹配到的 STATIC 控件数量
	 */
	int BoardTileStaticMatch(
		const std::shared_ptr<SDialogInfo>& primaryDlg,
		const std::shared_ptr<SDialogInfo>& secondaryDlg,
		std::map<int, bool>& outMatched);

	/*!
	 * \brief 构建拓扑网格 — 将组内控件按 Y 聚类为行、按 X 排序
	 * \param grp 要构建网格的瓷砖组（会修改 tiles 的 topoRow/topoCol 及 topoRows）
	 * \param ctrls 完整控件列表（用于取坐标）
	 */
	void BuildTopoGrid(STileGroup& grp,
		const std::vector<std::shared_ptr<SCtrlInfo>>& ctrls);

	/*!
	 * \brief 对齐两个 TileGroup 的拓扑行（anchor IDC 优先 + 序号回退）
	 * \return priRowIdx → secRowIdx 的映射
	 */
	std::map<int, int> AlignTopoRows(
		const STileGroup& priGrp,
		const STileGroup& secGrp,
		const std::vector<std::shared_ptr<SCtrlInfo>>& priCtrls,
		const std::vector<std::shared_ptr<SCtrlInfo>>& secCtrls);

	/*!
	 * \brief 基于拓扑网格的 STATIC 匹配（替代旧的 MatchGroupStatics）
	 * \param outSecConsumed 可选输出：本轮已消费的次语言 STATIC globalIndex 集合
	 */
	int MatchGroupByTopology(
		const STileGroup& priGrp,
		const STileGroup& secGrp,
		const std::vector<std::shared_ptr<SCtrlInfo>>& priCtrls,
		const std::vector<std::shared_ptr<SCtrlInfo>>& secCtrls,
		std::map<int, bool>& outMatched,
		std::set<int>* outSecConsumed = nullptr);

	//! stringtable信息
	struct SStringTableInfo
	{
		UINT nResourceStringID; /*!< 字符串资源块ID */

        //! 一般最多16个字符串	
              std::map<UINT, CStringW> mapIDAndString; /*!< 字符串ID和字符串文本的映射 */
		std::vector<UINT> vecLackStringID; /*!< 缺失的字符串ID -- 用于次语言版本 */
		// 记录新增的字符串 (使用vector存储新增字符串信息)
		struct SAddedString
		{
			UINT nStringID;      /*!< 字符串ID */
			CStringW csText;     /*!< 字符串文本 */
			bool bModified;      /*!< 是否已修改（翻译完成） */
			SAddedString() : nStringID(0), bModified(false) {}
			SAddedString(UINT id, const CStringW& text) : nStringID(id), csText(text), bModified(false) {}
		};
		std::vector<SAddedString> vecAddedStrings; /*!< 新增的字符串列表 */
		explicit SStringTableInfo()
		{
		nResourceStringID = 0;
		mapIDAndString.clear();
		vecLackStringID.clear();
		vecAddedStrings.clear();
		}
		SStringTableInfo& operator+=(const SStringTableInfo& other);
		SStringTableInfo& operator-=(const SStringTableInfo& other);
		SStringTableInfo operator+(const SStringTableInfo& other) const;
		SStringTableInfo operator-(const SStringTableInfo& other) const;

		//! 将字符串表信息转为PE信息头
		SConvertedResData Convert2ResData(LANGID langID) const;

	};

	//! version信息
	struct SVersionInfo
	{
		LANGID langID; /*!< 语言ID */
		UINT nVersionID; /*!< 版本资源ID */

		WORD wCodePage; /*!< 代码页 */

		VS_FIXEDFILEINFO fileInfo; /*!< 固定文件信息 */
        std::map<CStringW, CStringW> mapStringInfo; /*!< 字符串信息映射 key ==> 键值 value ==> 键值对应的文本 */


	public:
		explicit SVersionInfo()
		{
			langID = 0;
			nVersionID = 0;
			wCodePage = 0;
			mapStringInfo.clear();
			memset(&fileInfo, 0, sizeof(VS_FIXEDFILEINFO));
		}

		//! 将version信息转为PE信息头
		SConvertedResData Convert2ResData() const;

		SVersionInfo& operator+=(const SVersionInfo& other);
		SVersionInfo& operator-=(const SVersionInfo& other);
		SVersionInfo operator+(const SVersionInfo& other) const;
		SVersionInfo operator-(const SVersionInfo& other) const;

	private:
		//! 添加字符串（含 DWORD 对齐）
		size_t AddVersionString(std::vector<BYTE>& data, size_t offset, const CStringW& str) const;
		//! 写入 VERSION 节头（wLength 可先写 0，后续回填）
		size_t WriteVersionHeader(std::vector<BYTE>& data, size_t offset, WORD wLength, WORD wValueLength, WORD wType) const;
	};
    //! 菜单信息
	struct SMenuInfo
	{
		LANGID lanID;	/*!< 所属语言ID */
		UINT nID; /*!< 菜单ID */
		CStringW csText; /*!< 菜单文本 */
		std::vector<std::shared_ptr<SMenuInfo>> vecSubMenu; /*!< 子菜单 */
		bool blCurMenuLacked; /*!< 当前菜单是否为缺失 true ==> 缺失 false ==> 不缺失 仅为比对使用 */
		DWORD nType;                        /*!< 菜单项类型 (MENUEX用) */
		DWORD nState;                       /*!< 菜单项状态 (MENUEX用) */
		DWORD dwHelpID;                     /*!< 帮助ID (MENUEX用) */
		bool bIsPopup;						/*!< 是否为 POPUP 子菜单 */
		
		//! 非PE头信息 - 用于比较
		bool m_blIsAdded;                   /*!< 是否为新增菜单项 */
		std::vector<std::shared_ptr<SMenuInfo>> vecAddedMenuItems; /*!< 新增的菜单项列表 */
		
		public:
		explicit SMenuInfo()
		{
			nID = 0;
			lanID = 1033;
			csText.Empty();
			vecSubMenu.clear();
			nType = 0;
			nState = 0;
			dwHelpID = 0;
			bIsPopup = false;
			blCurMenuLacked = false;
			m_blIsAdded = false;
			vecAddedMenuItems.clear();
		}

		//! 将菜单信息转为PE信息头
		SConvertedResData Convert2ResData() const;

		SMenuInfo& operator+=(const SMenuInfo& other);
		SMenuInfo& operator-=(const SMenuInfo& other);
		SMenuInfo operator+(const SMenuInfo& other) const;
		SMenuInfo operator-(const SMenuInfo& other) const;

	private:
		//! 递归添加菜单项到资源数据
		static size_t AddMenuItemToResource(std::vector<BYTE>& data, size_t offset, const SMenuInfo& menuItem);
	};
	//! toolbar信息
	struct SToolbarInfo
	{
		LANGID	langID; /*!< 语言ID */
		int nToolbarID; /*!< 工具栏资源ID */

		//! toolbar的具体数据
		uint16_t wVersion; /*!< 版本 */
		uint16_t wWidth; /*!< 宽度 */
		uint16_t wHeight; /*!< 高度 */
		std::vector<uint16_t> vecItemIDs; /*!< 工具栏按钮ID */

		// Per-button level masks for LevelManager support (runtime editable)
		std::vector<UINT> vecLevelMask; /*!< per-button level mask */
        // Whether this toolbar participates in the Level Manager (analogous to SDialogInfo::m_blIsInLevelManager)
        bool m_blIsInLevelManager;
        // Optional per-button text/descriptions (populated from external JSON)
        std::vector<CStringW> vecItemText; /*!< per-button text descriptions */
        // Optional bitmap handle representing toolbar image strip (created when parsing PE)
        HBITMAP hBitmap;

		explicit SToolbarInfo()
		{
			langID = 0;
			nToolbarID = 0;
			wVersion = 0;
			wWidth = 0;
			wHeight = 0;
			vecItemIDs.clear();
			vecLevelMask.clear();
			m_blIsInLevelManager = false;
			vecItemText.clear();
			hBitmap = nullptr;
		}

		~SToolbarInfo()
		{
			if (hBitmap)
			{
				::DeleteObject(hBitmap);
				hBitmap = nullptr;
			}
		}

		//! 将toolbar信息转为PE信息头
		SConvertedResData Convert2ResData() const;

		SToolbarInfo& operator+=(const SToolbarInfo& other);
		SToolbarInfo& operator-=(const SToolbarInfo& other);
		SToolbarInfo operator+(const SToolbarInfo& other) const;
		SToolbarInfo operator-(const SToolbarInfo& other) const;

	};


	//! 加速表 -- 不需要


	//! 其他 - 待更新



	//! 单个封装的资源信息
	struct SSingleResourceInfo
	{
		LANGID langID; /*!< 语言ID */
		//! 窗口信息
        std::map<UINT, std::shared_ptr<SDialogInfo>> mapLangAndDialogInfo; /*!< Dialog资源ID -> 窗口信息 */

		//! 字符串表信息
        std::map<UINT, std::shared_ptr<SStringTableInfo>> mapLangAndStringTableInfo; /*!< StringTable资源块ID -> 字符串表信息 */

		//! 版本信息
		std::shared_ptr<SVersionInfo> pVersionInfo; /*!< 版本信息 */
		//! 菜单信息
        std::map<UINT, std::shared_ptr<SMenuInfo>> mapLangAndMenuInfo; /*!< Menu资源ID -> 菜单信息 */

		//! 工具栏信息
        std::map<UINT, std::shared_ptr<SToolbarInfo>> mapLangAndToolbarInfo; /*!< Toolbar资源ID -> 工具栏信息 */

		explicit SSingleResourceInfo()
		{
			langID = 0;
			mapLangAndDialogInfo.clear();
			mapLangAndStringTableInfo.clear();
			pVersionInfo = nullptr;
			mapLangAndMenuInfo.clear();
			mapLangAndToolbarInfo.clear();
		}

		//! 重载运算符
		SSingleResourceInfo& operator+=(const SSingleResourceInfo& other);
		SSingleResourceInfo& operator-=(const SSingleResourceInfo& other);
		SSingleResourceInfo operator+(const SSingleResourceInfo& other) const;
		SSingleResourceInfo operator-(const SSingleResourceInfo& other) const;

		//! 将单个资源信息转为PE信息头
		std::vector<SConvertedResData> Convert2ResData() const;
	};

	


#pragma endregion 中间转储



#pragma region 全局函数

	//! 通过窗口类名获取id
	WORD GetClassIdFromName(const CStringW& className);

	size_t AddStringToResource(std::vector<BYTE>& resource, size_t offset,
		const CStringW& str);

	// 对齐函数
	inline size_t AlignOffset(size_t offset, size_t alignment)
	{
		return (offset + alignment - 1) & ~(alignment - 1);
	}

	CStringW GetClassNameFromRc(WORD wClassId);

    // 将 Win32 风格标志转换为可读文本（例如 "WS_CHILD|WS_VISIBLE"）。
    // 用于 UI 显示（树/属性），而非用于精确的序列化/反序列化（round-tripping）。
	CStringW FormatWindowStyleText(DWORD style);
	CStringW FormatWindowExStyleText(DWORD exStyle);

    // ---- 深拷贝辅助（打破 shared_ptr 别名引用） ----
	std::shared_ptr<SCtrlInfo> DeepCloneCtrlInfo(const std::shared_ptr<SCtrlInfo>& src);
	std::shared_ptr<SDialogInfo> DeepCloneDialogInfo(const std::shared_ptr<SDialogInfo>& src);
	std::shared_ptr<SMenuInfo> DeepCloneMenuInfo(const std::shared_ptr<SMenuInfo>& src);
	SSingleResourceInfo DeepCloneResourceInfo(const SSingleResourceInfo& src);

    // ---- 自动排版辅助（用于SDialogInfo::operator+=时的智能布局） ----
    
    /*!
     * \brief 控件自动排版器
     * \details 根据源控件在源对话框中的相对位置，计算新位置并避免与现有控件重叠
     */
    // ---- 冲突检测结构 ----

    /*!
     * \brief 冲突类型
     */
    enum class EConflictType
    {
        None,               /*!< 无冲突 */
        MacroNameConflict,  /*!< 宏名冲突（同名不同值） */
        IdValueConflict,    /*!< ID数值冲突（同值不同名） */
        ControlIdConflict,  /*!< 控件ID冲突（次语言Dialog中已有同ID控件） */
    };

    /*!
     * \brief 冲突信息
     */
    struct SConflictInfo
    {
        EConflictType type;          /*!< 冲突类型 */
        CStringW primaryName;        /*!< 主语言中的宏名/资源名 */
        UINT primaryValue;           /*!< 主语言中的值 */
        CStringW secondaryName;      /*!< 次语言中冲突的宏名 */
        UINT secondaryValue;         /*!< 次语言中冲突的值 */
        UINT dialogId;               /*!< 所属对话框ID (控件冲突时) */
        CStringW description;        /*!< 冲突描述 */
        CString filePath;            /*!< 来源文件路径 */

        SConflictInfo()
            : type(EConflictType::None)
            , primaryValue(0)
            , secondaryValue(0)
            , dialogId(0)
        {}
    };

    class CControlAutoLayout
    {
    public:
        /*!
         * \brief 计算控件在目标对话框中的新位置
         * \param srcCtrlRect 源控件矩形（源对话框坐标系）
         * \param srcDlgRect 源对话框矩形
         * \param dstDlgRect 目标对话框矩形
         * \param existingCtrls 目标对话框中已存在的控件列表
         * \return 计算后的新位置矩形
         */
        static CRect CalculateNewPosition(
            const CRect& srcCtrlRect,
            const CRect& srcDlgRect,
            const CRect& dstDlgRect,
            const std::vector<std::shared_ptr<SCtrlInfo>>& existingCtrls);

        /*!
         * \brief 检测两个矩形是否重叠
         * \param rect1 矩形1
         * \param rect2 矩形2
         * \param margin 边距（可选，用于保持控件间距）
         * \return true 如果重叠
         */
        static bool IsOverlapping(const CRect& rect1, const CRect& rect2, int margin = 2);

        /*!
         * \brief 查找不重叠的位置
         * \param ctrlRect 要放置的控件矩形
         * \param dlgRect 对话框矩形
         * \param existingCtrls 已存在的控件列表
         * \return 调整后不重叠的位置
         */
        static CRect FindNonOverlappingPosition(
            const CRect& ctrlRect,
            const CRect& dlgRect,
            const std::vector<std::shared_ptr<SCtrlInfo>>& existingCtrls);

		/*!
		 * \brief 校验结果
		 */
		struct SValidationResult
		{
			bool bOutOfBounds;          /*!< 控件超出父窗口边界 */
			bool bCollision;            /*!< 与其他控件重叠 */
			CStringW description;       /*!< 校验不通过的描述 */
			CRect suggestedRect;        /*!< 建议的修正位置 */

			SValidationResult() : bOutOfBounds(false), bCollision(false) {}
			bool IsValid() const { return !bOutOfBounds && !bCollision; }
		};

		/*!
		 * \brief 校验控件位置是否合法
		 * \param ctrlRect 控件矩形
		 * \param dlgRect 父对话框矩形
		 * \param existingCtrls 已存在的其他控件
		 * \param excludeCtrlIndex 排除自身的索引（-1 表示不排除）
		 * \param computeSuggestion 是否计算建议修正位置（false 时跳过昂贵的 FindNonOverlappingPosition）
		 * \return 校验结果
		 */
		static SValidationResult ValidateControlPosition(
			const CRect& ctrlRect,
			const CRect& dlgRect,
			const std::vector<std::shared_ptr<SCtrlInfo>>& existingCtrls,
			int excludeCtrlIndex = -1,
			bool computeSuggestion = false);

    private:
        /*!
         * \brief 检查矩形是否与任何现有控件重叠
         */
        static bool CollidesWithAny(
            const CRect& rect,
            const std::vector<std::shared_ptr<SCtrlInfo>>& existingCtrls,
            int margin = 2);

        /*!
         * \brief 尝试在指定方向移动矩形以避免碰撞
         * \param rect 要移动的矩形
         * \param dlgRect 对话框边界
         * \param existingCtrls 已存在的控件
         * \param direction 方向 (0=右, 1=下, 2=左, 3=上)
         * \param maxAttempts 最大尝试次数
         * \return 是否找到有效位置
         */
        static bool TryMoveInDirection(
            CRect& rect,
            const CRect& dlgRect,
            const std::vector<std::shared_ptr<SCtrlInfo>>& existingCtrls,
            int direction,
            int maxAttempts = 50);

    };



#pragma endregion 全局函数

}


/*!

	1、combox的交互存在问题
	1.1 combox的展开高度无法设置，展开后应当无法移动 -实现
	2、动态调整时的参考线只有左侧，且根据参考线放置的位置也并不是参考线的对应的位置 - 实现
	3、bage无法通过勾选树节点，获取当前combox的选项，并根据当前levelmask的设置绘制，要求修改后立即刷新窗口显示，如果树节点不勾选就不管 - 实现
	4、文件交互，需要删除ini的文件写入写出，并且不支持新建，只能读取pe文件，修改后保存/另存为 - 实现
	5、删除所有添加控件的功能，控件只能通过pe文件读取获得 -实现
	6、CDesignerSurfaceWnd绘制相关
		6.1	控件显示的文本比控件本身更长，需要调整一下对话框精度或者实时计算 - 实现
		6.2 checkbox被绘制成普通的pushbutton - 实现
		6.3 list，tree等控件无edge，导致不容易分辨边界 - 实现
		6.4 点击groupbox时，如果groupbox中无控件，则不容易选中gropubox - 实现
	7、CFormRcEditor显示相关
		7.1 属性表中如果使用_variant_t表示bool值时，在多字节字符集下的中文会乱码 - 实现
	8、CFormLevelManagerEditor 相关
		8.1 等级管理器中，不需要显示菜单的信息，只需要显示对话框和工具栏的信息 - 实现
	9、core相关
		9.1 带有文本的信息如RT_DIALOG,RT_STRING,RT_MENU,RT_ACCELERATOR等需要支持导出原生的rc格式的文本，RT_VERSION,RT_DLGINIT(不处理)
		9.2 迁移所有的字符串转换函数到RcCore.h中，形成统一的转换接口
		9.3 SDialogInfo,SCtrlInfo等结构体需要添加拷贝构造函数，方便深拷贝
		9.4 是否需要将一些静态函数迁移到core中？？

	10、需要新增一个继承自CFormRcEdtor的CFormModifyRcEditor，用于修改已有的rc文件
		10.1 该类需要一个controler，用于添加互相比较PE资源的功能，并比对出新增结果，通过新增结果快速从CFormRcEdtor的树节点切换到对应窗口并选中新增的控件
		10.2 该类修改完成后需要生成一个RC文本，用于源代码处增加rc


*/