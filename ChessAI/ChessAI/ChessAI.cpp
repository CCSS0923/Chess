// ChessAI_Integrated.cpp
#define WIN32_LEAN_AND_MEAN
#include "framework.h"
#include "ChessAI.h"
#include <string>
#include <vector>
#include <set>
#include <windows.h>
#include <opencv2/opencv.hpp>
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>

using namespace std;
using namespace cv;

#define MAX_LOADSTRING 100
#define BLOCK_COUNT 8
#define BLOCK_SIZE 100

// 사용자 정의 메시지: Stockfish 엔진이 움직임을 찾았음을 UI 스레드에 알림
#define WM_STOCKFISH_MOVE_READY (WM_USER + 1)

HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING] = L"Chess Application";
WCHAR szWindowClass[MAX_LOADSTRING] = L"MyChessWindowClass";

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);

// -------------------- Blocks and board --------------------
struct Blocks {
    RECT rect;
    string name;
    BOOL color;
    int type;
    Blocks() : rect{ 0,0,0,0 }, name(""), color(FALSE), type(0) {}
    Blocks& operator=(const Blocks& rhs) {
        if (this != &rhs) {
            rect = rhs.rect;
            name = rhs.name;
            color = rhs.color;
            type = rhs.type;
        }
        return *this;
    }
};

Blocks g_block[BLOCK_COUNT][BLOCK_COUNT];
string pieceImage[8][8] = {
    {"rook_black.png","knight_black.png","bishop_black.png","queen_black.png","king_black.png","bishop_black.png","knight_black.png","rook_black.png"},
    {"pawn_black.png","pawn_black.png","pawn_black.png","pawn_black.png","pawn_black.png","pawn_black.png","pawn_black.png","pawn_black.png"},
    {"","","","","","","",""},
    {"","","","","","","",""},
    {"","","","","","","",""},
    {"","","","","","","",""},
    {"pawn_white.png","pawn_white.png","pawn_white.png","pawn_white.png","pawn_white.png","pawn_white.png","pawn_white.png","pawn_white.png"},
    {"rook_white.png","knight_white.png","bishop_white.png","queen_white.png","king_white.png","bishop_white.png","knight_white.png","rook_white.png"}
};

bool selected = false;
int selectedX = -1, selectedY = -1;
set<pair<int, int>> availableMoves;
bool isWhiteTurn = true;
vector<string> moveHistory;
bool isThinking = false; // AI 계산 중 상태 플래그

// 캐슬링 권한 및 킹/룩 이동 추적
bool whiteKingMoved = false;
bool whiteRookH1Moved = false; // 킹사이드 룩 (h1)
bool whiteRookA1Moved = false; // 퀸사이드 룩 (a1)
bool blackKingMoved = false;
bool blackRookH8Moved = false; // 킹사이드 룩 (h8)
bool blackRookA8Moved = false; // 퀸사이드 룩 (a8)


// double buffer
HBITMAP hbmBack = NULL;
HDC hdcBack = NULL;
int bufferWidth = 0, bufferHeight = 0;

void SetupDoubleBuffer(HDC hdc, HWND hWnd) {
    RECT rc; GetClientRect(hWnd, &rc);
    int width = rc.right - rc.left, height = rc.bottom - rc.top;
    bufferWidth = width; bufferHeight = height;

    if (hdcBack) { DeleteDC(hdcBack); hdcBack = NULL; }
    if (hbmBack) { DeleteObject(hbmBack); hbmBack = NULL; }

    hdcBack = CreateCompatibleDC(hdc);
    hbmBack = CreateCompatibleBitmap(hdc, width, height);
    SelectObject(hdcBack, hbmBack);
    FillRect(hdcBack, &rc, (HBRUSH)(COLOR_WINDOW + 1));
}
void CleanUpDoubleBuffer() {
    if (hdcBack) { DeleteDC(hdcBack); hdcBack = NULL; }
    if (hbmBack) { DeleteObject(hbmBack); hbmBack = NULL; }
}

// -------------------- chess logic prototypes --------------------
void copyBoard(Blocks(&dst)[BLOCK_COUNT][BLOCK_COUNT], Blocks(&src)[BLOCK_COUNT][BLOCK_COUNT]);
pair<int, int> findKing(bool white, Blocks(&board)[BLOCK_COUNT][BLOCK_COUNT]);
set<pair<int, int>> allPieceMoves(int x, int y, Blocks(&board)[BLOCK_COUNT][BLOCK_COUNT]);
set<pair<int, int>> getLegalMoves(int x, int y, Blocks(&board)[BLOCK_COUNT][BLOCK_COUNT]);
vector<pair<int, int>> checkProtectingPieces(bool isWhiteTurn, Blocks(&board)[BLOCK_COUNT][BLOCK_COUNT]);
string boardToFEN(Blocks(&board)[BLOCK_COUNT][BLOCK_COUNT], bool whiteTurn);
bool isCheck(bool white, Blocks(&board)[BLOCK_COUNT][BLOCK_COUNT]);
bool detectCheckmate(bool white, Blocks(&board)[BLOCK_COUNT][BLOCK_COUNT]);
void handlePromotion(int x, int y, Blocks(&board)[BLOCK_COUNT][BLOCK_COUNT]);
bool applyUCIMoveToBoard(const string& uciMove, Blocks(&board)[BLOCK_COUNT][BLOCK_COUNT]);

