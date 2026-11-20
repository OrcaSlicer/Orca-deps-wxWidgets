///////////////////////////////////////////////////////////////////////////////
// Name:        tests/controls/dialogtest.cpp
// Purpose:     wxWindow unit test
// Author:      Vaclav Slavik
// Created:     2012-08-30
// Copyright:   (c) 2012 Vaclav Slavik
///////////////////////////////////////////////////////////////////////////////

#include "testprec.h"


#include "wx/testing.h"

#include "wx/msgdlg.h"
#include "wx/filedlg.h"
#include "wx/filename.h"
#include "wx/modalhook.h"

// This test suite tests helpers from wx/testing.h intended for testing of code
// that calls modal dialogs. It does not test the implementation of wxWidgets'
// dialogs.

TEST_CASE("Modal::MessageDialog", "[modal]")
{
    int rc;

#if wxUSE_FILEDLG
    #define FILE_DIALOG_TEST ,\
        wxExpectModal<wxFileDialog>(wxGetCwd() + "/test.txt").Optional()
#else
    #define FILE_DIALOG_TEST
#endif

    wxTEST_DIALOG
    (
        rc = wxMessageBox("Should I fail?", "Question", wxYES|wxNO),
        wxExpectModal<wxMessageDialog>(wxNO)
        FILE_DIALOG_TEST
    );

    CHECK( rc == wxNO );
}

#if wxUSE_FILEDLG
TEST_CASE("Modal::FileDialog", "[modal]")
{
#if defined(__WXQT__) && defined(__WINDOWS__)
    WARN("Skipping test known to fail under wxQt for Windows");
    return;
#else
    wxFileDialog dlg(nullptr);
    int rc;

    wxTEST_DIALOG
    (
        rc = dlg.ShowModal(),
        wxExpectModal<wxFileDialog>(wxGetCwd() + "/test.txt")
    );

    CHECK( rc == wxID_OK );

    CHECK( dlg.GetFilename() == "test.txt" );

#ifdef __WXGTK3__
    // The native file dialog in GTK+ 3 launches an async operation which tries
    // to dereference the already deleted dialog object if we don't let it to
    // complete before leaving this function.
    wxYield();
#endif
#endif
}
#endif

#if wxUSE_FILEDLG && defined(wxHAS_GTK_FILEDLG_HOOK_ANSWER)
// Answers every file dialog with m_path, as code automating an application does: through
// wxFileDialogBase::SetPath(), which stores the path and starts no folder load in the GTK chooser
// (it only selects the filter the path's extension matches). With m_answer false it leaves the
// dialog to show, and closes it once its modal loop runs.
class PathAnswerHook : public wxModalDialogHook
{
public:
    PathAnswerHook() { Register(); }
    ~PathAnswerHook() { Unregister(); }

    wxString m_path;
    bool m_answer = true;

protected:
    int Enter(wxDialog* dialog) override
    {
        wxFileDialog* const dlg = wxDynamicCast(dialog, wxFileDialog);
        if ( !dlg )
            return wxID_NONE;
        if ( !m_answer )
        {
            dlg->CallAfter([dlg] { dlg->EndModal(wxID_CANCEL); });
            return wxID_NONE;
        }
        dlg->wxFileDialogBase::SetPath(m_path);
        return wxID_OK;
    }

    void Exit(wxDialog*) override { }
};

