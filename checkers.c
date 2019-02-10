#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <termios.h>
#include <unistd.h>

// constant definitions
#define MAN "  " // two character long symbol for ordinary checker
#define KING "ㆍ"
#define MAXSIDE 26 // maximal size of board's side
#define MINSIDE 4 // minimum size of board's side
#define LEN 3 // height of one cell (for centered checker placement must be odd)
#define BLACK 40 // color of black checker
#define WHITE 107 // color of white checker
#define BLACKBG 100 // color of black cell
#define WHITEBG 47 // color of white cell
#define BORDER 100 // background color of board's border
#define AVAILABLE 101 // background color of board's square if it is available for move
#define AMBIGUOUS 41 // background color of board's square if it is available for move and can be reached by different routes
#define ALLDIRECT 5 // used in board scanning - means that we do not prohibit any direction
#define DELAY 100000 // amount in microseconds that is to be passed to usleep function

// possible types of pieces
enum piece {nopiece = -1, bman, wman, bking, wking};

// struct that represents board's square
struct square
{
	// type of piece - NULL if none is on the square
	enum piece type;

	// values for square/piece highlighting
	int bgselection;
	bool pcselected;

	// pointers to four adjacent squares: 0 - top left, 1 - top right, 2 - bottom right, 3 - bottom left
	struct square * adjacent[4];

	// additional prohibited directions if square is crossed multiple times
	bool crossed[4];
};

// struct that refers to a square available for move
struct move
{
	// pointer to a board's square
	struct square * square;

	// pointers to next square with enemy piece
	struct square * tocapture[4];

	// pointers to other moves in respective directions
	struct move * next[4];
};

// struct that is used to create linked list of sequence of moves
struct chain
{
	// pointer to border's square
	struct square * square;

	// pointer to square with enemy piece
	struct square * tocapture;

	// pointer to next struct in linked list
	struct chain * next;
};

// global variables
int SIDE; // stores board's side size
int pieces[2]; // number of black (0) and white(1) pieces
struct square * board[MAXSIDE][MAXSIDE] = {}; // main board
struct move * movestart; // global pointer to move struct
struct chain * chainstart; // global pointer to chain struct
void * empty; // pointer returned in special cases
int turn; // indicates whose turn to move
typedef int (*MovePiece)(struct square *); // pointer to ManMove() and KingMove() functions
typedef bool (*ScanPiece)(struct square *);

// function prototypes
int Menu();
int Save();
int Load();
void ClearBoard();
bool IsStucked(int pcolor);
bool SimpleMoveScan(struct square * piece);
int Move(int pcolor);
int MoveKing(struct square * piece);
bool KingSimpleCaptureScan(struct square * piece);
int KingCaptureScan(struct square * piece);
struct square * KingEnemyScan(struct square * pointer, int direction, int enemy);
struct square * KingCapture(struct square * piece);
int KingMoveScan(struct square * piece);
int MoveMan(struct square *);
struct square * ManCapture(struct square * piece);
int Search(struct square * square, struct move * entry, struct chain * chain, int prohibited);
bool ManCaptureScan(struct square * current, struct move * entry, int enemy, int prohibited);
bool ManSimpleCaptureScan(struct square * square);
bool MustCapture(int color);
int MarkSquares(struct move * entry, int prohibited);
void UnmarkSquares(struct move * entry, int prohibited);
void ClearMoveList(struct move * entry, int prohibited);
int CheckSquare(char * s, int * row, int * col);
struct square * SimpleMove(struct square * piece, struct square * square);
int SimpleSearch(struct square * piece, struct move * entry, int prohibited);
int Opposite(int x);
struct square * GetSquare(char * prompt);
void InitializeBoard();
void InitializePieces();
void PrintVacantSquare(int bg);
void PrintSquare(struct square * piece);
void PrintRow(int row);
void PrintBoard();

MovePiece MovePointer[2] = {&MoveMan, &MoveKing};
ScanPiece ScanPointer[2] = {&ManSimpleCaptureScan, &KingSimpleCaptureScan};

