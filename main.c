#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <semphr.h>

#define PART_TM4C123GH6PM
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <inc/hw_ints.h>
#include <inc/hw_types.h>
#include <inc/hw_memmap.h>
#include <inc/hw_gpio.h>
#include <driverlib/sysctl.h>
#include <driverlib/gpio.h>
#include <driverlib/pin_map.h>
#include <driverlib/systick.h>
#include <driverlib/uart.h>
#include <driverlib/interrupt.h>

/* Game Configuration Parameters */
#define ENV_WIDTH 				12
#define ENV_HEIGHT				12
#define INITIAL_SNAKE_LENGTH	4
#define MAX_SNAKE_LENGTH 		50
#define INITIAL_SNAKE_SPEED		60
#define MAXIMUM_SNAKE_SPEED		135
#define SPECIAL_POWERUP_PERIOD	5000
#define SPECIAL_POWERUP_FREQ	10
#define ENEMY_PERIOD			5000
#define END_MESSAGE_DELAY		5000

/* Type Definitions */
typedef struct 
{
	int x;
	int y;
} PointType;

typedef enum
{
	UP,
	DOWN,
	RIGHT,
	LEFT
} Direction;

typedef struct GameStateType 
{
	PointType snakePositions[MAX_SNAKE_LENGTH];
	int snakeLength;
	PointType normalPowerUpPosition;
	PointType specialPowerUpPosition;
	PointType enemyPosition;
} GameStateType;

typedef enum
{
	MAIN_MENU,
	START_GAME,
	SNAKE_POSITION_UPDATE,
	NORMAL_POWERUP_POSITION_UPDATE,
	REMOVE_NORMAL_POWERUP,
	SPECIAL_POWERUP_POSITION_UPDATE,
	REMOVE_SPECIAL_POWERUP,
	ENEMY_POSITION_UPDATE,
	REMOVE_ENEMY,
	LOSS_MESSAGE,
	WIN_MESSAGE,
	SCORE_UPDATE,
	TIME_UPDATE
} RenderRequestCategory;

typedef struct RenderRequest
{
	RenderRequestCategory category;
	PointType headPosition;
	bool removeTail;
	PointType tailPosition;
} RenderRequestType;

/* Global Variables */
GameStateType GameState;
int SnakeSpeed = INITIAL_SNAKE_SPEED;
Direction LastDirection = RIGHT;
bool inGame = false;
bool wonLast = false;
int score = 0;
int time = 0;

/* Tasks */
void MainMenuTask(void *vpParameters);
xTaskHandle MainMenuTaskHandle;
void RenderTask(void *vpParameters);
xTaskHandle RenderTaskHandle;
void SnakePositionUpdateTask(void *vpParameters);
xTaskHandle SnakePositionUpdateTaskHandle;
void NormalPowerUpSpawnTask(void* vpParameters);
xTaskHandle NormalPowerUpSpawnTaskHandle;
void SpecialPowerUpSpawnTask(void* vpParameters);
xTaskHandle SpecialPowerUpSpawnTaskHandle;
void EnemySpawnTask(void* vpParameters);
xTaskHandle EnemySpawnTaskHandle;
void TimeUpdateTask(void* vpParameters);
xTaskHandle TimeUpdateTaskHandle;

/* Mutexes, Semaphores and Queues */
xSemaphoreHandle GameStateLock = NULL;
xSemaphoreHandle GenerateRandomNumberLock = NULL;
xSemaphoreHandle NormalPowerUpSemaphore = NULL;
xQueueHandle RenderQueue = NULL;

/* Util Functions */
void initializeHardware();
void resetGameState();
void clearScreen();
void moveCursorToPosition(int line, int column);
void moveCursorToBottom();
int generateRandomNumber();
PointType generateRandomPosition();

int main()
{	
	/* Creating Tasks */
	xTaskCreate(MainMenuTask, "Main Menu", 256, NULL, 1, &MainMenuTaskHandle);
	xTaskCreate(RenderTask,   "Render",    256, NULL, 2, &RenderTaskHandle);
	
	/* Creating Mutexes and Semaphores */
	RenderQueue = xQueueCreate(1, sizeof(RenderRequestType));
	
	initializeHardware();
	
	vTaskStartScheduler();
	
	for ( ;; );
}

