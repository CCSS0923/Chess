#define WIN32_LEAN_AND_MEAN
#include "framework.h"
#include "Chess2Player.h" // Assuming this header is locally defined
#include <string>
#include <vector>
#include <set>
#include <windows.h>
#include <opencv2/opencv.hpp>
#include <iostream>
#include <sstream>

using namespace std;
using namespace cv;

#define MAX_LOADSTRING 100
#define BLOCK_COUNT 8 // Chess board size (8x8)
#define BLOCK_SIZE 100 // Pixel size of each square

// Promotion Dialog IDs 및 관련 함수는 자동 퀸 승격 로직 적용을 위해 모두 제거되었습니다.

// Global variables
HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING] = L"Chess Application (2 Player) - Auto Queen Promote";
WCHAR szWindowClass[MAX_LOADSTRING] = L"MyChessWindowClass";

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);

// -------------------- Blocks and board --------------------
// Structure representing one square on the chessboard
struct Blocks {
	RECT rect;
	string name; // Piece file name (e.g., "rook_black.png")
	BOOL color; // TRUE: White, FALSE: Black
	int type; // Not used
	bool hasMoved; // For castling and pawn double move tracking

	// Explicit initialization (to avoid type.6 warnings)
	Blocks() noexcept : rect{ 0,0,0,0 }, name(""), color(FALSE), type(0), hasMoved(false) {}
	// Added noexcept to the assignment operator (f.6)
	Blocks& operator=(const Blocks& rhs) noexcept {
		if (this != &rhs) {
			rect = rhs.rect;
			name = rhs.name;
			color = rhs.color;
			type = rhs.type;
			hasMoved = rhs.hasMoved;
		}
		return *this;
	}
};

Blocks g_block[BLOCK_COUNT][BLOCK_COUNT];

// Initial piece placement
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

// Game state global variables
bool selected = false;
int selectedX = -1, selectedY = -1;
set<pair<int, int>> availableMoves;
bool isWhiteTurn = true;
pair<int, int> enPassantTarget = { -1, -1 }; // En Passant target {x, y}

// Double buffering variables
HBITMAP hbmBack = NULL;
HDC hdcBack = NULL;
int bufferWidth = 0, bufferHeight = 0;

// -------------------- chess logic prototypes --------------------
void copyBoard(Blocks(&dst)[BLOCK_COUNT][BLOCK_COUNT], Blocks(&src)[BLOCK_COUNT][BLOCK_COUNT]) noexcept;
pair<int, int> findKing(bool white, Blocks(&board)[BLOCK_COUNT][BLOCK_COUNT]) noexcept;
// checkKing: true only when checking if the king can escape check (to prevent infinite recursion)
set<pair<int, int>> allPieceMoves(int x, int y, Blocks(&board)[BLOCK_COUNT][BLOCK_COUNT], bool checkKing = true);
set<pair<int, int>> getLegalMoves(int x, int y, Blocks(&board)[BLOCK_COUNT][BLOCK_COUNT]);
bool isCheck(bool white, Blocks(&board)[BLOCK_COUNT][BLOCK_COUNT]) noexcept;
bool detectCheckmateOrStalemate(bool white, Blocks(&board)[BLOCK_COUNT][BLOCK_COUNT]) noexcept;

// -------------------- Double Buffering --------------------
void SetupDoubleBuffer(HDC hdc, HWND hWnd) {
	RECT rc; GetClientRect(hWnd, &rc);
	int width = rc.right - rc.left, height = rc.bottom - rc.top;
	bufferWidth = width; bufferHeight = height;

	// Cleanup existing buffer
	if (hdcBack) { DeleteDC(hdcBack); hdcBack = NULL; }
	if (hbmBack) { DeleteObject(hbmBack); hbmBack = NULL; }

	// Create new buffer
	hdcBack = CreateCompatibleDC(hdc);
	hbmBack = CreateCompatibleBitmap(hdc, width, height);

	if (hdcBack && hbmBack) {
		SelectObject(hdcBack, hbmBack);
		FillRect(hdcBack, &rc, (HBRUSH)(COLOR_WINDOW + 1));
	}
	else {
		// Clean up safely if creation fails
		if (hbmBack) { DeleteObject(hbmBack); hbmBack = NULL; }
		if (hdcBack) { DeleteDC(hdcBack); hdcBack = NULL; }
	}
}