int main(int argc, char * argv[])
{
	// check for custom board size
	if (argc > 1)
	{
		SIDE = atoi(argv[1]);
		if (SIDE > MAXSIDE)
		{
			fprintf(stderr, "Board cannot be more than 26 cells per side\n");
			return 1;
		}
		if (SIDE < 4)
		{
			fprintf(stderr, "Board cannot be less than 4 cells per side\n");
			return 1;	
		}
	}
	else
		SIDE = 8;

	// Initialize squares without pieces and print empty board
	InitializeBoard();
	PrintBoard();
	
	// Select menu option
	int mode = Menu();
	// Exit
	if (mode == 2)
	{
		ClearBoard();
		return 0;
	}
	// New
	if (mode == 0)
		InitializePieces();
	// Load
	if (mode == 1)
	{
		printf("\e[s");
		while (true)
		{
			printf("\e[u\e[J");
			int load = Load();
			if (load == 0)
				break;
			if (load == 1)
			{
				printf("Couldn't open savefile\n");
				usleep(DELAY * 10);
				continue;
			}
			if (load == 2)
			{
				printf("Savefile has different board size\n");
				usleep(DELAY * 10);
				continue;
			}
		}
	}

	// While there are pieces of both colors on board
	while (pieces[0] > 0 && pieces[1] > 0)
	{
		// Check if current player is able to move
		if (IsStucked(turn % 2))
		{
			printf("\e[1;92m%s'S VICTORY\e[0m\n", turn % 2 == 0 ? "WHITE" : "BLACK");
			return 0;
		}
		// Move and change the turn
		Move(turn % 2);
		turn++;
	}

	printf("\e[1;92m%s'S VICTORY\e[0m\n", pieces[0] == 0 ? "WHITE" : "BLACK");
	ClearBoard();
}

int Menu()
{
	char * highlight = "\e[1;91m"; // color properties of highlighted option
	int cursor = 0; // points to an option

	// Option names
	char * new = "New";
	char * load = "Load";
	char * exit = "Exit";

	// Tell terminal to hide input
	struct termios tty;
    tcgetattr(STDIN_FILENO, &tty);
    tty.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &tty);

    printf("\e[?25l\e[s"); // hide cursor and save its position
	while (1)
	{
		// Print option names with one highlighted
		printf("%s%s \e[0m", cursor % 3 == 0 ? highlight : "", new);
		printf("%s%s \e[0m", cursor % 3 == 1 ? highlight : "", load);
		printf("%s%s \e[0m", cursor % 3 == 2 ? highlight : "", exit);

		system("stty raw"); // force terminal to send input to stdin immediately (not waiting for Enter) 
		char c = getchar(); // read key
		system("stty cooked"); // restore terminal
		
		// If Enter is pressed
		if (c == 13)
		{
			printf("\n");
			break;
		}
		if (c == 'D') // if Left arrow key is pressed
			cursor += 2;
		if (c == 'C') // if right arrow key is pressed
			cursor++;

		printf("\e[2K\e[u"); // clear line and restore cursor position
	}

	// Tell terminal to show input
    tty.c_lflag |= ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &tty);

    printf("\e[?25h"); // show cursor

    // return selected option
    return cursor % 3;
}

// Save board status to file
int Save()
{
	printf("Enter save name: "); // prompt for input
	char *extension = ".save"; // set save extension
	char filename[32]; // string to store filename
	fgets(filename, sizeof(filename) - sizeof(extension), stdin); // get input
	filename[strcspn(filename, "\n")] = 0; // supplant \n with \0
	if (strlen(filename) == 0) // if just pressed Enter
		return 1;
	
	// Check if the user typed extension manually
	if (strcmp(extension, filename + strlen(filename) - strlen(extension)))
		strcat(filename, extension);

	FILE * file = fopen(filename, "w"); // create file or rewrite existing one
	fprintf(file, "%d\n", SIDE); // write side size
	// Write pieces
	for (int i = 0; i < SIDE; i++)
	{
		for (int j = 0; j < SIDE; j++)
		{
			fprintf(file, "%d", board[i][j] == NULL || board[i][j]->type == nopiece ? 0 : board[i][j]->type + 1);
		}
		fprintf(file, "\n");
	}
	fprintf(file, "%d\n%d\n%d\n", pieces[0], pieces[1], turn % 2); // write number of pieces and turn to move

	fclose(file); // close file
}