// A hook's answer is what the getters return, though the GTK chooser, which
// never showed, knows nothing of it.
static void CheckHookAnswer(long style)
{
    const wxString wildcard = "Text files (*.txt)|*.txt|PNG files (*.png)|*.png";
    // The dialog starts in another folder, on another file, so the constructor's
    // own work in the GTK chooser is under way and its answer, if it has one, is
    // not the hook's.
    const wxString answerDir = wxFileName::GetTempDir();
    const wxString first = wxFileName(answerDir, "answer.png").GetFullPath();
    const wxString second = wxFileName(answerDir, "second.png").GetFullPath();

    wxFileDialog dlg(nullptr, "Hook answer", wxGetCwd(), "default.txt", wildcard, style);

    // Before any ShowModal(), the getters are the GTK chooser's as always.
    dlg.wxFileDialogBase::SetPath(first);
    CHECK( dlg.GetPath() != first );

    PathAnswerHook hook;
    hook.m_path = first;
    CHECK( dlg.ShowModal() == wxID_OK );
    CHECK( dlg.GetPath() == first );
    CHECK( dlg.GetFilename() == "answer.png" );
    CHECK( dlg.GetDirectory() == answerDir );
    CHECK( dlg.GetFilterIndex() == 1 );

    wxArrayString paths, files;
    dlg.GetPaths(paths);
    dlg.GetFilenames(files);
    CHECK( paths.empty() );
    CHECK( files.empty() );

    // Answered again, the same dialog gives the new answer.
    hook.m_path = second;
    CHECK( dlg.ShowModal() == wxID_OK );
    CHECK( dlg.GetPath() == second );
    CHECK( dlg.GetFilename() == "second.png" );

    // Shown and closed after that, with no hook's answer, it gives the GTK
    // chooser's again, which never heard of the answers.
    hook.m_answer = false;
    CHECK( dlg.ShowModal() == wxID_CANCEL );
    CHECK( dlg.GetPath() != second );
    CHECK( dlg.GetDirectory() != answerDir );
}

TEST_CASE("Modal::FileDialogHookAnswer", "[modal]")
{
    SECTION("Open") { CheckHookAnswer(wxFD_OPEN); }
    SECTION("Save") { CheckHookAnswer(wxFD_SAVE | wxFD_OVERWRITE_PROMPT); }

    SECTION("Destroyed at once")
    {
        PathAnswerHook hook;
        hook.m_path = wxFileName(wxFileName::GetTempDir(), "answer.png").GetFullPath();
        {
            wxFileDialog dlg(nullptr, "Hook answer", wxGetCwd(), "default.txt");
            CHECK( dlg.ShowModal() == wxID_OK );
        }
        // The GTK chooser's work started by the constructor completes after the
        // dialog is gone, without touching it.
        wxYield();
    }
}
#endif

class MyDialog : public wxDialog
{
public:
    MyDialog(wxWindow *parent) : wxDialog(parent, wxID_ANY, "Entry"), m_value(-1)
    {
        // Dummy. Imagine it's a real dialog that shows some number-entry
        // controls.
    }

    int m_value;
};


template<>
class wxExpectModal<MyDialog> : public wxExpectModalBase<MyDialog>
{
public:
    wxExpectModal(int valueToSet) : m_valueToSet(valueToSet) {}

protected:
    virtual int OnInvoked(MyDialog *dlg) const override
    {
        // Simulate the user entering the expected number:
        dlg->m_value = m_valueToSet;
        return wxID_OK;
    }

    int m_valueToSet;
};

TEST_CASE("Modal::CustomDialog", "[modal]")
{
    MyDialog dlg(nullptr);

    wxTEST_DIALOG
    (
        dlg.ShowModal(),
        wxExpectModal<MyDialog>(42)
    );

    CHECK( dlg.m_value == 42 );
}


class MyModalDialog : public wxDialog
{
public:
    MyModalDialog() : wxDialog (nullptr, wxID_ANY, "Modal Dialog")
    {
        m_wasModal = false;
        Bind( wxEVT_INIT_DIALOG, &MyModalDialog::OnInit, this );
    }

    void OnInit(wxInitDialogEvent& WXUNUSED(event))
    {
        m_wasModal = IsModal();
        CallAfter( &MyModalDialog::EndModal, wxID_OK );
    }

    bool WasModal() const
    {
        return m_wasModal;
    }

private:
    bool m_wasModal;
};

TEST_CASE("Modal::InitDialog", "[modal]")
{
    MyModalDialog dlg;
    dlg.ShowModal();
    CHECK( dlg.WasModal() );
}