void CleanUpDoubleBuffer() {
	if (hdcBack) { DeleteDC(hdcBack); hdcBack = NULL; }
	if (hbmBack) { DeleteObject(hbmBack); hbmBack = NULL; }
}

// -------------------- chess logic implementations --------------------
void copyBoard(Blocks(&dst)[BLOCK_COUNT][BLOCK_COUNT], Blocks(&src)[BLOCK_COUNT][BLOCK_COUNT]) noexcept {
	// Use only the BLOCK_COUNT range to prevent array overrun warnings
	for (int x = 0; x < BLOCK_COUNT; x++) for (int y = 0; y < BLOCK_COUNT; y++) dst[x][y] = src[x][y];
}

pair<int, int> findKing(bool white, Blocks(&board)[BLOCK_COUNT][BLOCK_COUNT]) noexcept {
	for (int x = 0; x < BLOCK_COUNT; x++) for (int y = 0; y < BLOCK_COUNT; y++)
		if (!board[x][y].name.empty() && board[x][y].name.find("king") != string::npos && board[x][y].color == (white ? TRUE : FALSE))
			return { x, y };
	return { -1, -1 };
}

// Check if the king of a specific color is in check
bool isCheck(bool white, Blocks(&board)[BLOCK_COUNT][BLOCK_COUNT]) noexcept {
	auto kingPos = findKing(white, board);
	if (kingPos.first < 0) return true; // (Should not happen) Assume check if king is missing

	for (int x = 0; x < BLOCK_COUNT; x++) for (int y = 0; y < BLOCK_COUNT; y++)
		if (!board[x][y].name.empty() && board[x][y].color != (white ? TRUE : FALSE)) {
			// Pass checkKing=false when getting opponent's moves to prevent infinite recursion
			auto mv = allPieceMoves(x, y, board, false);
			if (mv.find(kingPos) != mv.end()) return true;
		}
	return false;
}

