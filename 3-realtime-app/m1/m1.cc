#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <limits.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <sys/neutrino.h>
#include <sys/netmgr.h>
#include <sys/types.h>
#include <pthread.h>
#include <signal.h>

// Возвращает результат функции
double getFunctionResult(double t);

int main(int argc, char *argv[]) {
	// Если не задан адрес именованной памяти, то завершаем выполненние
	if (argc < 2)
	{
		std::cout << "Incorrect number of arguments." << std::endl;
		return EXIT_FAILURE;
	}

	std::cout << "Module M1 is running" << std::endl;

	int const START_TIMER_CODE = -10;
	int const START_READ_CODE = -11;

	// Получаем id родительского процесса
	pid_t ppid = getppid();

	// Размер именнованной памяти
	size_t sharedMemoryLength = 32;

	// Присоединяем именнованную область к процессу
	int fd = shm_open("/sharemap", O_CREAT | O_RDWR | O_TRUNC, 0);
	if (fd == -1)
	{
		std::cout << "The error occurred while creating share memory." << std::endl;
		std::cout << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}

	ftruncate(fd, sharedMemoryLength);

	// Оображаем именованную память в адресное пространство процесса
	char *addr = (char*)mmap(0, sharedMemoryLength, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED)
	{
		std::cout << "The error occurred while mapping shared memory." << std::endl;
		std::cout << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}

	// Создаем канал
	int chid;
	if ((chid = ChannelCreate(0)) == -1)
	{
		std::cout << "The error occurred while creating channel." << std::endl;
		std::cout << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}

	// Устанавливаем соединение с собственным каналом
	int coid = 0;
	if ((coid = ConnectAttach(ND_LOCAL_NODE, 0, chid, _NTO_SIDE_CHANNEL, 0)) == -1)
	{
		std::cout << "The error occurred while connecting to channel." << std::endl;
		std::cout << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}

	// Тип уведомления таймера
	struct sigevent event;

	// Инициализируем уведомление типа "Импульс"
	SIGEV_PULSE_INIT(&event, coid, SIGEV_PULSE_PRIO_INHERIT, -1, 0);

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
	timer.it_value.tv_nsec = 50000000;
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_nsec = 50000000;

	char msg[32]; // сообщение импульса
	double functionResult = 0; // результат функции
	char buffer[sharedMemoryLength]; // буффер для записи результата функции
	double time = 0; // время

	// Запускаем таймер на запись значения функции в буфер
	timer_settime(timerid, 0, &timer, NULL);

	// Отправляем сигнал родительскому процессу о том, что он может запускать таймер
	if (SignalKill(ND_LOCAL_NODE, ppid, 0, SIGUSR1, START_TIMER_CODE, 0))
	{
		std::cout << "The error occurred while sending START_TIMER signal." << std::endl;
		std::cout << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}

	while (true)
	{
		// Возвращаем результат функции
		functionResult = getFunctionResult(time);

		// Преобразуем результат функции к строке
		sprintf(buffer, "%f", functionResult);

		//std::cout << "write: " << buffer << std::endl;

		// Копируем результат функции в именованную память
		strcpy(addr, buffer);

		if (time == 0)
		{
			// Отправляем сигнал родительскому процессу о том, что он может начинать читать
			if (SignalKill(ND_LOCAL_NODE, ppid, 0, SIGUSR1, START_READ_CODE, 0))
			{
				std::cout << "The error occurred while sending READY_TO_START_READ signal." << std::endl;
				std::cout << strerror(errno) << std::endl;
				return EXIT_FAILURE;
			}
		}

		//std::cout << buffer << std::endl;

		// Ожидаем импульс
		MsgReceivePulse(chid, &msg, sizeof(msg), NULL);

		// Увеличиваем время
		time += 0.05;
	}

	return EXIT_SUCCESS;
}

double getFunctionResult(double t)
{
	return sqrt(1 + 4.2 * t + 1.5 * cos(t));
}
