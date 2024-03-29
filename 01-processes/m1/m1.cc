﻿#include <cstdlib>
#include <iostream>
#include <stdlib.h>
#include <sys/neutrino.h>
#include <unistd.h>
#include <process.h>
#include <string.h>

/* Процесс M1 создаёт свой канал и, используя функцию семейства spawn*(),
 * запускает процессы М2 и М2,
 * передавая им в качестве аргумента chid своего канала,
 * затем переходит в состояние приёма сообщений по своему каналу. */
int main(int argc, char *argv[]) {
    const char *MODULE_2_PATH = "/home/host/m2/x86/o/m2";

    /* Создание канала.
     * flags - набор флагов свойств канала,
     * 0 – устанавливает свойства по умолчанию */
    int chid = ChannelCreate(0);
    std::cout << "P1" << " running with chid = " << chid << std::endl;

    // Преобразование числа в Си-строку
    char buffer[20];
    const char *strChid = itoa(chid, buffer, 10);

    /* Вызов дочерних процессов.
     * Функции семейства spawn*() способны
     * создавать новый (дочерний) процесс со своим pid,
     * параллельно выполняющийся вместе с родительским процессом.
     *
     * P_NOWAIT – родительский процесс не блокируется,
     * выполняется параллельно с дочерним процессом
     * и должен ожидать завершения дочернего процесса.
     * path – пусть к .exe
     *
     * В случае ошибки возвращается -1 (устанавливается errno). */
    int p2pid = spawnl(P_NOWAIT, MODULE_2_PATH, MODULE_2_PATH, strChid, "2",
                       NULL);
    if (p2pid < 0)
        std::cout << "P2 spawn error" << std::endl;

    int p3pid = spawnl(P_NOWAIT, MODULE_2_PATH, MODULE_2_PATH, strChid, "3",
                       NULL);
    if (p3pid < 0)
        std::cout << "P3 spawn error" << std::endl;

    /* Приём сообщения.
     * Процесс-сервер должен принять сообщение и послать ответ клиенту.
     *
     * Если сообщение уже было в канале, когда нить сервера вызывает MsgReceive(),
     * то оно немедленно копируется ядром в адресное пространство сервера.
     * Если сообщения в канале нет, то принимающая сообщение нить сервера
     * переходит в RECEIVE-блокированное состояние,
     * ожидая пока сообщение от клиента не поступит в канал.
     * При получении сообщения нить переходит в
     * состояние готовности к выполнению (READY).
     *
     * Получив сообщение от клиента сервер должен послать ему ответное сообщение.
     * При этом нить должна находится в REPLY-блокированном состоянии.
     * Ответ может послать любая нить сервера.
     * Важно только, чтобы на каждое
     * принятое сервером сообщение следовал бы ответ и только один.
     * Выполнение функции MsgSend(), вызванной нитью клиента,
     * которой соответствует rcvid, завершается разблокированием нити
     * и возвратом функцией MsgSend() значения статуса, заданного сервером
     * в аргументе status при выполнении функции MsgReply().
     *
     * Функция MsgReply() не блокирует нить сервера,
     * передача ответа выполняется немедленно. */
    int rcvid = -1; // ссылка на нить клиента
    char msg[512]; // буфер приема сообщения
    _msg_info info; // информация о сообщении

    int msgCounter = 0;
    while (msgCounter < 2) { // получить два сообщения
        rcvid = MsgReceive(chid, msg, sizeof(msg), &info); // получить сообщение
        if (rcvid == -1)
            std::cout << "P1 msg receive error" << std::endl;
        else
            std::cout << msg << std::endl;
        MsgReply(rcvid, 0, msg, sizeof(msg)); // посылка ответа клиенту
        msgCounter++;
    }

    std::cout << "P1 OK" << std::endl;
    return EXIT_SUCCESS;
}