// Returns all potential moves for a piece (without considering pins or self-check)
set<pair<int, int>> allPieceMoves(int x, int y, Blocks(&board)[BLOCK_COUNT][BLOCK_COUNT], bool checkKing) {
	set<pair<int, int>> moves;
	if (x < 0 || x >= BLOCK_COUNT || y < 0 || y >= BLOCK_COUNT) return moves;
	Blocks from = board[x][y];
	if (from.name.empty()) return moves;
	bool isWhite = (from.color == TRUE);
	string name = from.name;
	int forward = isWhite ? -1 : 1; // Pawn's forward direction

	// Pawn move logic
	if (name.find("pawn") != string::npos) {
		int ny = y + forward;
		// 1. One step forward
		if (ny >= 0 && ny < BLOCK_COUNT && board[x][ny].name.empty()) moves.insert({ x, ny });
		// 2. Initial two steps move
		if (((isWhite && y == 6) || (!isWhite && y == 1))) {
			int ny2 = y + 2 * forward;
			if (ny2 >= 0 && ny2 < BLOCK_COUNT && board[x][ny].name.empty() && board[x][ny2].name.empty()) moves.insert({ x, ny2 });
		}
		// 3. Diagonal attack
		for (int dx : { -1, 1 }) {
			int nx = x + dx;
			if (nx >= 0 && nx < BLOCK_COUNT && ny >= 0 && ny < BLOCK_COUNT && !board[nx][ny].name.empty() && board[nx][ny].color != from.color)
				moves.insert({ nx, ny });
		}
		// 4. En Passant
		if (enPassantTarget.first != -1 && enPassantTarget.second == ny) {
			for (int dx : { -1, 1 }) {
				int nx = x + dx;
				// Added bounds check for nx to prevent array access issues
				if (nx >= 0 && nx < BLOCK_COUNT && nx == enPassantTarget.first) moves.insert({ nx, ny });
			}
		}
	}
	// Rook, Bishop, Queen move logic (straight/diagonal)
	else if (name.find("rook") != string::npos || name.find("bishop") != string::npos || name.find("queen") != string::npos) {
		vector<pair<int, int>> dirs;
		if (name.find("rook") != string::npos || name.find("queen") != string::npos) dirs.insert(dirs.end(), { {1,0},{-1,0},{0,1},{0,-1} });
		if (name.find("bishop") != string::npos || name.find("queen") != string::npos) dirs.insert(dirs.end(), { {1,1},{1,-1},{-1,1},{-1,-1} });
		for (auto& d : dirs) {
			for (int s = 1; s < BLOCK_COUNT; ++s) {
				int nx = x + d.first * s, ny = y + d.second * s;
				if (nx < 0 || nx >= BLOCK_COUNT || ny < 0 || ny >= BLOCK_COUNT) break;
				if (board[nx][ny].name.empty()) moves.insert({ nx, ny });
				else {
					if (board[nx][ny].color != from.color) moves.insert({ nx, ny });
					break;
				}
			}
		}
	}
	// Knight move logic
	else if (name.find("knight") != string::npos) {
		int c[8][2] = { {1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2} };
		for (auto& co : c) {
			int nx = x + co[0], ny = y + co[1];
			if (nx >= 0 && nx < BLOCK_COUNT && ny >= 0 && ny < BLOCK_COUNT && (board[nx][ny].name.empty() || board[nx][ny].color != from.color))
				moves.insert({ nx, ny });
		}
	}
	// King move logic
	else if (name.find("king") != string::npos) {
		int dirs[8][2] = { {1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1} };
		for (auto& d : dirs) {
			int nx = x + d[0], ny = y + d[1];
			if (nx >= 0 && nx < BLOCK_COUNT && ny >= 0 && ny < BLOCK_COUNT && (board[nx][ny].name.empty() || board[nx][ny].color != from.color))
				moves.insert({ nx, ny });
		}

		// --- Castling logic ---
		if (checkKing) { // Only check when generating legal moves (not for check detection)
			if (!from.hasMoved && (y == (isWhite ? 7 : 0))) { // King hasn't moved and is on 1st/8th rank

				// 1. Kingside Castling (h file Rook)
				if (!board[7][y].name.empty() && board[7][y].name.find("rook") != string::npos && !board[7][y].hasMoved && board[5][y].name.empty() && board[6][y].name.empty() &&
					!isCheck(isWhite, board)) { // King is not currently in check

					Blocks tmp[BLOCK_COUNT][BLOCK_COUNT];

					// a) Check if F file (5) is safe (transit square)
					copyBoard(tmp, board);
					tmp[5][y] = tmp[4][y]; tmp[4][y].name.clear(); // Temp move King to F file
					if (!isCheck(isWhite, tmp)) {
						// b) Check if G file (6) is safe (destination square)
						copyBoard(tmp, board);
						tmp[6][y] = tmp[4][y]; tmp[4][y].name.clear(); // Temp move King to G file
						if (!isCheck(isWhite, tmp)) {
							moves.insert({ 6, y }); // Castling is possible
						}
					}
				}

				// 2. Queenside Castling (a file Rook)
				if (!board[0][y].name.empty() && board[0][y].name.find("rook") != string::npos && !board[0][y].hasMoved && board[1][y].name.empty() && board[2][y].name.empty() && board[3][y].name.empty() &&
					!isCheck(isWhite, board)) { // King is not currently in check

					Blocks tmp[BLOCK_COUNT][BLOCK_COUNT];

					// a) Check if D file (3) is safe (transit square)
					copyBoard(tmp, board);
					tmp[3][y] = tmp[4][y]; tmp[4][y].name.clear(); // Temp move King to D file
					if (!isCheck(isWhite, tmp)) {
						// b) Check if C file (2) is safe (destination square)
						copyBoard(tmp, board);
						tmp[2][y] = tmp[4][y]; tmp[4][y].name.clear(); // Temp move King to C file
						if (!isCheck(isWhite, tmp)) {
							moves.insert({ 2, y }); // Castling is possible
						}
					}
				}
			}
		}
		// --- Castling logic end ---
	}
	return moves;
}