void MainMenuTask(void *vpParameters)
{
	const RenderRequestType mainMenuRenderRequest = {MAIN_MENU, false, {0, 0}, false};
	const RenderRequestType gameStartRenderRequest = {START_GAME, false, {0, 0}, false};
	for ( ;; )
	{
		while(inGame);
		GameStateLock = xSemaphoreCreateMutex();
		GenerateRandomNumberLock = xSemaphoreCreateMutex();
		NormalPowerUpSemaphore = xSemaphoreCreateBinary();
		xSemaphoreTake(NormalPowerUpSemaphore, 0);
		
		xQueueSend(RenderQueue, (const void *)&mainMenuRenderRequest, portMAX_DELAY);
		while (UARTCharGet(UART0_BASE) != 'e');
		resetGameState();
		if (wonLast) SnakeSpeed = (SnakeSpeed * 3) / 2;
		else SnakeSpeed = INITIAL_SNAKE_SPEED;
		xQueueSend(RenderQueue, (const void *)&gameStartRenderRequest, portMAX_DELAY);
		
		xSemaphoreGive(NormalPowerUpSemaphore);
		vTaskPrioritySet(NULL, 5);
		xTaskCreate(SnakePositionUpdateTask, "Snake", 			 256, NULL, 4, &SnakePositionUpdateTaskHandle);
		xTaskCreate(NormalPowerUpSpawnTask,  "Normal PowerUps",  256, NULL, 3, &NormalPowerUpSpawnTaskHandle);
		xTaskCreate(SpecialPowerUpSpawnTask, "Special PowerUps", 256, NULL, 3, &SpecialPowerUpSpawnTaskHandle);
		xTaskCreate(EnemySpawnTask, 		 "Enemy", 			 256, NULL, 3, &EnemySpawnTaskHandle);
		xTaskCreate(TimeUpdateTask,		 	 "Time",			 256, NULL, 3, &TimeUpdateTaskHandle);
		inGame = true;
		vTaskPrioritySet(NULL,1);
	}
}

