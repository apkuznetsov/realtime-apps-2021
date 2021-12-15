#include <cstdlib>
#include <stdio.h>
#include <iostream>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <pthread.h>
#include <stdlib.h>
#include <iostream>
#include <signal.h>

// Записать значение функции и время в файл
void writeToFile(char* functionValue, char* time);

// Обработать пользовательские сигналы
void handler(int signo, siginfo_t *info, void *over);

// Завершить все процессы
void terminate(union sigval arg);

// Готов ли процесс запустить таймер
volatile bool isReadyToStartTimer = false;

// Готов ли процесс начать чтение
volatile bool isReadyToStartRead = false;

FILE* file;

int main(int argc, char *argv[]) {

	std::cout << "M2 running" << std::endl;

	// Задаем диспозицию пользовательскому сигналу,
	// отвечающему за запуск таймера и начало чтения из именованной памяти
	sigset_t signalSet;
	sigemptyset(&signalSet);
	sigaddset(&signalSet, SIGUSR1);

	struct sigaction act;
	act.sa_flags = 0;
	act.sa_mask = signalSet;
	act.sa_sigaction = &handler;

	sigaction(SIGUSR1, &act, NULL);

	// Инициализируем сигнал
	int signo;
	sigset_t timerSignalSet;
	sigemptyset(&timerSignalSet);
	sigaddset(&timerSignalSet, SIGUSR2);

	// Размер именнованной памяти
	size_t sharedMemoryLength = 32;

	// Создаем именованную память
	int fd = shm_open("/sharemap", O_CREAT | O_RDWR | O_TRUNC, 0);
	if (fd == -1)
	{
		std::cout << "The error occurred while creating share memory." << std::endl;
		std::cout << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}

	ftruncate(fd, sharedMemoryLength);

	// Оображаем именованную память в адресное пространство процесса
	char* addr = (char*)mmap(0, sharedMemoryLength, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED)
	{
		std::cout << "The error occurred while mapping shared memory." << std::endl;
		std::cout << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}

	// Запускаем дочерний процесс на основе модуля L3_M1
	int cid = spawnl(P_NOWAIT, "L3_M1", "L3_M1", "L3_M1", NULL);
	if (cid == -1)
	{
		std::cout << "The error occurred while starting new process." << std::endl;
		std::cout << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}

	// Тип уведомления таймера
	struct sigevent event;

	// Инициализируем уведомление типа "Сигнал"
	SIGEV_SIGNAL_INIT(&event, SIGUSR2);

	// Id таймера
	timer_t timerid;

	// Создаем таймер
	if (timer_create(CLOCK_REALTIME, &event, &timerid) == -1)
	{
		std::cout << "The error occurred while creating timer." << std::endl;
		std::cout << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}

	// Свойства таймера
	struct itimerspec timer;

	// Задаем свойства таймера
	timer.it_value.tv_sec = 0;
	timer.it_value.tv_nsec = 200000000;
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_nsec = 200000000;

	// Тип уведомления таймера
	struct sigevent threadEvent;

	// Инициализируем уведомление типа "Создать нить"
	SIGEV_THREAD_INIT(&threadEvent, terminate, cid, NULL);

	// Id таймера
	timer_t stopTimerId;

	// Создаем таймер
	if (timer_create(CLOCK_REALTIME, &threadEvent, &stopTimerId) == -1)
	{
		std::cout << "The error occurred while creating timer." << std::endl;
		std::cout << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}

	// Свойства таймера
	struct itimerspec stopTimer;

	// Задаем свойства таймера
	stopTimer.it_value.tv_sec = 57;
	stopTimer.it_value.tv_nsec = 0;
	stopTimer.it_interval.tv_sec = 0;
	stopTimer.it_interval.tv_nsec = 0;

	file = fopen("/home/host/trend", "wt");

	// Буффер для записи значения функции и времени
	char bufferFunctionResult[sharedMemoryLength];
	char bufferTime[sizeof(double)];

	// Время
	double time = 0;

	// Ожидаем сигнал для запуска таймера
	while (!isReadyToStartTimer);

	// Запускаем таймер на считывание значений из именованной память
	timer_settime(timerid, 0, &timer, NULL);

	// Запускаем таймер на завершение всех процессов
	timer_settime(stopTimerId, 0, &stopTimer, NULL);

	while (true)
	{
		// Ожидаем сигнал для чтения первого значения
		while (!isReadyToStartRead);

		// Копируем результат функции в именованную память
		strcpy(bufferFunctionResult, addr);

		//std::cout << "read: " << bufferFunctionResult << std::endl;

		// Преобразуем время к строке
		sprintf(bufferTime, "%f", time);

		//std::cout << bufferTime << std::endl;
		//std::cout << bufferFunctionResult << std::endl;

		// Записываем в файл
		writeToFile(bufferFunctionResult, bufferTime);

		// Ожидаем сигнал
		sigwait(&timerSignalSet, &signo);

		// Увеличиваем время
		time += 0.2;
	}

	return EXIT_SUCCESS;
}

void handler(int signo, siginfo_t *info, void *over)
{
	switch (info->si_code)
	{
		case -10:
			std::cout << "Received signal with code \"READY_TO_START_TIMER\"" << std::endl;
			isReadyToStartTimer = true;
			break;

		case -11:
			std::cout << "Received signal with code \"READY_TO_START_READ\"" << std::endl;
			isReadyToStartRead = true;
			break;

		default:
			std::cout << "Received signal with unknown code" << std::endl;
	}
}

void writeToFile(char* functionValue, char* time)
{
	char text[200] = "";

	strcat(text, functionValue);
	strcat(text, "\t");
	strcat(text, time);
	strcat(text, "\n");

	fputs(text, file);
}

void terminate(union sigval arg)
{
	std::cout << "Terminate all processes" << std::endl;

	fclose(file);

	kill(arg.sival_int, SIGKILL);
	kill(getpid(), SIGKILL);
}

