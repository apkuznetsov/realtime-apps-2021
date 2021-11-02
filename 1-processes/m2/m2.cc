#include <cstdlib>
#include <iostream>
#include <stdlib.h>
#include <sys/neutrino.h>
#include <unistd.h>
#include <process.h>
#include <string.h>
#include <string>

/* Процесс M2 создаёт свой канал и, используя функцию семейства spawn*(),
 * запускает процесс М3, передавая в качестве аргумента chid своего канала,
 * затем переходит в состояние приёма сообщений по созданному каналу.
 *
 * Процесс Р?(М2) после ответа на запрос процесса Р?(М3)
 * выводит на терминал "Р? ОК" и терминируется. */
int main(int argc, char *argv[]) {
	const char* MODULE_3_PATH = "/home/host/m3/x86/o/m3";

	std::string pnum = argv[2];
	std::string pname = "P" + pnum;

	int grandparentChid = atoi(argv[1]); // id канала родительского процесса

	int chid = ChannelCreate(0); // создание канала
	std::cout << pname << " running with chid = " << chid << std::endl;

	// Преобразование числа в Си-строку
	char buffer[20];
	const char* strChid = itoa(chid, buffer, 10);

	// Вызов дочернего процесса
	int childPid = spawnl(P_NOWAIT, MODULE_3_PATH, MODULE_3_PATH, strChid,
			argv[2], NULL);
	if (childPid < 0)
		std::cout << pname << "1 spawn error" << std::endl;

	// Приём сообщения
	int rcvid = -1; // ссылка на нить клиента
	char msg[512]; // буфер приёма сообщения
	_msg_info info; // информация о сообщении

	int replyMsg[2]; // буфер ответа
	replyMsg[0] = grandparentChid; // chid P1
	replyMsg[1] = getppid(); // pid P1

	while (rcvid == -1) { // получить одно сообщение
		rcvid = MsgReceive(chid, msg, sizeof(msg), &info); // получить сообщение
		if (rcvid == -1)
			std::cout << pname << " msg receive error" << std::endl;
		else
			std::cout << msg << std::endl;
		MsgReply(rcvid, 0, replyMsg, sizeof(replyMsg)); // посылка ответа клиенту
	}

	std::cout << pname << " OK" << std::endl;
	return EXIT_SUCCESS;
}
