#include <cstdlib>
#include <iostream>
#include <stdlib.h>
#include <sys/neutrino.h>
#include <unistd.h>
#include <process.h>
#include <string.h>
#include <string>

/* Процесс М3 устанавливает соединение c каналом родительского процесса М2
 * и посылает запрос на получение pid процесса Р1 и chid его канала,
 * затем, после получения ответа (pid и chid),
 * устанавливает соединение с каналом процесса Р1
 * и посылает ему сообщение "Р? loaded".
 * После получения ответа выводит на экран "Р? ОК" и терминируется. */
int main(int argc, char *argv[]) {
	std::string pnum = argv[2];
	std::string pname = "P" + pnum + "1";
	std::cout << pname << " running" << std::endl;

	int parentChid = atoi(argv[1]); // id канала родительского процесса

	/* Передача сообщения родителю.
	 * connectionid — id канала для отправки сообщения.
	 * Установление соединения с каналом: */
	int connectionId = ConnectAttach(0, getppid(), parentChid,
			_NTO_SIDE_CHANNEL, 0);
	if (connectionId == -1) {
		std::cout << pname << " connection error" << std::endl;
		exit(EXIT_FAILURE);
	}

	// Запрос на получение pid процесса Р1 и chid его канала
	int replyMsg1[2]; // буфер ответа
	const char* msg = (pname + " sending request").c_str(); // сообщение процессу серверу
	if (MsgSend(connectionId, msg, strlen(msg) + 1, // послать сообщение P2
			replyMsg1, sizeof(replyMsg1)) == -1) {
		std::cout << pname << " msg send error" << std::endl;
		exit(EXIT_FAILURE);
	}
	if (replyMsg1 != NULL) {
		std::cout << pname << " server responded with P1 chid = "
				<< replyMsg1[0] << " and P1 pid = " << replyMsg1[1]
				<< std::endl;
	}

	// Установление соединения с каналом P1
	connectionId = ConnectAttach(0, replyMsg1[1], replyMsg1[0],
			_NTO_SIDE_CHANNEL, 0);
	if (connectionId == -1) {
		std::cout << pname << " connection error" << std::endl;
		exit(EXIT_FAILURE);
	}

	// Посылка сообщения P1
	char replyMsg2[512];//буфер ответа
	std::string temp = pname + " loaded";
	msg = temp.c_str();
	if (MsgSend(connectionId, msg, strlen(msg) + 1, // послать сообщение P1
			replyMsg2, sizeof(replyMsg2)) == -1) {
		std::cout << pname << " msg send error" << std::endl;
		exit(EXIT_FAILURE);
	}
	if (replyMsg2 != NULL)
		std::cout << pname << " OK " << std::endl;

	return EXIT_SUCCESS;
}