// -------------------- chess logic implementations --------------------
void copyBoard(Blocks(&dst)[BLOCK_COUNT][BLOCK_COUNT], Blocks(&src)[BLOCK_COUNT][BLOCK_COUNT]) {
    for (int x = 0; x < BLOCK_COUNT; x++) for (int y = 0; y < BLOCK_COUNT; y++) dst[x][y] = src[x][y];
}

pair<int, int> findKing(bool white, Blocks(&board)[BLOCK_COUNT][BLOCK_COUNT]) {
    for (int x = 0; x < BLOCK_COUNT; x++) for (int y = 0; y < BLOCK_COUNT; y++)
        if (!board[x][y].name.empty() && board[x][y].name.find("king") != string::npos && board[x][y].color == (white ? TRUE : FALSE))
            return { x, y };
    return { -1, -1 };
}

set<pair<int, int>> allPieceMoves(int x, int y, Blocks(&board)[BLOCK_COUNT][BLOCK_COUNT]) {
    set<pair<int, int>> moves;
    if (x < 0 || x >= BLOCK_COUNT || y < 0 || y >= BLOCK_COUNT) return moves;
    Blocks from = board[x][y];
    if (from.name.empty()) return moves;
    bool isWhite = (from.color == TRUE);
    string name = from.name;
    int forward = isWhite ? -1 : 1;

    if (name.find("pawn") != string::npos) {
        int ny = y + forward;
        if (ny >= 0 && ny < BLOCK_COUNT && board[x][ny].name.empty()) moves.insert({ x, ny });
        if ((isWhite && y == 6) || (!isWhite && y == 1)) {
            int ny2 = y + 2 * forward;
            if (ny2 >= 0 && ny2 < BLOCK_COUNT && board[x][ny].name.empty() && board[x][ny2].name.empty()) moves.insert({ x, ny2 });
        }
        for (int dx : { -1, 1 }) {
            int nx = x + dx;
            if (nx >= 0 && nx < BLOCK_COUNT && ny >= 0 && ny < BLOCK_COUNT && !board[nx][ny].name.empty() && board[nx][ny].color != from.color)
                moves.insert({ nx, ny });
        }
    }
    else if (name.find("rook") != string::npos) {
        int dirs[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
        for (auto& d : dirs) {
            for (int s = 1; s < BLOCK_COUNT; ++s) {
                int nx = x + d[0] * s, ny = y + d[1] * s;
                if (nx < 0 || nx >= BLOCK_COUNT || ny < 0 || ny >= BLOCK_COUNT) break;
                if (board[nx][ny].name.empty()) moves.insert({ nx, ny });
                else { if (board[nx][ny].color != from.color) moves.insert({ nx, ny }); break; }
            }
        }
    }
    else if (name.find("knight") != string::npos) {
        int c[8][2] = { {1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2} };
        for (auto& co : c) {
            int nx = x + co[0], ny = y + co[1];
            if (nx >= 0 && nx < BLOCK_COUNT && ny >= 0 && ny < BLOCK_COUNT && (board[nx][ny].name.empty() || board[nx][ny].color != from.color))
                moves.insert({ nx, ny });
        }
    }
    else if (name.find("bishop") != string::npos) {
        int dirs[4][2] = { {1,1},{1,-1},{-1,1},{-1,-1} };
        for (auto& d : dirs) {
            for (int s = 1; s < BLOCK_COUNT; ++s) {
                int nx = x + d[0] * s, ny = y + d[1] * s;
                if (nx < 0 || nx >= BLOCK_COUNT || ny < 0 || ny >= BLOCK_COUNT) break;
                if (board[nx][ny].name.empty()) moves.insert({ nx, ny });
                else { if (board[nx][ny].color != from.color) moves.insert({ nx, ny }); break; }
            }
        }
    }
    else if (name.find("queen") != string::npos) {
        int dirs[8][2] = { {1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1} };
        for (auto& d : dirs) {
            for (int s = 1; s < BLOCK_COUNT; ++s) {
                int nx = x + d[0] * s, ny = y + d[1] * s;
                if (nx < 0 || nx >= BLOCK_COUNT || ny < 0 || ny >= BLOCK_COUNT) break;
                if (board[nx][ny].name.empty()) moves.insert({ nx, ny });
                else { if (board[nx][ny].color != from.color) moves.insert({ nx, ny }); break; }
            }
        }
    }
    else if (name.find("king") != string::npos) {
        int dirs[8][2] = { {1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1} };
        for (auto& d : dirs) {
            int nx = x + d[0], ny = y + d[1];
            if (nx >= 0 && nx < BLOCK_COUNT && ny >= 0 && ny < BLOCK_COUNT && (board[nx][ny].name.empty() || board[nx][ny].color != from.color))
                moves.insert({ nx, ny });
        }

        // --- 캐슬링 로직 추가 (pseudo-legal moves) ---
        int startRank = isWhite ? 7 : 0;
        if (y == startRank && x == 4) { // 킹의 초기 위치 (e1 또는 e8)

            // 킹사이드 캐슬링 (O-O)
            if (isWhite ? !whiteKingMoved && !whiteRookH1Moved : !blackKingMoved && !blackRookH8Moved) {
                if (board[5][startRank].name.empty() && board[6][startRank].name.empty()) {
                    // 공격받는 칸 체크는 getLegalMoves에서 수행됨
                    moves.insert({ 6, startRank }); // g1 또는 g8
                }
            }

            // 퀸사이드 캐슬링 (O-O-O)
            if (isWhite ? !whiteKingMoved && !whiteRookA1Moved : !blackKingMoved && !blackRookA8Moved) {
                if (board[1][startRank].name.empty() && board[2][startRank].name.empty() && board[3][startRank].name.empty()) {
                    // 공격받는 칸 체크는 getLegalMoves에서 수행됨
                    moves.insert({ 2, startRank }); // c1 또는 c8
                }
            }
        }
        // ------------------------------------------
    }
    return moves;
}

bool isCheck(bool white, Blocks(&board)[BLOCK_COUNT][BLOCK_COUNT]) {
    auto kingPos = findKing(white, board);
    if (kingPos.first < 0) return true; // 킹이 없으면 (캡처된 경우 등) 체크된 것으로 간주

    // 킹이 공격받는지 확인하기 위해 상대방 기물들의 이동 가능 칸 확인
    for (int x = 0; x < BLOCK_COUNT; x++) for (int y = 0; y < BLOCK_COUNT; y++)
        if (!board[x][y].name.empty() && board[x][y].color != (white ? TRUE : FALSE)) {
            // allPieceMoves 사용 (킹 보호 로직 제외한 기본 이동)
            auto mv = allPieceMoves(x, y, board);
            if (mv.find(kingPos) != mv.end()) return true;
        }
    return false;
}

set<pair<int, int>> getLegalMoves(int x, int y, Blocks(&board)[BLOCK_COUNT][BLOCK_COUNT]) {
    set<pair<int, int>> moves;
    set<pair<int, int>> src = allPieceMoves(x, y, board);
    bool who = board[x][y].color == TRUE;
    string pieceName = board[x][y].name;

    for (auto& dest : src) {
        Blocks tmp[BLOCK_COUNT][BLOCK_COUNT];
        copyBoard(tmp, board);

        // 이동 적용 (임시 보드)
        tmp[dest.first][dest.second] = tmp[x][y];
        tmp[x][y].name = ""; tmp[x][y].color = FALSE;

        // 캐슬링: 킹의 2칸 이동 (e1->g1, e1->c1, e8->g8, e8->c8)
        bool isCastling = (pieceName.find("king") != string::npos && abs(x - dest.first) == 2);

        if (isCastling) {
            int rank = dest.second;
            // 킹사이드 캐슬링 (e1->g1 또는 e8->g8)
            if (dest.first == 6) {
                // 룩 이동 (h1->f1 또는 h8->f8)
                tmp[5][rank] = tmp[7][rank];
                tmp[7][rank].name = ""; tmp[7][rank].color = FALSE;
            }
            // 퀸사이드 캐슬링 (e1->c1 또는 e8->c8)
            else if (dest.first == 2) {
                // 룩 이동 (a1->d1 또는 a8->d8)
                tmp[3][rank] = tmp[0][rank];
                tmp[0][rank].name = ""; tmp[0][rank].color = FALSE;
            }

            // 캐슬링 특수 규칙: 킹의 원래 칸, 통과하는 칸, 도착하는 칸이 공격받지 않아야 함
            bool pathAttacked = false;
            // 통과하는 칸 (킹사이드: f열 / 퀸사이드: d열)
            int passFile = (dest.first == 6) ? 5 : 3;
            // 도착하는 칸 (g열 / c열)
            int destFile = dest.first;

            // 1. 현재 체크 상태가 아닌지
            if (isCheck(who, board)) { pathAttacked = true; }
            // 2. 킹이 통과하는 칸이 공격받는지 (e1->g1: f1 체크 / e1->c1: d1 체크)
            if (!pathAttacked) {
                Blocks checkTmp[BLOCK_COUNT][BLOCK_COUNT]; copyBoard(checkTmp, board);
                checkTmp[passFile][rank] = checkTmp[x][y]; // 킹을 통과하는 칸으로 임시 이동
                checkTmp[x][y].name = ""; checkTmp[x][y].color = FALSE;
                if (isCheck(who, checkTmp)) { pathAttacked = true; }
            }
            // 3. 킹이 도착하는 칸이 공격받는지 (g1 체크 / c1 체크)
            if (!pathAttacked) {
                Blocks checkTmp[BLOCK_COUNT][BLOCK_COUNT]; copyBoard(checkTmp, board);
                checkTmp[destFile][rank] = checkTmp[x][y]; // 킹을 도착하는 칸으로 임시 이동
                checkTmp[x][y].name = ""; checkTmp[x][y].color = FALSE;
                if (isCheck(who, checkTmp)) { pathAttacked = true; }
            }

            if (pathAttacked) {
                continue; // 공격받는 길목이 있으면 이 캐슬링은 불가능
            }
        }

        // 이동 후에도 킹이 체크 상태가 아닌지 확인
        if (!isCheck(who, tmp)) moves.insert(dest);
    }
    return moves;
}


vector<pair<int, int>> checkProtectingPieces(bool isWhiteTurn, Blocks(&board)[BLOCK_COUNT][BLOCK_COUNT]) {
    vector<pair<int, int>> result;
    for (int x = 0; x < BLOCK_COUNT; x++) for (int y = 0; y < BLOCK_COUNT; y++)
        if (!board[x][y].name.empty() && board[x][y].color == (isWhiteTurn ? TRUE : FALSE)) {
            set<pair<int, int>> ms = getLegalMoves(x, y, board);
            if (!ms.empty()) result.push_back({ x,y });
        }
    return result;
}

bool detectCheckmate(bool white, Blocks(&board)[BLOCK_COUNT][BLOCK_COUNT]) {
    if (!isCheck(white, board)) return false;
    for (int x = 0; x < BLOCK_COUNT; x++) for (int y = 0; y < BLOCK_COUNT; y++)
        if (!board[x][y].name.empty() && board[x][y].color == (white ? TRUE : FALSE)) {
            auto moves = getLegalMoves(x, y, board);
            if (!moves.empty()) return false;
        }
    return true;
}

// -------------------- rendering & utilities --------------------
Mat createBoardImage() {
    Mat boardMat(BLOCK_SIZE * BLOCK_COUNT, BLOCK_SIZE * BLOCK_COUNT, CV_8UC3);
    Scalar lightColor(175, 223, 240);
    Scalar darkColor(99, 136, 181);
    for (int row = 0; row < BLOCK_COUNT; ++row) for (int col = 0; col < BLOCK_COUNT; ++col) {
        Rect block(col * BLOCK_SIZE, row * BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE);
        bool isLight = ((row + col) % 2 == 0);
        rectangle(boardMat, block, isLight ? lightColor : darkColor, FILLED);
        rectangle(boardMat, block, Scalar(0, 0, 0), 1);
    }
    for (int row = 0; row < BLOCK_COUNT; ++row) for (int col = 0; col < BLOCK_COUNT; ++col) {
        string imgPath = g_block[col][row].name;
        if (imgPath.empty()) continue;
        Mat piece = imread(imgPath, IMREAD_UNCHANGED);
        if (piece.empty()) continue;
        Mat resizedPiece;
        resize(piece, resizedPiece, Size(BLOCK_SIZE, BLOCK_SIZE), 0, 0, INTER_CUBIC);
        if (resizedPiece.channels() == 4) {
            vector<Mat> channels; split(resizedPiece, channels);
            Mat rgb; merge(vector<Mat>{ channels[0], channels[1], channels[2] }, rgb);
            Mat alpha = channels[3];
            Rect roi(col * BLOCK_SIZE, row * BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE);
            Mat boardROI = boardMat(roi);
            rgb.copyTo(boardROI, alpha);
        }
        else {
            resizedPiece.copyTo(boardMat(Rect(col * BLOCK_SIZE, row * BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE)));
        }
    }
    if (selected) {
        // 선택된 기물 하이라이트
        Rect r(selectedX * BLOCK_SIZE, selectedY * BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE);
        Mat roi = boardMat(r);
        Mat selectOverlay(roi.size(), roi.type(), Scalar(0, 0, 255)); // 파란색
        addWeighted(selectOverlay, 0.4, roi, 0.6, 0, roi);

        for (auto& pos : availableMoves) {
            Rect moveR(pos.first * BLOCK_SIZE, pos.second * BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE);
            Mat moveRoi = boardMat(moveR);
            Mat overlay(moveRoi.size(), moveRoi.type(), Scalar(0, 255, 0)); // 녹색
            addWeighted(overlay, 0.5, moveRoi, 0.5, 0, moveRoi);
        }
    }
    return boardMat;
}

HBITMAP matToHBitmap(const Mat& mat) {
    BITMAPINFOHEADER bi; ZeroMemory(&bi, sizeof(BITMAPINFOHEADER));
    bi.biSize = sizeof(BITMAPINFOHEADER); bi.biWidth = mat.cols; bi.biHeight = -mat.rows;
    bi.biPlanes = 1; bi.biBitCount = 24; bi.biCompression = BI_RGB;
    Mat rgb;
    if (mat.channels() == 4) cvtColor(mat, rgb, COLOR_BGRA2BGR);
    else if (mat.channels() == 3) rgb = mat;
    else if (mat.channels() == 1) cvtColor(mat, rgb, COLOR_GRAY2BGR);
    else return nullptr;
    void* ptr = nullptr;
    HBITMAP hBitmap = CreateDIBSection(NULL, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &ptr, NULL, 0);
    if (!hBitmap) return nullptr;
    memcpy(ptr, rgb.data, rgb.total() * rgb.elemSize());
    return hBitmap;
}

pair<int, int> pointToBlock(POINT pt) {
    // UI 보드가 (25, 25)에서 시작한다고 가정하고 pt에서 오프셋을 뺌
    pt.x -= 25;
    pt.y -= 25;

    for (int x = 0; x < BLOCK_COUNT; ++x) {
        for (int y = 0; y < BLOCK_COUNT; ++y) {
            // g_block[x][y].rect의 좌표가 0부터 시작한다고 가정하고 계산
            if (pt.x >= x * BLOCK_SIZE && pt.x < (x + 1) * BLOCK_SIZE &&
                pt.y >= y * BLOCK_SIZE && pt.y < (y + 1) * BLOCK_SIZE) {
                return { x, y };
            }
        }
    }
    return { -1,-1 };
}

void DrawBoardOnBackBuffer(HDC hdc, HWND hWnd) {
    RECT rc; GetClientRect(hWnd, &rc);
    int width = rc.right - rc.left, height = rc.bottom - rc.top;
    if (hdcBack == NULL || hbmBack == NULL || bufferWidth != width || bufferHeight != height) SetupDoubleBuffer(hdc, hWnd);
    FillRect(hdcBack, &rc, (HBRUSH)(COLOR_WINDOW + 1));
    Mat boardMat = createBoardImage();
    HBITMAP hBitmap = matToHBitmap(boardMat);
    if (hBitmap) {
        HDC memDC = CreateCompatibleDC(hdc);
        SelectObject(memDC, hBitmap);
        // 보드를 (25, 25) 위치에 그립니다.
        BitBlt(hdcBack, 25, 25, boardMat.cols, boardMat.rows, memDC, 0, 0, SRCCOPY);
        DeleteDC(memDC);
        DeleteObject(hBitmap);
    }
    BitBlt(hdc, 0, 0, width, height, hdcBack, 0, 0, SRCCOPY);
}

// -------------------- Stockfish integration --------------------
HANDLE g_hChildStd_IN_Wr = NULL;
HANDLE g_hChildStd_OUT_Rd = NULL;
PROCESS_INFORMATION g_piStockfish = { 0 };

bool LaunchStockfish(const wstring& path) {
    SECURITY_ATTRIBUTES saAttr{}; saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); saAttr.bInheritHandle = TRUE; saAttr.lpSecurityDescriptor = NULL;
    HANDLE out_read = NULL, out_write = NULL, in_read = NULL, in_write = NULL;
    if (!CreatePipe(&out_read, &out_write, &saAttr, 0)) return false;
    if (!SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0)) return false;
    if (!CreatePipe(&in_read, &in_write, &saAttr, 0)) return false;
    if (!SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0)) return false;

    STARTUPINFOW si{}; si.cb = sizeof(STARTUPINFOW); si.hStdError = out_write; si.hStdOutput = out_write; si.hStdInput = in_read; si.dwFlags |= STARTF_USESTDHANDLES;
    ZeroMemory(&g_piStockfish, sizeof(PROCESS_INFORMATION));

    BOOL ok = CreateProcessW(path.c_str(), NULL, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &g_piStockfish);
    if (!ok) { CloseHandle(out_read); CloseHandle(out_write); CloseHandle(in_read); CloseHandle(in_write); return false; }

    // close child side handles
    CloseHandle(out_write); CloseHandle(in_read);

    // assign global handles for communication
    g_hChildStd_OUT_Rd = out_read;
    g_hChildStd_IN_Wr = in_write;
    return true;
}