// Returns only the legal moves that do not result in self-check
set<pair<int, int>> getLegalMoves(int x, int y, Blocks(&board)[BLOCK_COUNT][BLOCK_COUNT]) {
	set<pair<int, int>> moves;
	set<pair<int, int>> src = allPieceMoves(x, y, board); // All potential moves
	bool who = board[x][y].color == TRUE; // Current piece color

	for (auto& dest : src) {
		Blocks tmp[BLOCK_COUNT][BLOCK_COUNT];
		copyBoard(tmp, board);

		int destX = dest.first;
		int destY = dest.second;
		bool isPawn = board[x][y].name.find("pawn") != string::npos;
		bool isKing = board[x][y].name.find("king") != string::npos;

		// Detect En Passant move
		bool isEnPassant = isPawn && (destX != x && tmp[destX][destY].name.empty() && (who ? destY == y - 1 : destY == y + 1));

		// Detect Castling move (King moves 2 squares horizontally)
		bool isCastling = isKing && abs(destX - x) == 2;

		if (isCastling) {
			// Castling path check is already done in allPieceMoves.
			// Only simulate the king's final position here to check for check.
		}
		else if (isEnPassant) {
			// En Passant: move pawn, remove captured pawn
			tmp[destX][destY] = tmp[x][y];
			tmp[x][y].name.clear();
			int capturedY = who ? y + 1 : y - 1;
			tmp[destX][capturedY].name.clear(); // Remove captured pawn
		}
		else {
			// Normal move (including capturing opponent's piece)
			tmp[destX][destY] = tmp[x][y];
			tmp[x][y].name.clear();
		}

		// Check if the king is not in check after the move
		if (!isCheck(who, tmp)) {
			moves.insert(dest);
		}
	}
	return moves;
}

// Check if there are any movable pieces (for checkmate/stalemate detection)
bool detectCheckmateOrStalemate(bool white, Blocks(&board)[BLOCK_COUNT][BLOCK_COUNT]) noexcept {
	// Check if any piece can move
	for (int x = 0; x < BLOCK_COUNT; x++)
		for (int y = 0; y < BLOCK_COUNT; y++)
			if (!board[x][y].name.empty() && board[x][y].color == (white ? TRUE : FALSE)) {
				auto moves = getLegalMoves(x, y, board);
				if (!moves.empty()) return false; // Game continues if at least one legal move exists
			}

	// No legal moves available
	if (isCheck(white, board)) {
		// Checkmate
		wstring winner = white ? L"Black" : L"White";
		wstring message = winner + L" wins by Checkmate!";
		MessageBoxW(NULL, message.c_str(), L"Game Over", MB_OK);
		return true;
	}
	else {
		// Stalemate (Draw)
		MessageBoxW(NULL, L"Stalemate! (Draw)", L"Game Over", MB_OK);
		return true;
	}
}

// -------------------- Promotion Dialog Implementation (REMOVED) --------------------
// 대화 상자 생성 및 처리를 위한 함수는 제거되었습니다.