// Restore board status from file
int Load()
{
	printf("Enter save name: "); // prompt for input
	char *extension = ".save"; // set save extension
	char filename[32]; // string to store filename
	fgets(filename, sizeof(filename) - sizeof(extension), stdin); // get input
	filename[strcspn(filename, "\n")] = 0; // supplant \n with \0
	if (strlen(filename) == 0) // if just pressed Enter
		return 1;

	// Check if the user typed extension manually
	if (strcmp(extension, filename + strlen(filename) - strlen(extension)))
		strcat(filename, extension);

	// Try to open a file
	FILE * file = fopen(filename, "r");
	if (file == NULL) // if couldn't open (most likely, file doesn't exist)
		return 1;

	// Read board size and check if it is the same as current's
	int side;
	fscanf(file, "%d", &side);
	if (side != SIDE)
	{
		fclose(file);
		return 2;
	}
	fseek(file, sizeof(char), SEEK_CUR); // skip '\n' 

	// Read pieces
	char * c = malloc(sizeof(char));
	for (int i = 0; i < SIDE; i++)
	{
		for (int j = 0; j < SIDE; j++)
		{
			fread(c, sizeof(char), 1, file);
			int type = atoi(c) - 1;
			if (type != -1 && board[i][j] != NULL)
				board[i][j]->type = type;
		}
		fseek(file, sizeof(char), SEEK_CUR);
	}
	// Read number of pieces and current turn
	fscanf(file, "%d", &pieces[0]);
	fseek(file, sizeof(char), SEEK_CUR);
	fscanf(file, "%d", &pieces[1]);
	fseek(file, sizeof(char), SEEK_CUR);
	fscanf(file, "%d", &turn);

	// Close the file
	free(c);
	fclose(file);
	return 0;
}

// Free the board
void ClearBoard()
{
	for (int i = 0; i < SIDE; i++)
	{
		for (int j = 0; j < SIDE; j++)
			free(board[i][j]);
	}
}

// Check if the player is able to move
bool IsStucked(int pcolor)
{
	for (int i = 0; i < SIDE; i++)
	{
		for (int j = 0; j < SIDE; j++)
		{
			if (board[i][j] != NULL && board[i][j]->type % 2 == pcolor)
			{
				if (SimpleMoveScan(board[i][j]))
					return false;
			}
		}
	}

	return true;
}

// Check if the piece is able to move
bool SimpleMoveScan(struct square * piece)
{
	if (piece == NULL || piece->type == nopiece)
		return false;

	if (ScanPointer[piece->type / 2](piece))
		return true;
	
	int start, end;
	if (piece->type / 2 == 0)
	{
		start = piece->type % 2 == 0 ? 2 : 0;
		end = start + 2;
	}
	else
	{
		start = 0;
		end = 4;
	}

	for (int i = start; i < end; i++)
	{
		if (piece->adjacent[i] != NULL && piece->adjacent[i]->type == nopiece)
			return true;
	}

	return false;
}

// Pick the piece to move and call respective move function
int Move(int pcolor)
{
	PrintBoard();

	printf("\e[1m%s's move\e[0m\n\e[s", pcolor == 0 ? "Black" : "White");
	int row = 0, col = 0;
	while (true)
	{
		printf("\e[u\e[J");
		struct square * piece = GetSquare("Pick a piece: ");
		// If picked wrong square
		if (piece == NULL || piece->type == nopiece || piece->type % 2 != pcolor)
			continue;
		// If capture must be done and picked piece isn't able to capture
		if (MustCapture(pcolor) && !ScanPointer[piece->type / 2](piece))
			continue;
		// If moving is successful
		if (!MovePointer[piece->type / 2](piece))
			break;
	}

	PrintBoard();
}

// Move king piece
int MoveKing(struct square * piece)
{
	movestart = calloc(1, sizeof(struct move));
	int enemy = (piece->type % 2 + 1) % 2;

	// Check if capture must be done and call respective scanning function
	bool mustcapture = KingSimpleCaptureScan(piece);
	if (mustcapture)
		KingCaptureScan(piece);
	else
		KingMoveScan(piece);

	int movecount = MarkSquares(movestart, ALLDIRECT);	
	// If there are available moves
	if (movecount > 0)
		piece->pcselected = true;
	else
	{
		ClearMoveList(movestart, ALLDIRECT);
		return 1;
	}

	// Pick a destination square and move to it
	int row = 0, col = 0;
	struct square * dest;
	int result = 0;
	while (true)
	{
		PrintBoard();
		printf("\e[1m%s's move\e[0m\n\e[s", piece->type % 2 == 0 ? "Black" : "White");
		dest = GetSquare("Pick destination: ");
		// If picked wrong destination
		if (dest == NULL || dest->type != nopiece)
			continue;
		// If piece have already been moved in previous iterations and player pressed Enter
		if (dest == empty && result != 0)
		{
			UnmarkSquares(movestart, ALLDIRECT);
			ClearMoveList(movestart, ALLDIRECT);
			break;
		}
		// If picked destination is not an available square
		if (SimpleSearch(dest, movestart, ALLDIRECT) == 0)
			continue;

		// Move
		if (!mustcapture)
		{
			piece = SimpleMove(piece, dest);
			UnmarkSquares(movestart, ALLDIRECT);
			ClearMoveList(movestart, ALLDIRECT);
			break;
		}
		// Capture
		else
		{
			chainstart = calloc(1, sizeof(struct chain));
			result = Search(dest, movestart, chainstart, ALLDIRECT);
			
			UnmarkSquares(movestart, ALLDIRECT);
			PrintBoard();
			piece = KingCapture(piece);
			ClearMoveList(movestart, ALLDIRECT);
			// If there still are available squares
			if (KingSimpleCaptureScan(piece))
			{
				movestart = calloc(1,sizeof(struct move));
				KingCaptureScan(piece);
				MarkSquares(movestart, ALLDIRECT);
				continue;
			}

			break;
		}
	}

	PrintBoard();
	usleep(DELAY);
	piece->pcselected = false;
	return 0;
}