void WriteToStockfish(const string& cmd) {
    if (!g_hChildStd_IN_Wr) return;
    string data = cmd + "\n";
    DWORD written = 0;
    WriteFile(g_hChildStd_IN_Wr, data.c_str(), (DWORD)data.size(), &written, NULL);
}

string ReadFromStockfish(bool waitForBestmove = true) {
    if (!g_hChildStd_OUT_Rd) return "";
    string result;
    CHAR buffer[4096];
    DWORD read = 0;

    // 5초 타임아웃 추가: 파이프에서 영원히 블로킹되는 것을 방지
    auto startTime = chrono::high_resolution_clock::now();
    const chrono::milliseconds timeout(5000); // 5초

    while (true) {
        if (chrono::high_resolution_clock::now() - startTime > timeout) {
            break;
        }

        // 파이프에 데이터가 있는지 확인 (PeekNamedPipe)
        DWORD bytesAvailable = 0;
        if (!PeekNamedPipe(g_hChildStd_OUT_Rd, NULL, 0, NULL, &bytesAvailable, NULL)) break;

        if (bytesAvailable > 0) {
            BOOL success = ReadFile(g_hChildStd_OUT_Rd, buffer, sizeof(buffer) - 1, &read, NULL);
            if (!success || read == 0) break;
            buffer[read] = '\0';
            result += buffer;
        }

        if (waitForBestmove) {
            if (result.find("bestmove") != string::npos) break;
        }
        else {
            if (result.find("readyok") != string::npos) break;
        }

        // 데이터가 없으면 잠시 대기하여 CPU 부하를 줄임
        if (bytesAvailable == 0) this_thread::sleep_for(chrono::milliseconds(20));
        else this_thread::sleep_for(chrono::milliseconds(5));
    }
    return result;
}

