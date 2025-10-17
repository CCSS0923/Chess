#include <windows.h>
#include <tchar.h>
#include <string>

// 헬퍼 함수: exe 전체 경로로부터 폴더만 추출
std::wstring GetDirectoryFromPath(const std::wstring& exePath)
{
    size_t pos = exePath.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        return exePath.substr(0, pos);
    }
    return L".";
}

int APIENTRY _tWinMain(HINSTANCE hInstance, HINSTANCE, LPTSTR, int nCmdShow)
{
    int ret = MessageBoxW(
        NULL,
        L"2인 모드로 시작하려면 '예', AI와 대결 모드로 시작하려면 '아니오'를 선택하세요.",
        L"체스 모드 선택",
        MB_YESNO | MB_ICONQUESTION
    );

    std::wstring exePath;
    if (ret == IDYES) {
        exePath = L"..\\..\\Chess2Player\\x64\\Release\\Chess2Player.exe";
    }
    else {
        exePath = L"..\\..\\ChessAI\\x64\\Release\\ChessAI.exe";
    }

    // 작업 디렉터리(실행파일 폴더) 지정
    std::wstring exeDir = GetDirectoryFromPath(exePath);

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };

    BOOL success = CreateProcessW(
        exePath.c_str(),             // exe 절대/상대 경로
        NULL,                        // 커맨드라인
        NULL,                        // 프로세스 보안 속성
        NULL,                        // 쓰레드 보안 속성
        FALSE,                       // 핸들 상속
        0,                           // 생성 플래그
        NULL,                        // 환경 변수
        exeDir.c_str(),              // 작업 디렉터리 (중요!)
        &si,
        &pi
    );

    if (!success) {
        MessageBoxW(NULL, L"실행 파일을 찾을 수 없습니다.\nChess2Player.exe 또는 ChessAI.exe가 지정 위치에 있는지 확인하세요.", L"실행 오류", MB_OK | MB_ICONERROR);
        return 1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return 0;
}