void RenderTask(void *vpParameters)
{
	const char welcomeString[] = "Welcome to Snake Game, Press e to start the game\r\n";
	const char movekeyInstructionsString[] = "Use WASD keys for movement\r\n";
	const char symbolInstructionsString[] = "Your snake is o, normal powerups are +, special powerups are *, enemies are x\r\n";
	const char gameHeader[] = "Score:000  Time:000\r\n";
	const char lossMessageString[] = "You Lost!";
	const char winMessageString[] = "You Won!";
	RenderRequestType currentRequest;
	int currentScore;
	int currentTime;
	int i = 0;
	int j = 0;
	for ( ;; )
	{
		xQueueReceive(RenderQueue, (void *)&currentRequest, portMAX_DELAY);
		switch (currentRequest.category)
		{
			case MAIN_MENU:
				clearScreen();
				i = 0;
				while (welcomeString[i] != 0) UARTCharPut(UART0_BASE, welcomeString[i++]);
				i = 0;
				while (movekeyInstructionsString[i] != 0) UARTCharPut(UART0_BASE, movekeyInstructionsString[i++]);
				i = 0;
				while (symbolInstructionsString[i] != 0) UARTCharPut(UART0_BASE, symbolInstructionsString[i++]);
				break;
			case START_GAME:
				clearScreen();
				for (i = 0; i < ENV_WIDTH + 2; i++) UARTCharPut(UART0_BASE, '#');
				UARTCharPut(UART0_BASE, '\r');
				UARTCharPut(UART0_BASE, '\n');
				for (i = 0; i < ENV_HEIGHT; i++)
				{
					UARTCharPut(UART0_BASE, '#');
					for (j = 0; j < ENV_WIDTH; j++)
					{
						UARTCharPut(UART0_BASE, ' ');
					}
					UARTCharPut(UART0_BASE, '#');
					UARTCharPut(UART0_BASE, '\r');
					UARTCharPut(UART0_BASE, '\n');
				}
				for (i = 0; i < ENV_WIDTH + 2; i++) UARTCharPut(UART0_BASE, '#');
				UARTCharPut(UART0_BASE, '\r');
				UARTCharPut(UART0_BASE, '\n');
				i = 0;
				while (gameHeader[i] != 0) UARTCharPut(UART0_BASE, gameHeader[i++]);
				xSemaphoreTake(GameStateLock, portMAX_DELAY);
				for (i = 0; i < GameState.snakeLength; i++)
				{
					moveCursorToPosition(GameState.snakePositions[i].y + 1, GameState.snakePositions[i].x + 1);
					UARTCharPut(UART0_BASE, 'o');
				}
				xSemaphoreGive(GameStateLock);
				break;
			case SNAKE_POSITION_UPDATE:
				moveCursorToPosition(currentRequest.headPosition.y + 1, currentRequest.headPosition.x + 1);
				UARTCharPut(UART0_BASE, 'o');
				if (currentRequest.removeTail)
				{
					moveCursorToPosition(currentRequest.tailPosition.y + 1, currentRequest.tailPosition.x + 1);
					UARTCharPut(UART0_BASE, ' ');
				}
				moveCursorToBottom();
				break;
			case NORMAL_POWERUP_POSITION_UPDATE:
				moveCursorToPosition(currentRequest.headPosition.y + 1, currentRequest.headPosition.x + 1);
				UARTCharPut(UART0_BASE, '+');
				moveCursorToBottom();
				break;
			case REMOVE_NORMAL_POWERUP:
				moveCursorToPosition(currentRequest.headPosition.y + 1, currentRequest.headPosition.x + 1);
				UARTCharPut(UART0_BASE, ' ');
				moveCursorToBottom();
				break;
			case SPECIAL_POWERUP_POSITION_UPDATE:
				moveCursorToPosition(currentRequest.headPosition.y + 1, currentRequest.headPosition.x + 1);
				UARTCharPut(UART0_BASE, '*');
				moveCursorToBottom();
				break;
			case REMOVE_SPECIAL_POWERUP:
				moveCursorToPosition(currentRequest.headPosition.y + 1, currentRequest.headPosition.x + 1);
				UARTCharPut(UART0_BASE, ' ');
				break;
			case ENEMY_POSITION_UPDATE:
				moveCursorToPosition(currentRequest.headPosition.y + 1, currentRequest.headPosition.x + 1);
				UARTCharPut(UART0_BASE, 'x');
				moveCursorToBottom();
				break;
			case REMOVE_ENEMY:
				moveCursorToPosition(currentRequest.headPosition.y + 1, currentRequest.headPosition.x + 1);
				UARTCharPut(UART0_BASE, ' ');
				moveCursorToBottom();
				break;
			case LOSS_MESSAGE:
				clearScreen();
				i = 0;
				while (lossMessageString[i] != 0) UARTCharPut(UART0_BASE, lossMessageString[i++]);
				vTaskDelay(END_MESSAGE_DELAY/portTICK_RATE_MS);
				break;
			case WIN_MESSAGE:
				clearScreen();
				i = 0;
				while (winMessageString[i] != 0) UARTCharPut(UART0_BASE, winMessageString[i++]);
				vTaskDelay(END_MESSAGE_DELAY/portTICK_RATE_MS);
				break;
			case SCORE_UPDATE:
				moveCursorToPosition(ENV_HEIGHT + 2, 6);
				currentScore = score;
				UARTCharPut(UART0_BASE, (currentScore/100) + 48);
				UARTCharPut(UART0_BASE, ((currentScore % 100) / 10) + 48);
				UARTCharPut(UART0_BASE, (currentScore%10) + 48);
				moveCursorToBottom();
				break;
			case TIME_UPDATE:
				moveCursorToPosition(ENV_HEIGHT + 2, 16);
				currentTime = time;
				UARTCharPut(UART0_BASE, (currentTime/100) + 48);
				UARTCharPut(UART0_BASE, ((currentTime % 100) / 10) + 48);
				UARTCharPut(UART0_BASE, (currentTime%10) + 48);
				moveCursorToBottom();
				break;
		}
	}
}