// Scan if there are capture moves
bool KingSimpleCaptureScan(struct square * piece)
{
	if (piece == NULL || piece->type == nopiece)
		return false;

	int enemy = (piece->type % 2 + 1) % 2;
	for (int i = 0; i < 4; i++)
	{
		// Go through diagonal untill enemy is found or line is over
		struct square * pointer = piece->adjacent[i];
		while (true)
		{
			// If pointer has reached the end of the diagonal 
			if (pointer == NULL || (pointer->type != nopiece && pointer->type % 2 != enemy))
				break;
			// If pointer has reached an enemy square
			else if (pointer->type != nopiece && pointer->type % 2 == enemy)
			{
				if (pointer->adjacent[i] != NULL && pointer->adjacent[i]->type == nopiece)
					return true;
				else
					break;
			}
			// If pointer is pointing to an empty squa
			else
				pointer = pointer->adjacent[i];
		}
	}

	return false;
}

// Scan for available king-capture squares and build move structure
int KingCaptureScan(struct square * piece)
{
	int count = 0;
	int enemy = (piece->type % 2 + 1) % 2;
	for (int i = 0; i < 4; i++)
	{
		// Check if there are enemies on the current diagonal
		struct square * penemy = KingEnemyScan(piece->adjacent[i], i, enemy);
		if (penemy != NULL && penemy->adjacent[i] != NULL && penemy->adjacent[i]->type == nopiece)
		{
			// Write the first move
			count++;
			movestart->next[i] = calloc(1, sizeof(struct move));
			movestart->tocapture[i] = penemy;
			struct square * pointer = penemy->adjacent[i];
			struct move * current = movestart->next[i];
			current->square = pointer;
			pointer = pointer->adjacent[i];
			// Write next moves
			while (pointer != NULL && pointer->type == nopiece)
			{
				count++;
				current->next[i] = calloc(1, sizeof(struct move));
				current->tocapture[i] = penemy;
				current = current->next[i];
				current->square = pointer;
				pointer = pointer->adjacent[i];
			}
		}
	}

	return count;
}

// Scan for an enemy on the diagonal
struct square * KingEnemyScan(struct square * pointer, int direction, int enemy)
{
	// Go through the diagonal
	while (true)
	{
		if (pointer == NULL || (pointer->type != nopiece && pointer->type % 2 != enemy))
			return NULL;
		if (pointer->type != nopiece)
			return pointer;

		pointer = pointer->adjacent[direction];
	}
}

// Perform capture
struct square * KingCapture(struct square * piece)
{
	// Preparings
	int count = 0;
	int index = chainstart->tocapture->type % 2;
	struct chain * current = chainstart;
	usleep(DELAY);
	PrintBoard();
	pieces[index]--;
	// Go through the chain structure
	while (current != NULL)
	{
		piece = SimpleMove(piece, current->square);
		current->tocapture->type = nopiece;
		struct chain * tmp = current;
		current = current->next;
		free(tmp);
	}
	chainstart = NULL;

	return piece;
}

// Scan for available squares for move and build move structure
int KingMoveScan(struct square * piece)
{
	int count = 0;
	for (int i = 0; i < 4; i++)
	{
		struct square * pointer = piece->adjacent[i];
		if (pointer != NULL && pointer->type == nopiece)
		{
			count++;
			movestart->next[i] = calloc(1, sizeof(struct move));
			struct move * current = movestart->next[i];
			current->square = pointer;
			pointer = pointer->adjacent[i];
			while (pointer != NULL && pointer->type == nopiece)
			{
				count++;
				current->next[i] = calloc(1, sizeof(struct move));
				current = current->next[i];
				current->square = pointer;
				pointer = pointer->adjacent[i];
			}
		}
	}

	return count;
}