// -------------------- rendering & utilities --------------------
// Create chessboard image using OpenCV
Mat createBoardImage() {
	Mat boardMat(BLOCK_SIZE * BLOCK_COUNT, BLOCK_SIZE * BLOCK_COUNT, CV_8UC3);
	Scalar lightColor(175, 223, 240); // Light sky blue
	Scalar darkColor(99, 136, 181); // Dark blue

	// 1. Draw chessboard background
	for (int row = 0; row < BLOCK_COUNT; ++row) for (int col = 0; col < BLOCK_COUNT; ++col) {
		Rect block(col * BLOCK_SIZE, row * BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE);
		bool isLight = ((row + col) % 2 == 0);
		rectangle(boardMat, block, isLight ? lightColor : darkColor, FILLED);
		rectangle(boardMat, block, Scalar(0, 0, 0), 1);
	}

	// 2. Draw pieces
	for (int row = 0; row < BLOCK_COUNT; ++row) for (int col = 0; col < BLOCK_COUNT; ++col) {
		string imgPath = g_block[col][row].name;
		if (imgPath.empty()) continue;

		// 주의: 실제 실행 환경에서 이 경로에 PNG 파일이 존재해야 합니다.
		Mat piece = imread(imgPath, IMREAD_UNCHANGED);
		if (piece.empty()) {
			// 디버그: 이미지 대신 조각 이름을 텍스트로 표시
			putText(boardMat, imgPath, Point(col * BLOCK_SIZE + 5, row * BLOCK_SIZE + 50), FONT_HERSHEY_SIMPLEX, 0.5, Scalar(0, 0, 0), 1);
			continue;
		}

		Mat resizedPiece;
		resize(piece, resizedPiece, Size(BLOCK_SIZE, BLOCK_SIZE), 0, 0, INTER_CUBIC);

		// Handle Alpha channel (transparency)
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

	// 3. Highlight available moves for the selected piece
	if (selected) {
		for (auto& pos : availableMoves) {
			Rect r(pos.first * BLOCK_SIZE, pos.second * BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE);
			Mat roi = boardMat(r);
			// Green translucent overlay
			Mat overlay(roi.size(), roi.type(), Scalar(0, 255, 0));
			addWeighted(overlay, 0.5, roi, 0.5, 0, roi);
		}
		// Highlight the selected square
		Rect r_sel(selectedX * BLOCK_SIZE, selectedY * BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE);
		Mat roi_sel = boardMat(r_sel);
		Mat overlay_sel(roi_sel.size(), roi_sel.type(), Scalar(255, 255, 0)); // Yellow
		addWeighted(overlay_sel, 0.5, roi_sel, 0.5, 0, roi_sel);
	}
	return boardMat;
}

// Convert OpenCV Mat to Win32 HBITMAP
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
	// Use memcpy safely
	memcpy(ptr, rgb.data, rgb.total() * rgb.elemSize());
	return hBitmap;
}

// Convert window coordinates to chessboard coordinates
pair<int, int> pointToBlock(POINT pt) {
	// Calculate considering 25-pixel margin
	int x = (pt.x - 25) / BLOCK_SIZE;
	int y = (pt.y - 25) / BLOCK_SIZE;

	if (x >= 0 && x < BLOCK_COUNT && y >= 0 && y < BLOCK_COUNT) {
		return { x, y };
	}
	return { -1,-1 };
}

// Draw board on back buffer and display on screen
void DrawBoardOnBackBuffer(HDC hdc, HWND hWnd) {
	RECT rc; GetClientRect(hWnd, &rc);
	int width = rc.right - rc.left, height = rc.bottom - rc.top;

	// Reset buffer if needed
	if (hdcBack == NULL || hbmBack == NULL || bufferWidth != width || bufferHeight != height) {
		SetupDoubleBuffer(hdc, hWnd);
	}

	// Return if SetupDoubleBuffer failed
	if (hdcBack == NULL) return;

	// Clear background
	FillRect(hdcBack, &rc, (HBRUSH)(COLOR_WINDOW + 1));

	Mat boardMat = createBoardImage();
	HBITMAP hBitmap = matToHBitmap(boardMat);

	if (hBitmap) {
		HDC memDC = CreateCompatibleDC(hdc);
		SelectObject(memDC, hBitmap);
		// Display board centered (25, 25 pixel margin)
		BitBlt(hdcBack, 25, 25, boardMat.cols, boardMat.rows, memDC, 0, 0, SRCCOPY);
		DeleteDC(memDC);
		DeleteObject(hBitmap);
	}
	else {
		// Notify if image loading failed
		SetBkMode(hdcBack, TRANSPARENT);
		TextOutW(hdcBack, 50, 50, L"Image Loading Failed or Invalid Mat", 34);
	}

	// Display turn
	wstring turnMsg = isWhiteTurn ? L"White's Turn" : L"Black's Turn";
	SetTextColor(hdcBack, isWhiteTurn ? RGB(255, 255, 255) : RGB(0, 0, 0));
	SetBkColor(hdcBack, isWhiteTurn ? RGB(0, 0, 0) : RGB(255, 255, 255));
	// Display in the center of the top margin (25 pixels height)
	RECT turnRect = { 25, 5, 25 + 800, 25 };
	DrawTextW(hdcBack, turnMsg.c_str(), -1, &turnRect, DT_SINGLELINE | DT_CENTER | DT_VCENTER);


	// Double buffering: transfer back buffer to front buffer
	BitBlt(hdc, 0, 0, width, height, hdcBack, 0, 0, SRCCOPY);
}