void SnakePositionUpdateTask(void *vpParameters)
{
	bool firstLoop = true;
	portTickType lastWokenTime;
	int i;
	RenderRequestType snakeUpdateRenderRequest = {SNAKE_POSITION_UPDATE, {-1, -1}, true, {-1, -1}};
	RenderRequestType powerUpRemoveRenderRequest = {REMOVE_NORMAL_POWERUP, {-1, -1}, true, {-1, -1}};
	RenderRequestType lossMessageRenderRequest = {LOSS_MESSAGE, {-1, -1}, true, {-1, -1}};
	RenderRequestType winMessageRenderRequest = {WIN_MESSAGE, {-1, -1}, true, {-1, -1}};
	RenderRequestType updateScoreRenderRequest = {SCORE_UPDATE, {-1, -1}, true, {-1, -1}};
	PointType newHeadPosition;
	for ( ;; )
	{
		xSemaphoreTake(GameStateLock, portMAX_DELAY);
		/* Check for Input */
		if (UARTCharsAvail(UART0_BASE) > 0)
		{
			switch(UARTCharGet(UART0_BASE))
			{
				case 'a':
					if (LastDirection == UP || LastDirection == DOWN) LastDirection = LEFT;
					break;
				case 's':
					if (LastDirection== LEFT || LastDirection == RIGHT) LastDirection = DOWN;
					break;
				case 'd':
					if (LastDirection == UP || LastDirection == DOWN) LastDirection = RIGHT;
					break;
				case 'w':
					if (LastDirection == LEFT || LastDirection == RIGHT) LastDirection = UP;
					break;
			}	
		}
		/* Calculate New Head Position */
		newHeadPosition = GameState.snakePositions[0];
		switch (LastDirection)
		{
			case UP:
				newHeadPosition.y--;
				if (newHeadPosition.y == -1) newHeadPosition.y = ENV_HEIGHT - 1;
				break;
			case DOWN:
				newHeadPosition.y++;
				if (newHeadPosition.y == ENV_HEIGHT) newHeadPosition.y = 0;
				break;
			case LEFT:
				newHeadPosition.x--;
				if (newHeadPosition.x == -1) newHeadPosition.x = ENV_WIDTH - 1;
				break;
			case RIGHT:
				newHeadPosition.x++;
				if (newHeadPosition.x == ENV_WIDTH) newHeadPosition.x = 0;
				break;
		}
		snakeUpdateRenderRequest.headPosition = newHeadPosition;
		snakeUpdateRenderRequest.removeTail = true;
		snakeUpdateRenderRequest.tailPosition = GameState.snakePositions[GameState.snakeLength - 1];
		/* Check for Self-Collision */
		for (i = 1; i < GameState.snakeLength; i++)
		{
			if (newHeadPosition.x == GameState.snakePositions[i].x && newHeadPosition.y == GameState.snakePositions[i].y)
			{
				vSemaphoreDelete(GameStateLock);
				vSemaphoreDelete(GenerateRandomNumberLock);
				vSemaphoreDelete(NormalPowerUpSemaphore);
				vTaskDelete(NormalPowerUpSpawnTaskHandle);
				vTaskDelete(SpecialPowerUpSpawnTaskHandle);
				vTaskDelete(EnemySpawnTaskHandle);
				vTaskDelete(TimeUpdateTaskHandle);
				inGame = false;
				wonLast = false;
				xQueueSend(RenderQueue, (const void *)&lossMessageRenderRequest, portMAX_DELAY);
				vTaskDelete(NULL);
			}
		}
		/* Check for enemy Collision */
		if (newHeadPosition.x == GameState.enemyPosition.x && newHeadPosition.y == GameState.enemyPosition.y)
		{
			vSemaphoreDelete(GameStateLock);
			vSemaphoreDelete(GenerateRandomNumberLock);
			vSemaphoreDelete(NormalPowerUpSemaphore);
			vTaskDelete(NormalPowerUpSpawnTaskHandle);
			vTaskDelete(SpecialPowerUpSpawnTaskHandle);
			vTaskDelete(EnemySpawnTaskHandle);
			vTaskDelete(TimeUpdateTaskHandle);
			inGame = false;
			wonLast = false;
			xQueueSend(RenderQueue, (const void *)&lossMessageRenderRequest, portMAX_DELAY);
			vTaskDelete(NULL);
		}
		/* Check for Normal Power Up */
		if (newHeadPosition.x == GameState.normalPowerUpPosition.x && newHeadPosition.y == GameState.normalPowerUpPosition.y)
		{
			GameState.snakeLength++;
			snakeUpdateRenderRequest.removeTail = false;
			powerUpRemoveRenderRequest.category = REMOVE_NORMAL_POWERUP;
			powerUpRemoveRenderRequest.headPosition = GameState.normalPowerUpPosition;
			GameState.normalPowerUpPosition.x = -1;
			GameState.normalPowerUpPosition.y = -1;
			xSemaphoreGive(NormalPowerUpSemaphore);
			xQueueSend(RenderQueue, (const void *)&powerUpRemoveRenderRequest, portMAX_DELAY);
			score++;
			xQueueSend(RenderQueue, (const void *)&updateScoreRenderRequest, portMAX_DELAY);
		}
		/* Check for special Power Up */
		if (newHeadPosition.x == GameState.specialPowerUpPosition.x && newHeadPosition.y == GameState.specialPowerUpPosition.y)
		{
			GameState.snakeLength++;
			snakeUpdateRenderRequest.removeTail = false;
			powerUpRemoveRenderRequest.category = REMOVE_SPECIAL_POWERUP;
			powerUpRemoveRenderRequest.headPosition = GameState.specialPowerUpPosition;
			GameState.specialPowerUpPosition.x = -1;
			GameState.specialPowerUpPosition.y = -1;
			xQueueSend(RenderQueue, (const void *)&powerUpRemoveRenderRequest, portMAX_DELAY);
			score += 5;
			xQueueSend(RenderQueue, (const void *)&updateScoreRenderRequest, portMAX_DELAY);
		}
		/* Check for win */
		if (GameState.snakeLength == MAX_SNAKE_LENGTH) {
			vSemaphoreDelete(GameStateLock);
			vSemaphoreDelete(GenerateRandomNumberLock);
			vSemaphoreDelete(NormalPowerUpSemaphore);
			vTaskDelete(NormalPowerUpSpawnTaskHandle);
			vTaskDelete(SpecialPowerUpSpawnTaskHandle);
			vTaskDelete(EnemySpawnTaskHandle);
			vTaskDelete(TimeUpdateTaskHandle);
			inGame = false;
			wonLast = true;
			xQueueSend(RenderQueue, (const void *)&winMessageRenderRequest, portMAX_DELAY);
			vTaskDelete(NULL);
		}
		/* Move Snake */
		for (i = GameState.snakeLength - 1; i > 0; i--) 
		{
			GameState.snakePositions[i] = GameState.snakePositions[i-1];
		}
		GameState.snakePositions[0] = newHeadPosition;
		xQueueSend(RenderQueue, (const void *)&snakeUpdateRenderRequest, portMAX_DELAY);
		xSemaphoreGive(GameStateLock);
		if (firstLoop)
		{
			lastWokenTime = xTaskGetTickCount();
			firstLoop = false;
		}
		vTaskDelayUntil(&lastWokenTime, (60000/SnakeSpeed) / portTICK_RATE_MS);
	}
}
void NormalPowerUpSpawnTask(void* vpParameters)
{
	PointType powerUpPosition;
	bool freePosition;
	int i;
	RenderRequestType powerUpRenderRequest = {NORMAL_POWERUP_POSITION_UPDATE, {-1, -1}, true, {-1, -1}};
	for ( ;; )
	{
		xSemaphoreTake(NormalPowerUpSemaphore, portMAX_DELAY);
		xSemaphoreTake(GameStateLock, portMAX_DELAY);
		
		do
		{
			powerUpPosition = generateRandomPosition();
			freePosition = true;
			for (i = 0; i < GameState.snakeLength; i++)
			{
				if (GameState.snakePositions[i].x == powerUpPosition.x && GameState.snakePositions[i].y == powerUpPosition.y)
				{
					freePosition = false;
					break;
				}
			}
			if (GameState.specialPowerUpPosition.x == powerUpPosition.x && GameState.specialPowerUpPosition.y == powerUpPosition.y) freePosition = false;
			if (GameState.enemyPosition.x == powerUpPosition.x && GameState.enemyPosition.y == powerUpPosition.y) freePosition = false;
		} while (freePosition == false);
		GameState.normalPowerUpPosition = powerUpPosition;
		powerUpRenderRequest.headPosition = powerUpPosition;
		xQueueSend(RenderQueue, (const void *)&powerUpRenderRequest, portMAX_DELAY);
		xSemaphoreGive(GameStateLock);	
	}
}
void SpecialPowerUpSpawnTask(void* vpParameters)
{
	PointType powerUpPosition;
	bool freePosition;
	int i;
	RenderRequestType powerUpRenderRequest = {SPECIAL_POWERUP_POSITION_UPDATE, {-1, -1}, true, {-1, -1}};
	RenderRequestType powerUpRemoveRenderRequest = {REMOVE_SPECIAL_POWERUP, {-1, -1}, true, {-1, -1}};
	for ( ;; )
	{
		xSemaphoreTake(GameStateLock, portMAX_DELAY);
		if (GameState.specialPowerUpPosition.x >= 0 && GameState.specialPowerUpPosition.y >= 0)
		{
			powerUpRemoveRenderRequest.headPosition = GameState.specialPowerUpPosition;
			GameState.specialPowerUpPosition.x = -1;
			GameState.specialPowerUpPosition.y = -1;
			xQueueSend(RenderQueue, (const void *)&powerUpRemoveRenderRequest, portMAX_DELAY);
		}
		if (generateRandomNumber() % SPECIAL_POWERUP_FREQ == 0)
		{
			do
			{
				powerUpPosition = generateRandomPosition();
				freePosition = true;
				for (i = 0; i < GameState.snakeLength; i++)
				{
					if (GameState.snakePositions[i].x == powerUpPosition.x && GameState.snakePositions[i].y == powerUpPosition.y)
					{
						freePosition = false;
						break;
					}
				}
				if (GameState.normalPowerUpPosition.x == powerUpPosition.x && GameState.normalPowerUpPosition.y == powerUpPosition.y) freePosition = false;
				if (GameState.enemyPosition.x == powerUpPosition.x && GameState.enemyPosition.y == powerUpPosition.y) freePosition = false;
			} while (freePosition == false);
			GameState.specialPowerUpPosition = powerUpPosition;
			powerUpRenderRequest.headPosition = powerUpPosition;
			xQueueSend(RenderQueue, (const void *)&powerUpRenderRequest, portMAX_DELAY);
		}
		xSemaphoreGive(GameStateLock);
		vTaskDelay(SPECIAL_POWERUP_PERIOD/portTICK_RATE_MS);
	}
}
void EnemySpawnTask(void* vpParameters)
{
	PointType enemyPosition;
	bool freePosition;
	int i;
	RenderRequestType enemyRenderRequest = {ENEMY_POSITION_UPDATE, {-1, -1}, true, {-1, -1}};
	RenderRequestType enemyRemoveRenderRequest = {REMOVE_ENEMY, {-1, -1}, true, {-1, -1}};
	for ( ;; )
	{
		xSemaphoreTake(GameStateLock, portMAX_DELAY);
		
		if (GameState.enemyPosition.x >= 0 && GameState.enemyPosition.y >= 0)
		{
			enemyRemoveRenderRequest.headPosition = GameState.enemyPosition;
			GameState.enemyPosition.x = -1;
			GameState.enemyPosition.y = -1;
			xQueueSend(RenderQueue, (const void *)&enemyRemoveRenderRequest, portMAX_DELAY);
		}
		do
		{
			enemyPosition = generateRandomPosition();
			freePosition = true;
			for (i = 0; i < GameState.snakeLength; i++)
			{
				if (GameState.snakePositions[i].x == enemyPosition.x && GameState.snakePositions[i].y == enemyPosition.y)
				{
					freePosition = false;
					break;
				}
			}
			if (GameState.normalPowerUpPosition.x == enemyPosition.x && GameState.normalPowerUpPosition.y == enemyPosition.y) freePosition = false;
			if (GameState.specialPowerUpPosition.x == enemyPosition.x && GameState.enemyPosition.y == enemyPosition.y) freePosition = false;
		} while (freePosition == false);
		GameState.enemyPosition = enemyPosition;
		enemyRenderRequest.headPosition = enemyPosition;
		xQueueSend(RenderQueue, (const void *)&enemyRenderRequest, portMAX_DELAY);
		
		xSemaphoreGive(GameStateLock);
		
		vTaskDelay(ENEMY_PERIOD/portTICK_RATE_MS);
	}
}

