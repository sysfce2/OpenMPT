/*
 * QuickStartDialog.h
 * ------------------
 * Purpose: Dialog to show inside the MDI client area when no modules are loaded.
 * Notes  : (currently none)
 * Authors: OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#pragma once

#include "openmpt/all/BuildSettings.hpp"

#include "AccessibleButton.h"
#include "CListCtrl.h"
#include "ResizableDialog.h"
#include "../common/mptPathString.h"

OPENMPT_NAMESPACE_BEGIN

class QuickStartDlg : public ResizableDialog
{
public:
	QuickStartDlg(const std::vector<mpt::PathString> &templates, const std::vector<mpt::PathString> &examples, CWnd *parent);

	void UpdateHeight();

protected:
	void DoDataExchange(CDataExchange *pDX) override;
	BOOL OnInitDialog() override;
	void OnDPIChanged() override;
	INT_PTR OnToolHitTest(CPoint point, TOOLINFO* pTI) const override;
	BOOL PreTranslateMessage(MSG *pMsg) override;
	void OnOK() override;
	void OnCancel() override { DestroyWindow(); }

	void UpdateFileList(CString filter = {});
	size_t GetItemIndex(int index) const { return static_cast<size_t>(m_list.GetItemData(index) & 0x00FF'FFFF); }
	int GetItemGroup(int index) const { return static_cast<int>(m_list.GetItemData(index) >> 24); }

	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnNew();
	afx_msg void OnOpen();
	afx_msg void OnRemoveMRUItem();
	afx_msg void OnRemoveAllMRUItems();
	afx_msg void OnUpdateFilter();
	afx_msg void OnOpenFile(NMHDR *, LRESULT *);
	afx_msg void OnRightClickFile(NMHDR *, LRESULT *);

	DECLARE_MESSAGE_MAP()

	CListCtrlEx m_list;
	CEdit m_find;
	AccessibleButton m_newButton, m_openButton, m_closeButton;
	CFont m_buttonFont;
	CBitmap m_bmpNew, m_bmpOpen;
	std::array<std::vector<mpt::PathString>, 3> m_paths;
	CSize m_prevSize;
	int m_prevDPI = 96;
	bool m_groupsEnabled = false;
};

OPENMPT_NAMESPACE_END