// -------------------- Win32 registration / init --------------------
ATOM MyRegisterClass(HINSTANCE hInstance) {
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

	// Set window size considering board size + margin (approx. 850x850 for 800x800 board)
	HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, 0, BLOCK_SIZE * BLOCK_COUNT + 50, BLOCK_SIZE * BLOCK_COUNT + 80, // 50, 80 for margins and title bar
		nullptr, nullptr, hInstance, nullptr);
	if (!hWnd) return FALSE;

	ShowWindow(hWnd, nCmdShow); UpdateWindow(hWnd);
	return TRUE;
}

// Retain function definition to comply with standard wWinMain annotation
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
	MyRegisterClass(hInstance);
	if (!InitInstance(hInstance, nCmdShow)) return FALSE;
	MSG msg;
	while (GetMessage(&msg, nullptr, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	CleanUpDoubleBuffer(); // Ensure cleanup on exit
	return (int)msg.wParam;
}

// -------------------- WndProc and event handling (Core logic) --------------------
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
	case WM_CREATE:
		for (int r = 0; r < BLOCK_COUNT; ++r) {
			for (int c = 0; c < BLOCK_COUNT; ++c) {
				// Store relative position within the window (not directly used for logic)
				g_block[c][r].rect = { c * BLOCK_SIZE, r * BLOCK_SIZE, (c + 1) * BLOCK_SIZE, (r + 1) * BLOCK_SIZE };
				g_block[c][r].name = pieceImage[r][c];

				// Color and hasMoved initialization
				if (g_block[c][r].name.find("white") != string::npos) g_block[c][r].color = TRUE;
				else if (g_block[c][r].name.find("black") != string::npos) g_block[c][r].color = FALSE;
				else g_block[c][r].color = FALSE;

				// Initial hasMoved setup for King and Rook
				if (r == 0 || r == 7) {
					string pieceName = g_block[c][r].name;
					if (pieceName.find("king") != string::npos || pieceName.find("rook") != string::npos) {
						g_block[c][r].hasMoved = false;
					}
				}
			}
		}
		break;

	case WM_LBUTTONDOWN: {
		POINT pt = { LOWORD(lParam), HIWORD(lParam) };
		pair<int, int> pos = pointToBlock(pt); // Clicked chessboard coordinate (x, y)
		int x = pos.first, y = pos.second;
		if (x == -1 || y == -1) break;

		bool isPieceOfTurn = !g_block[x][y].name.empty() && g_block[x][y].color == (isWhiteTurn ? TRUE : FALSE);

		if (!selected) {
			// Select piece: only pieces of the current turn can be selected
			if (isPieceOfTurn) {
				selected = true; selectedX = x; selectedY = y;
				availableMoves = getLegalMoves(selectedX, selectedY, g_block);
				InvalidateRect(hWnd, NULL, TRUE);
			}
		}
		else {
			// Move or re-select
			if (x == selectedX && y == selectedY) {
				// Re-select (Deselect)
				selected = false;
				availableMoves.clear();
				InvalidateRect(hWnd, NULL, TRUE);
				break;
			}

			// Check if it's a valid target move
			if (availableMoves.find({ x,y }) != availableMoves.end()) {
				// --- 1. Perform valid move ---
				Blocks movedPiece = g_block[selectedX][selectedY];
				bool isKing = movedPiece.name.find("king") != string::npos;
				bool isPawn = movedPiece.name.find("pawn") != string::npos;

				// Save En Passant target temporarily (the captured location)
				pair<int, int> tempEnPassantTarget = enPassantTarget;
				enPassantTarget = { -1, -1 }; // En Passant target becomes invalid after the turn

				// 2. Handle En Passant
				// En Passant move condition: Pawn moves diagonally to an empty square, and that square was the temporary En Passant target
				bool isEnPassant = isPawn && (x != selectedX && g_block[x][y].name.empty() && tempEnPassantTarget.first == x && tempEnPassantTarget.second == y);
				if (isEnPassant) {
					int capturedY = isWhiteTurn ? y + 1 : y - 1;
					g_block[x][capturedY].name.clear(); // Remove captured pawn
				}

				// 3. Handle Castling
				bool isCastling = isKing && abs(x - selectedX) == 2;
				if (isCastling) {
					int rookX, newRookX;
					if (x > selectedX) { // Kingside Castling (g file)
						rookX = 7; newRookX = 5; // Move h-file rook to f-file
					}
					else { // Queenside Castling (c file)
						rookX = 0; newRookX = 3; // Move a-file rook to d-file
					}
					// Move Rook
					g_block[newRookX][y] = g_block[rookX][y];
					g_block[newRookX][y].hasMoved = true;
					g_block[rookX][y].name.clear();
					g_block[rookX][y].color = FALSE; // Clear previous rook location
					g_block[rookX][y].hasMoved = false;
				}

				// 4. Normal piece move
				// Temporarily copy piece, then clear and move
				Blocks tempPiece = g_block[selectedX][selectedY];
				g_block[selectedX][selectedY].name.clear(); // Clear starting square
				g_block[selectedX][selectedY].color = FALSE;
				g_block[selectedX][selectedY].type = 0;
				g_block[selectedX][selectedY].hasMoved = false; // Clear flag at previous location

				g_block[x][y] = tempPiece; // Move to destination
				g_block[x][y].hasMoved = true; // Set moved flag


				// 5. Set En Passant target (if pawn moved two squares)
				if (isPawn && abs(y - selectedY) == 2) {
					enPassantTarget = { x, isWhiteTurn ? y + 1 : y - 1 };
				}

				// 6. Check for Promotion (AI Logic: Automatic Queen Promotion)
				if (isPawn && (y == 0 || y == 7)) {
					// 폰 승격: AI/엔진의 표준 로직에 따라 무조건 퀸으로 승격
					string newPieceName = isWhiteTurn ? "queen_white.png" : "queen_black.png";
					g_block[x][y].name = newPieceName;
					g_block[x][y].hasMoved = true;
					g_block[x][y].color = isWhiteTurn ? TRUE : FALSE;
				}

				// 7. Switch turn and update UI
				selected = false;
				availableMoves.clear();
				isWhiteTurn = !isWhiteTurn;
				InvalidateRect(hWnd, NULL, TRUE);

				// 8. Check game end conditions (Checkmate/Stalemate)
				if (detectCheckmateOrStalemate(isWhiteTurn, g_block)) {
					// Game over handling
				}
				else if (isCheck(isWhiteTurn, g_block)) {
					// Notify check status
					wstring checkMsg = isWhiteTurn ? L"White is in Check!" : L"Black is in Check!";
					MessageBoxW(hWnd, checkMsg.c_str(), L"Check", MB_OK | MB_ICONWARNING);
				}

			}
			else {
				// Clicked an invalid target -> Deselect or try to select a new piece
				selected = false;
				availableMoves.clear();
				// If another piece of the current turn was clicked, auto-reselect
				if (isPieceOfTurn) {
					selected = true; selectedX = x; selectedY = y;
					availableMoves = getLegalMoves(selectedX, selectedY, g_block);
				}
				InvalidateRect(hWnd, NULL, TRUE);
			}
		}
		break;
	}

	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hWnd, &ps);
		DrawBoardOnBackBuffer(hdc, hWnd);
		EndPaint(hWnd, &ps);
		break;
	}

	case WM_ERASEBKGND: return 1; // Prevent flickering
	case WM_SIZE: CleanUpDoubleBuffer(); InvalidateRect(hWnd, NULL, TRUE); break;
	case WM_DESTROY: CleanUpDoubleBuffer(); PostQuitMessage(0); break;
	default: return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	return 0;
}

// About dialog
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
	// Use INT_PTR for pointer safety
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