string parseBestMove(const string& out) {
    size_t pos = out.find("bestmove ");
    if (pos == string::npos) return "";
    size_t start = pos + 9;
    size_t end = out.find_first_of(" \r\n", start);
    if (end == string::npos) end = out.size();
    return out.substr(start, end - start);
}

// FEN 문자열의 캐슬링 권한 부분 생성
string getCastlingRights() {
    string rights = "";
    if (!whiteKingMoved && !whiteRookH1Moved) rights += 'K';
    if (!whiteKingMoved && !whiteRookA1Moved) rights += 'Q';
    if (!blackKingMoved && !blackRookH8Moved) rights += 'k';
    if (!blackKingMoved && !blackRookA8Moved) rights += 'q';
    return rights.empty() ? "-" : rights;
}

// Convert g_block to FEN
string boardToFEN(Blocks(&board)[BLOCK_COUNT][BLOCK_COUNT], bool whiteTurn) {
    string fen;
    for (int y = 0; y < BLOCK_COUNT; ++y) {
        int empty = 0;
        for (int x = 0; x < BLOCK_COUNT; ++x) {
            string piece = board[x][y].name;
            if (piece.empty()) { empty++; }
            else {
                if (empty > 0) { fen += to_string(empty); empty = 0; }
                if (piece.find("pawn_white") != string::npos) fen.push_back('P');
                else if (piece.find("pawn_black") != string::npos) fen.push_back('p');
                else if (piece.find("rook_white") != string::npos) fen.push_back('R');
                else if (piece.find("rook_black") != string::npos) fen.push_back('r');
                else if (piece.find("knight_white") != string::npos) fen.push_back('N');
                else if (piece.find("knight_black") != string::npos) fen.push_back('n');
                else if (piece.find("bishop_white") != string::npos) fen.push_back('B');
                else if (piece.find("bishop_black") != string::npos) fen.push_back('b');
                else if (piece.find("queen_white") != string::npos) fen.push_back('Q');
                else if (piece.find("queen_black") != string::npos) fen.push_back('q');
                else if (piece.find("king_white") != string::npos) fen.push_back('K');
                else if (piece.find("king_black") != string::npos) fen.push_back('k');
                else fen.push_back('?');
            }
        }
        if (empty > 0) fen += to_string(empty);
        if (y != BLOCK_COUNT - 1) fen.push_back('/');
    }

    // 턴, 캐슬링 권한, 앙파상 타겟, 하프무브, 풀무브
    fen += whiteTurn ? " w " : " b ";
    fen += getCastlingRights() + " "; // 수정된 캐슬링 권한
    fen += "- 0 1"; // 앙파상, 하프무브, 풀무브 임시값

    return fen;
}