// Move man piece
int MoveMan(struct square * piece)
{
	// variables definition/initialization
	int direction = piece->type % 2 == 0 ? 1 : -1;
	movestart = calloc(1, sizeof(struct move));
	int enemy = (piece->type % 2 + 1) % 2;

	// Scan for available moves if capture is not mandatory
	piece->pcselected = true;
	bool mustcapture = ManSimpleCaptureScan(piece);
	if (!mustcapture)
	{
		movestart->square = piece;
		for (int i = direction + 1, max = i + 2; i < max; i++)
		{
			if (piece->adjacent[i] != NULL && piece->adjacent[i]->type == nopiece)
			{
				movestart->next[i] = calloc(1, sizeof(struct move));
				movestart->next[i]->square = piece->adjacent[i];
			}
		}
	}
	else
		ManCaptureScan(piece, movestart, enemy, ALLDIRECT);

	int movecount = MarkSquares(movestart, ALLDIRECT);
	// If there are no available moves
	if (movecount <= 0)
	{
		piece->pcselected = false;
		ClearMoveList(movestart, ALLDIRECT);
		return 1;
	}
	

	// Pick a destination square and move to it
	int row = 0, col = 0;
	struct square * dest;
	int result = 0;
	while (true)
	{
		PrintBoard();
		printf("\e[1m%s's move\e[0m\n\e[s", piece->type % 2 == 0 ? "Black" : "White");
		dest = GetSquare("Pick destination: ");
		// If picked wrong destination
		if (dest == NULL || dest->type != nopiece)
			continue;
		// If piece have already been moved in previous iterations and player pressed Enter
		if (dest == empty && result == -2)
		{
			UnmarkSquares(movestart, ALLDIRECT);
			ClearMoveList(movestart, ALLDIRECT);
			break;
		}
		// If picked destination is not an available square
		if (SimpleSearch(dest, movestart, ALLDIRECT) == 0)
			continue;

		// Move
		if (!mustcapture)
		{
			piece = SimpleMove(piece, dest);
			UnmarkSquares(movestart, ALLDIRECT);
			ClearMoveList(movestart, ALLDIRECT);
			break;
		}
		// Capture
		else
		{
			chainstart = calloc(1, sizeof(struct chain));
			result = Search(dest, movestart, chainstart, ALLDIRECT);
			// If picked ambiguous destination
			if (result == 3)
			{
				printf("You cannot move to ambiguous destination in more than one move away!\n");
				usleep(10 * DELAY);
				continue;
			}
			else
			{
				UnmarkSquares(movestart, ALLDIRECT);
				PrintBoard();
				piece = ManCapture(piece);
				ClearMoveList(movestart, ALLDIRECT);
				// If moving through squares one at a time
				if (result == 2)
				{
					movestart = calloc(1,sizeof(struct move));
					ManCaptureScan(piece, movestart, enemy, ALLDIRECT);
					MarkSquares(movestart, ALLDIRECT);
					continue;
				}

				break;
			}
		}

	}

	// If reached the end of board
	if (piece->adjacent[direction+1] == NULL && piece->adjacent[direction+2] == NULL)
		piece->type += 2; // change piece type from man to king

	PrintBoard();
	usleep(DELAY);
	piece->pcselected = false;
	return 0;
}

// Perform capture
struct square * ManCapture(struct square * piece)
{
	// Preparings
	int count = 0;
	int index = chainstart->tocapture->type % 2;
	struct chain * current = chainstart;
	// Go through the chain structure
	while (current != NULL)
	{
		piece = SimpleMove(piece, current->square);	
		pieces[index]--;
		current->tocapture->type = nopiece;
		struct chain * tmp = current;
		current = current->next;
		free(tmp);
		usleep(DELAY);
		PrintBoard();
	}
	chainstart = NULL;

	return piece;
}

