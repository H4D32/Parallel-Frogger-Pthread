#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <curses.h>
#include <termios.h>
#include <fcntl.h>

#define ROW 10
#define COLUMN 50

enum
{
	SECS = 0,
	NANOSEC = 3
};

struct Node
{
	int x, y;
	Node(int _x, int _y) : x(_x), y(_y){};
	Node(){};
} frog;

int finish_state = 3; // 0 = win, 1 = lose, 2 = quit (3 = error for debug)
char map[ROW + 10][COLUMN];
int logs[ROW - 1] = {10, 20, 10, 40, 5, 30, 10, 10, 40}; // initial configuration of the log position, could be randomized
// int logs_size[ROW-1] = {15,15,15,15,15,15,15,15,15}; // keeping the size consistant for now
bool game_on = TRUE;

struct timespec r1, r2 = {SECS, NANOSEC};

pthread_mutex_t lock1;
pthread_mutex_t lock2;

// Determine a keyboard is hit or not. If yes, return 1. If not, return 0.
int kbhit(void)
{
	struct termios oldt, newt;
	int ch;
	int oldf;

	tcgetattr(STDIN_FILENO, &oldt);

	newt = oldt;
	newt.c_lflag &= ~(ICANON | ECHO);

	tcsetattr(STDIN_FILENO, TCSANOW, &newt);
	oldf = fcntl(STDIN_FILENO, F_GETFL, 0);

	fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

	ch = getchar();

	tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
	fcntl(STDIN_FILENO, F_SETFL, oldf);

	if (ch != EOF)
	{
		ungetc(ch, stdin);
		return 1;
	}
	return 0;
}

int make_banks()
{
	// fills map with the river banks ||||||||
	int j;
	for (j = 0; j < COLUMN - 1; ++j)
		map[ROW][j] = map[0][j] = '|';

	for (j = 0; j < COLUMN - 1; ++j)
		map[0][j] = map[0][j] = '|';

	return 0;
}

int make_log(int initial_col, int row, int log_size, char symbol)
{
	int COLMOD = COLUMN - 1; // refactor to clear up code
	int n;
	// the modulo operator is used throughout to deal with boundary situations
	int last_col = (initial_col + log_size) % (COLMOD);
	// Clear row
	for (n = 0; n < COLMOD; ++n)
		map[row][n] = ' ';
	// Print log
	for (n = initial_col; n < initial_col + log_size; ++n)
		map[row][(n + COLMOD) % COLMOD] = symbol;
	return 0;
}

int render_map()
{
	// Update map values
	pthread_mutex_lock(&lock2);
	make_banks();
	for (int m = 1; m < ROW; ++m)
		make_log(logs[m - 1], m, 15, '=');
	// Careful not to index into an illegal value
	if (not(frog.x > 10 || frog.y < 0 || frog.y > 48 || frog.x < 0))
		map[frog.x][frog.y] = '0';
	// Clear Terminal/Cursor
	printf("\033[?25l\033[0;0H\033[2J");
	/*  Print the map on the screen  */
	for (int i = 0; i <= ROW; ++i)
		puts(map[i]);
	pthread_mutex_unlock(&lock2);
	return 0;
}

void *logs_move(void *t)
{
	int m;
	while (game_on)
	{
		/*  Move the logs  */
		for (m = 1; m < ROW; ++m)
		{
			int direction = 2 * (m % 2) - 1;
			logs[m - 1] += direction;
			logs[m - 1] += COLUMN - 1;
			logs[m - 1] = logs[m - 1] % (COLUMN - 1);
			// Assume frog is on log if game is still on
			if (frog.x == m)
			{
				pthread_mutex_lock(&lock1);
				frog.y += direction;
				pthread_mutex_unlock(&lock1);
			}
		}
		usleep(100000); // 10 moves per second
	}
	pthread_exit(NULL);
}

void *game_control(void *t)
{

	while (game_on)
	{
		/*  Check game's status  */
		if (frog.x > 10 || frog.y < 0 || frog.y > 48 || frog.x < 0)
		{
			finish_state = 1;
			game_on = FALSE;
		}
		if (frog.x == 0)
		{
			finish_state = 0;
			game_on = FALSE;
			render_map();
		}
		if (frog.x != 10 && frog.x != 0)
		{
			int log_start = logs[frog.x - 1];
			int log_end = (log_start + 15) % (COLUMN - 1);
			if (log_start > log_end)
			{
				if (frog.y > log_end && frog.y < log_start)
				{
					finish_state = 1;
					game_on = FALSE;
					render_map();
				}
			}
			else
			{
				if (frog.y > log_end || frog.y < log_start)
				{
					finish_state = 1;
					game_on = FALSE;
					render_map();
				}
			}
		}

		/*  Check keyboard hits, to change frog's position or quit the game. */
		if (kbhit())
		{
			char dir = getchar();
			pthread_mutex_lock(&lock1);
			if (dir == 'w' || dir == 'W')
				frog.x -= 1;
			if (dir == 's' || dir == 'S')
				frog.x += 1;
			if (dir == 'a' || dir == 'A')
				frog.y -= 1;
			if (dir == 'd' || dir == 'D')
				frog.y += 1;
			pthread_mutex_unlock(&lock1);
			if (dir == 'q' || dir == 'Q')
			{
				finish_state = 2;
				game_on = FALSE;
				render_map();
			}
		}
		nanosleep(&r2, &r1);
	}
	pthread_exit(NULL);
}

void *game_render(void *t)
{
	while (game_on)
	{
		render_map();
		usleep(5000); // 5 miliseconds for each render
	}
	pthread_exit(NULL);
}

int main(int argc, char *argv[])
{

	pthread_t render;
	pthread_t logs;
	pthread_t control;
	int rc;
	int lc;
	int cc;
	long ri;
	long li;
	long ci;

	// Initialize the river map and frog's starting position
	memset(map, 0, sizeof(map));
	frog = Node(ROW, (COLUMN - 1) / 2);
	/*  Initialize mutex */
	if (pthread_mutex_init(&lock1, NULL) != 0)
	{
		printf("\n mutex init has failed\n");
		return 1;
	}
	if (pthread_mutex_init(&lock2, NULL) != 0)
	{
		printf("\n mutex init has failed\n");
		return 1;
	}
	/*  Create pthreads */
	lc = pthread_create(&logs, NULL, logs_move, (void *)li);
	if (lc)
	{
		printf("ERROR: return code from pthread_create(logs_move) is %d", lc);
		exit(1);
	}

	rc = pthread_create(&render, NULL, game_render, (void *)ri);
	if (rc)
	{
		printf("ERROR: return code from pthread_create(render) is %d", rc);
		exit(1);
	}

	cc = pthread_create(&control, NULL, game_control, (void *)ci);
	if (cc)
	{
		printf("ERROR: return code from pthread_create(render) is %d", cc);
		exit(1);
	}

	pthread_join(render, NULL);
	pthread_join(logs, NULL);
	pthread_join(control, NULL);
	pthread_mutex_destroy(&lock1);
	pthread_mutex_destroy(&lock2);
	
	/*  Display the output for user: win, lose or quit.  */
	usleep(500000); // wait to show end screen (how you won/lost)
	printf("\033[?25l\033[0;0H\033[2J");
	if (finish_state == 0)
		printf("You win the game!!\n");
	if (finish_state == 1)
		printf("You lost the game..\n");
	if (finish_state == 2)
		printf("Game exit manually\n");
	printf("\033[?25h");
	pthread_exit(NULL);

	return 0;
}