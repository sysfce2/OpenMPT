/*
 * CloseMainDialog.cpp
 * -------------------
 * Purpose: Class for displaying a dialog with a list of unsaved documents, and the ability to choose which documents should be saved or not.
 * Notes  : (currently none)
 * Authors: OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#include "stdafx.h"
#include "Mptrack.h"
#include "Mainfrm.h"
#include "InputHandler.h"
#include "Moddoc.h"
#include "CloseMainDialog.h"


OPENMPT_NAMESPACE_BEGIN


BEGIN_MESSAGE_MAP(CloseMainDialog, CDialog)
	ON_WM_GETMINMAXINFO()
	ON_COMMAND(IDC_BUTTON1,			&CloseMainDialog::OnSaveAll)
	ON_COMMAND(IDC_BUTTON2,			&CloseMainDialog::OnSaveNone)
	ON_COMMAND(IDC_CHECK1,			&CloseMainDialog::OnSwitchFullPaths)
END_MESSAGE_MAP()


void CloseMainDialog::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(DoDataExchange)
	DDX_Control(pDX, IDC_LIST1,		m_List);
	//}}AFX_DATA_MAP
}


CloseMainDialog::CloseMainDialog() : CDialog(IDD_CLOSEDOCUMENTS)
{
	CMainFrame::GetInputHandler()->Bypass(true);
};


CloseMainDialog::~CloseMainDialog()
{
	CMainFrame::GetInputHandler()->Bypass(false);
};


CString CloseMainDialog::FormatTitle(const CModDoc *modDoc, bool fullPath)
{
	const CString &path = (!fullPath || modDoc->GetPathNameMpt().empty()) ? modDoc->GetTitle() : modDoc->GetPathNameMpt().ToCString();
	return mpt::ToCString(modDoc->GetSoundFile().GetCharsetInternal(), modDoc->GetSoundFile().GetTitle()) + _T(" (") + path + _T(")");
}


BOOL CloseMainDialog::OnInitDialog()
{
	CDialog::OnInitDialog();

	// Create list of unsaved documents
	m_List.ResetContent();

	CheckDlgButton(IDC_CHECK1, BST_CHECKED);

	auto documents = theApp.GetOpenDocuments();
	for(const auto &modDoc : documents)
	{
		if(modDoc->IsModified())
		{
			int item = m_List.AddString(FormatTitle(modDoc, true));
			m_List.SetItemDataPtr(item, modDoc);
			m_List.SetSel(item, TRUE);
		}
	}

	if(m_List.GetCount() == 0)
	{
		// No modified documents...
		OnOK();
	}

	CRect rect;
	GetWindowRect(rect);
	m_minSize = rect.Size();

	return TRUE;
}


void CloseMainDialog::OnOK()
{
	const int count = m_List.GetCount();
	for(int i = 0; i < count; i++)
	{
		CModDoc *modDoc = static_cast<CModDoc *>(m_List.GetItemDataPtr(i));
		MPT_ASSERT(modDoc != nullptr);
		if(m_List.GetSel(i))
		{
			modDoc->ActivateWindow();
			if(modDoc->DoFileSave() == FALSE)
			{
				// If something went wrong, or if the user decided to cancel saving (when using "Save As"), we'll better not proceed...
				OnCancel();
				return;
			}
		} else
		{
			modDoc->SetModified(FALSE);
		}
	}

	CDialog::OnOK();
}


void CloseMainDialog::OnSaveAll()
{
	if(m_List.GetCount() == 1)
		m_List.SetSel(0, TRUE);	// SelItemRange can't select one item: http://support.microsoft.com/kb/129428/en-us
	else
		m_List.SelItemRange(TRUE, 0, m_List.GetCount() - 1);
	OnOK();
}


void CloseMainDialog::OnSaveNone()
{
	if(m_List.GetCount() == 1)
		m_List.SetSel(0, FALSE);	// SelItemRange can't select one item: http://support.microsoft.com/kb/129428/en-us
	else
		m_List.SelItemRange(FALSE, 0, m_List.GetCount() - 1);
	OnOK();
}


// Switch between full path / filename only display
void CloseMainDialog::OnSwitchFullPaths()
{
	const int count = m_List.GetCount();
	const bool fullPath = (IsDlgButtonChecked(IDC_CHECK1) == BST_CHECKED);
	for(int i = 0; i < count; i++)
	{
		CModDoc *modDoc = static_cast<CModDoc *>(m_List.GetItemDataPtr(i));
		int item = m_List.InsertString(i + 1, FormatTitle(modDoc, fullPath));
		m_List.SetItemDataPtr(item, modDoc);
		m_List.SetSel(item, m_List.GetSel(i));
		m_List.DeleteString(i);
	}
}


void CloseMainDialog::OnGetMinMaxInfo(MINMAXINFO *mmi)
{
	mmi->ptMinTrackSize = m_minSize;
	CDialog::OnGetMinMaxInfo(mmi);
}

OPENMPT_NAMESPACE_END