// Build chain structure based on move structure
int Search(struct square * square, struct move * entry, struct chain * chain, int prohibited)
{
	if (square == NULL || entry == NULL)
		return 0;

	// Check if given square is in the move structure
	int found = SimpleSearch(square, entry, prohibited);
	if (!found)
		return 0;

	// Go through all directions and find the shortest route if the square is an ambiguous destination 
	int steps[4] = {};
	int count = 0, min = 0, minindex;
	for (int i = 0; i < 4; i++)
	{
		if (i != prohibited)
		{
			steps[i] = SimpleSearch(square, entry->next[i], Opposite(i));
			if (steps[i] > 0)
			{
				count++;
				if (min == 0 || min > steps[i])
				{
					min = steps[i];
					minindex = i;
				}
			}
		}
	}

	// If the square is not an ambiguous destination
	if (count == 1)
	{
		chain->square = entry->next[minindex]->square;
		chain->tocapture = entry->tocapture[minindex];
		if (entry->next[minindex]->square == square)
		{
			if (square->bgselection > 1)
				return 2;
			else
				return 1;
		}
		chain->next = calloc(1, sizeof(struct chain));
		Search(square, entry->next[minindex], chain->next, Opposite(minindex));
	}
	else
	{
		if (min == 1)
		{
			chain->square = entry->next[minindex]->square;
			chain->tocapture = entry->tocapture[minindex];
			if (entry == movestart)
				return 2;
			else
				return 3;
		}
		else
			return 3;
	}
}

// Scan for capture moves and build move srtucture
bool ManCaptureScan(struct square * current, struct move * entry, int enemy, int prohibited)
{
	// Go through all directions
	bool mustcapture = false;
	entry->square = current;
	for (int i = 0; i < 4; i++)
	{
		// If direction is not one we came from and next square on it has not been crossed
		if (i != prohibited && !current->crossed[i])
		{
			// If next square is enemy
			if (current->adjacent[i] != NULL && current->adjacent[i]->type != nopiece && current->adjacent[i]->type % 2 == enemy)
			{
				// If next to next square is empty (or contains piece that is being moved)
				if (current->adjacent[i]->adjacent[i] != NULL && (current->adjacent[i]->adjacent[i]->type == nopiece || current->adjacent[i]->adjacent[i]->pcselected))
				{
					mustcapture = true;
					entry->next[i] = calloc(1, sizeof(struct move));
					current->crossed[i] = true;
					current->adjacent[i]->adjacent[i]->crossed[Opposite(i)] = true;
					entry->tocapture[i] = current->adjacent[i];
					int nextprohibited = Opposite(i);
					ManCaptureScan(current->adjacent[i]->adjacent[i], entry->next[i], enemy, nextprohibited);
					current->crossed[i] = false;	
				}
			}
		}
	}

	current->crossed[prohibited] = false;

	return mustcapture;
}

// Scan if there are capture moves
bool ManSimpleCaptureScan(struct square * square)
{
	if (square == NULL || square->type == nopiece)
		return false;

	int enemy = (square->type % 2 + 1) % 2;
	for (int i = 0; i < 4; i++)
	{
		if (square->adjacent[i] != NULL && square->adjacent[i]->type != nopiece && square->adjacent[i]->type % 2 == enemy)
		{
			if (square->adjacent[i]->adjacent[i] != NULL && square->adjacent[i]->adjacent[i]->type == nopiece)
				return true;
		}
	}

	return false;
}

// Check if there are capture moves on the board
bool MustCapture(int color)
{
	for (int i = 0; i < SIDE; i++)
	{
		for (int j = 0; j < SIDE; j++)
		{
			if (board[i][j] != NULL && board[i][j]->type != nopiece && board[i][j]->type % 2 == color)
			{
				// Call respective to the piece type function
				if (ScanPointer[board[i][j]->type / 2](board[i][j]))
					return true;
			}
		}
	}

	return false;
}

// Go through the move structure and mark all squares selected
int MarkSquares(struct move * entry, int prohibited)
{
	if (entry == NULL)
		return 0;

	int count = 0;
	if (entry->square != NULL && entry->square->type == nopiece)
	{
		entry->square->bgselection++;
		count++;
	}

	// Recursively call the function for the every available direction
	for (int i = 0; i < 4; i++)
	{
		if (i != prohibited)
			count += MarkSquares(entry->next[i], Opposite(i));
	}

	return count;
}

// Go through the move structure and unmark all squares
void UnmarkSquares(struct move * entry, int prohibited)
{
	if (entry == NULL)
		return;

	if (entry->square != NULL)
		entry->square->bgselection = 0;

	// Recursively call the function for the every available direction
	for (int i = 0; i < 4; i++)
	{
		if (i != prohibited)
			UnmarkSquares(entry->next[i], Opposite(i));
	}
}

// Clear move structure
void ClearMoveList(struct move * entry, int prohibited)
{
	if (entry == NULL)
		return;

	struct move * next[4];
	for (int i = 0; i < 4; i++)
	{
		next[i] = entry->next[i];
		if (entry->square != NULL)
			entry->square->crossed[i] = false;
	}
	free(entry);
	entry = NULL;

	// Recursive call
	for (int i = 0; i < 4; i++)
		ClearMoveList(next[i], ALLDIRECT);
}