// Apply uci move like e2e4 to g_block board
bool applyUCIMoveToBoard(const string& uciMove, Blocks(&board)[BLOCK_COUNT][BLOCK_COUNT]) {
    if (uciMove.size() < 4) return false;
    int fileFrom = uciMove[0] - 'a';
    int rankFrom = 8 - (uciMove[1] - '0');
    int fileTo = uciMove[2] - 'a';
    int rankTo = 8 - (uciMove[3] - '0');
    if (fileFrom < 0 || fileFrom >= 8 || fileTo < 0 || fileTo >= 8 || rankFrom < 0 || rankFrom >= 8 || rankTo < 0 || rankTo >= 8) return false;

    // 프로모션 처리 전에 현재 기물을 복사
    Blocks movedPiece = board[fileFrom][rankFrom];
    string movedPieceName = movedPiece.name;
    bool isKingMove = movedPieceName.find("king") != string::npos;
    bool isRookMove = movedPieceName.find("rook") != string::npos;
    bool isWhite = movedPiece.color == TRUE;

    // --- 캐슬링 룩 이동 처리 (AI 또는 사용자) ---
    // 킹이 2칸 이동하는 경우 (e1g1, e1c1, e8g8, e8c8)
    if (isKingMove && abs(fileFrom - fileTo) == 2) {
        int rookFrom, rookTo;
        if (fileTo == 6) { // 킹사이드 (O-O): e1g1 또는 e8g8
            rookFrom = 7; // h열
            rookTo = 5;   // f열
        }
        else if (fileTo == 2) { // 퀸사이드 (O-O-O): e1c1 또는 e8c8
            rookFrom = 0; // a열
            rookTo = 3;   // d열
        }
        else {
            // 킹의 2칸 이동이지만 캐슬링이 아닌 경우는 무시 (있을 수 없음)
            goto normal_move;
        }

        // 룩 이동
        Blocks rookPiece = board[rookFrom][rankFrom];
        board[rookTo][rankTo] = rookPiece;
        board[rookTo][rankTo].rect = { rookTo * BLOCK_SIZE, rankTo * BLOCK_SIZE, (rookTo + 1) * BLOCK_SIZE, (rankTo + 1) * BLOCK_SIZE };

        // 원래 룩 자리 비우기
        board[rookFrom][rankFrom].name.clear();
        board[rookFrom][rankFrom].color = FALSE;
        board[rookFrom][rankFrom].type = 0;
    }

normal_move:
    // 기물 이동
    board[fileTo][rankTo] = movedPiece;
    board[fileFrom][rankFrom].name.clear();
    board[fileFrom][rankFrom].color = FALSE;
    board[fileFrom][rankFrom].type = 0;

    // UI 업데이트를 위해 rect도 업데이트 (사실은 매번 다시 그리므로 중요하진 않으나 일관성을 위해)
    board[fileTo][rankTo].rect = { fileTo * BLOCK_SIZE, rankTo * BLOCK_SIZE, (fileTo + 1) * BLOCK_SIZE, (rankTo + 1) * BLOCK_SIZE };

    // --- 캐슬링 권한 플래그 업데이트 ---
    if (isKingMove) {
        if (isWhite) whiteKingMoved = true;
        else blackKingMoved = true;
    }
    // 룩이 초기 위치(A, H 파일)에서 이동하면 해당 캐슬링 권한을 제거
    if (isRookMove) {
        if (isWhite) {
            if (fileFrom == 0 && rankFrom == 7) whiteRookA1Moved = true; // a1
            if (fileFrom == 7 && rankFrom == 7) whiteRookH1Moved = true; // h1
        }
        else {
            if (fileFrom == 0 && rankFrom == 0) blackRookA8Moved = true; // a8
            if (fileFrom == 7 && rankFrom == 0) blackRookH8Moved = true; // h8
        }
    }
    // ------------------------------------------

    // 프로모션 처리
    if (uciMove.size() == 5) {
        char p = uciMove[4];
        bool isWhitePawn = movedPieceName.find("pawn_white") != string::npos;
        bool isBlackPawn = movedPieceName.find("pawn_black") != string::npos;

        if (isWhitePawn || isBlackPawn) {
            string color = isWhitePawn ? "white" : "black";
            string newPiece;
            if (p == 'q') newPiece = "queen_" + color + ".png";
            else if (p == 'r') newPiece = "rook_" + color + ".png";
            else if (p == 'b') newPiece = "bishop_" + color + ".png";
            else if (p == 'n') newPiece = "knight_" + color + ".png";

            if (!newPiece.empty()) {
                board[fileTo][rankTo].name = newPiece;
            }
        }
    }

    // 앙파상 로직 추가 필요 (현재는 생략)

    return true;
}