void TimeUpdateTask(void *vpParameters)
{
	portTickType lastWokenTime;
	lastWokenTime = xTaskGetTickCount();
	RenderRequestType timeUpdateRenderRequest = {TIME_UPDATE, {-1, -1}, true, {-1, -1}};
	for ( ;; ) 
	{
		vTaskDelayUntil(&lastWokenTime, 1000 / portTICK_RATE_MS);
		time++;
		xQueueSend(RenderQueue, (const void *)&timeUpdateRenderRequest, portMAX_DELAY);
	}
}

void initializeHardware()
{
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
	while(!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOA));
	GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);
	GPIOPinConfigure(GPIO_PA0_U0RX);
	GPIOPinConfigure(GPIO_PA1_U0TX);
	SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
	while(!SysCtlPeripheralReady(SYSCTL_PERIPH_UART0));
	UARTConfigSetExpClk(UART0_BASE, SysCtlClockGet(), 128000, UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE | UART_CONFIG_PAR_NONE);
	UARTFIFODisable(UART0_BASE);
	
	UARTCharPut(UART0_BASE, '\033');
	UARTCharPut(UART0_BASE, '[');
	UARTCharPut(UART0_BASE, '?');
	UARTCharPut(UART0_BASE, '2');
	UARTCharPut(UART0_BASE, '5');
	UARTCharPut(UART0_BASE, '1');
}