// Parse given string and find row and col values
int CheckSquare(char * s, int * row, int * col)
{
	int tmpcol, tmprow;
	tmpcol = toupper(s[0]) - 'A';
	if (tmpcol >= SIDE || !isalpha(s[0]))
		return 1;

	for (int i = 1; s[i] != '\0' && s[i] != '\n'; i++)
	{
		if (!isdigit(s[i]))
			return 2;
	}

	tmprow = atoi(s + 1);
	if (tmprow > SIDE)
		return 3;

	*col = tmpcol;
	*row = SIDE - tmprow;

	return 0;
}

// Change piece types between two squares
struct square * SimpleMove(struct square * piece, struct square * square)
{
	if (piece == NULL || piece->type == nopiece)
		return NULL;
	if (square == NULL || square->type != nopiece)
		return NULL;

	square->type = piece->type;
	piece->type = nopiece;
	square->pcselected = piece->pcselected;
	piece->pcselected = false;

	return square;
}

// Check if the given square is in the move structure
int SimpleSearch(struct square * square, struct move * entry, int prohibited)
{
	if (square == NULL || entry == NULL)
		return 0;

	if (square == entry->square && entry != movestart)
		return 1;

	int steps[4] = {};
	int min = 0, count = 0;
	for (int i = 0; i < 4; i++)
	{
		if (i != prohibited)
		{
			steps[i] = SimpleSearch(square, entry->next[i], Opposite(i));
			if (steps[i] > 0)
			{
				if (min == 0)
					min = steps[i];
				else if (min > steps[i])
					min = steps[i];
			}
		}
	}

	if (min > 0)
		count = min + 1;
	return count;
}

// returns "adjacent" array index that is opposite to the given
int Opposite(int x)
{
	return (x - 2 < 0) ? x + 2 : x - 2;
}

// Return pointer to a board's square by parsing typed string and process commands
struct square * GetSquare(char * prompt)
{
	int row = 0, col = 0;
	printf("\e[s");
	while (true)
	{
		printf("%s", prompt);

		char buff[8];
		fgets(buff, sizeof(buff), stdin);
		buff[strcspn(buff, "\n")] = 0;
		if(strlen(buff) == 0)
			return empty;

		if (strcmp("save", buff) == 0)
		{
			if (Save() == 0)
				printf("Saved\n");
			else
				printf("Not Saved\n");
			usleep(DELAY * 10);
			printf("\e[u\e[J");
			continue;
		}
		if (strcmp("exit", buff) == 0)
			exit(0);
		if (CheckSquare(buff, &row, &col))
		{
			printf("\e[u\e[J");
			continue;
		}

		break;
	}

	return board[row][col];	
}

// Perform actions that are mandatory for playing
void InitializeBoard()
{
	empty = calloc(1, 1);
	int rows = (4 * SIDE) / 10;

	// Allocate memory for black squares
	int rowswitch = 0;
	int colswitch = 0;
	for (int i = 0; i < SIDE; i++)
	{
		rowswitch++;
		colswitch = rowswitch;
		for (int j = 0; j < SIDE; j++)
		{
			if (colswitch % 2 == 0)
			{
				board[i][j] = calloc(1, sizeof(struct square));
				board[i][j]->type = nopiece;
			}

			colswitch++;
		}
	}

	// Create pointers to adjacent squares
	for (int i = 0; i < SIDE; i++)
	{
		for (int j = 0; j < SIDE; j++)
		{
			if (board[i][j] != NULL)
			{
				if (i > 0)
				{
					if (j > 0)
						board[i][j]->adjacent[0] = board[i-1][j-1];
					if (j < SIDE-1)
						board[i][j]->adjacent[1] = board[i-1][j+1];
				}

				if (i < SIDE - 1)
				{
					if (j > 0)
						board[i][j]->adjacent[3] = board[i+1][j-1];
					if (j < SIDE-1)
						board[i][j]->adjacent[2] = board[i+1][j+1];
				}
			}	
		}
	}
}