// -------------------- AI Move Logic (Non-blocking) --------------------
void AskForAIMove(HWND hWnd) {
    if (isThinking) return;
    isThinking = true;

    // 백그라운드 스레드에서 AI 계산 실행
    thread aiThread([hWnd]() {
        // 현재 보드 상태를 FEN으로 변환 (업데이트된 FEN 사용)
        string fen = boardToFEN(g_block, isWhiteTurn);
        string posCmd = "position fen " + fen;

        // Stockfish에 명령 전송
        WriteToStockfish(posCmd);
        WriteToStockfish("go depth 12");

        // Stockfish 응답 읽기 (최대 5초 대기)
        string out = ReadFromStockfish(true);
        string aiMove = parseBestMove(out);

        // UI 스레드에 결과 알림 (힙에 동적 할당하여 데이터 전달)
        // 메인 스레드에서 이 메모리를 해제해야 함
        PostMessage(hWnd, WM_STOCKFISH_MOVE_READY, 0, (LPARAM)new string(aiMove));
        });
    // 스레드가 완료되면 시스템이 자동으로 리소스를 정리하도록 분리
    aiThread.detach();
}

// -------------------- Win32 registration / init --------------------
ATOM MyRegisterClass(HINSTANCE hInstance) {
    wcscpy_s(szWindowClass, L"MyChessWindowClass");
    WNDCLASSEXW wcex{}; wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW; wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0; wcex.cbWndExtra = 0; wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(nullptr, IDI_APPLICATION); wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = szWindowClass; wcex.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);
    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow) {
    hInst = hInstance;
    // 창 크기를 보드 크기에 맞게 조정 (+여백)
    HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, BLOCK_SIZE * BLOCK_COUNT + 50, BLOCK_SIZE * BLOCK_COUNT + 70, // 보드 크기 + 여백
        nullptr, nullptr, hInstance, nullptr);
    if (!hWnd) return FALSE;
    ShowWindow(hWnd, nCmdShow); UpdateWindow(hWnd);
    return TRUE;
}