void resetGameState()
{
	int i = 0;
	for (i = 0; i < INITIAL_SNAKE_LENGTH; i++)
	{
		GameState.snakePositions[i].x = ENV_WIDTH/2 - i;
		GameState.snakePositions[i].y = ENV_HEIGHT/2;
	}
	for (i = INITIAL_SNAKE_LENGTH; i < MAX_SNAKE_LENGTH; i++)
	{
		GameState.snakePositions[i].x = -1;
		GameState.snakePositions[i].y = -1;
	}
	GameState.snakeLength = INITIAL_SNAKE_LENGTH;
	GameState.normalPowerUpPosition.x = -1;
	GameState.normalPowerUpPosition.y = -1;
	GameState.specialPowerUpPosition.x = -1;
	GameState.specialPowerUpPosition.y = -1;
	GameState.enemyPosition.x = -1;
	GameState.enemyPosition.y = -1;
	LastDirection = RIGHT;
	score = 0;
	time = 0;
}

void clearScreen()
{
	UARTCharPut(UART0_BASE, '\033');
	UARTCharPut(UART0_BASE, '[');
	UARTCharPut(UART0_BASE, '2');
	UARTCharPut(UART0_BASE, 'J');
	UARTCharPut(UART0_BASE, '\033');
	UARTCharPut(UART0_BASE, '[');
	UARTCharPut(UART0_BASE, 'H');
}

