// runexe.cpp - minimal Series 80 .app whose only job is to launch
// sdtest1.exe as a PROCESS (the notaz way: Create/Logon/Resume), and
// log the result code of RProcess::Create to C:\runexe.log.
//
// Why: tapping a TARGETTYPE exe in the file manager makes the app
// framework try to load it as an APPLICATION (wrong loader) -> KERN-EXEC 3.
// The correct way to start a bare EXE on EKA1 is RProcess::Create, exactly
// as snes9x.app starts snes9xemu.exe. This launcher reproduces that.
//
// The launcher itself is a DLL (.app) and contains NO writable static data,
// so it is unaffected by the constraint we're investigating. It writes the
// Create() return code from a stack variable, so we learn the result even
// if the child EXE faults.
//
// Reads back:
//   C:\runexe.log  -> "Create rc=0"  means the EXE image loaded & started
//                     (then look at C:\sdtest1.log for the child's own output)
//   C:\runexe.log  -> "Create rc=<neg>" means the loader rejected the image
//                     (that points at the abld-built exe image itself)

#include <eikenv.h>
#include <eikapp.h>
#include <eikappui.h>
#include <eikdoc.h>
#include <f32file.h>
#include <e32std.h>

// Use your own registered app UID range here if needed.
const TUid KUidRunExeApp = { 0x01000791 };

_LIT(KLog, "C:\\runsms.log");
_LIT(KExe, "C:\\System\\Apps\\e32smsemu\\e32smsemu.exe"); // where the SIS put it

static void Log(const TDesC8& aLine)
    {
    RFs fs;
    if (fs.Connect() != KErrNone) return;
    RFile f;
    TInt e = f.Open(fs, KLog, EFileWrite | EFileShareAny);
    if (e == KErrNotFound || e == KErrPathNotFound)
        e = f.Create(fs, KLog, EFileWrite | EFileShareAny);
    if (e == KErrNone)
        {
        TInt pos = 0;
        f.Seek(ESeekEnd, pos);
        f.Write(aLine);
        f.Flush();
        f.Close();
        }
    fs.Close();
    }

static void LaunchExe()
    {
    Log(_L8("runexe: app started\r\n"));

    RProcess proc;
    TInt rc = proc.Create(KExe, _L(""));   // <-- the correct EKA1 launch path

    TBuf8<48> buf;
    buf.Format(_L8("Create rc=%d\r\n"), rc);
    Log(buf);

    if (rc == KErrNone)
        {
        TRequestStatus status;
        proc.Logon(status);
        proc.Resume();              // start child execution
        // Wait for the child to actually finish (vidtest runs ~5s).
        // This keeps the launcher out of the way so vidtest holds the screen.
        User::WaitForRequest(status);
        TInt exitReason = proc.ExitReason();
        TExitType exitType = proc.ExitType();
        TBuf8<64> b;
        b.Format(_L8("child exit: type=%d reason=%d\r\n"),
                 (TInt)exitType, exitReason);
        Log(b);
        proc.Close();
        }
    else
        {
        Log(_L8("runexe: Create FAILED (loader rejected image)\r\n"));
        }
    }

////////////////////////////////////////////////////////////////
// Minimal EIKON app plumbing. All work happens in AppUi::ConstructL.
////////////////////////////////////////////////////////////////

class CRunSmsAppUi : public CEikAppUi
    {
public:
    void ConstructL();
    ~CRunSmsAppUi();
    };

void CRunSmsAppUi::ConstructL()
    {
    CEikAppUi::BaseConstructL();
    // Do the launch, then immediately exit the launcher.
    LaunchExe();
    // Close ourselves; this probe has no UI to keep.
    Exit();
    }

CRunSmsAppUi::~CRunSmsAppUi() {}

class CRunSmsDocument : public CEikDocument
    {
public:
    CRunSmsDocument(CEikApplication& aApp) : CEikDocument(aApp) {}
private:
    CEikAppUi* CreateAppUiL() { return new(ELeave) CRunSmsAppUi; }
    };

class CRunSmsApplication : public CEikApplication
    {
private:
    CApaDocument* CreateDocumentL()
        { return new(ELeave) CRunSmsDocument(*this); }
    TUid AppDllUid() const { return KUidRunExeApp; }
    };

EXPORT_C CApaApplication* NewApplication()
    {
    return new CRunSmsApplication;
    }

GLDEF_C TInt E32Dll(TDllReason)
    {
    return KErrNone;
    }