// -------------------- WndProc and event handling --------------------
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static bool inCheck = false;
    static vector<pair<int, int>> restrictedPieces;
    switch (msg) {
    case WM_CREATE:
        for (int r = 0; r < BLOCK_COUNT; ++r) for (int c = 0; c < BLOCK_COUNT; ++c) {
            // rect는 보드 좌표계를 사용하되, 실제 그리는 위치는 DrawBoardOnBackBuffer에서 오프셋 적용
            g_block[c][r].rect = { c * BLOCK_SIZE, r * BLOCK_SIZE, (c + 1) * BLOCK_SIZE, (r + 1) * BLOCK_SIZE };
            g_block[c][r].name = pieceImage[r][c];
            if (g_block[c][r].name.find("white") != string::npos) g_block[c][r].color = TRUE;
            else if (g_block[c][r].name.find("black") != string::npos) g_block[c][r].color = FALSE;
            else g_block[c][r].color = FALSE;
        }
        break;

    case WM_LBUTTONDOWN: {
        // AI가 생각 중일 때는 클릭을 무시
        if (isThinking) break;

        if (isWhiteTurn) { // 현재는 사용자(White) 턴이라고 가정
            inCheck = isCheck(isWhiteTurn, g_block);
            // restrictedPieces는 현재 사용되지 않으나 (getLegalMoves에서 이미 처리), UI에 표시할 경우 사용 가능
            // restrictedPieces = checkProtectingPieces(isWhiteTurn, g_block); 

            POINT pt = { LOWORD(lParam), HIWORD(lParam) };
            pair<int, int> pos = pointToBlock(pt);
            int x = pos.first, y = pos.second;
            if (x == -1 || y == -1) break;

            bool isPieceOfTurn = !g_block[x][y].name.empty() && g_block[x][y].color == (isWhiteTurn ? TRUE : FALSE);

            if (!selected) {
                if (isPieceOfTurn) {
                    selected = true; selectedX = x; selectedY = y;
                    Blocks tmpForMove[BLOCK_COUNT][BLOCK_COUNT];
                    copyBoard(tmpForMove, g_block);
                    availableMoves = getLegalMoves(selectedX, selectedY, tmpForMove);
                    InvalidateRect(hWnd, NULL, FALSE); // FALSE: 배경 지우지 않음 (더블 버퍼링 사용)
                }
            }
            else {
                if (availableMoves.find({ x,y }) != availableMoves.end()) {
                    // 1. 사용자(White) 이동 적용
                    Blocks movedPiece = g_block[selectedX][selectedY];
                    string pieceName = movedPiece.name;
                    bool isKingMove = pieceName.find("king") != string::npos;
                    bool isRookMove = pieceName.find("rook") != string::npos;

                    // UCI 무브 생성 및 적용
                    string moveStr;
                    moveStr += char('a' + selectedX);
                    moveStr += char('8' - selectedY);
                    moveStr += char('a' + x);
                    moveStr += char('8' - y);
                    // 프로모션 처리 로직은 생략 (클릭으로는 구현 불가, 별도 UI 필요)
                    // 지금은 UCI 무브만 생성하고, applyUCIMoveToBoard에서 이동 및 캐슬링 처리

                    if (applyUCIMoveToBoard(moveStr, g_block)) { // 이동 성공
                        moveHistory.push_back(moveStr);

                        // 선택 해제 및 턴 전환
                        selected = false;
                        availableMoves.clear();
                        isWhiteTurn = !isWhiteTurn; // 턴 전환 (Black 턴)
                        InvalidateRect(hWnd, NULL, FALSE);

                        // 2. 체크메이트/스테일메이트 확인
                        if (detectCheckmate(isWhiteTurn, g_block)) {
                            MessageBoxW(hWnd, L"White wins (checkmate)", L"Game Over", MB_OK);
                            PostQuitMessage(0); break;
                        }
                        // 스테일메이트 (체크가 아니면서 둘 곳이 없는 경우) 확인 로직은 현재 생략됨.

                        // 3. AI (Black)에게 다음 수 요청 (비동기)
                        AskForAIMove(hWnd);
                    }
                }
                else {
                    // 유효하지 않은 타겟 클릭 -> 선택 해제
                    selected = false;
                    availableMoves.clear();
                    InvalidateRect(hWnd, NULL, FALSE);
                }
            }
        }
        break;
    }

    case WM_STOCKFISH_MOVE_READY: {
        // AI 스레드에서 결과를 받아 처리 (메인 UI 스레드에서 실행됨)
        string* p_aiMove = (string*)lParam;
        string aiMove = *p_aiMove;
        delete p_aiMove; // 동적 할당된 메모리 해제

        isThinking = false; // AI 계산 완료

        if (!aiMove.empty()) {
            // 1. AI 이동 적용
            applyUCIMoveToBoard(aiMove, g_block);
            moveHistory.push_back(aiMove);
            isWhiteTurn = !isWhiteTurn; // 턴 전환 (White 턴)
            InvalidateRect(hWnd, NULL, FALSE);

            // 2. 체크메이트/스테일메이트 확인
            if (detectCheckmate(isWhiteTurn, g_block)) {
                MessageBoxW(hWnd, L"Black wins (checkmate)", L"Game Over", MB_OK);
                PostQuitMessage(0);
            }
            // 스테일메이트 (체크가 아니면서 둘 곳이 없는 경우) 확인 로직은 현재 생략됨.
        }
        else {
            // AI가 수를 찾지 못한 경우 (무승부 또는 오류)
            isWhiteTurn = !isWhiteTurn; // 턴을 되돌림
            MessageBoxW(hWnd, L"AI가 유효한 수를 찾지 못했습니다. (무승부 또는 오류)", L"AI 오류", MB_OK | MB_ICONWARNING);
        }
        break;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
        DrawBoardOnBackBuffer(hdc, hWnd);
        EndPaint(hWnd, &ps);
        break;
    }

    case WM_ERASEBKGND: return 1;
    case WM_SIZE: CleanUpDoubleBuffer(); InvalidateRect(hWnd, NULL, TRUE); break;
    case WM_DESTROY:
        CleanUpDoubleBuffer();
        // 엔진 종료
        WriteToStockfish("quit");
        if (g_piStockfish.hProcess) { CloseHandle(g_piStockfish.hProcess); CloseHandle(g_piStockfish.hThread); }
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

// About dialog (same as original)
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: return (INT_PTR)TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

// -------------------- entry point --------------------
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    MyRegisterClass(hInstance);
    if (!InitInstance(hInstance, nCmdShow)) return FALSE;

    // Launch Stockfish now (path: change if necessary)
    if (!LaunchStockfish(L"stockfish\\stockfish-windows-x86-64-avx2.exe")) {
        MessageBoxW(NULL, L"Failed to start Stockfish. Make sure path is correct.", L"Error", MB_OK | MB_ICONERROR);
        // proceed without engine (UI will still show)
    }
    else {
        // Stockfish 초기 설정 (초기 응답은 블로킹해도 무방)
        WriteToStockfish("uci");
        ReadFromStockfish(false);
        WriteToStockfish("isready");
        ReadFromStockfish(false);
    }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}