void moveCursorToPosition(int line, int column)
{
	UARTCharPut(UART0_BASE, '\033');
	UARTCharPut(UART0_BASE, '[');
	UARTCharPut(UART0_BASE, 'H');
	UARTCharPut(UART0_BASE, '\033');
	UARTCharPut(UART0_BASE, '[');
	if (line < 10)
	{
		UARTCharPut(UART0_BASE, line + 48);
	}
	else 
	{
		UARTCharPut(UART0_BASE, line/10 + 48);
		UARTCharPut(UART0_BASE, line%10 + 48);
	}
	UARTCharPut(UART0_BASE, 'B');
	UARTCharPut(UART0_BASE, '\033');
	UARTCharPut(UART0_BASE, '[');
	if (column < 10)
	{
		UARTCharPut(UART0_BASE, column + 48);
	}
	else 
	{
		UARTCharPut(UART0_BASE, column/10 + 48);
		UARTCharPut(UART0_BASE, column%10 + 48);
	}
	UARTCharPut(UART0_BASE, 'C');
}

void moveCursorToBottom()
{
	moveCursorToPosition(ENV_HEIGHT + 3, 0);
}

int generateRandomNumber()
{
	static int x = 0;
	
	PointType position;
	
	xSemaphoreTake(GenerateRandomNumberLock, portMAX_DELAY);
	
	if (x == 0) x = xTaskGetTickCount();
	
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	
	xSemaphoreGive(GenerateRandomNumberLock);
	
	return x;
}

PointType generateRandomPosition()
{
	PointType position;
	position.x = generateRandomNumber();
	position.y = generateRandomNumber();
	position.x = position.x % ENV_WIDTH;
	position.y = position.y % ENV_HEIGHT;
	if (position.x < 0) position.x = position.x + ENV_WIDTH;
	if (position.y < 0) position.y = position.y + ENV_HEIGHT;
	
	return position;
}