// Add pieces to the board
void InitializePieces()
{
	turn = 1;
	int rows = (4 * SIDE) / 10;
	int rowswitch = 0;
	int colswitch = 0;
	for (int i = 0; i < SIDE; i++)
	{
		rowswitch++;
		colswitch = rowswitch;
		for (int j = 0; j < SIDE; j++)
		{
			if (colswitch % 2 == 0)
			{
				board[i][j]->type = (i < rows || i >= SIDE - rows) ? bman : nopiece;
				if (i >= SIDE - rows)
					board[i][j]->type = wman;

				if (board[i][j]->type != nopiece)
					pieces[board[i][j]->type % 2]++;
			}

			colswitch++;
		}
	}
}

// Print square without piece
void PrintVacantSquare(int bg)
{
	for (int i = 0; i < LEN * 2; i++)
		printf("\e[%dm \e[0m", bg);
}

// Print square with piece
void PrintSquare(struct square * piece)
{
	printf("\e[%dm", BLACKBG);
	for (int i = 0; i < LEN - 1; i++)
		printf(" ");

	int bg = piece->pcselected ? AVAILABLE : (piece->type % 2 == 0 ? BLACK : WHITE);
	int fg = bg == WHITE ? BLACK-10 : WHITE-10;
	char * fill = piece->type / 2 == 0 ? MAN : KING;
	printf("\e[%d;%dm%s\e[0m", bg, fg, fill);

	printf("\e[%dm", BLACKBG);
	for (int i = 0; i < LEN - 1; i++)
		printf(" ");

	printf("\e[0m");
}

// Print board's complete row
void PrintRow(int row)
{
	// Print upper part of the row
	for (int i = 0; i < (LEN - 1) / 2; i++)
	{
		printf("\e[%dm   ", BORDER);
		for (int j = 0; j < SIDE; j++)
		{
			if (board[row][j] == NULL)
				PrintVacantSquare(WHITEBG);
			else
			{
				if (board[row][j]->bgselection == 1)
					PrintVacantSquare(AVAILABLE);
				else if (board[row][j]->bgselection > 1)
					PrintVacantSquare(AMBIGUOUS);
				else
					PrintVacantSquare(BLACKBG);
			}
		}

		printf("\e[%dm   \e[0m\n", BORDER);
	}

	// Print middle part with the pieces
	printf("\e[%d;%dm %-2d", WHITEBG-10, BORDER, SIDE - row);
	for (int i = 0; i < SIDE; i++)
	{
		if (board[row][i] == NULL)
			PrintVacantSquare(WHITEBG);
		else if (board[row][i]->type == nopiece)
			{
				if (board[row][i]->bgselection == 1)
					PrintVacantSquare(AVAILABLE);
				else if (board[row][i]->bgselection > 1)
					PrintVacantSquare(AMBIGUOUS);
				else
					PrintVacantSquare(BLACKBG);
			}

		else
		{
			PrintSquare(board[row][i]);
		}
	}
	printf("\e[%dm   \e[0m\n", BORDER);

	// Print lower part of the row
	for (int i = 0; i < (LEN - 1) / 2; i++)
	{
		printf("\e[%dm   ", BORDER);
		for (int j = 0; j < SIDE; j++)
		{
			if (board[row][j] == NULL)
				PrintVacantSquare(WHITEBG);
			else
			{
				if (board[row][j]->bgselection == 1)
					PrintVacantSquare(AVAILABLE);
				else if (board[row][j]->bgselection > 1)
					PrintVacantSquare(AMBIGUOUS);
				else
					PrintVacantSquare(BLACKBG);
			}
		}

		printf("\e[%dm   \e[0m\n", BORDER);
	}
}

// Print board
void PrintBoard()
{
	printf("\e[2J\e[H");

	// prints upper border without letters
	printf("\e[%dm   ", BORDER);
	for (int i = 0; i < SIDE; i++)
	{
		for (int j = 0; j < 2 * LEN; j++)
		{
			printf(" ");
		}
	}
	printf("   \e[0m\n");

	for (int i = 0; i < SIDE; i++)
		PrintRow(i);

	// prints bottom border with letters
	printf("\e[%d;%dm   ", WHITEBG-10, BORDER);
	for (int i = 0; i < SIDE; i++)
	{
		char c = 'A' + i;
		for (int j = 0; j < 2 * LEN; j++)
		{
			if (j == LEN - 1)
			{
				j++;
				printf("%c%c", c, tolower(c));
			}
			else
				printf(" ");
		}
	}
	printf("   \e[0m\n\e[s");

	/*int shift = SIDE * LEN * 2 + 7;
	printf("\n\e[1;%dH", shift);

	printf("\tWhite pieces: %d", pieces[1]);
	printf("\n\e[2;%dH", shift);
	printf("\tBlack pieces: %d", pieces[0]);
	printf("\e[u");*/
}